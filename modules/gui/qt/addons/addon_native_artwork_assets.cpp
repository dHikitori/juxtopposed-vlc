// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "addon_native_artwork_assets.hpp"
#include "addon_artwork_image.hpp"
#include "../../../../src/preparser/metadata_native_provider.h"
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <vlc_stream.h>
#include <vlc_objects.h>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QLockFile>
#include <algorithm>
#include <array>
#include <optional>
#include <QPointer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
struct Budget {
    std::mutex mutex;
    std::condition_variable changed;
    unsigned jobs = 0, active = 0;
};
std::shared_ptr<Budget> budget() { static const auto value = std::make_shared<Budget>(); return value; }
class BoundedOutput : public QIODevice {
public:
    explicit BoundedOutput(qint64 limit) : m_limit(limit) { open(QIODevice::WriteOnly); }
    QByteArray bytes;
protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char* data, qint64 length) override {
        if (length < 0 || length > m_limit - bytes.size()) return -1;
        bytes.append(data, length); return length;
    }
private:
    qint64 m_limit;
};
bool networkUri(const QUrl& url)
{
    if (!url.isValid() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
        || url.host().isEmpty() || url.hasFragment() || url.toEncoded().size() > 8192) return false;
    // Native HTTP retains the existing native account and LAN behavior. This
    // route has no add-on permission or credential binding.
    return true;
}
bool missingNativeArtwork(input_item_t* item)
{
    vlc_mutex_lock(&item->lock);
    const char* artwork = item->p_meta ? vlc_meta_Get(item->p_meta, vlc_meta_ArtworkURL) : nullptr;
    const bool missing = !artwork || !artwork[0];
    vlc_mutex_unlock(&item->lock);
    return missing;
}
}
struct AddonNativeArtworkAssets::Permit::Consent {
    std::mutex mutex;
    bool allowed = false, closed = false;
    quint64 epoch = 1;
    vlc::addons::AddonStreamLifetime::Handle lifetime;
};
bool AddonNativeArtworkAssets::Permit::current() const
{
    if (!m_consent || !m_epoch) return false;
    std::lock_guard<std::mutex> lock(m_consent->mutex);
    return m_consent->lifetime.current() && m_consent->allowed && !m_consent->closed && m_consent->epoch == m_epoch;
}
int AddonNativeArtworkAssets::Permit::admit(const std::function<int()>& commit) const
{
    if (!m_consent || !m_epoch || !commit) return VLC_EACCES;
    std::lock_guard<std::mutex> lock(m_consent->mutex);
    if (!m_consent->lifetime.current() || !m_consent->allowed || m_consent->closed || m_consent->epoch != m_epoch) return VLC_EACCES;
    return commit();
}
struct AddonNativeArtworkAssets::Work {
    enum Reason { None, User, Timeout, ConsentChanged, Closed };
    // A returned Cancel holds only this native control. It cannot retain the
    // Work URL, queued pixels, backend data, or any QObject completion capture.
    struct Control {
        std::mutex jobMutex;
        std::shared_ptr<vlc_metadata_job_t> job;
        std::unique_ptr<vlc_interrupt_t, decltype(&vlc_interrupt_destroy)> interrupt{vlc_interrupt_create(), vlc_interrupt_destroy};
        std::atomic_int reason{None};
        bool done = false;
        std::shared_ptr<vlc_metadata_job_t> holdJob() { std::lock_guard<std::mutex> lock(jobMutex); return job; }
        std::shared_ptr<vlc_metadata_job_t> takeForDelivery() {
            std::lock_guard<std::mutex> lock(jobMutex);
            if (done) return {};
            done = true; return std::move(job);
        }
        void cancel(Reason value) {
            bool won = false;
            std::shared_ptr<vlc_metadata_job_t> released;
            {
                std::lock_guard<std::mutex> lock(jobMutex);
                if (done) return;
                int expected = None; won = reason.compare_exchange_strong(expected, value);
                // Final retirement must detach the job even after Timeout.
                if (value != Timeout) { done = true; released = std::move(job); }
            }
            if (won && interrupt) vlc_interrupt_kill(interrupt.get());
            if (value == User && released) vlc_metadata_job_Cancel(released.get());
        }
    };
    std::shared_ptr<Control> native = std::make_shared<Control>();
    std::atomic_bool finished{false};
    Clock::time_point deadline;
    Permit permit;
    std::shared_ptr<vlc_metadata_native_claim_t> claim;
    QUrl url;
    quint64 id = 0;
    bool quota = false;
    std::shared_ptr<vlc_metadata_job_t> holdJob() { return native->holdJob(); }
    void cancel(Reason value) { native->cancel(value); }
    bool live() {
        if (native->reason.load() != None || !permit.current()) return false;
        const auto held = holdJob();
        return held && vlc_metadata_job_IsPending(held.get())
            && (!claim || vlc_metadata_native_claim_MatchesJob(claim.get(), held.get()));
    }
};

