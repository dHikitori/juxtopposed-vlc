// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QHash>
#include <QString>

namespace vlc::addons {

// Deliberately not a QObject: credentials cannot be read through QML/reflection.
// References, rather than values, can be stored in a package settings record.
class CredentialStore
{
public:
    struct Created { QString reference; bool persistent = false; };
    Created create(const QString& packageId, const QString& field, const QByteArray& value, bool allowSessionOnly = false);
    static bool persistentAvailable();
    QByteArray read(const QString& packageId, const QString& reference) const;
    bool present(const QString& packageId, const QString& reference) const;
    bool sessionOnly(const QString& reference) const;
    void remove(const QString& packageId, const QString& reference);
    void clearSession();
private:
    struct Secret { QString packageId; QByteArray value; bool persistent; };
    mutable QHash<QString, Secret> m_session;
    static bool platformWrite(const QString&, const QString&, const QByteArray&);
    static QByteArray platformRead(const QString&, const QString&);
    static void platformRemove(const QString&, const QString&);
};
}
