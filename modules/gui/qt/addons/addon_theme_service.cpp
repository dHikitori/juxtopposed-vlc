// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_theme_service.hpp"
#include "addon_theme_document.hpp"
#include "addon_manager.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace vlc::addons {
QPointer<AddonThemeService> AddonThemeService::s_instance;

AddonThemeService::AddonThemeService(AddonManager* manager, QObject* parent)
    : QObject(parent), m_manager(manager)
{
    if (!s_instance) {
        s_instance = this;
        connect(this, &AddonThemeService::revisionChanged, notifications(), &AddonThemeNotifications::changed);
    }
    if (manager) {
        m_settingsFile = QDir(manager->storageDirectory()).filePath(QStringLiteral("theme-selection.json"));
        QFile file(m_settingsFile);
        if (file.open(QIODevice::ReadOnly) && file.size() <= 4096) {
            const auto saved = QJsonDocument::fromJson(file.readAll()).object();
            m_selected = saved.value(QStringLiteral("selectedTheme")).toString();
            if (m_selected.size() > 256) m_selected.clear();
            m_requestedHighContrast = saved.value(QStringLiteral("highContrast")).toBool();
            m_reducedMotion = saved.value(QStringLiteral("reducedMotion")).toBool();
        }
        connect(manager, &AddonManager::revisionChanged, this, &AddonThemeService::refresh);
        connect(manager, &QObject::destroyed, this, [this] { m_manager = nullptr; refresh(); });
    }
    refresh();
}
AddonThemeService::~AddonThemeService()
{
    if (s_instance == this) { s_instance = nullptr; emit notifications()->changed(); }
}
AddonThemeService* AddonThemeService::instance() { return s_instance; }
AddonThemeNotifications* AddonThemeService::notifications()
{
    // Native menu watchers are created before MainUI/the service. A stable bus
    // lets those existing surfaces observe service creation, updates and recovery.
    static AddonThemeNotifications notifications;
    return &notifications;
}

bool AddonThemeService::validate(const QJsonObject& document, QString* error)
{ return themeDocument::validate(document,error); }
QVariantMap AddonThemeService::resolve(const QJsonObject& document, bool light, const QString& accent, bool highContrast, bool reducedTransparency)
{ return themeDocument::resolve(document,light,accent,highContrast,reducedTransparency); }
QVariantMap AddonThemeService::tokens(bool light, const QString& accent, bool contrast, bool reducedTransparency) const
{
    const auto effectiveAccent = accent.isEmpty() ? m_userAccent : accent;
    const QString key = QString::number(light) + QLatin1Char(':') + effectiveAccent + QLatin1Char(':')
        + QString::number(contrast || highContrast()) + QLatin1Char(':') + QString::number(reducedTransparency);
    const auto found = m_tokenCache.constFind(key);
    if (found != m_tokenCache.cend()) return found.value();
    const auto value = resolve(m_active, light, effectiveAccent, contrast || highContrast(), reducedTransparency);
    if (m_tokenCache.size() >= 16) m_tokenCache.clear();
    m_tokenCache.insert(key, value);
    return value;
}

bool AddonThemeService::persist(const QString& selected, bool highContrast)
{
    if (m_settingsFile.isEmpty() || !QDir().mkpath(QFileInfo(m_settingsFile).absolutePath())) return false;
    QSaveFile file(m_settingsFile);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(QJsonObject{{"selectedTheme", selected}, {"highContrast", highContrast}, {"reducedMotion", m_reducedMotion}}).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}