struct AddonNativeArtworkAssets::State {
    std::mutex mutex;
    std::condition_variable changed;
    Backend backend;
    vlc::addons::AddonStreamLifetime::Handle lifetime;
    Options options;
    QString directory;
    std::shared_ptr<Permit::Consent> consent = std::make_shared<Permit::Consent>();
    std::atomic_bool closed{false};
    bool retirePrepared = false;
    struct Ready { std::shared_ptr<Work> work; Result result; };
    std::array<std::optional<Ready>, 32> ready;
    unsigned readyCount = 0;
    std::mutex cacheMutex;
    std::vector<std::shared_ptr<Work>> queue;
    std::vector<std::shared_ptr<Work>> jobs;
    std::vector<std::thread> workers;
    std::thread watchdog;
    qint64 cacheBytes = 0;
    int cacheEntries = 0;
    void releaseQuota(const std::shared_ptr<Work>& work) {
        if (!work->quota) return;
        work->quota = false; const auto global = budget();
        { std::lock_guard<std::mutex> lock(global->mutex); --global->jobs; }
        global->changed.notify_all();
    }
    void run()
    {
        for (;;) {
            std::shared_ptr<Work> work;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [this] { return closed || !queue.empty(); });
                if (closed) return;
                work = queue.front(); queue.erase(queue.begin());
            }
            const auto global = budget(); bool admitted = false;
            {
                std::unique_lock<std::mutex> lock(global->mutex);
                while (global->active >= 2 && work->live()) global->changed.wait_for(lock, std::chrono::milliseconds(20));
                if (work->live()) { ++global->active; admitted = true; }
            }
            Result result; result.permit = work->permit;
            if (admitted) {
                try {
                    ReadResult bytes;
                    {
                        struct InterruptReset {
                            vlc_interrupt_t* previous;
                            ~InterruptReset() { vlc_interrupt_set(previous); }
                        } reset{vlc_interrupt_set(work->native->interrupt.get())};
                        bytes = backend.read(work->url, options.maximumEncodedBytes, [work] { return work->live(); });
                    }
                    if (work->live() && bytes.error.isEmpty()) {
                        const vlc::addons::AddonArtworkImage::Limits limits{options.maximumEncodedBytes, options.maximumPixelBytes, options.maximumDimension};
                        auto pixels = vlc::addons::AddonArtworkImage::decode(bytes.bytes, vlc::addons::AddonArtworkImage::Profile::NativeArtwork, limits, &result.error);
                        bytes.bytes.clear();
                        if (!pixels.isNull() && work->live()) {
                            if (pixels.width() > options.maximumOutputDimension || pixels.height() > options.maximumOutputDimension)
                                pixels = pixels.scaled(options.maximumOutputDimension, options.maximumOutputDimension, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                            BoundedOutput output(options.maximumEncodedBytes);
                            if (!pixels.save(&output, "PNG")) result.error = QStringLiteral("The app cannot create native artwork within the byte limit.");
                            else if (work->live()) {
                                const QString key = QString::fromLatin1(QCryptographicHash::hash(output.bytes, QCryptographicHash::Sha256).toHex());
                                const QString path = QDir(directory).filePath(key + QStringLiteral(".png"));
                                // File access stays on the native worker. The owner
                                // lock does not cover directory or file I/O.
                                std::lock_guard<std::mutex> lock(cacheMutex);
                                const QString lockPath = QDir(directory).filePath(QStringLiteral("cache.lock"));
                                if (closed || !work->live()) {}
                                else if (QFileInfo(directory).isSymLink() || QFileInfo(lockPath).isSymLink() || !QDir().mkpath(directory))
                                    result.error = QStringLiteral("The app cannot create the native artwork cache.");
                                else {
                                    QLockFile cacheLock(lockPath);
                                    if (!cacheLock.tryLock(0)) result.error = QStringLiteral("The native artwork cache is busy.");
                                    else {
                                        cacheEntries = 0; cacheBytes = 0;
                                        QDirIterator entries(directory, {QStringLiteral("*.png")}, QDir::Files | QDir::NoSymLinks);
                                        while (entries.hasNext() && cacheEntries <= options.maximumEntries && cacheBytes <= options.maximumCacheBytes) {
                                            entries.next(); const auto size = entries.fileInfo().size();
                                            ++cacheEntries; cacheBytes += std::min<qint64>(size, options.maximumCacheBytes + 1);
                                        }
                                        const QFileInfo existing(path);
                                        if (existing.isSymLink() || (!existing.exists() && (cacheEntries >= options.maximumEntries
                                            || output.bytes.size() > options.maximumCacheBytes - cacheBytes)))
                                            result.error = QStringLiteral("The native artwork cache limit has been reached.");
                                        else if (!closed && work->live()) {
                                            QSaveFile file(path);
                                            if (!file.open(QIODevice::WriteOnly)) result.error = QStringLiteral("The app cannot save the native artwork.");
                                            else {
                                                file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
                                                if (file.write(output.bytes) != output.bytes.size() || !file.commit()) result.error = QStringLiteral("The app cannot save the native artwork.");
                                                else result.file = QUrl::fromLocalFile(path);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else result.error = bytes.error;
                } catch (...) {
                    result.file = QUrl{}; result.error = QStringLiteral("The native artwork read failed.");
                }
                { std::lock_guard<std::mutex> lock(global->mutex); --global->active; }
                global->changed.notify_all();
            }
            if (work->native->reason.load() == Work::Timeout) result.error = QStringLiteral("The native artwork request exceeded its time limit.");
            {
                std::lock_guard<std::mutex> lock(mutex);
                work->finished.store(true);
                if (closed || !lifetime.current()) continue;
                assert(readyCount < ready.size());
                ready[readyCount++].emplace(Ready{work, std::move(result)});
            }
        }
    }
    void retire()
    {
        {
            std::lock_guard<std::mutex> lock(consent->mutex);
            consent->closed = true; consent->allowed = false; ++consent->epoch;
        }
        std::array<std::shared_ptr<Work>, 32> snapshot;
        unsigned count = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (closed.exchange(true)) return;
            for (const auto& work : jobs) snapshot[count++] = work;
        }
        // No callback runs under the owner lock. jobs retains every Qt capture
        // until this native-only cancellation path has dropped its copies.
        for (unsigned index = 0; index < count; ++index) {
            const auto& work = snapshot[index];
            const auto job = work->holdJob(); work->cancel(Work::Closed);
            if (job) vlc_metadata_job_Cancel(job.get());
        }
        for (auto& work : snapshot) work.reset();
        { std::lock_guard<std::mutex> lock(mutex); retirePrepared = true; }
        changed.notify_all(); budget()->changed.notify_all();
    }
    void join()
    {
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [this] { return retirePrepared; });
        }
        // Retirement closes admission before this task reads the thread list.
        for (auto& worker : workers) if (worker.joinable()) worker.join();
        if (watchdog.joinable()) watchdog.join();
        std::vector<std::shared_ptr<Work>> retiredJobs;
        std::array<std::optional<Ready>, 32> retiredResults;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const auto& work : jobs) releaseQuota(work);
            retiredJobs.swap(jobs); retiredResults.swap(ready); readyCount = 0;
        }
        // Admission is closed and all worker threads have joined. Release Qt
        // captures and the owned native context on this counted cleanup path.
        retiredJobs.clear(); queue.clear();
        for (auto& result : retiredResults) result.reset();
        backend = {};
    }
    void watch()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!closed) {
            changed.wait_for(lock, std::chrono::milliseconds(20));
            if (closed) break;
            const auto snapshot = jobs; const auto now = Clock::now(); lock.unlock();
            for (const auto& work : snapshot) if (!work->finished.load() && now >= work->deadline) work->cancel(Work::Timeout);
            lock.lock();
        }
    }
};
AddonNativeArtworkAssets::Backend AddonNativeArtworkAssets::nativeBackend(vlc_object_t* parent)
{
    struct Context {
        vlc_object_t* object = nullptr;
        ~Context() { if (object) vlc_object_delete(object); }
    };
    if (!parent) return {};
    const auto context = std::make_shared<Context>();
    context->object = vlc_object_create<vlc_object_t>(parent);
    if (!context->object) return {};
    return {[context](const QUrl& url, qint64 limit, const std::function<bool()>& live) {
        ReadResult result;
        if (!live()) { result.error = QStringLiteral("The native artwork request is no longer current."); return result; }
        const auto address = url.toEncoded();
        stream_t* stream = vlc_stream_NewURL(context->object, address.constData());
        if (!stream) { result.error = QStringLiteral("The app cannot open the native artwork address."); return result; }
        char buffer[16384]; bool complete = false;
        while (live()) {
            const auto count = vlc_stream_ReadPartial(stream, buffer, size_t(std::min<qint64>(sizeof(buffer), limit + 1 - result.bytes.size())));
            if (count < 0) break;
            if (!count) { complete = true; break; }
            if (count > limit - result.bytes.size()) { result.error = QStringLiteral("The native artwork exceeds the byte limit."); break; }
            result.bytes.append(buffer, count);
        }
        vlc_stream_Delete(stream);
        if (!complete && result.error.isEmpty()) result.error = QStringLiteral("The native artwork transfer did not finish.");
        if (!result.error.isEmpty()) result.bytes.clear();
        return result;
    }};
}
AddonNativeArtworkAssets::AddonNativeArtworkAssets(vlc::addons::AddonStreamLifetime::Handle lifetime,
    vlc_object_t* object, const QString& directory, const Options& options, QObject* parent)
    : AddonNativeArtworkAssets(std::move(lifetime), nativeBackend(object), directory, options, parent) {}
