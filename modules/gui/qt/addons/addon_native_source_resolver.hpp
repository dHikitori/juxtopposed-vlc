// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_source_consent.hpp"
#include "addon_stream_lifetime.hpp"
#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

struct vlc_object_t;
struct vlc_input_policy;
struct vlc_source_backend;
struct vlc_source_job;
struct vlc_source_lease;

namespace vlc::addons {
// Call this owner on its Qt thread. Native callbacks never wait for that thread.
class AddonNativeSourceResolver final : public QObject
{
public:
    using Cancel = std::function<void()>;
    using Current = std::function<bool()>;
    using Started = std::function<void(int status)>;
    struct Admission {
        std::shared_ptr<AddonSourceConsent> consent;
        AddonSourceConsent::Authorization authorization;
        // Use captured host data. Do not call a QObject here.
        // Run the full check on a background admission worker.
        Current configurationCurrent;
        // This check must use memory only. It must not wait for file access.
        Current configurationLive;
        AddonStreamLifetime::Handle lifetime;
        bool current(const QString& uri, quint64 generation) const;
        bool authorize(const QString& uri, quint64 generation) const;
    };
    using AdmissionReader = std::function<std::optional<Admission>()>;
    class Prepared final
    {
    public:
        using StartIo = std::function<Cancel(Current, Started)>;
        // Transfer the caller's native references, even if this result is late.
        // Preparation can bind the private listener. It cannot start media I/O.
        // The native lease must clear active before its cancel callback returns.
        // Use the core proxy profile with source=NULL and attachments=false.
        static std::unique_ptr<Prepared> adopt(QByteArray proxyUri, vlc_input_policy*, vlc_source_lease*,
                                              std::shared_ptr<std::atomic_bool> active, StartIo);
        ~Prepared();
        Prepared(const Prepared&) = delete;
        Prepared& operator=(const Prepared&) = delete;
        void revoke() noexcept;
    private:
        friend class AddonNativeSourceResolver;
        Prepared() = default;
        QByteArray m_uri;
        vlc_input_policy* m_policy = nullptr;
        vlc_source_lease* m_lease = nullptr;
        std::shared_ptr<std::atomic_bool> m_active;
        StartIo m_start;
        std::atomic_bool m_revoked{false};
    };
    using PreparedCallback = std::function<void(std::unique_ptr<Prepared>)>;
    struct Backend {
        // Only the registered native primary-playback path calls admit.
        // Consume an exact PrimaryPlayback intent after current admission checks.
        // This callback runs as joined Qt work. Capture owned data.
        // Do not call a QObject or retain a raw Core instance here.
        std::function<std::optional<Admission>(const QString& uri, quint64 generation)> admit;
        // Check current after each asynchronous hop. It uses memory only.
        // Run admission.authorize on a background worker before each request.
        // Return a dormant transport. The resolver attaches it before startIo.
        std::function<Cancel(const Admission&, Current, PreparedCallback)> prepare;
        // The app must drain this owner before it destroys Qt.
        AddonStreamLifetime::Handle lifetime;
        // Optional owner-thread preparation of an exact admission reader.
        // Preparation reads memory only. The reader runs as joined background
        // work and owns all captured values, with no QObject or raw Core.
        std::function<AdmissionReader(const QString& uri, quint64 generation)> captureAdmission;
        // Register on the owner thread before the saved admission read starts.
        // The callback must revoke native state without a Qt call.
        std::function<StoreAuthorityWatch(Cancel)> watchAdmission;
    };
    struct Limits { unsigned pending = 16, issued = 64; };
    AddonNativeSourceResolver(vlc_object_t* instance, Backend, QObject* parent = nullptr);
    AddonNativeSourceResolver(vlc_object_t* instance, Backend, Limits, QObject* parent = nullptr);
    ~AddonNativeSourceResolver() override;
    bool bound() const;
    // Capture on the owner thread. The returned query uses native state only.
    Current liveGuard() const;
    void withdraw(const AddonSourceConsent::Binding&);
    void close();
    unsigned pendingCount() const { return unsigned(m_jobs.size()); }
    unsigned issuedCount();
    static unsigned admissionWorkCount() noexcept;
    static unsigned admissionWorkLimit() noexcept;
private:
    struct Gate;
    struct Guard;
    struct Pending;
    struct Issued;
    static bool post(const std::shared_ptr<Gate>&, std::function<void(AddonNativeSourceResolver*)>) noexcept;
    static int nativeDispatch(void*, vlc_source_job*);
    static void nativeCancel(void*, vlc_source_job*);
    static void nativeDestroy(void*);
    void dispatch(std::shared_ptr<vlc_source_job>);
    void admitted(const std::shared_ptr<Pending>&, std::optional<Admission>);
    void prepared(const std::shared_ptr<Pending>&, std::unique_ptr<Prepared>);
    void started(const std::shared_ptr<Pending>&, int);
    void canceled(vlc_source_job*);
    void drainCancellations();
    void fail(const std::shared_ptr<Pending>&, int);
    bool prune();
    Backend m_backend;
    Limits m_limits;
    std::shared_ptr<Gate> m_gate;
    std::shared_ptr<vlc_source_backend> m_native;
    std::unordered_map<vlc_source_job*, std::shared_ptr<Pending>> m_jobs;
    std::vector<Issued> m_issued;
    QTimer m_cancelPoll;
    bool m_bound = false;
};
}
