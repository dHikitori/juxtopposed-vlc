// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_store_process_fence.hpp"
#include <QDir>
#include <QFile>
#include <utility>
#ifdef Q_OS_WIN
# include <windows.h>
#elif defined(Q_OS_UNIX)
# include <cerrno>
# include <fcntl.h>
# include <sys/file.h>
# include <sys/stat.h>
# include <unistd.h>
#endif

namespace vlc::addons {
struct StoreProcessFence::Native {
#ifdef Q_OS_WIN
    HANDLE file = INVALID_HANDLE_VALUE;
    ~Native() {
        const auto saved = GetLastError();
        if (file != INVALID_HANDLE_VALUE) {
            OVERLAPPED range{};
            UnlockFileEx(file, 0, 1, 0, &range);
            CloseHandle(file);
        }
        SetLastError(saved);
    }
#elif defined(Q_OS_UNIX)
    int file = -1;
    ~Native() {
        const auto saved = errno;
        // Closing the last handle also covers abnormal process termination.
        // Do not unlock a description still inherited by a child before exec.
        if (file >= 0) ::close(file);
        errno = saved;
    }
#endif
};
StoreProcessFence::StoreProcessFence(std::unique_ptr<Native> native) : m_native(std::move(native)) {}
StoreProcessFence::~StoreProcessFence() = default;
std::shared_ptr<const StoreProcessFence> StoreProcessFence::acquire(
    const QString& file, Mode mode, Status* status, QString* error)
{
    if (status) *status = Status::Error;
    if (error) error->clear();
    const auto fail = [error](const char* message) -> std::shared_ptr<const StoreProcessFence> {
        if (error) *error = QString::fromLatin1(message);
        return {};
    };
    if (!QDir::isAbsolutePath(file) || file.size() > 32760 || file.contains(QChar::Null)
        || (mode != Mode::Peer && mode != Mode::Replacement))
        return fail("The add-on profile lock path is not correct.");
    auto native = std::make_unique<Native>();
#ifdef Q_OS_WIN
    const auto path = QDir::toNativeSeparators(file);
    // No handle inheritance or delete sharing. The profile owns this stable
    // file for all coordinator generations; it must not replace or remove it.
    native->file = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (native->file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION) { if (status) *status = Status::Busy; return {}; }
        return fail("The app cannot open the add-on profile lock.");
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(native->file) != FILE_TYPE_DISK || !GetFileInformationByHandle(native->file, &info)
        || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        || info.nFileSizeHigh || info.nFileSizeLow || info.nNumberOfLinks != 1)
        return fail("The add-on profile lock file is not correct.");
    OVERLAPPED range{};
    if (!LockFileEx(native->file, LOCKFILE_FAIL_IMMEDIATELY
            | (mode == Mode::Replacement ? LOCKFILE_EXCLUSIVE_LOCK : 0), 0, 1, 0, &range)) {
        if (GetLastError() == ERROR_LOCK_VIOLATION) { if (status) *status = Status::Busy; return {}; }
        return fail("The app cannot lock the add-on profile.");
    }
#elif defined(Q_OS_UNIX)
    const auto path = QFile::encodeName(file);
    native->file = ::open(path.constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (native->file < 0) return fail("The app cannot open the add-on profile lock.");
    struct stat info{};
    if (fstat(native->file, &info) || !S_ISREG(info.st_mode) || info.st_size || info.st_nlink != 1
        || info.st_uid != geteuid() || (info.st_mode & 0077))
        return fail("The add-on profile lock file is not correct.");
    if (flock(native->file, (mode == Mode::Peer ? LOCK_SH : LOCK_EX) | LOCK_NB)) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) { if (status) *status = Status::Busy; return {}; }
        return fail("The app cannot lock the add-on profile.");
    }
#else
    return fail("The app cannot lock add-on profiles on this system.");
#endif
    const auto result = std::shared_ptr<const StoreProcessFence>(new StoreProcessFence(std::move(native)));
    if (status) *status = Status::Acquired;
    return result;
}
}
