// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_lifetime.hpp"
#include <QObject>
#include <QHash>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>
#include <vlc_common.h>
#include <vlc_interrupt.h>
#include <vlc_metadata_backend.h>
typedef struct vlc_metadata_native_claim vlc_metadata_native_claim_t;
class AddonNativeProviderArtworkAssets;
namespace vlc::addons { class AddonMetadataNativeAdmission; }

/** This adapter accepts native job snapshots only. It has no guest or QML API.
 * Call the Qt API on the owner thread. Construction requires the app lifetime.
 * The native parent must outlive its joined cleanup. Core closes its metadata
 * backend before interface drain; interface cleanup precedes parent teardown.
 * A Cancel function can run on any thread. close() withdraws authority at once.
 * Counted app work joins native threads without a Qt event or a GUI wait.
 */
class AddonNativeArtworkAssets : public QObject
{
public:
    struct Options {
        qint64 maximumEncodedBytes = 32 * 1024 * 1024;
        qint64 maximumPixelBytes = 128 * 1024 * 1024;
        qint64 maximumCacheBytes = 512 * 1024 * 1024;
        int maximumDimension = 16384;
        int maximumOutputDimension = 4096;
        int maximumEntries = 8192;
        int timeoutMs = 10000;
    };
    struct ReadResult { QByteArray bytes; QString error; };
    struct Backend {
        // The native interrupt context is set before this call. live() reads
        // native state only. Refuse an obsolete request before each I/O step.
        // Capture owned data. The app retains this callback through its join.
        std::function<ReadResult(const QUrl&, qint64, const std::function<bool()>& live)> read;
    };
    class Permit {
    public:
        bool current() const;
        // Bind only the native gate. Consent change/close revokes and joins it.
        // No Permit, app lifetime, job or Qt value enters the final keeper.
        bool arm(const std::shared_ptr<vlc::addons::AddonMetadataNativeAdmission>&) const noexcept;
        // Use only for a native, callback-free commit such as job_Apply.
        // The function must not call this adapter or change network consent.
        int admit(const std::function<int()>& commit) const;
    private:
        friend class AddonNativeArtworkAssets;
        struct Consent;
        std::shared_ptr<Consent> m_consent;
        quint64 m_epoch = 0;
    };
    struct Result { QUrl file; QString error; Permit permit; };
    using Completion = std::function<void(const Result&)>;
    using Cancel = std::function<void()>;
    AddonNativeArtworkAssets(vlc::addons::AddonStreamLifetime::Handle, vlc_object_t*,
        const QString& cacheDirectory, const Options&, QObject* parent = nullptr);
    AddonNativeArtworkAssets(vlc::addons::AddonStreamLifetime::Handle, Backend,
        const QString& cacheDirectory, const Options&, QObject* parent = nullptr);
    ~AddonNativeArtworkAssets() override;
    // Only original HTTP/HTTPS artwork on an ordinary native item can enter.
    // A provider result must use its own broker, even for a native target item.
    Cancel resolve(vlc_metadata_job_t*, Completion);
    void setNetworkAllowed(bool);
    void close();
    bool available() const;
    unsigned pendingDeliveryCount() const;
    bool joined() const noexcept { return m_retirement.joined(); }
    static Backend nativeBackend(vlc_object_t*);
private:
    friend class AddonNativeProviderArtworkAssets;
    struct State;
    struct Work;
    Cancel resolveProviderClaim(vlc_metadata_native_claim_t*, vlc_metadata_job_t*, Completion);
    Cancel enqueue(vlc_metadata_job_t*, const QUrl&,
                   std::shared_ptr<vlc_metadata_native_claim_t>, Completion);
    void deliver(const std::shared_ptr<State>&, const std::shared_ptr<Work>&, Result);
    void poll();
    std::shared_ptr<State> m_state;
    vlc::addons::AddonStreamLifetime::NetworkTicket m_retirement;
    QTimer m_poll;
    QHash<quint64, Completion> m_completions;
    quint64 m_sequence = 0;
    bool m_admitted = false;
};
