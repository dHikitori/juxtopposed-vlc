// SPDX-License-Identifier: GPL-2.0-or-later
// Host C++ only. No guest constructor, QVariant conversion or QML API.
#pragma once
#include "addon_stream_lifetime.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QHash>
#include <QStringList>
#include <QSet>
#include <functional>
#include <memory>
#include <optional>

namespace vlc::addons {
class AddonManager;
class AddonWorker;
class AddonMetadataAttemptAdmission;
struct MetadataAttemptBudgetLease;

// Implement this interface with the Core admission handle on the native side.
// It and its destructor must use native/std state only. It must not hold a job,
// QObject, Qt value, callback into Qt, disk reader or full authority reader.
// This keeps the pure-host library independent of native Core symbols.
class AddonMetadataNativeAdmission {
public:
    virtual ~AddonMetadataNativeAdmission() = default;
    virtual bool current() const noexcept = 0;
    virtual void revoke() noexcept = 0; // repeated calls join an admitted commit
};

enum class AddonMetadataHostTerminal {
    Complete, Failed, Denied, Unavailable, Timeout, Canceled, Incomplete, Malformed
};
struct AddonMetadataHostObservation {
    AddonMetadataHostTerminal terminal = AddonMetadataHostTerminal::Failed;
    bool network = false;
    int httpStatus = 0;
};

class AddonMetadataCompletionReceipt final {
public:
    // Match/Partial certify the exact completed invocation and result bytes.
    // The consumer must still validate identity, fields and image authority.
    enum class Outcome { Invalid, Match, Partial, NoMatch };
    AddonMetadataCompletionReceipt(AddonMetadataCompletionReceipt&&) noexcept;
    AddonMetadataCompletionReceipt& operator=(AddonMetadataCompletionReceipt&&) noexcept;
    ~AddonMetadataCompletionReceipt();
    AddonMetadataCompletionReceipt(const AddonMetadataCompletionReceipt&) = delete;
    AddonMetadataCompletionReceipt& operator=(const AddonMetadataCompletionReceipt&) = delete;
    Outcome outcome() const noexcept;
    bool current() const noexcept;
    // Exact private subject and source binding. Never compare a public title.
    bool matches(const QByteArray& subject, unsigned ordinal, const QString& contribution,
                 const QByteArray& contextDigest, const QString& partition) const;
    QByteArray resultDigest() const;
    bool matchesResult(const QJsonObject&) const;
    // This returns only the native gate, not the Qt/job-rich receipt data.
    std::shared_ptr<AddonMetadataNativeAdmission> nativeAdmission() const;
private:
    friend class AddonMetadataAttemptAdmission;
    struct Data;
    explicit AddonMetadataCompletionReceipt(std::unique_ptr<Data>);
    std::unique_ptr<Data> m_data;
};

class AddonMetadataAttemptAdmission final {
public:
    struct Capture {
        QByteArray nativeSubject; // host-issued exact-child/snapshot nonce, 32 bytes
        std::shared_ptr<void> nativeChildHold; // native Hold/Release only; no Qt capture
        unsigned ordinal = 0;
        QString contribution, feature, operation;
        QByteArray releaseDigest;
        quint64 releaseGeneration = 0, settingsRevision = 0;
        QString accountReference, partition;
        QJsonObject approvedGrants;
        QStringList requestedFields;
        AddonStreamLifetime::Handle lifetime;
        std::shared_ptr<AddonMetadataNativeAdmission> admission;
        // Memory/native only. Full saved checks run on joined work before capture.
        // Watches are retained by the aggregate's separate native keeper.
        std::function<bool()> live;
    };
    static std::shared_ptr<AddonMetadataAttemptAdmission> create(Capture);
    ~AddonMetadataAttemptAdmission();
    // Owner-thread, one-use delivery. Cache/shared completions cannot call this.
    std::optional<AddonMetadataCompletionReceipt> take();
    // Native/memory only, also safe before the owner receives a queued result.
    bool current() const noexcept;
    void revoke() noexcept;
private:
    friend class AddonManager;
    friend class AddonWorker;
    struct ManagerBinding {
        QString contribution, contract, feature, operation, accountReference, partition;
        QByteArray releaseDigest;
        quint64 releaseGeneration = 0, settingsRevision = 0;
        QJsonObject approvedGrants, context;
        QStringList requestedFields;
        // Parsed by Manager from the exact installed contribution, never JSON
        // supplied by the worker. Empty is the default. Only 404/410 may enter.
        QSet<int> absentHttpStatuses;
    };
    struct BoundScope;
    enum class Phase { Captured, ManagerBound, WorkerBound, Dispatched, Terminal, Blocked, Taken };
    explicit AddonMetadataAttemptAdmission(Capture, std::shared_ptr<MetadataAttemptBudgetLease>);
    bool bindManager(ManagerBinding);
    bool bindWorker(const QString& instance, const QString& invocation,
                    const QString& localContribution, const QString& operation,
                    const QJsonObject& actualContext);
    bool dispatched(const QString& invocation);
    bool hostStarted(const QString& invocation, const QString& id, const QString& operation);
    void hostFinished(const QString& invocation, const QString& id, AddonMetadataHostObservation);
    void terminal(const QString& invocation, const QString& status,
                  const QJsonObject& data, const QString& error);
    void blocked() noexcept;
    bool ownerThread() const noexcept;
    // Released last, after all scope/receipt/callback captures are destroyed.
    const std::shared_ptr<MetadataAttemptBudgetLease> m_budget;
    const Capture m_capture;
    const void* const m_thread;
    std::unique_ptr<const BoundScope> m_scope;
    std::unique_ptr<AddonMetadataCompletionReceipt::Data> m_receipt;
    QHash<QString, bool> m_calls; // false=pending; true=completed; no raw request data
    Phase m_phase = Phase::Captured;
};
}
