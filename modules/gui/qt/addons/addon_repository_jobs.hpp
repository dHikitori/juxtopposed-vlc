// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_lifetime.hpp"
#include "addon_native_notifications.hpp"
#include <QObject>
#include <functional>
#include <memory>

namespace vlc::addons {
// Use this owner on its Qt thread. Work must capture held data only.
class AddonRepositoryJobs final : public QObject {
public:
    enum class Result { Finished,Canceled,Failed };
    enum class Refusal { None,Invalid,Busy,Closed,ProcessQuota,ResourceFailure,QtAdmissionFailure };
    using Work=std::function<void()>;
    using Complete=std::function<void(Result)>;
    explicit AddonRepositoryJobs(AddonStreamLifetime::Handle,QObject* parent=nullptr);
    ~AddonRepositoryJobs() override;
    bool submit(Work,Complete);
    bool submit(Work,Complete,Refusal*);
    static NativeNotification onProcessAvailable(std::function<void()> nativeWake) noexcept;
    bool busy() const noexcept { return bool(m_job); }
    void cancel() noexcept;
    void close();
    static unsigned processCount() noexcept;
    static constexpr unsigned ProcessLimit=2;
private:
    struct Gate;
    struct Job;
    void completed(const std::shared_ptr<Job>&,bool failed);
    AddonStreamLifetime::Handle m_lifetime;
    std::shared_ptr<Gate> m_gate;
    std::shared_ptr<Job> m_job;
    Complete m_complete;
};
}
