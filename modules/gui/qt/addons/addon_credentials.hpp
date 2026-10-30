// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>
#include <functional>

namespace vlc::addons {

// Deliberately not a QObject: credentials cannot be read through QML/reflection.
// References, rather than values, can be stored in a package settings record.
class CredentialStore
{
public:
    using Reader = std::function<QByteArray(const QString& packageId, const QString& reference)>;
    struct Created { QString reference; bool persistent = false; };
    Created create(const QString& packageId, const QString& field, const QByteArray& value, bool allowSessionOnly = false);
    static bool persistentAvailable();
    QByteArray read(const QString& packageId, const QString& reference) const;
    // A new value under an existing reference, kept where the old one was
    // (an account token refresh keeps its reference and account identity).
    bool replace(const QString& packageId, const QString& reference, const QByteArray& value);
    bool present(const QString& packageId, const QString& reference) const;
    bool sessionOnly(const QString& reference) const;
    QHash<QString, QHash<QString, bool>> presenceSnapshot() const;
    // Prepare on the store owner thread. The reader owns a serialized copy.
    // This call reads memory only. The reader can use the platform store.
    Reader snapshotReader() const;
    void remove(const QString& packageId, const QString& reference);
    // Call on an owned background store. No credential value is returned.
    bool removePackage(const QString& packageId);
    // This call changes memory only. It does not contact the platform store.
    void forgetPackage(const QString& packageId);
    void clearSession();
private:
    struct Secret { QString packageId; QByteArray value; bool persistent; };
    mutable QHash<QString, Secret> m_session;
    static bool platformWrite(const QString&, const QString&, const QByteArray&);
    static QByteArray platformRead(const QString&, const QString&);
    static void platformRemove(const QString&, const QString&);
};
}
