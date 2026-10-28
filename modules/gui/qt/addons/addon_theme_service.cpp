// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_theme_service.hpp"
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
namespace {
bool fail(QString* error, const QString& message) { if (error) *error = message; return false; }
bool keys(const QJsonObject& object, const QSet<QString>& allowed)
{ for (auto it = object.begin(); it != object.end(); ++it) if (!allowed.contains(it.key())) return false; return true; }
bool hexColor(const QString& value)
{
    static const QRegularExpression expression(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    return expression.match(value).hasMatch();
}
QVariantMap defaults(bool light)
{
    const QVariantMap colors{
        {"surface.base", light ? "#acacac" : "#1b1b1b"}, {"surface.panel", light ? "#c3c3c3" : "#0f0f0f"},
        {"surface.field", light ? "#dedede" : "#090909"}, {"surface.hover", light ? "#d5d5d5" : "#292929"},
        {"surface.selected", light ? "#b8b8b8" : "#333333"}, {"text.primary", light ? "#2e2e2e" : "#e5e5e5"},
        {"text.secondary", light ? "#404040" : "#8f8f8f"}, {"accent.primary", "#d37918"},
        {"focus.ring", light ? "#785000" : "#d37918"}, {"border.default", light ? "#969696" : "#292929"},
        {"status.danger", "#d33718"}
    };
    return {{"colors", colors}, {"shape", QVariantMap{{"controlRadius", 6}, {"panelRadius", 14}}},
        {"effects", QVariantMap{{"panelOpacity", 0.90}}}};
}
double luminance(const QString& color)
{
    const auto linear = [](double channel) { return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4); };
    return linear(color.mid(1, 2).toInt(nullptr, 16) / 255.) * 0.2126
        + linear(color.mid(3, 2).toInt(nullptr, 16) / 255.) * 0.7152
        + linear(color.mid(5, 2).toInt(nullptr, 16) / 255.) * 0.0722;
}
double contrast(const QString& first, const QString& second)
{ const auto a = luminance(first), b = luminance(second); return (std::max(a, b) + .05) / (std::min(a, b) + .05); }
bool readableStatus(const QString& foreground, const QVariantMap& colors)
{
    for (const auto& surface : {QStringLiteral("surface.base"), QStringLiteral("surface.panel"), QStringLiteral("surface.field"), QStringLiteral("surface.hover"), QStringLiteral("surface.selected")})
        if (contrast(foreground, colors.value(surface).toString()) < 4.5) return false;
    return true;
}
QVariantMap merged(const QJsonObject& variant, bool light)
{
    auto result = defaults(light);
    for (const auto& group : {QStringLiteral("colors"), QStringLiteral("shape"), QStringLiteral("effects")}) {
        auto values = result.value(group).toMap();
        const auto supplied = variant.value(group).toObject();
        for (auto it = supplied.begin(); it != supplied.end(); ++it) values.insert(it.key(), it.value().toVariant());
        result.insert(group, values);
    }
    return result;
}
}

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
{
    if (error) error->clear();
    if (QJsonDocument(document).toJson(QJsonDocument::Compact).size() > 256 * 1024
        || !keys(document, {"contract", "id", "variants"})
        || document.value("contract").toString() != QLatin1String("theme@1"))
        return fail(error, tr("The app cannot use this theme contract."));
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,95}$"));
    const auto id = document.value("id").toString();
    if (!identifier.match(id).hasMatch() || id == QLatin1String("__proto__") || id == QLatin1String("constructor") || id == QLatin1String("prototype"))
        return fail(error, tr("The theme identifier is not correct."));
    if (!document.value("variants").isObject()) return fail(error, tr("The theme variants are not correct."));
    const auto variants = document.value("variants").toObject();
    if (variants.isEmpty() || !keys(variants, {"light", "dark"})) return fail(error, tr("The theme variants are not correct."));
    const auto knownColors = defaults(false).value(QStringLiteral("colors")).toMap();
    for (auto it = variants.begin(); it != variants.end(); ++it) {
        if (!it.value().isObject()) return fail(error, tr("A theme variant must be an object."));
        const auto variant = it.value().toObject();
        if (!keys(variant, {"colors", "shape", "effects"})) return fail(error, tr("The app cannot use this theme group."));
        for (const auto& group : {QStringLiteral("colors"), QStringLiteral("shape"), QStringLiteral("effects")})
            if (variant.contains(group) && !variant.value(group).isObject()) return fail(error, tr("A theme token group is not correct."));
        const auto colors = variant.value("colors").toObject();
        for (auto color = colors.begin(); color != colors.end(); ++color)
            if (!knownColors.contains(color.key()) || !color.value().isString() || !hexColor(color.value().toString()))
                return fail(error, tr("The app cannot use this theme color token."));
        const auto shape = variant.value("shape").toObject();
        if (!keys(shape, {"controlRadius", "panelRadius"})) return fail(error, tr("The app cannot use this theme shape token."));
        for (auto item = shape.begin(); item != shape.end(); ++item)
            if (!item.value().isDouble() || !std::isfinite(item.value().toDouble()) || item.value().toDouble() < 0
                || item.value().toDouble() > (item.key() == QLatin1String("controlRadius") ? 12 : 20))
                return fail(error, tr("A theme shape token is not within the permitted limits."));
        const auto effects = variant.value("effects").toObject();
        if (!keys(effects, {"panelOpacity"})) return fail(error, tr("The app cannot use this theme effect token."));
        if (effects.contains("panelOpacity") && (!effects.value("panelOpacity").isDouble()
            || !std::isfinite(effects.value("panelOpacity").toDouble()) || effects.value("panelOpacity").toDouble() < .85
            || effects.value("panelOpacity").toDouble() > 1)) return fail(error, tr("The theme opacity is not within the permitted limits."));
        const auto resolved = merged(variant, it.key() == QLatin1String("light")).value("colors").toMap();
        if (colors.contains("status.danger") && !readableStatus(resolved.value("status.danger").toString(), resolved))
            return fail(error, tr("The theme status text contrast is less than the minimum."));
        for (const auto& surface : {QStringLiteral("surface.base"), QStringLiteral("surface.panel"), QStringLiteral("surface.field"), QStringLiteral("surface.hover"), QStringLiteral("surface.selected")}) {
            if ((surface == QLatin1String("surface.hover") || surface == QLatin1String("surface.selected"))
                && !colors.contains(surface) && !colors.contains("text.primary") && !colors.contains("text.secondary") && !colors.contains("focus.ring")) continue;
            for (const auto& foreground : {QStringLiteral("text.primary"), QStringLiteral("text.secondary")})
                if (contrast(resolved.value(foreground).toString(), resolved.value(surface).toString()) < 4.5)
                    return fail(error, tr("The theme text contrast is less than the minimum."));
            if (contrast(resolved.value("focus.ring").toString(), resolved.value(surface).toString()) < 3)
                return fail(error, tr("The theme focus indicator does not have enough contrast."));
        }
    }
    return true;
}
QVariantMap AddonThemeService::resolve(const QJsonObject& document, bool light, const QString& accent, bool highContrast, bool reducedTransparency)
{
    const auto variant = validate(document) ? document.value("variants").toObject().value(light ? "light" : "dark").toObject() : QJsonObject{};
    auto result = merged(variant, light), colors = result.value("colors").toMap();
    if (hexColor(accent)) colors.insert(QStringLiteral("accent.primary"), accent.toLower());
    auto effects = result.value("effects").toMap();
    if (highContrast) {
        const auto background = light ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
        const auto foreground = light ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
        for (const auto& surface : {QStringLiteral("surface.base"), QStringLiteral("surface.panel"), QStringLiteral("surface.field")}) colors.insert(surface, background);
        colors.insert(QStringLiteral("surface.hover"), light ? "#dddddd" : "#333333");
        colors.insert(QStringLiteral("surface.selected"), light ? "#cccccc" : "#444444");
        colors.insert(QStringLiteral("text.primary"), foreground); colors.insert(QStringLiteral("text.secondary"), foreground);
        colors.insert(QStringLiteral("accent.primary"), light ? "#00428f" : "#ffff00");
        colors.insert(QStringLiteral("status.danger"), foreground);
        colors.insert(QStringLiteral("focus.ring"), colors.value("accent.primary")); colors.insert(QStringLiteral("border.default"), foreground);
        effects.insert(QStringLiteral("panelOpacity"), 1);
    }
    if (reducedTransparency) effects.insert(QStringLiteral("panelOpacity"), 1);
    if (!readableStatus(colors.value(QStringLiteral("status.danger")).toString(), colors)) {
        const auto fallback = light ? QStringLiteral("#681409") : QStringLiteral("#ffb4a6");
        colors.insert(QStringLiteral("status.danger"), readableStatus(fallback, colors) ? fallback : colors.value(QStringLiteral("text.primary")));
    }
    const auto resolvedAccent = colors.value(QStringLiteral("accent.primary")).toString();
    colors.insert(QStringLiteral("text.onAccent"), contrast(resolvedAccent, QStringLiteral("#161616")) >= 4.5 ? QStringLiteral("#161616")
        : contrast(resolvedAccent, QStringLiteral("#000000")) >= contrast(resolvedAccent, QStringLiteral("#ffffff")) ? QStringLiteral("#000000") : QStringLiteral("#ffffff"));
    // Read-only host roles preserve the redesigned built-in palette while mapping
    // a selected theme consistently onto QML, native dialog and menu surfaces.
    const bool customized = !variant.isEmpty() || highContrast;
    colors.insert(QStringLiteral("surface.menu"), customized ? colors.value("surface.panel") : QVariant(light ? "#acacac" : "#191919"));
    colors.insert(QStringLiteral("surface.menuHover"), customized ? colors.value("surface.hover") : QVariant(light ? "#d5d5d5" : "#1f1f1f"));
    colors.insert(QStringLiteral("border.menu"), customized ? colors.value("border.default") : QVariant(light ? "#969696" : "#222222"));
    colors.insert(QStringLiteral("text.muted"), customized ? colors.value("text.secondary") : QVariant(light ? "#404040" : "#6b6b6b"));
    colors.insert(QStringLiteral("text.disabled"), customized ? colors.value("text.secondary") : QVariant(light ? "#737373" : "#595959"));
    colors.insert(QStringLiteral("surface.library"), customized ? colors.value("surface.panel") : QVariant(light ? "#c3c3c3" : "#0e0e0e"));
    colors.insert(QStringLiteral("surface.floating"), customized ? colors.value("surface.panel") : QVariant(light ? "#c3c3c3" : "#0e0e0e"));
    colors.insert(QStringLiteral("surface.libraryOverlay"), customized ? colors.value("surface.panel") : QVariant(light ? "#e7e7e7" : "#0e0e0e"));
    colors.insert(QStringLiteral("surface.navigation"), customized ? colors.value("surface.base") : QVariant(light ? "#acacac" : "#191919"));
    colors.insert(QStringLiteral("surface.navigationOverlay"), customized ? colors.value("surface.base") : QVariant(light ? "#d5d5d5" : "#191919"));
    colors.insert(QStringLiteral("text.heading"), customized ? colors.value("text.primary") : QVariant(light ? "#2e2e2e" : "#c6c6c6"));
    colors.insert(QStringLiteral("text.playerHeading"), customized ? colors.value("text.secondary") : QVariant(light ? "#2e2e2e" : "#969696"));
    colors.insert(QStringLiteral("subtitle.cue"), customized ? colors.value("surface.field") : QVariant(light ? "#a3a3a3" : "#202020"));
    colors.insert(QStringLiteral("subtitle.cueText"), customized ? colors.value("text.primary") : QVariant(light ? "#161616" : "#e5e5e5"));
    colors.insert(QStringLiteral("status.dangerBackground"), customized ? colors.value("surface.panel") : QVariant(light ? "#f8e2d9" : "#1a1111"));
    auto shape = result.value("shape").toMap();
    shape.insert(QStringLiteral("menuRadius"), variant.value("shape").toObject().contains("panelRadius") ? shape.value("panelRadius") : QVariant(10));
    result.insert(QStringLiteral("shape"), shape);
    result.insert(QStringLiteral("customized"), customized); result.insert(QStringLiteral("highContrast"), highContrast);
    result.insert(QStringLiteral("colors"), colors); result.insert(QStringLiteral("effects"), effects);
    return result;
}
QVariantMap AddonThemeService::tokens(bool light, const QString& accent, bool contrast, bool reducedTransparency) const
{
    const auto effectiveAccent = accent.isEmpty() ? m_userAccent : accent;
    const auto key = QString::number(light) + QLatin1Char(':') + effectiveAccent + QLatin1Char(':')
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
    const auto value = hexColor(option) ? option.toLower() : QString();
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
