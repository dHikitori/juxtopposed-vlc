// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QUrl>
#include <functional>
#include <memory>
#include <vlc_common.h>
#include <vlc_interrupt.h>
#include <vlc_metadata_backend.h>

/** This adapter accepts native job snapshots only. It has no guest or QML API.
 * The native object must outlive close(). Call the Qt API on the owner thread.
 * A Cancel function can run on any thread. close() interrupts native I/O and
 * joins native threads. Those threads never wait for a Qt event.
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
        std::function<ReadResult(const QUrl&, qint64, const std::function<bool()>& live)> read;
    };
    class Permit {
    public:
        bool current() const;
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
    AddonNativeArtworkAssets(vlc_object_t*, const QString& cacheDirectory, const Options&, QObject* parent = nullptr);
    AddonNativeArtworkAssets(Backend, const QString& cacheDirectory, const Options&, QObject* parent = nullptr);
    ~AddonNativeArtworkAssets() override;
    // Only original HTTP/HTTPS artwork on an ordinary native item can enter.
    // A provider result must use its own broker, even for a native target item.
    Cancel resolve(vlc_metadata_job_t*, Completion);
    void setNetworkAllowed(bool);
    void close();
    static Backend nativeBackend(vlc_object_t*);
private:
    struct State;
    struct Work;
    static void deliver(const std::shared_ptr<State>&, const std::shared_ptr<Work>&, Result);
    std::shared_ptr<State> m_state;
};
