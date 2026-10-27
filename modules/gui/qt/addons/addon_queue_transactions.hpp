// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QHash>
#include <QVariantList>
#include <functional>
#include <memory>
#include <mutex>

namespace vlc::addons {
// Only host adapters receive a transaction. A package cannot call a commit API.
class AddonQueueTransactions final : public QObject
{
    Q_OBJECT
public:
    struct Snapshot { bool available = false; QString identity; quint64 revision = 0; };
    struct Transaction {
        QString id, view, target, targetIdentity;
        quint64 expectedRevision = 0;
        QVariantList references;
        QString playbackHandle;
        // The host captures this query before admission. A native adapter does a
        // check under its final write lock. It has no I/O, signal, or QObject call.
        std::function<bool()> authorityCurrent;
    };
    using Completion = std::function<void(bool, const QString&)>;
    using Cancel = std::function<void()>;
    struct Backend {
        std::function<Snapshot(const QString& target)> snapshot;
        // The adapter must do the last target identity/revision check under its write lock.
        // It commits the whole list once, or commits nothing. It must not resolve
        // a reference to a URL during an append operation.
        std::function<Cancel(const Transaction&, Completion)> append;
        // A host adapter consumes the one-use native playback handle only after
        // its last identity/revision check. No URL or input pointer enters QML.
        std::function<Cancel(const Transaction&, Completion)> play;
        std::function<void(const QString& handle)> releasePlayable;
        // A trusted native lazy resolver opens the saved reference at playback.
        // This callback must not ask a provider for a stream URL before enqueue.
        std::function<Cancel(const Transaction&, Completion)> playReference;
    };
    explicit AddonQueueTransactions(Backend, QObject* parent = nullptr);
    ~AddonQueueTransactions() override;
    bool appendAvailable() const { return bool(m_backend.snapshot) && bool(m_backend.append); }
    bool referencePlayAvailable() const { return bool(m_backend.snapshot) && bool(m_backend.playReference); }
    bool playAvailable() const { return bool(m_backend.snapshot) && bool(m_backend.play); }
    QString append(const QString& view, const QVariantList& references, const QString& target = QStringLiteral("queue"));
    QString play(const QString& view, const QVariantMap& reference, const QString& playbackHandle,
                 const Snapshot& expected, const QString& target = QStringLiteral("queue"));
    QString playReference(const QString& view, const QVariantMap& reference, const Snapshot& expected,
                          const QString& target = QStringLiteral("queue"));
    Snapshot snapshot(const QString& target = QStringLiteral("queue")) const;
    void releasePlayable(const QString& handle);
    void cancelView(const QString& view);
    void cancel(const QString& transaction);
    static bool validReference(const QVariantMap&);
signals:
    void finished(const QString& transaction, bool success, const QString& error);
private:
    struct Job { Transaction transaction; Cancel cancel; };
    struct Gate { std::mutex mutex; AddonQueueTransactions* owner = nullptr; };
    QString start(Transaction, const Snapshot&, bool play, bool stableReference = false);
    void finish(const QString&, bool, const QString&);
    Backend m_backend;
    QHash<QString, Job> m_jobs;
    std::shared_ptr<Gate> m_gate;
};
}
