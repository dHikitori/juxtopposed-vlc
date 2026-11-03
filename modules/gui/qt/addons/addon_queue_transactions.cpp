// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_queue_transactions.hpp"
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>

namespace vlc::addons {
namespace {
bool boundedIdentifier(const QVariant& value)
{
    if (value.metaType().id() != QMetaType::QString) return false;
    const auto text = value.toString(); if (text.isEmpty() || text.size() > 512) return false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto character = text[i];
        if (character.isNull() || character.category() == QChar::Other_Control || character.isLowSurrogate()) return false;
        if (character.isHighSurrogate() && (++i >= text.size() || !text[i].isLowSurrogate())) return false;
    }
    return true;
}
}
AddonQueueTransactions::AddonQueueTransactions(Backend backend, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)), m_gate(std::make_shared<Gate>())
{ m_gate->owner = this; }
AddonQueueTransactions::~AddonQueueTransactions()
{
    { std::lock_guard<std::mutex> lock(m_gate->mutex); m_gate->owner = nullptr; }
    const auto jobs = m_jobs; m_jobs.clear();
    for (const auto& job : jobs) {
        if (job.cancel) job.cancel();
        if (!job.transaction.playbackHandle.isEmpty() && m_backend.releasePlayable) m_backend.releasePlayable(job.transaction.playbackHandle);
    }
}
bool AddonQueueTransactions::validReference(const QVariantMap& reference)
{
    static const QRegularExpression source(QStringLiteral("^[a-z][a-z0-9.-]{1,127}/[a-z][a-z0-9.-]{0,63}\\z"));
    if (reference.value("format").toString() != QLatin1String("hakone-addon-source-v1")
        || !source.match(reference.value("source").toString()).hasMatch()
        || !boundedIdentifier(reference.value("itemId")) || !boundedIdentifier(reference.value("accountReference"))
        || reference.value("snapshot").metaType().id() != QMetaType::QVariantMap) return false;
    for (auto it = reference.begin(); it != reference.end(); ++it)
        if (!QStringList{"format", "source", "itemId", "accountReference", "snapshot"}.contains(it.key())) return false;
    const auto display = reference.value("snapshot").toMap();
    for (auto it = display.begin(); it != display.end(); ++it)
        if ((it.key() != QLatin1String("title") && it.key() != QLatin1String("mediaKind"))
            || it.value().metaType().id() != QMetaType::QString || it.value().toString().size() > (it.key() == QLatin1String("mediaKind") ? 32 : 512)) return false;
    return true;
}
AddonQueueTransactions::Snapshot AddonQueueTransactions::snapshot(const QString& target) const
{ return m_backend.snapshot ? m_backend.snapshot(target) : Snapshot{}; }
void AddonQueueTransactions::releasePlayable(const QString& handle)
{ if (!handle.isEmpty() && m_backend.releasePlayable) m_backend.releasePlayable(handle); }
QString AddonQueueTransactions::append(const QString& view, const QVariantList& references, const QString& target,
                                      const QString& fillTag, const QString& fillCredit)
{
    Transaction transaction; transaction.view = view; transaction.target = target; transaction.references = references;
    transaction.fillTag = fillTag.left(128); transaction.fillCredit = fillCredit.left(256);
    return start(transaction, snapshot(target), false);
}
QString AddonQueueTransactions::play(const QString& view, const QVariantMap& reference, const QString& handle,
                                    const Snapshot& expected, const QString& target)
{
    Transaction transaction; transaction.view = view; transaction.target = target; transaction.references = {reference}; transaction.playbackHandle = handle;
    return start(transaction, expected, true);
}
QString AddonQueueTransactions::playReference(const QString& view, const QVariantMap& reference, const Snapshot& expected, const QString& target)
{
    Transaction transaction; transaction.view=view; transaction.target=target; transaction.references={reference};
    return start(std::move(transaction),expected,true,true);
}
QString AddonQueueTransactions::start(Transaction transaction, const Snapshot& expected, bool playing, bool stableReference)
{
    transaction.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    transaction.targetIdentity = expected.identity; transaction.expectedRevision = expected.revision;
    const auto id = transaction.id;
    QString error;
    const auto current = snapshot(transaction.target);
    if (m_jobs.size() >= 32 || transaction.view.isEmpty() || transaction.view.size() > 128 || transaction.references.isEmpty() || transaction.references.size() > 200)
        error = tr("The queue transaction is larger than the limit.");
    else if (!expected.available || expected.identity.isEmpty() || !current.available || current.identity != expected.identity || current.revision != expected.revision)
        error = tr("The destination changed. Select the items again.");
    else if (playing ? (stableReference ? !referencePlayAvailable() : !playAvailable() || !QRegularExpression(QStringLiteral("^[A-Za-z0-9_.:-]{1,256}\\z")).match(transaction.playbackHandle).hasMatch()) : !appendAvailable())
        error = tr("This host cannot apply the discovery action.");
    else for (const auto& reference : transaction.references) if (reference.metaType().id() != QMetaType::QVariantMap || !validReference(reference.toMap())) {
        error = tr("A discovery item reference is not correct."); break;
    }
    if (!error.isEmpty()) {
        if (!transaction.playbackHandle.isEmpty() && m_backend.releasePlayable) m_backend.releasePlayable(transaction.playbackHandle);
        QTimer::singleShot(0, this, [this, id, error] { emit finished(id, false, error); }); return id;
    }
    m_jobs.insert(id, {transaction, {}});
    const auto gate = m_gate;
    auto completion = [gate, id](bool success, const QString& error) {
        std::lock_guard<std::mutex> lock(gate->mutex);
        if (auto* owner = gate->owner) QMetaObject::invokeMethod(owner, [owner, id, success, error] { owner->finish(id, success, error); }, Qt::QueuedConnection);
    };
    const QPointer<AddonQueueTransactions> owner(this);
    const auto execute=playing ? (stableReference ? m_backend.playReference : m_backend.play) : m_backend.append;
    auto cancel=execute(transaction,completion);
    if(!owner) { if(cancel) cancel(); return id; }
    if (m_jobs.contains(id)) m_jobs[id].cancel = std::move(cancel); else if (cancel) cancel();
    QTimer::singleShot(15000, this, [this, id] { if (m_jobs.contains(id)) finish(id, false, tr("The queue transaction time limit has ended.")); });
    return id;
}
void AddonQueueTransactions::finish(const QString& id, bool success, const QString& error)
{
    if (!m_jobs.contains(id)) return;
    const auto job=m_jobs.take(id); const auto release=m_backend.releasePlayable;
    const auto message=error.left(512); const QPointer<AddonQueueTransactions> owner(this);
    if(!success && job.cancel) job.cancel();
    if(!owner) return;
    if(!success && !job.transaction.playbackHandle.isEmpty() && release) release(job.transaction.playbackHandle);
    if(owner) emit finished(id,success,message);
}
void AddonQueueTransactions::cancelView(const QString& view)
{
    const auto ids=m_jobs.keys(); const QPointer<AddonQueueTransactions> owner(this);
    for(const auto& id:ids) if(m_jobs.value(id).transaction.view==view) { cancel(id); if(!owner) return; }
}
void AddonQueueTransactions::cancel(const QString& id)
{
    if (!m_jobs.contains(id)) return;
    const auto job=m_jobs.take(id); const auto release=m_backend.releasePlayable;
    const auto error=tr("The discovery action was canceled."); const QPointer<AddonQueueTransactions> owner(this);
    if(job.cancel) job.cancel();
    if(!owner) return;
    if(!job.transaction.playbackHandle.isEmpty() && release) release(job.transaction.playbackHandle);
    if(owner) emit finished(id,false,error);
}
}
