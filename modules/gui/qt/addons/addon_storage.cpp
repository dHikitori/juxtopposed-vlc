// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_storage.hpp"
#include "addon_stream_lifetime.hpp"
#include <QPointer>

namespace vlc::addons {
AddonStorage::AddonStorage(PackageStore store, Capture capture, QObject* parent)
    : QObject(parent), m_store(std::move(store)), m_capture(std::move(capture)), m_state(std::make_shared<State>()) {}
AddonStorage::~AddonStorage() { close(); }
bool AddonStorage::mount(AddonHostServices* services)
{
    if (!services || m_state->closed || !m_capture) return false;
    const QPointer<AddonStorage> owner(this);
    for (const auto& operation : {QStringLiteral("storage.get"), QStringLiteral("storage.set"), QStringLiteral("storage.remove")}) {
        if (!services->registerOperation(operation, this, [owner, operation](const auto& context, const auto& request, auto done) {
            if (!owner) { done({{}, QStringLiteral("Private storage is not available.")}); return AddonHostServices::Cancel{}; }
            return owner->start(context, operation, request, std::move(done));
        })) return false;
        if (!owner) return false;
    }
    return true;
}
AddonHostServices::Cancel AddonStorage::start(const AddonHostServices::Context& host, const QString& operation,
                                             const QJsonObject& request, AddonHostServices::Completion done)
{
    const auto lifetime = AddonStreamLifetime::applicationHandle();
    if (!lifetime.current()) { done({{}, QStringLiteral("Private storage is not available.")}); return {}; }
    const auto state = m_state; const auto capture = m_capture; const auto store = m_store;
    const QPointer<AddonStorage> owner(this);
    PrivateStorageContext context; QString error;
    if (state->closed || !capture || !capture(host, &context, &error) || !owner) {
        done({{}, error.isEmpty() ? QStringLiteral("The private storage configuration changed.") : error}); return {};
    }
    // The commit admission is the cancellation boundary. No UI thread waits for disk I/O.
    const auto pending = std::make_shared<std::atomic_int>(0);
    bool busy = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (auto it = state->jobs.begin(); it != state->jobs.end();) {
            const auto active = it->lock();
            if (!active || active->load() == 3) it = state->jobs.erase(it); else ++it;
        }
        busy = state->closed || state->jobs.size() >= 4;
        if (!busy) state->jobs.append(pending);
    }
    if (busy) { done({{}, QStringLiteral("Private storage is busy. Try again.")}); return {}; }
    const auto cancel = [pending] { int expected = 0; pending->compare_exchange_strong(expected, 1); };
    auto task = [state, pending, store, context = std::move(context), operation, request, done, lifetime]() mutable {
        AddonHostServices::Result result;
        const auto current = [state, pending, lifetime] { return lifetime.current() && !state->closed.load() && pending->load() == 0; };
        const auto commit = [state, pending, lifetime] {
            std::lock_guard<std::mutex> lock(state->mutex);
            int expected = 0;
            return !state->closed.load() && pending->compare_exchange_strong(expected, 2) && lifetime.current();
        };
        if (!current()) result.error = QStringLiteral("The private storage request was canceled.");
        else if (!store.privateStorage(context, operation, request, &result.response, current, commit, &result.error) && result.error.isEmpty())
            result.error = QStringLiteral("The private storage request did not complete.");
        pending->store(3);
        done(std::move(result));
    };
    const bool started = lifetime.startQtWork(std::move(task), cancel);
    if (!started) {
        pending->store(3);
        done({{}, QStringLiteral("Private storage is busy. Try again.")}); return {};
    }
    return cancel;
}
void AddonStorage::close()
{
    const auto state = m_state;
    std::lock_guard<std::mutex> lock(state->mutex);
    state->closed.store(true);
    for (const auto& job : state->jobs) if (const auto pending = job.lock()) { int expected = 0; pending->compare_exchange_strong(expected, 1); }
    state->jobs.clear();
}
}
