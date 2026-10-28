// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_lifetime.hpp"
#include <QObject>
#include <QHash>
#include <QTimer>
#include <functional>
#include <memory>
#include <vlc_common.h>
#include <vlc_meta_fetcher.h>
#include <vlc_metadata_backend.h>
typedef struct vlc_metadata_native_claim vlc_metadata_native_claim_t;
typedef struct vlc_metadata_artwork_admission vlc_metadata_artwork_admission_t;
typedef struct vlc_metadata_native_outcome_authority vlc_metadata_native_outcome_authority_t;

// The native parent must remain valid until the counted join ends. Call this
// QObject on its owner thread. Native cancellation has no Qt dependency.
// close() withdraws admission at once, including concurrent repeated native
// retirement. The app join drains all native calls and retirement stacks.
class AddonNativeProviderContext final : public QObject
{
public:
    struct Result { QString claim; int status = VLC_EGENERIC; };
    using Completion = std::function<void(const Result&)>;
    using Cancel = std::function<void()>;
    AddonNativeProviderContext(vlc::addons::AddonStreamLifetime::Handle, vlc_object_t*, QObject* parent = nullptr);
    ~AddonNativeProviderContext() override;
    Cancel probe(vlc_metadata_job_t*, meta_fetcher_provider_t, Completion);
    // Host only. The opaque result can carry a closed native outcome. It does
    // not grant an image or a final status write.
    Cancel probeOutcome(vlc_metadata_job_t*, meta_fetcher_provider_t, Completion);
    struct NegativeOutcome {
        meta_fetcher_outcome_t value = META_FETCHER_OUTCOME_UNKNOWN;
        std::shared_ptr<vlc_metadata_native_outcome_authority_t> authority;
        explicit operator bool() const noexcept { return bool(authority); }
    };
    // Consume one exact negative receipt while its child remains pending.
    // Keep this native-only authority and admission through final Apply.
    NegativeOutcome takeOutcome(const QString&, vlc_metadata_job_t*, meta_fetcher_provider_t,
                                vlc_metadata_artwork_admission_t*);
    // This query has no I/O or callback. A media URL cannot create a claim.
    bool validates(const QString& claim, vlc_metadata_job_t*, meta_fetcher_provider_t) const;
    void close();
    bool available() const;
    bool joined() const noexcept { return m_retirement.joined(); }
private:
    friend class AddonNativeProviderArtworkAssets;
    using Claim = std::shared_ptr<vlc_metadata_native_claim_t>;
    Claim takeClaim(const QString&, vlc_metadata_job_t*, meta_fetcher_provider_t, bool outcome = false);
    Cancel probe(vlc_metadata_job_t*, meta_fetcher_provider_t, Completion, bool outcome);
    struct State;
    void poll();
    std::shared_ptr<State> m_state;
    vlc::addons::AddonStreamLifetime::NetworkTicket m_retirement;
    QHash<quint64, Completion> m_completions;
    QTimer m_poll;
    quint64 m_sequence = 0;
    bool m_admitted = false;
};
