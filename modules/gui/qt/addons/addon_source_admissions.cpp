// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_source_admissions.hpp"
#include "addon_queue_transactions.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace vlc::addons {
namespace {
constexpr qsizetype MaximumPayload = 8192, MaximumSavedBytes = 4 * 1024 * 1024, MaximumAdmissions = 8192;
const QString prefix = QStringLiteral("hakone-addon-source:v1:");
bool fail(QString* error, const QString& message) { if (error) *error = message; return false; }
QByteArray payload(QVariantMap reference) { reference.remove(QStringLiteral("snapshot")); return QJsonDocument(QJsonObject::fromVariantMap(reference)).toJson(QJsonDocument::Compact); }
QString digest(const QByteArray& bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
bool admissionId(const QString& id)
{ static const QRegularExpression expression(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")); return expression.match(id).hasMatch(); }
QString encode(const QByteArray& bytes, const QString& id)
{ return prefix + QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) + '.' + id; }
}
AddonSourceAdmissions::AddonSourceAdmissions(QString directory, Validator currentSource)
    : m_directory(std::move(directory)), m_currentSource(std::move(currentSource)) {}
QVariantMap AddonSourceAdmissions::decodeUntrusted(const QString& uri, QString* id)
{
    if (id) id->clear();
    if (uri.size() > 12000 || !uri.startsWith(prefix)) return {};
    const auto separator = uri.lastIndexOf('.'); if (separator <= prefix.size()) return {};
    const auto token = uri.mid(separator + 1); if (!admissionId(token)) return {};
    const auto encoded = uri.mid(prefix.size(), separator - prefix.size()).toLatin1();
    const auto bytes = QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding);
    if (bytes.isEmpty() || bytes.size() > MaximumPayload
        || bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals) != encoded) return {};
    QJsonParseError error; const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
    auto reference = document.object().toVariantMap();
    if (reference.contains(QStringLiteral("snapshot"))) return {};
    reference.insert(QStringLiteral("snapshot"), QVariantMap{});
    if (!AddonQueueTransactions::validReference(reference) || payload(reference) != bytes) return {};
    if (id) *id = token;
    return reference;
}
bool AddonSourceAdmissions::read(QJsonObject* records, QString* error) const
{
    *records = {};
    QFile file(QDir(m_directory).filePath(QStringLiteral("source-admissions.json")));
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaximumSavedBytes) return fail(error, QObject::tr("The app cannot read the saved discovery references."));
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(file.readAll(), &parse); const auto root = document.object();
    if (parse.error != QJsonParseError::NoError || !document.isObject() || root.size() != 2
        || root.value("version").toInt() != 1 || !root.value("admissions").isObject())
        return fail(error, QObject::tr("The saved discovery references are not correct."));
    const auto entries = root.value("admissions").toObject();
    if (entries.size() > MaximumAdmissions) return fail(error, QObject::tr("The saved discovery references are larger than the limit."));
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (!admissionId(it.key()) || !it.value().isObject()) return fail(error, QObject::tr("The saved discovery references are not correct."));
        const auto record = it.value().toObject();
        if (record.size() != 4 || !hash.match(record.value("digest").toString()).hasMatch()
            || !record.value("source").isString() || record.value("source").toString().size() > 256
            || !record.value("account").isString() || record.value("account").toString().size() > 512 || !record.value("snapshot").isObject()
            || !AddonQueueTransactions::validReference({{"format", "hakone-addon-source-v1"}, {"source", record.value("source").toVariant()},
                {"itemId", "record"}, {"accountReference", record.value("account").toVariant()}, {"snapshot", record.value("snapshot").toVariant()}}))
            return fail(error, QObject::tr("The saved discovery references are not correct."));
    }
    *records = entries; return true;
}
bool AddonSourceAdmissions::write(const QJsonObject& records, QString* error) const
{
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"admissions", records}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaximumSavedBytes || records.size() > MaximumAdmissions)
        return fail(error, QObject::tr("The saved discovery references are larger than the limit."));
    QSaveFile file(QDir(m_directory).filePath(QStringLiteral("source-admissions.json")));
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) return fail(error, QObject::tr("The app cannot save the discovery references."));
    return true;
}
bool AddonSourceAdmissions::admit(const QVariantList& references, QStringList* uris, QString* error)
{
    uris->clear(); if (error) error->clear();
    if (!m_currentSource || references.isEmpty() || references.size() > 200 || m_directory.isEmpty())
        return fail(error, QObject::tr("The app cannot admit this discovery selection."));
    for (const auto& reference : references) if (reference.metaType().id() != QMetaType::QVariantMap
        || !AddonQueueTransactions::validReference(reference.toMap()) || payload(reference.toMap()).size() > MaximumPayload
        || !m_currentSource(reference.toMap())) return fail(error, QObject::tr("The discovery source or account is not available."));
    if (!QDir().mkpath(m_directory)) return fail(error, QObject::tr("The app cannot save the discovery references."));
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral("source-admissions.lock")));
    if (!lock.tryLock(0)) return fail(error, QObject::tr("Another operation is saving discovery references. Try again."));
    QJsonObject records; if (!read(&records, error)) return false;
    QStringList staged;
    bool changed = false;
    for (const auto& value : references) {
        const auto reference = value.toMap(); const auto bytes = payload(reference); const auto hash = digest(bytes);
        QString id;
        for (auto it = records.begin(); it != records.end(); ++it) if (it.value().toObject().value("digest").toString() == hash) { id = it.key(); break; }
        if (id.isEmpty()) {
            if (records.size() >= MaximumAdmissions) return fail(error, QObject::tr("The saved discovery reference limit has been reached."));
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            records.insert(id, QJsonObject{{"digest", hash}, {"source", reference.value("source").toString()}, {"account", reference.value("accountReference").toString()},
                {"snapshot", QJsonObject::fromVariantMap(reference.value("snapshot").toMap())}});
            changed = true;
        } else {
            auto record = records.value(id).toObject(); const auto snapshot = QJsonObject::fromVariantMap(reference.value("snapshot").toMap());
            if (record.value("snapshot").toObject() != snapshot) { record.insert(QStringLiteral("snapshot"), snapshot); records.insert(id, record); changed = true; }
        }
        staged.append(encode(bytes, id));
    }
    if (changed && !write(records, error)) return false;
    *uris = staged; return true;
}
QVariantMap AddonSourceAdmissions::admittedReference(const QString& uri) const
{
    QString id; auto reference = decodeUntrusted(uri, &id); if (reference.isEmpty()) return {};
    QJsonObject records; if (!read(&records, nullptr)) return {};
    const auto record = records.value(id).toObject();
    if (record.value("digest").toString() != digest(payload(reference)) || record.value("source").toString() != reference.value("source").toString()
        || record.value("account").toString() != reference.value("accountReference").toString()) return {};
    reference.insert(QStringLiteral("snapshot"), record.value("snapshot").toObject().toVariantMap());
    // The caller still needs current source/account grants and a playback intent.
    return reference;
}
QVariantMap AddonSourceAdmissions::currentReference(const QString& uri) const
{
    const auto reference = admittedReference(uri);
    return !reference.isEmpty() && m_currentSource && m_currentSource(reference) ? reference : QVariantMap{};
}
QString AddonSourceAdmissions::uriForReference(const QVariantMap& reference) const
{
    if (!AddonQueueTransactions::validReference(reference)) return {};
    const auto bytes = payload(reference); const auto hash = digest(bytes);
    QJsonObject records; if (!read(&records, nullptr)) return {};
    for (auto it = records.begin(); it != records.end(); ++it) {
        const auto record = it.value().toObject();
        if (record.value("digest").toString() == hash && record.value("source").toString() == reference.value("source").toString()
            && record.value("account").toString() == reference.value("accountReference").toString()) return encode(bytes, it.key());
    }
    return {};
}
}
