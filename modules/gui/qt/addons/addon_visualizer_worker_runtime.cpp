// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visualizer_worker.hpp"
#include "addon_visualizer_activation.hpp"
#include "addon_worker.hpp"
#include "addon_stream_lifetime.hpp"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <optional>

namespace vlc::addons {
namespace {
std::atomic_int workerCount{0};
struct WorkerRequest {
    QJsonObject context;
    AddonVisualizerWorker::Completion completion;
    std::shared_ptr<std::atomic_bool> live;
};
struct WorkerState : std::enable_shared_from_this<WorkerState> {
    QMutex mutex;
    std::atomic_bool closing{false};
    bool started = false;
    QObject* dispatcher = nullptr;
    std::function<void()> runPending, quit;
    std::function<void(std::shared_ptr<std::atomic_bool>)> cancelRequest;
    std::optional<WorkerRequest> pending;
    std::shared_ptr<const AddonVisualizerActivation> activation;
    QString contribution;
    AddonStreamLifetime::Handle lifetime;
    bool start()
    {
        if (closing.load(std::memory_order_acquire) || !lifetime.current()) return false;
        if (started) return true;
        int expected = 0;
        if (!workerCount.compare_exchange_strong(expected, 1)) return false;
        struct Slot { ~Slot() { workerCount.fetch_sub(1); } };
        const auto slot = std::make_shared<Slot>();
        started = true; const auto self = shared_from_this();
        const bool accepted = lifetime.startQtWork([self] { self->run(); },
            [self] { self->closing.store(true, std::memory_order_release); },
            [slot](const auto& ticket) { ticket.retain(slot); });
        if (!accepted) started = false;
        return accepted;
    }
    void wake()
    {
        QMutexLocker lock(&mutex); const auto self = shared_from_this();
        if (dispatcher) QMetaObject::invokeMethod(dispatcher, [self] {
            std::function<void()> call; { QMutexLocker lock(&self->mutex); call = self->runPending; }
            if (call) call();
        }, Qt::QueuedConnection);
    }
    void stop()
    {
        closing.store(true, std::memory_order_release);
        QMutexLocker lock(&mutex); if (pending) pending->live->store(false); pending.reset();
        const auto self = shared_from_this();
        if (dispatcher) QMetaObject::invokeMethod(dispatcher, [self] {
            std::function<void()> call; { QMutexLocker lock(&self->mutex); call = self->quit; }
            if (call) call();
        }, Qt::QueuedConnection);
    }
    void cancel(const std::shared_ptr<std::atomic_bool>& live)
    {
        live->store(false, std::memory_order_release); QMutexLocker lock(&mutex);
        if (pending && pending->live == live) pending.reset();
        const auto self = shared_from_this();
        if (dispatcher) QMetaObject::invokeMethod(dispatcher, [self, live] {
            std::function<void(std::shared_ptr<std::atomic_bool>)> call;
            { QMutexLocker lock(&self->mutex); call = self->cancelRequest; }
            if (call) call(live);
        }, Qt::QueuedConnection);
    }
    void startupFailure()
    {
        closing.store(true, std::memory_order_release);
        std::optional<WorkerRequest> request;
        { QMutexLocker lock(&mutex); request = std::move(pending); pending.reset(); }
        if (request && request->live->load()) request->completion(QStringLiteral("error"), {});
    }
    void run()
    {
        if (closing.load() || !activation->current || !activation->current()) { startupFailure(); return; }
        QTemporaryDir directory;
        if (!directory.isValid() || activation->runtime.isEmpty() || activation->runtime.size() > Manifest::MaximumFileBytes) { startupFailure(); return; }
        const auto path = QDir(directory.path()).filePath(QStringLiteral("visualizer.wasm"));
        QFile code(path);
        if (!code.open(QIODevice::WriteOnly) || code.write(activation->runtime) != activation->runtime.size()) { startupFailure(); return; }
        code.close();
        if (closing.load() || !activation->current()) { startupFailure(); return; }
        auto package = activation->package; package.directory = directory.path(); package.manifest.runtimeEntry = QStringLiteral("visualizer.wasm");
        QObject context; NetworkBroker broker; AddonWorker worker(package, &broker); QEventLoop loop;
        QTimer retirement;
        retirement.setInterval(20);
        QObject::connect(&retirement, &QTimer::timeout, &context, [this, &loop, &worker] {
            if (closing.load(std::memory_order_acquire)) { worker.stop(); loop.quit(); }
        });
        retirement.start();
        worker.setExecutable(activation->executable);
        worker.setConfiguration(activation->settings, activation->secretStatus, {});
        worker.setHostCapabilities({{"workerProtocol", "wasm-message-v1"}, {"visualizerParameters", "visualizer.parameters@1"},
            {"coreOperations", QJsonArray{}}, {"privateStorageAvailable", false}, {"webSocketAvailable", false}});
        const auto self = shared_from_this();
        worker.setRequestGuard([self] { return !self->closing.load() && self->activation->current && self->activation->current(); });
        QString currentId; std::optional<WorkerRequest> active;
        QObject::connect(&worker, &AddonWorker::completed, &context,
            [&currentId, &active](const QString& id, const QString& status, const QJsonObject& data, const QString&) {
                if (id != currentId || !active) return;
                auto request = std::move(*active); active.reset(); currentId.clear();
                if (request.live->load()) request.completion(status, data);
            });
        QObject::connect(&worker, &AddonWorker::unavailable, &context, [&loop] { loop.quit(); });
        {
            QMutexLocker lock(&mutex); dispatcher = &context;
            runPending = [this, &worker, &currentId, &active] {
                if (closing.load()) return;
                std::optional<WorkerRequest> request;
                { QMutexLocker lock(&mutex); if (active || !pending) return; request = std::move(pending); pending.reset(); }
                if (!request->live->load()) return;
                if (!activation->current()) { request->completion(QStringLiteral("denied"), {}); return; }
                active = std::move(request);
                currentId = worker.invoke(contribution.section('/', 1), QStringLiteral("updateParameters"), active->context);
            };
            cancelRequest = [this, &worker, &currentId, &active](const std::shared_ptr<std::atomic_bool>& live) {
                if (active && active->live == live) {
                    const auto id = currentId; currentId.clear(); active.reset(); if (!id.isEmpty()) worker.cancel(id);
                }
                wake();
            };
            quit = [&loop, &worker, &currentId, &active] {
                if (active) active->live->store(false); active.reset(); currentId.clear(); worker.stop(); loop.quit();
            };
        }
        if (closing.load()) QTimer::singleShot(0, &context, [&loop] { loop.quit(); }); else wake();
        loop.exec(); closing.store(true, std::memory_order_release);
        QObject::disconnect(&worker, nullptr, &context, nullptr);
        if (active && active->live->load()) active->completion(QStringLiteral("error"), {});
        {
            QMutexLocker lock(&mutex); dispatcher = nullptr; runPending = {}; cancelRequest = {}; quit = {}; pending.reset();
        }
        worker.stop();
    }
};
}
AddonVisualizerWorker::Backend AddonVisualizerWorker::production(std::shared_ptr<const AddonVisualizerActivation> activation, const QString& contribution)
{
    const auto state = std::make_shared<WorkerState>(); state->activation = std::move(activation); state->contribution = contribution;
    state->lifetime = AddonStreamLifetime::applicationHandle();
    return {[state](const QJsonObject& context, Completion completion) -> Cancel {
        if (!state->activation || !state->activation->live || !state->activation->live() || state->closing.load()) { completion(QStringLiteral("denied"), {}); return {}; }
        const auto* entry = state->activation->package.manifest.contribution(state->contribution.section('/', 1));
        if (!entry || !entry->operations.contains(QStringLiteral("updateParameters"))
            || state->activation->scene.value("parameters").toObject().isEmpty()) { completion(QStringLiteral("unsupported"), {}); return {}; }
        if (!state->start()) { completion(QStringLiteral("busy"), {}); return {}; }
        const auto live = std::make_shared<std::atomic_bool>(true);
        {
            QMutexLocker lock(&state->mutex);
            if (state->closing.load()) { lock.unlock(); completion(QStringLiteral("denied"), {}); return {}; }
            if (state->pending) { lock.unlock(); completion(QStringLiteral("busy"), {}); return {}; }
            state->pending = WorkerRequest{context, std::move(completion), live};
        }
        state->wake(); return [state, live] { state->cancel(live); };
    }, [state] { state->stop(); }};
}
int AddonVisualizerWorker::liveWorkers() { return workerCount.load(); }
}
