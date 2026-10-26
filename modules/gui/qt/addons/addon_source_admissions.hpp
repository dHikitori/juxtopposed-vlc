// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVariantList>
#include <QJsonObject>
#include <QStringList>
#include <functional>

namespace vlc::addons {
// An admission proves that the host accepted this exact source/account/item. It does not
// grant source activation, account access, or playback authority.
class AddonSourceAdmissions
{
public:
    using Validator = std::function<bool(const QVariantMap&)>;
    AddonSourceAdmissions(QString directory, Validator currentSource);
    bool admit(const QVariantList& references, QStringList* uris, QString* error = nullptr);
    QVariantMap admittedReference(const QString& uri) const;
    QVariantMap currentReference(const QString& uri) const;
    QString uriForReference(const QVariantMap& reference) const;
    static QVariantMap decodeUntrusted(const QString& uri, QString* admissionId = nullptr);
private:
    bool read(QJsonObject*, QString*) const;
    bool write(const QJsonObject&, QString*) const;
    QString m_directory;
    Validator m_currentSource;
};
}
