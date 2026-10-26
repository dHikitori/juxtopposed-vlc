// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_ADDON_MANIFEST_HPP
#define VLC_QT_ADDON_MANIFEST_HPP

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QUrl>

namespace vlc::addons {

struct SemanticVersion
{
    quint64 major = 0, minor = 0, patch = 0;
    QString prerelease;
    static bool parse(const QString&, SemanticVersion*);
    int compare(const SemanticVersion&) const;
};

bool versionSatisfies(const QString& version, const QString& range);
bool versionSatisfies(const QString& version, const QString& range, bool allowPrerelease);
bool portablePackagePath(const QString& path);
bool validateUiSchema(const QJsonObject& schema);
bool validateUiValue(const QJsonObject& schema, const QJsonValue& value);
// Returns an empty string for non-HTTPS origins, credentials, paths or fragments.
QString exactHttpsOrigin(const QString& origin);

struct Contribution
{
    QString id;
    QString type;
    QString contract;
    QStringList features, mediaKinds, operations, requires;
    QJsonObject document;
};

struct Manifest
{
    static constexpr qsizetype MaximumManifestBytes = 256 * 1024;
    static constexpr qsizetype MaximumFiles = 256;
    static constexpr qint64 MaximumFileBytes = 16 * 1024 * 1024;
    static constexpr qint64 MaximumPackageBytes = 64 * 1024 * 1024;

    QString id, name, version, publisher, license;
    QUrl source;
    QString runtimeKind, runtimeEntry;
    QStringList files;
    QList<Contribution> contributions;
    QJsonObject requiredPermissions, optionalPermissions;
    QString settingsSchema, settingsUi;
    QStringList accountFields;
    int settingsVersion = 0;
    QJsonObject document;

    static bool parse(const QByteArray&, Manifest*, QString* error = nullptr);
    static bool loadDirectory(const QString&, Manifest*, QMap<QString, QByteArray>* contents = nullptr,
                              QString* error = nullptr);
    bool permissionDeclared(const QString& capability) const;
    QJsonObject permissionScope(const QString& capability) const;
    const Contribution* contribution(const QString& localId) const;
};

// SHA-256 over "vlc-addon-package-v1\n" and a sorted, length-framed file map.
// Each record is path UTF-8 length, path, byte length, then binary SHA-256.
QByteArray packageDigest(const QMap<QString, QByteArray>& files);

} // namespace vlc::addons
#endif
