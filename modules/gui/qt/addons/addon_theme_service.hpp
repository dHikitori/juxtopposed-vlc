// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QJsonObject>
#include <QPointer>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>

namespace vlc::addons {
class AddonManager;
class AddonCatalogueView;
class AddonThemeNotifications final : public QObject
{
    Q_OBJECT
signals:
    void changed();
};

// Core-owned theme selection and one semantic snapshot for all desktop adapters.
class AddonThemeService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList availableThemes READ availableThemes NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QString selectedTheme READ selectedTheme NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool usingFallback READ usingFallback NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QString userAccentOverride READ userAccentOverride NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool platformHighContrast READ platformHighContrast NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY revisionChanged FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
public:
    explicit AddonThemeService(AddonManager*, QObject* parent = nullptr);
    ~AddonThemeService() override;
    static AddonThemeService* instance();
    static AddonThemeNotifications* notifications();
    QVariantList availableThemes() const { return m_available; }
    QString selectedTheme() const { return m_selected; }
    QString errorMessage() const { return m_error; }
    bool usingFallback() const { return !m_loading && !m_selected.isEmpty() && m_active.isEmpty(); }
    bool loading() const { return m_loading; }
    QString userAccentOverride() const { return m_userAccent; }
    bool highContrast() const { return m_requestedHighContrast || m_platformHighContrast; }
    bool platformHighContrast() const { return m_platformHighContrast; }
    bool reducedMotion() const { return m_reducedMotion; }
    int revision() const { return m_revision; }
    Q_INVOKABLE bool selectTheme(const QString& id);
    Q_INVOKABLE bool setHighContrast(bool enabled);
    Q_INVOKABLE bool setReducedMotion(bool enabled);
    Q_INVOKABLE QVariantMap tokens(bool light, const QString& userAccent = QString(),
                                  bool highContrast = false, bool reducedTransparency = false) const;
    void setUserAccentOverride(const QString& option);
    void observePlatformPreferences(QObject* source);
    static bool validate(const QJsonObject&, QString* error = nullptr);
    static QVariantMap resolve(const QJsonObject&, bool light, const QString& accent = QString(),
                               bool highContrast = false, bool reducedTransparency = false);
public slots:
    void refresh();
    void refreshPlatformPreferences();
signals:
    void revisionChanged();
private:
    bool persist(const QString& selected, bool highContrast);
    bool readyForChange();
    void refreshCatalogue();
    QPointer<AddonManager> m_manager;
    QPointer<AddonCatalogueView> m_catalogue;
    QPointer<QObject> m_platformSource;
    QVariantList m_available;
    QJsonObject m_active;
    mutable QHash<QString, QVariantMap> m_tokenCache;
    QString m_selected, m_userAccent, m_error, m_settingsFile;
    bool m_requestedHighContrast = false, m_platformHighContrast = false;
    bool m_reducedMotion = false;
    bool m_catalogueMode = false, m_preferencesLoaded = false, m_loading = false;
    int m_revision = 0;
    static QPointer<AddonThemeService> s_instance;
};
}
