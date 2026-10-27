// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addons/addon_stream_lifetime.hpp"
#include <QObject>
#include <QImage>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>

struct input_item_t;
struct vlc_metadata_backend;

/** Explicit native file choices only. No guest or QML method can create a
 * receipt. Call this owner on its Qt thread. Workers retain native item/job
 * references and copied data. They never retain a QObject or Core instance.
 */
class ArtworkSelection final : public QObject
{
    struct Work;
    struct Mailbox;
public:
    using Current = std::function<bool()>;
    struct Options {
        qint64 maximumEncodedBytes = 512 * 1024 * 1024;
        qint64 maximumCacheBytes = 512 * 1024 * 1024;
        int maximumCacheEntries = 8192;
        int maximumOutputDimension = 4096;
        int previewDimension = 512;
    };
    struct Backend {
        // These callbacks run as joined Qt work. Use copied owned data.
        std::function<QByteArray(const QString&, qint64, const Current&, QString*)> read;
        std::function<QImage(const QByteArray&, int, QString*)> decode;
    };
    class Receipt final {
    public:
        ~Receipt();
        QUrl file() const;
        quint64 generation() const noexcept;
        bool current(input_item_t*, quint64 expectedGeneration) const;
        // Prepare the file-tag writer's private copy. This does not apply the
        // selection or change its live target. No file I/O occurs here.
        bool stampCopy(input_item_t*) const;
        // One native CAS attempt on the exact original target.
        int apply(input_item_t*, quint64 expectedGeneration);
        void cancel() noexcept;
    private:
        friend class ArtworkSelection;
        explicit Receipt(std::shared_ptr<Work>);
        std::shared_ptr<Work> m_work;
    };
    struct Result {
        quint64 generation = 0;
        std::shared_ptr<Receipt> receipt;
        QImage preview;
        QString error;
    };
    using Complete = std::function<void(Result)>;
    ArtworkSelection(vlc::addons::AddonStreamLifetime::Handle, QString cacheDirectory, QObject* parent = nullptr);
    ArtworkSelection(vlc::addons::AddonStreamLifetime::Handle, QString cacheDirectory, Options, Backend, QObject* parent = nullptr);
    ~ArtworkSelection() override;
    quint64 prepare(input_item_t*, const QUrl&, Complete);
    quint64 generation() const noexcept { return m_generation; }
    bool busy() const noexcept { return bool(m_work); }
    void cancel();
    void close();
    static bool localFile(const QUrl&);
    static unsigned retainedCount() noexcept;
    static unsigned activeCount() noexcept;
private:
    void poll();
    vlc::addons::AddonStreamLifetime::Handle m_lifetime;
    QString m_directory;
    Options m_options;
    Backend m_backend;
    std::shared_ptr<Mailbox> m_mailbox;
    std::shared_ptr<Work> m_work;
    std::weak_ptr<Receipt> m_receipt;
    std::shared_ptr<vlc_metadata_backend> m_native;
    Complete m_complete;
    QTimer m_timer;
    quint64 m_generation = 0;
    bool m_closed = false;
};

/** Preview an already admitted native local artwork path. This keeps the
 * existing native reader and format support. It does not approve a file.
 */
class ArtworkNativePreview final : public QObject
{
    struct State;
public:
    using Complete = std::function<void(QImage)>;
    explicit ArtworkNativePreview(vlc::addons::AddonStreamLifetime::Handle, QObject* parent = nullptr);
    ~ArtworkNativePreview() override;
    void request(QString nativePath, Complete);
    void cancel();
    void close();
private:
    void poll();
    vlc::addons::AddonStreamLifetime::Handle m_lifetime;
    std::shared_ptr<State> m_state;
    QTimer m_timer;
    Complete m_complete;
};
