// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addons/addon_stream_lifetime.hpp"
#include "mltaskid.hpp"
#include <QObject>
#include <functional>
#include <memory>

// Call this owner on its Qt thread. Close does not wait for a worker.
// Keep each receiver on this thread until delivery or cancellation.
// The app lifetime joins each worker, its captures, and its Qt thread storage.
class MLJoinedThreadRunner final : public QObject
{
public:
    using Handle=vlc::addons::AddonStreamLifetime::Handle;
    explicit MLJoinedThreadRunner(Handle,QObject* parent=nullptr);
    ~MLJoinedThreadRunner() override;
    void close();
    bool cancelTask(const QObject*,quint64);
    static unsigned processTaskCount() noexcept;
    static constexpr unsigned OwnerLimit=16,ProcessLimit=16;

    template<typename Ctx>
    quint64 runOnThread(const QObject* receiver,std::function<void(Ctx&)> work,
                        std::function<void(quint64,Ctx&)> complete)
    {
        const auto context=std::make_shared<Ctx>();
        return submit(receiver,[context,work=std::move(work)] { work(*context); },
            [context,complete=std::move(complete)](quint64 id) { complete(id,*context); });
    }
private:
    struct Shared;
    struct Entry;
    quint64 submit(const QObject*,std::function<void()>,std::function<void(quint64)>);
    void completed(quint64);
    void receiverDestroyed(const QObject*);
    Handle m_lifetime;
    std::shared_ptr<Shared> m_shared;
    struct Entries;
    std::unique_ptr<Entries> m_entries;
};
