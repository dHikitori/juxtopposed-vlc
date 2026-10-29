// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_store_profile_generation.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QtEndian>
#include <chrono>
#include <atomic>
#include <limits>
#include <utility>

namespace vlc::addons {
namespace {
using Generation = StoreProfileGeneration;
using Status = Generation::Status;
using Epoch = Generation::Epoch;
enum class Phase : quint8 { Ready, Writing, Recovering };
struct Record { Epoch epoch; Phase phase = Phase::Recovering; QByteArray request; };
constexpr qsizetype RecordSize = 88;
bool nonce(const QByteArray& value) { return value.size() == 16 && value != QByteArray(16, '\0'); }
QString rootPath(const QString& input)
{
    if (!QDir::isAbsolutePath(input) || input.size() > 32000 || input.contains(QChar::Null)) return {};
    const QFileInfo info(input);
    return info.isDir() ? info.canonicalFilePath() : QString{};
}
QString stampPath(const QString& root) { return QDir(root).filePath(QStringLiteral("hakone-authority.stamp")); }
QString fencePath(const QString& root) { return QDir(root).filePath(QStringLiteral("hakone-authority.fence")); }
bool fail(QString* error, const char* message) { if (error) *error = QString::fromLatin1(message); return false; }
QByteArray encode(const Record& record)
{
    QByteArray bytes(56, '\0');
    bytes.replace(0, 8, QByteArrayLiteral("HAKONEA1")); bytes[8] = char(record.phase);
    bytes.replace(16, 16, record.epoch.generation);
    qToBigEndian<quint64>(record.epoch.revision, bytes.data() + 32);
    if (record.phase == Phase::Writing) bytes.replace(40, 16, record.request);
    bytes.append(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
    return bytes;
}
bool decode(const QByteArray& bytes, Record* output)
{
    if (bytes.size() != RecordSize || bytes.first(8) != QByteArrayLiteral("HAKONEA1")
        || bytes.sliced(9, 7) != QByteArray(7, '\0')
        || bytes.sliced(56) != QCryptographicHash::hash(bytes.first(56), QCryptographicHash::Sha256)) return false;
    const auto phase = quint8(bytes.at(8));
    if (phase > quint8(Phase::Recovering)) return false;
    Record value{{bytes.sliced(16, 16), qFromBigEndian<quint64>(bytes.constData() + 32)}, Phase(phase), {}};
    const auto request = bytes.sliced(40, 16);
    if (!nonce(value.epoch.generation) || (value.phase == Phase::Writing ? !nonce(request) : request != QByteArray(16, '\0'))) return false;
    if (value.phase == Phase::Writing) value.request = request;
    *output = std::move(value); return true;
}
bool read(const QString& root, Record* output, QString* error)
{
    QFile file(stampPath(root)); const QFileInfo info(file);
    if (info.isSymLink() || !info.isFile() || info.size() != RecordSize || !file.open(QIODevice::ReadOnly))
        return fail(error, "The app cannot read the add-on profile record.");
    if (!decode(file.read(RecordSize + 1), output)) return fail(error, "The add-on profile record is not correct.");
    return true;
}
bool save(const QString& root, const Record& record, QString* error)
{
    const auto path = stampPath(root); const QFileInfo info(path);
    if (info.isSymLink() || (info.exists() && !info.isFile())) return fail(error, "The add-on profile record file is not correct.");
    QSaveFile file(path); file.setDirectWriteFallback(false);
    const auto bytes = encode(record);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) return fail(error, "The app cannot save the add-on profile record.");
    return true;
}
struct Guard {
    Generation::Control control;
    qint64 started = -1;
    explicit Guard(const Generation::Control& value) : control(value) {
        try { if (control.clock) started = control.clock(); } catch (...) {}
    }
    bool valid() const noexcept {
        return control.current && control.clock && started >= 0 && control.deadline > 0
            && (control.deadline <= started || control.deadline - started <= Generation::MaximumDuration);
    }
    bool current() const noexcept {
        try {
            const auto tick = control.clock();
            return tick >= started && tick < control.deadline && control.current();
        } catch (...) { return false; }
    }
};
// Destruction order releases store.lock before the exclusive native fence.
struct Locks {
    std::shared_ptr<const StoreProcessFence> fence;
    QLockFile store;
    explicit Locks(const QString& root) : store(QDir(root).filePath(QStringLiteral("store.lock"))) { store.setStaleLockTime(0); }
    Status acquire(const QString& root, QString* error) {
        if (!store.tryLock(0)) {
            if (store.error() == QLockFile::LockFailedError) return Status::Busy;
            fail(error, "The app cannot lock the add-on profile."); return Status::Error;
        }
        StoreProcessFence::Status status;
        fence = StoreProcessFence::acquire(fencePath(root), StoreProcessFence::Mode::Replacement, &status, error);
        return fence ? Status::Ready : status == StoreProcessFence::Status::Busy ? Status::Busy : Status::Error;
    }
};
Generation::Result result(Status status, const QString& error = {}) { Generation::Result value; value.status = status; value.error = error; return value; }
}
struct StoreProfileGeneration::State {
    std::atomic_bool open{true};
    const QString root;
    QLockFile owner;
    explicit State(QString directory) : root(std::move(directory)), owner(QDir(root).filePath(QStringLiteral("hakone-authority.owner.lock"))) {
        owner.setStaleLockTime(0);
    }
};
StoreProfileWriteContext::StoreProfileWriteContext(QString directory, std::function<bool()> current)
    : m_directory(std::move(directory)), m_current(std::move(current)) {}
bool StoreProfileWriteContext::current() const noexcept
{ try { return m_current && m_current(); } catch (...) { return false; } }
StoreProfileGeneration::StoreProfileGeneration(std::unique_ptr<State> state) : m_state(std::move(state)) {}
StoreProfileGeneration::~StoreProfileGeneration() { m_state->open.store(false, std::memory_order_release); }
qint64 StoreProfileGeneration::now() noexcept
{ return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::unique_ptr<StoreProfileGeneration> StoreProfileGeneration::acquireCoordinator(const QString& directory, Status* status, QString* error)
{
    if (status) *status = Status::Error;
    if (error) error->clear();
    const auto root = rootPath(directory);
    if (root.isEmpty()) { fail(error, "The add-on profile directory is not correct."); return {}; }
    auto state = std::make_unique<State>(root);
    if (!state->owner.tryLock(0)) {
        if (state->owner.error() == QLockFile::LockFailedError) { if (status) *status = Status::Busy; }
        else fail(error, "The app cannot lock the add-on profile owner.");
        return {};
    }
    if (status) *status = Status::Ready;
    return std::unique_ptr<StoreProfileGeneration>(new StoreProfileGeneration(std::move(state)));
}
StoreProfileGeneration::Result StoreProfileGeneration::recover(const Control& control, Recovery recovery)
{
    const auto state = m_state;
    const Guard guard(control);
    if (!recovery || !guard.valid()) return result(Status::Error, QStringLiteral("The add-on profile recovery request is not correct."));
    if (!state->open.load(std::memory_order_acquire) || !guard.current()) return result(Status::Stale);
    Locks locks(state->root); QString error;
    const auto acquired = locks.acquire(state->root, &error);
    if (acquired != Status::Ready) return result(acquired, error);
    if (!state->open.load(std::memory_order_acquire) || !guard.current() || !state->owner.isLocked()) return result(Status::Stale);
    Record previous;
    if (QFileInfo::exists(stampPath(state->root)) && !read(state->root, &previous, &error)) return result(Status::NeedsRecovery, error);
    QByteArray generation(16, '\0');
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        for (qsizetype at = 0; at < 16; at += 4) qToBigEndian<quint32>(QRandomGenerator::system()->generate(), generation.data() + at);
        if (nonce(generation) && generation != previous.epoch.generation) break;
    }
    if (!nonce(generation) || generation == previous.epoch.generation)
        return result(Status::Error, QStringLiteral("The app cannot create a new add-on profile generation."));
    Record record{{generation, 0}, Phase::Recovering, {}};
    if (!save(state->root, record, &error)) return result(Status::NeedsRecovery, error);
    const StoreProfileWriteContext context(state->root, [state, &guard, &locks] {
        return state->open.load(std::memory_order_acquire) && guard.current() && state->owner.isLocked() && locks.store.isLocked();
    });
    bool coherent = false;
    try { if (context.current()) coherent = recovery(context, &error); }
    catch (...) { error = QStringLiteral("The add-on profile recovery did not complete."); }
    if (!coherent || !context.current()) return result(Status::NeedsRecovery, error);
    record.phase = Phase::Ready;
    if (!save(state->root, record, &error)) return result(Status::NeedsRecovery, error);
    if (!context.current()) return result(Status::NeedsRecovery);
    auto value = result(Status::Ready); value.epoch = record.epoch; return value;
}
StoreProfileGeneration::Result StoreProfileGeneration::write(const QString& directory, const Ticket& ticket, Work work)
{
    const Guard guard(ticket.control);
    if (!work || !nonce(ticket.expected.generation) || !nonce(ticket.request) || !guard.valid()
        || ticket.expected.revision == std::numeric_limits<quint64>::max())
        return result(Status::Error, QStringLiteral("The add-on profile write request is not correct."));
    if (!guard.current()) return result(Status::Stale);
    const auto root = rootPath(directory);
    if (root.isEmpty()) return result(Status::Error, QStringLiteral("The add-on profile directory is not correct."));
    Locks locks(root); QString error;
    const auto acquired = locks.acquire(root, &error);
    if (acquired != Status::Ready) return result(acquired, error);
    Record record;
    if (!read(root, &record, &error)) return result(Status::NeedsRecovery, error);
    if (record.epoch != ticket.expected) return result(Status::Stale);
    if (record.phase != Phase::Ready) return result(Status::NeedsRecovery);
    if (!guard.current()) return result(Status::Stale);
    ++record.epoch.revision; record.phase = Phase::Writing; record.request = ticket.request;
    if (!save(root, record, &error)) return result(Status::NeedsRecovery, error);
    const StoreProfileWriteContext context(root, [&] { return guard.current() && locks.store.isLocked(); });
    Change change = Change::NeedsRecovery;
    try { if (context.current()) change = work(context, &error); }
    catch (...) { error = QStringLiteral("The add-on profile change did not complete."); }
    if (change == Change::NeedsRecovery || !context.current()) return result(Status::NeedsRecovery, error);
    if (change != Change::Applied && change != Change::RolledBack) return result(Status::NeedsRecovery);
    record.phase = Phase::Ready; record.request.clear();
    if (!save(root, record, &error)) return result(Status::NeedsRecovery, error);
    if (!context.current()) return result(Status::NeedsRecovery);
    auto value = result(Status::Ready, change == Change::RolledBack ? error : QString{});
    value.epoch = record.epoch; value.applied = change == Change::Applied; return value;
}
StoreProfileGeneration::PreparedPeer StoreProfileGeneration::prepareJoin(const QString& directory, const Epoch& expected,
    const Control& control, Status* status, QString* error)
{
    if (status) *status = Status::Error;
    if (error) error->clear();
    const Guard guard(control);
    if (!nonce(expected.generation) || !guard.valid()) { fail(error, "The add-on profile join request is not correct."); return {}; }
    if (!guard.current()) { if (status) *status = Status::Stale; return {}; }
    const auto root = rootPath(directory);
    if (root.isEmpty()) { fail(error, "The add-on profile directory is not correct."); return {}; }
    StoreProcessFence::Status acquired;
    auto fence = StoreProcessFence::acquire(fencePath(root), StoreProcessFence::Mode::Peer, &acquired, error);
    if (!fence) { if (status) *status = acquired == StoreProcessFence::Status::Busy ? Status::Busy : Status::Error; return {}; }
    Record record;
    if (!read(root, &record, error) || record.phase != Phase::Ready) { if (status) *status = Status::NeedsRecovery; return {}; }
    if (record.epoch != expected || !guard.current()) { if (status) *status = Status::Stale; return {}; }
    if (status) *status = Status::Ready;
    return {record.epoch, std::move(fence)};
}
}
