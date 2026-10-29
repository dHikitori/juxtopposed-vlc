// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_store_process_fence.hpp"
#include <QByteArray>
#include <QString>
#include <functional>
#include <memory>

namespace vlc::addons {
class StoreProfileGeneration;
// Host only. Use this context inside its callback on joined background work.
// Do not retain it or its references. The callback already owns store.lock.
class StoreProfileWriteContext final {
public:
    const QString& directory() const noexcept { return m_directory; }
    bool current() const noexcept;
    StoreProfileWriteContext(const StoreProfileWriteContext&) = delete;
    StoreProfileWriteContext& operator=(const StoreProfileWriteContext&) = delete;
private:
    friend class StoreProfileGeneration;
    StoreProfileWriteContext(QString, std::function<bool()>);
    const QString m_directory;
    const std::function<bool()> m_current;
};
// This owner and every operation below use the filesystem. Run them on joined
// background work. Only the returned peer's native fence may enter a native
// cancellation capture. The coordinator control channel is a separate owner.
class StoreProfileGeneration final {
public:
    enum class Status { Ready, Busy, Stale, NeedsRecovery, Error };
    enum class Change { Applied, RolledBack, NeedsRecovery };
    struct Epoch {
        QByteArray generation;
        quint64 revision = 0;
        bool operator==(const Epoch& other) const noexcept {
            return generation == other.generation && revision == other.revision;
        }
        bool operator!=(const Epoch& other) const noexcept { return !(*this == other); }
    };
    static qint64 now() noexcept;
    static constexpr qint64 MaximumDuration = 30 * 1000 * 1000;
    struct Control {
        // Memory/native state only. No QObject or saved-state reader.
        std::function<bool()> current;
        std::function<qint64()> clock = now;
        qint64 deadline = 0;
    };
    struct Ticket { Epoch expected; QByteArray request; Control control; };
    struct Result {
        Status status = Status::Error;
        Epoch epoch;
        bool applied = false;
        QString error;
    };
    struct PreparedPeer {
        Epoch epoch;
        std::shared_ptr<const StoreProcessFence> fence;
    };
    using Recovery = std::function<bool(const StoreProfileWriteContext&, QString*)>;
    using Work = std::function<Change(const StoreProfileWriteContext&, QString*)>;

    static std::unique_ptr<StoreProfileGeneration> acquireCoordinator(
        const QString& directory, Status*, QString* error = nullptr);
    ~StoreProfileGeneration();
    StoreProfileGeneration(const StoreProfileGeneration&) = delete;
    StoreProfileGeneration& operator=(const StoreProfileGeneration&) = delete;
    // Recovery must validate/restore the whole store before it returns true.
    Result recover(const Control&, Recovery);
    // The work must end with a coherent commit/rollback or NeedsRecovery.
    // It must check context.current() at its ordinary final commit boundary.
    static Result write(const QString& directory, const Ticket&, Work);
    // This is a prepared join only. The control channel must confirm this
    // exact epoch before the caller publishes ready local authority.
    static PreparedPeer prepareJoin(const QString& directory, const Epoch& expected,
        const Control&, Status*, QString* error = nullptr);
private:
    struct State;
    explicit StoreProfileGeneration(std::unique_ptr<State>);
    const std::shared_ptr<State> m_state;
};
}