bool AddonThemeService::selectTheme(const QString& id)
{
    if (!id.isEmpty() && std::none_of(m_available.begin(), m_available.end(), [&](const QVariant& row) { return row.toMap().value("id").toString() == id; })) {
        m_error = tr("This theme is not available. First, enable its add-on."); ++m_revision; emit revisionChanged(); return false;
    }
    if (!persist(id, m_requestedHighContrast)) { m_error = tr("The app cannot save the theme selection."); ++m_revision; emit revisionChanged(); return false; }
    m_selected = id; refresh(); return true;
}
bool AddonThemeService::setHighContrast(bool enabled)
{
    if (!persist(m_selected, enabled)) { m_error = tr("The app cannot save the accessibility setting."); ++m_revision; emit revisionChanged(); return false; }
    m_requestedHighContrast = enabled; m_tokenCache.clear(); ++m_revision; emit revisionChanged(); return true;
}
void AddonThemeService::setUserAccentOverride(const QString& option)
{
    const auto value = themeDocument::isColor(option) ? option.toLower() : QString();
    if (m_userAccent == value) return;
    m_userAccent = value; m_tokenCache.clear(); ++m_revision; emit revisionChanged();
}
bool AddonThemeService::setReducedMotion(bool enabled)
{
    const auto previous = m_reducedMotion; m_reducedMotion = enabled;
    if (!persist(m_selected, m_requestedHighContrast)) {
        m_reducedMotion = previous; m_error = tr("The app cannot save the motion setting."); ++m_revision; emit revisionChanged(); return false;
    }
    ++m_revision; emit revisionChanged(); return true;
}
void AddonThemeService::refresh()
{
    QVariantList available;
    QJsonObject active;
    QString selectedError;
    if (m_manager) for (const auto& value : m_manager->contributionsOfType(QStringLiteral("theme"))) {
        const auto item = value.toMap(); const auto id = item.value("id").toString();
        const auto contribution = QJsonObject::fromVariantMap(item.value("document").toMap());
        const auto field = contribution.contains("theme") ? QStringLiteral("theme") : QStringLiteral("resource");
        const auto document = m_manager->contributionResource(id, field);
        QString error;
        if (!validate(document, &error)) { if (id == m_selected) selectedError = error; continue; }
        available.append(QVariantMap{{"id", id}, {"packageId", item.value("packageId")}, {"name", item.value("name")},
            {"light", document.value("variants").toObject().contains("light")}, {"dark", document.value("variants").toObject().contains("dark")}});
        if (id == m_selected) active = document;
    }
    std::sort(available.begin(), available.end(), [](const QVariant& a, const QVariant& b) { return a.toMap().value("id").toString() < b.toMap().value("id").toString(); });
    m_available = available; m_active = active; m_tokenCache.clear();
    m_error = !selectedError.isEmpty() ? selectedError : usingFallback() ? tr("The selected theme is not available. The app uses the built-in theme.") : QString();
    ++m_revision; emit revisionChanged();
}
void AddonThemeService::observePlatformPreferences(QObject* source)
{
    if (m_platformSource) disconnect(m_platformSource, nullptr, this, nullptr);
    m_platformSource = source;
    if (source) {
        connect(source, &QObject::destroyed, this, &AddonThemeService::refreshPlatformPreferences);
        const auto index = source->metaObject()->indexOfProperty("contrastPreference");
        if (index >= 0) {
            const auto property = source->metaObject()->property(index);
            if (property.hasNotifySignal()) connect(source, property.notifySignal(), this, metaObject()->method(metaObject()->indexOfSlot("refreshPlatformPreferences()")));
        }
    }
    refreshPlatformPreferences();
}
void AddonThemeService::refreshPlatformPreferences()
{
    bool highContrast = false;
    if (m_platformSource) {
        const auto index = m_platformSource->metaObject()->indexOfProperty("contrastPreference");
        if (index >= 0) {
            const auto property = m_platformSource->metaObject()->property(index);
            const auto value = property.read(m_platformSource);
            const auto name = property.isEnumType() ? QString::fromLatin1(property.enumerator().valueToKey(value.toInt())) : value.toString();
            highContrast = name.contains(QLatin1String("high"), Qt::CaseInsensitive) || name.contains(QLatin1String("more"), Qt::CaseInsensitive);
        }
    }
    if (m_platformHighContrast == highContrast) return;
    m_platformHighContrast = highContrast; m_tokenCache.clear(); ++m_revision; emit revisionChanged();
}
}
