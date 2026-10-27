// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_source_consent.hpp"
#include <QByteArray>
#include <QObject>
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
        // Use immutable captured host data. Do not call a QObject here.
        Current configurationCurrent;
        bool current(const QString& uri, quint64 generation) const;
    };
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
        std::function<std::optional<Admission>(const QString& uri, quint64 generation)> admit;
        // Check current before each request and after each asynchronous hop.
        // Return a dormant transport. The resolver attaches it before startIo.
        std::function<Cancel(const Admission&, Current, PreparedCallback)> prepare;
    };
    struct Limits { unsigned pending = 16, issued = 64; };
    AddonNativeSourceResolver(vlc_object_t* instance, Backend, QObject* parent = nullptr);
    AddonNativeSourceResolver(vlc_object_t* instance, Backend, Limits, QObject* parent = nullptr);
    ~AddonNativeSourceResolver() override;
    bool bound() const;
    void withdraw(const AddonSourceConsent::Binding&);
    void close();
    unsigned pendingCount() const { return unsigned(m_jobs.size()); }
    unsigned issuedCount();
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
    void prepared(const std::shared_ptr<Pending>&, std::unique_ptr<Prepared>);
    void started(const std::shared_ptr<Pending>&, int);
    void canceled(vlc_source_job*);
    void fail(const std::shared_ptr<Pending>&, int);
    bool prune();
    Backend m_backend;
    Limits m_limits;
    std::shared_ptr<Gate> m_gate;
    std::shared_ptr<vlc_source_backend> m_native;
    std::unordered_map<vlc_source_job*, std::shared_ptr<Pending>> m_jobs;
    std::vector<Issued> m_issued;
    bool m_bound = false;
};
}
