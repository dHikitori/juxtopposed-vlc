// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "addon_native_artwork_assets.hpp"
#include "addon_artwork_image.hpp"
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <vlc_stream.h>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QLockFile>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
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
}
struct AddonNativeArtworkAssets::Permit::Consent {
    std::mutex mutex;
    bool allowed = false, closed = false;
    quint64 epoch = 1;
};
bool AddonNativeArtworkAssets::Permit::current() const
{
    if (!m_consent || !m_epoch) return false;
    std::lock_guard<std::mutex> lock(m_consent->mutex);
    return m_consent->allowed && !m_consent->closed && m_consent->epoch == m_epoch;
}
int AddonNativeArtworkAssets::Permit::admit(const std::function<int()>& commit) const
{
    if (!m_consent || !m_epoch || !commit) return VLC_EACCES;
    std::lock_guard<std::mutex> lock(m_consent->mutex);
    if (!m_consent->allowed || m_consent->closed || m_consent->epoch != m_epoch) return VLC_EACCES;
    return commit();
}
struct AddonNativeArtworkAssets::Work {
    enum Reason { None, User, Timeout, ConsentChanged, Closed };
    std::mutex jobMutex;
    std::shared_ptr<vlc_metadata_job_t> job;
    std::shared_ptr<vlc_metadata_job_t> holdJob() { std::lock_guard<std::mutex> lock(jobMutex); return job; }
    std::unique_ptr<vlc_interrupt_t, decltype(&vlc_interrupt_destroy)> interrupt{vlc_interrupt_create(), vlc_interrupt_destroy};
    std::atomic_int reason{None};
    std::atomic_bool finished{false};
    Clock::time_point deadline;
    Permit permit;
    QUrl url;
    Completion completion;
    bool quota = true;
    void cancel(Reason value) {
        int expected = None;
        const bool won = reason.compare_exchange_strong(expected, value);
        std::shared_ptr<vlc_metadata_job_t> released;
        if (won && value != Timeout) {
            std::lock_guard<std::mutex> lock(jobMutex); released = std::move(job);
        }
        if (won && interrupt) vlc_interrupt_kill(interrupt.get());
        if (value == User && released) vlc_metadata_job_Cancel(released.get());
    }
    bool live() {
        if (reason.load() != None || !permit.current()) return false;
        const auto held = holdJob(); return held && vlc_metadata_job_IsPending(held.get());
    }
};
struct AddonNativeArtworkAssets::State {
    std::mutex mutex;
    std::condition_variable changed;
    AddonNativeArtworkAssets* owner = nullptr;
    Backend backend;
    Options options;
    QString directory;
    std::shared_ptr<Permit::Consent> consent = std::make_shared<Permit::Consent>();
    std::atomic_bool closed{false};
    std::mutex cacheMutex;
    std::deque<std::shared_ptr<Work>> queue;
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
    void run(const std::shared_ptr<State>& state)
    {
        for (;;) {
            std::shared_ptr<Work> work;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [this] { return closed || !queue.empty(); });
                if (closed) return;
                work = queue.front(); queue.pop_front();
            }
            const auto global = budget(); bool admitted = false;
            {
                std::unique_lock<std::mutex> lock(global->mutex);
                while (global->active >= 2 && work->live()) global->changed.wait_for(lock, std::chrono::milliseconds(20));
                if (work->live()) { ++global->active; admitted = true; }
            }
            Result result; result.permit = work->permit;
            if (admitted) {
                auto* previous = vlc_interrupt_set(work->interrupt.get());
                auto bytes = backend.read(work->url, options.maximumEncodedBytes, [work] { return work->live(); });
                vlc_interrupt_set(previous);
                if (work->live() && bytes.error.isEmpty()) {
                    const vlc::addons::AddonArtworkImage::Limits limits{options.maximumEncodedBytes, options.maximumPixelBytes, options.maximumDimension};
                    auto pixels = vlc::addons::AddonArtworkImage::decode(bytes.bytes, vlc::addons::AddonArtworkImage::Profile::NativeArtwork, limits, &result.error);
                    bytes.bytes.clear();
                    if (!pixels.isNull() && work->live()) {
                        if (pixels.width() > options.maximumOutputDimension || pixels.height() > options.maximumOutputDimension)
                            pixels = pixels.scaled(options.maximumOutputDimension, options.maximumOutputDimension, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        BoundedOutput output(options.maximumEncodedBytes);
                        if (!pixels.save(&output, "PNG")) result.error = QStringLiteral("VLC cannot encode the native artwork within the byte limit.");
                        else if (work->live()) {
                            const QString key = QString::fromLatin1(QCryptographicHash::hash(output.bytes, QCryptographicHash::Sha256).toHex());
                            const QString path = QDir(directory).filePath(key + QStringLiteral(".png"));
                            // File access stays on the native worker. The owner
                            // lock does not cover directory or file I/O.
                            std::lock_guard<std::mutex> lock(cacheMutex);
                            const QString lockPath = QDir(directory).filePath(QStringLiteral("cache.lock"));
                            if (closed || !work->live()) {}
                            else if (QFileInfo(directory).isSymLink() || QFileInfo(lockPath).isSymLink() || !QDir().mkpath(directory))
                                result.error = QStringLiteral("VLC cannot create the native artwork cache.");
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
                                        if (!file.open(QIODevice::WriteOnly)) result.error = QStringLiteral("VLC cannot save the native artwork.");
                                        else {
                                            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
                                            if (file.write(output.bytes) != output.bytes.size() || !file.commit()) result.error = QStringLiteral("VLC cannot save the native artwork.");
                                            else result.file = QUrl::fromLocalFile(path);
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else result.error = bytes.error;
                { std::lock_guard<std::mutex> lock(global->mutex); --global->active; }
                global->changed.notify_all();
            }
            if (work->reason.load() == Work::Timeout) result.error = QStringLiteral("The native artwork request exceeded its time limit.");
            {
                std::lock_guard<std::mutex> lock(mutex);
                work->finished.store(true);
                if (!owner) continue;
                auto* target = owner;
                QMetaObject::invokeMethod(target, [state, work, result] { AddonNativeArtworkAssets::deliver(state, work, result); }, Qt::QueuedConnection);
            }
        }
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
AddonNativeArtworkAssets::Backend AddonNativeArtworkAssets::nativeBackend(vlc_object_t* object)
{
    return {[object](const QUrl& url, qint64 limit, const std::function<bool()>& live) {
        ReadResult result;
        if (!object || !live()) { result.error = QStringLiteral("The native artwork request is no longer current."); return result; }
        const auto address = url.toEncoded();
        stream_t* stream = vlc_stream_NewURL(object, address.constData());
        if (!stream) { result.error = QStringLiteral("VLC cannot open the native artwork address."); return result; }
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
AddonNativeArtworkAssets::AddonNativeArtworkAssets(vlc_object_t* object, const QString& directory, const Options& options, QObject* parent)
    : AddonNativeArtworkAssets(nativeBackend(object), directory, options, parent) {}
AddonNativeArtworkAssets::AddonNativeArtworkAssets(Backend backend, const QString& directory, const Options& requested, QObject* parent)
    : QObject(parent), m_state(std::make_shared<State>())
{
    auto& state = *m_state; state.owner = this; state.backend = std::move(backend); state.options = requested;
    auto& options = state.options;
    options.maximumEncodedBytes = std::clamp<qint64>(requested.maximumEncodedBytes, 1, 32 * 1024 * 1024);
    options.maximumPixelBytes = std::clamp<qint64>(requested.maximumPixelBytes, 8, 128 * 1024 * 1024);
    options.maximumCacheBytes = std::clamp<qint64>(requested.maximumCacheBytes, 1, 512 * 1024 * 1024);
    options.maximumDimension = std::clamp(requested.maximumDimension, 1, 16384);
    options.maximumOutputDimension = std::clamp(requested.maximumOutputDimension, 1, 4096);
    options.maximumEntries = std::clamp(requested.maximumEntries, 1, 8192);
    options.timeoutMs = std::clamp(requested.timeoutMs, 1, 10000);
    if (QDir::isAbsolutePath(directory)) state.directory = QDir::cleanPath(directory);
}
AddonNativeArtworkAssets::~AddonNativeArtworkAssets() { close(); }
AddonNativeArtworkAssets::Cancel AddonNativeArtworkAssets::resolve(vlc_metadata_job_t* job, Completion completion)
{
    const auto state = m_state;
    if (!job || !completion || !state->backend.read || state->directory.isEmpty() || !vlc_metadata_job_IsPending(job)) return {};
    const auto* request = vlc_metadata_job_GetRequest(job);
    if (!request->item || !(request->flags & VLC_METADATA_REQUEST_NETWORK) || vlc_artwork_item_IsRestricted(request->item)) return {};
    char* raw = input_item_GetArtURL(request->item);
    const QUrl url = raw ? QUrl::fromEncoded(QByteArray(raw), QUrl::StrictMode) : QUrl{}; free(raw);
    if (!networkUri(url)) return {};
    Permit permit; permit.m_consent = state->consent;
    {
        std::lock_guard<std::mutex> lock(state->consent->mutex);
        if (state->consent->closed || !state->consent->allowed) return {};
        permit.m_epoch = state->consent->epoch;
    }
    const auto global = budget();
    { std::lock_guard<std::mutex> lock(global->mutex); if (global->jobs >= 32) return {}; ++global->jobs; }
    const auto work = std::make_shared<Work>();
    work->job = {vlc_metadata_job_Hold(job), vlc_metadata_job_Release}; work->permit = permit;
    work->url = url; work->completion = std::move(completion); work->deadline = Clock::now() + std::chrono::milliseconds(state->options.timeoutMs);
    bool startFailed = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed || !work->interrupt || !work->live()) { state->releaseQuota(work); return {}; }
        state->jobs.push_back(work); state->queue.push_back(work);
        if (state->workers.empty()) {
            try {
                for (int index = 0; index < 2; ++index) state->workers.emplace_back([state] { state->run(state); });
                state->watchdog = std::thread([state] { state->watch(); });
            } catch (...) { startFailed = true; }
        }
    }
    if (startFailed) { close(); return {}; }
    state->changed.notify_all();
    const std::weak_ptr<State> weak = state;
    return [work, weak] { work->cancel(Work::User); if (const auto state = weak.lock()) state->changed.notify_all(); };
}
void AddonNativeArtworkAssets::deliver(const std::shared_ptr<State>& state, const std::shared_ptr<Work>& work, Result result)
{
    Completion completion;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto found = std::find(state->jobs.begin(), state->jobs.end(), work);
        if (found == state->jobs.end()) return;
        state->jobs.erase(found);
        state->releaseQuota(work);
        const auto reason = work->reason.load();
        const auto job = work->holdJob();
        if (!state->owner || (reason != Work::None && reason != Work::Timeout)
            || !work->permit.current() || !job || !vlc_metadata_job_IsPending(job.get())) return;
        completion = std::move(work->completion);
    }
    if (work->reason.load() == Work::Timeout) result.file = QUrl{};
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
void AddonNativeArtworkAssets::close()
{
    const auto state = m_state;
    {
        std::lock_guard<std::mutex> lock(state->consent->mutex);
        state->consent->closed = true; state->consent->allowed = false; ++state->consent->epoch;
    }
    std::vector<std::shared_ptr<Work>> snapshot;
    { std::lock_guard<std::mutex> lock(state->mutex); if (state->closed) return; state->closed = true; state->owner = nullptr; snapshot = state->jobs; }
    for (const auto& work : snapshot) { const auto job = work->holdJob(); work->cancel(Work::Closed); if (job) vlc_metadata_job_Cancel(job.get()); }
    state->changed.notify_all(); budget()->changed.notify_all();
    for (auto& worker : state->workers) worker.join();
    if (state->watchdog.joinable()) state->watchdog.join();
    std::lock_guard<std::mutex> lock(state->mutex);
    for (const auto& work : state->jobs) state->releaseQuota(work);
    state->jobs.clear(); state->queue.clear();
}