AddonNativeArtworkAssets::AddonNativeArtworkAssets(vlc::addons::AddonStreamLifetime::Handle lifetime,
    Backend backend, const QString& directory, const Options& requested, QObject* parent)
    : QObject(parent), m_state(std::make_shared<State>()), m_poll(this)
{
    auto& state = *m_state; state.backend = std::move(backend); state.options = requested;
    state.lifetime = lifetime; state.consent->lifetime = lifetime;
    auto& options = state.options;
    options.maximumEncodedBytes = std::clamp<qint64>(requested.maximumEncodedBytes, 1, 32 * 1024 * 1024);
    options.maximumPixelBytes = std::clamp<qint64>(requested.maximumPixelBytes, 8, 128 * 1024 * 1024);
    options.maximumCacheBytes = std::clamp<qint64>(requested.maximumCacheBytes, 1, 512 * 1024 * 1024);
    options.maximumDimension = std::clamp(requested.maximumDimension, 1, 16384);
    options.maximumOutputDimension = std::clamp(requested.maximumOutputDimension, 1, 4096);
    options.maximumEntries = std::clamp(requested.maximumEntries, 1, 8192);
    options.timeoutMs = std::clamp(requested.timeoutMs, 1, 10000);
    if (QDir::isAbsolutePath(directory)) state.directory = QDir::cleanPath(directory);
    state.jobs.reserve(32); state.queue.reserve(32);
    m_poll.setInterval(20);
    connect(&m_poll, &QTimer::timeout, this, [this] { poll(); });
    const auto retained = m_state;
    // Admit the join before any worker can start. A destructor never needs to
    // submit new app work after global retirement has closed the app handle.
    m_admitted = lifetime.startQtWork([retained] { retained->join(); },
        [retained] { retained->retire(); },
        [this](auto ticket) { m_retirement = std::move(ticket); });
    if (!m_admitted) { state.retire(); state.backend = {}; }
}
AddonNativeArtworkAssets::~AddonNativeArtworkAssets() { close(); }
AddonNativeArtworkAssets::Cancel AddonNativeArtworkAssets::resolve(vlc_metadata_job_t* job, Completion completion)
{
    if (!job || !vlc_metadata_job_IsPending(job)) return {};
    const auto* request = vlc_metadata_job_GetRequest(job);
    if (!request->item || !(request->flags & VLC_METADATA_REQUEST_NETWORK) || vlc_artwork_item_IsRestricted(request->item)) return {};
    char* raw = input_item_GetArtURL(request->item);
    const QUrl url = raw ? QUrl::fromEncoded(QByteArray(raw), QUrl::StrictMode) : QUrl{}; free(raw);
    if (!networkUri(url)) return {};
    return enqueue(job, url, {}, std::move(completion));
}
AddonNativeArtworkAssets::Cancel AddonNativeArtworkAssets::resolveProviderClaim(
    vlc_metadata_native_claim_t* claim, vlc_metadata_job_t* job, Completion completion)
{
    if (!claim || !job || !vlc_metadata_native_claim_MatchesJob(claim, job)) return {};
    const auto* request = vlc_metadata_job_GetRequest(job);
    if (!request->item || !(request->flags & VLC_METADATA_REQUEST_NETWORK)
        || vlc_artwork_item_IsRestricted(request->item) || !missingNativeArtwork(request->item)) return {};
    const QUrl url = QUrl::fromEncoded(QByteArray(vlc_metadata_native_claim_GetArtworkURI(claim)), QUrl::StrictMode);
    if (!networkUri(url)) return {};
    return enqueue(job, url, {vlc_metadata_native_claim_Hold(claim), vlc_metadata_native_claim_Release}, std::move(completion));
}
AddonNativeArtworkAssets::Cancel AddonNativeArtworkAssets::enqueue(vlc_metadata_job_t* job, const QUrl& url,
    std::shared_ptr<vlc_metadata_native_claim_t> claim, Completion completion)
{
    const auto state = m_state;
    if (!available() || m_sequence == UINT64_MAX || !job || !completion || state->directory.isEmpty()
        || !vlc_metadata_job_IsPending(job)) return {};
    Permit permit; permit.m_consent = state->consent;
    {
        std::lock_guard<std::mutex> lock(state->consent->mutex);
        if (state->consent->closed || !state->consent->allowed) return {};
        permit.m_epoch = state->consent->epoch;
    }
    const auto work = std::make_shared<Work>();
    if (!work->native->interrupt) return {};
    work->native->job = {vlc_metadata_job_Hold(job), vlc_metadata_job_Release}; work->permit = permit;
    work->claim = std::move(claim);
    work->url = url; work->id = ++m_sequence; work->deadline = Clock::now() + std::chrono::milliseconds(state->options.timeoutMs);
    const auto global = budget();
    { std::lock_guard<std::mutex> lock(global->mutex); if (global->jobs >= 32) return {}; ++global->jobs; work->quota = true; }
    bool startFailed = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed || !state->backend.read || !work->native->interrupt || !work->live()) { state->releaseQuota(work); return {}; }
        state->jobs.push_back(work); state->queue.push_back(work);
        if (state->workers.empty()) {
            try {
                for (int index = 0; index < 2; ++index) state->workers.emplace_back([state] { state->run(); });
                state->watchdog = std::thread([state] { state->watch(); });
            } catch (...) { startFailed = true; }
        }
    }
    if (startFailed) { close(); return {}; }
    m_completions.insert(work->id, std::move(completion));
    state->changed.notify_all(); m_poll.start();
    const auto native = work->native;
    return [native] { native->cancel(Work::User); budget()->changed.notify_all(); };
}
void AddonNativeArtworkAssets::deliver(const std::shared_ptr<State>& state, const std::shared_ptr<Work>& work, Result result)
{
    bool permitted = false;
    const auto job = work->native->takeForDelivery();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto found = std::find(state->jobs.begin(), state->jobs.end(), work);
        if (found != state->jobs.end()) {
            state->jobs.erase(found); state->releaseQuota(work);
            const auto reason = work->native->reason.load();
            permitted = !state->closed && state->lifetime.current()
                && (reason == Work::None || reason == Work::Timeout)
                && work->permit.current() && job && vlc_metadata_job_IsPending(job.get());
        }
    }
    // Complete the map and native mutations before a capture can destroy or
    // reenter this owner. Callback values never enter the worker state.
    auto completion = m_completions.take(work->id);
    if (!permitted || !completion) {
        if (job) vlc_metadata_job_Cancel(job.get());
        return;
    }
    if (work->native->reason.load() == Work::Timeout) result.file = QUrl{};
    if (result.file.isEmpty() && result.error.isEmpty()) result.error = QStringLiteral("The native artwork request did not finish.");
    completion(result);
}
void AddonNativeArtworkAssets::setNetworkAllowed(bool allowed)
{
    const auto state = m_state;
    {
        std::lock_guard<std::mutex> lock(state->consent->mutex);
        if (state->consent->closed || state->consent->allowed == allowed) return;
        state->consent->allowed = allowed; ++state->consent->epoch;
    }
    std::vector<std::shared_ptr<Work>> snapshot;
    { std::lock_guard<std::mutex> lock(state->mutex); snapshot = state->jobs; }
    for (const auto& work : snapshot) { const auto job = work->holdJob(); work->cancel(Work::ConsentChanged); if (job) vlc_metadata_job_Cancel(job.get()); }
    state->changed.notify_all();
}
void AddonNativeArtworkAssets::poll()
{
    const auto state = m_state;
    std::array<std::optional<State::Ready>, 32> ready;
    unsigned count = 0; bool stop = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        stop = state->closed || !state->lifetime.current();
        if (!stop) count = state->readyCount;
        for (unsigned index = 0; index < count; ++index) {
            ready[index] = std::move(state->ready[index]); state->ready[index].reset();
        }
        if (!stop) state->readyCount = 0;
        stop = stop || state->jobs.empty();
    }
    if (stop) m_poll.stop();
    QPointer<AddonNativeArtworkAssets> alive(this);
    for (unsigned index = 0; index < count; ++index) {
        auto& value = *ready[index]; deliver(state, value.work, std::move(value.result));
        if (!alive) return;
    }
}
bool AddonNativeArtworkAssets::available() const
{ return m_admitted && !m_state->closed && m_state->lifetime.current(); }
unsigned AddonNativeArtworkAssets::pendingDeliveryCount() const
{ std::lock_guard<std::mutex> lock(m_state->mutex); return m_state->readyCount; }
void AddonNativeArtworkAssets::close()
{
    const auto state = m_state;
    QHash<quint64, Completion> retired;
    retired.swap(m_completions); m_poll.stop();
    // Native cancellation can invoke a terminal callback. No owner mutation
    // remains after this call. Retired capture values die on the GUI thread.
    state->retire();
}
