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

// The native parent must remain valid until the counted join ends. Call this
// QObject on its owner thread. Native cancellation has no Qt dependency.
// close() withdraws admission at once; the app join drains the native context.
class AddonNativeProviderContext final : public QObject
{
public:
    struct Result { QString claim; int status = VLC_EGENERIC; };
    using Completion = std::function<void(const Result&)>;
    using Cancel = std::function<void()>;
    AddonNativeProviderContext(vlc::addons::AddonStreamLifetime::Handle, vlc_object_t*, QObject* parent = nullptr);
    ~AddonNativeProviderContext() override;
    Cancel probe(vlc_metadata_job_t*, meta_fetcher_provider_t, Completion);
    // This query has no I/O or callback. A media URL cannot create a claim.
    bool validates(const QString& claim, vlc_metadata_job_t*, meta_fetcher_provider_t) const;
    void close();
    bool available() const;
    bool joined() const noexcept { return m_retirement.joined(); }
private:
    friend class AddonNativeProviderArtworkAssets;
    using Claim = std::shared_ptr<vlc_metadata_native_claim_t>;
    Claim takeClaim(const QString&, vlc_metadata_job_t*, meta_fetcher_provider_t);
    struct State;
    void poll();
    std::shared_ptr<State> m_state;
    vlc::addons::AddonStreamLifetime::NetworkTicket m_retirement;
    QHash<quint64, Completion> m_completions;
    QTimer m_poll;
    quint64 m_sequence = 0;
    bool m_admitted = false;
};
