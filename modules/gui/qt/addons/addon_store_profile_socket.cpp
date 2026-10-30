// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_store_profile_socket.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QRandomGenerator>
#include <QtEndian>
#include <utility>
#include <limits>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#include <QWinEventNotifier>
#include <vector>
#else
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace vlc::addons {
namespace {
#ifdef Q_OS_WIN
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE release() { return std::exchange(value, INVALID_HANDLE_VALUE); }
};
QByteArray processUser(HANDLE process)
{
    HANDLE raw = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) return {};
    Handle token(raw); DWORD bytes = 0;
    GetTokenInformation(raw, TokenUser, nullptr, 0, &bytes);
    if (bytes < sizeof(TOKEN_USER) || bytes > 65536) return {};
    std::vector<unsigned char> buffer(bytes);
    if (!GetTokenInformation(raw, TokenUser, buffer.data(), bytes, &bytes)) return {};
    const auto sid = reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid;
    if (!IsValidSid(sid)) return {};
    const auto length = GetLengthSid(sid);
    if (!length || length > SECURITY_MAX_SID_SIZE) return {};
    QByteArray result(qsizetype(length), '\0');
    return CopySid(length, result.data(), sid) ? result : QByteArray{};
}
bool sameUser(qintptr descriptor, bool server)
{
    ULONG pid = 0; const auto pipe = reinterpret_cast<HANDLE>(descriptor);
    if (!(server ? GetNamedPipeClientProcessId(pipe, &pid) : GetNamedPipeServerProcessId(pipe, &pid)) || !pid) return false;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process.value || process.value == INVALID_HANDLE_VALUE) return false;
    const auto own = processUser(GetCurrentProcess()), peer = processUser(process.value);
    return !own.isEmpty() && own == peer;
}
void closeDescriptor(qintptr descriptor) { CloseHandle(reinterpret_cast<HANDLE>(descriptor)); }
QString pipeName(const QString& address) { return QStringLiteral("\\\\.\\pipe\\") + address; }
#else
bool sameUser(qintptr descriptor, bool)
{
#if defined(Q_OS_LINUX)
    struct ucred credentials{}; socklen_t length = sizeof(credentials);
    return getsockopt(int(descriptor), SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0
        && length == sizeof(credentials) && credentials.uid == geteuid();
#elif defined(Q_OS_DARWIN) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD)
    uid_t uid; gid_t gid;
    return getpeereid(int(descriptor), &uid, &gid) == 0 && uid == geteuid();
#else
    Q_UNUSED(descriptor); return false;
#endif
}
void closeDescriptor(qintptr descriptor) { ::close(int(descriptor)); }
bool privateDirectory(const QString& directory, bool create = true)
{
    if (!QDir::isAbsolutePath(directory) || directory.size() > 32000 || directory.contains(QChar::Null)) return false;
    const auto path = QFile::encodeName(directory);
    if (create && ::mkdir(path.constData(), 0700) != 0 && errno != EEXIST) return false;
    struct stat info{};
    return lstat(path.constData(), &info) == 0 && S_ISDIR(info.st_mode)
        && info.st_uid == geteuid() && (info.st_mode & 0077) == 0;
}
#endif
// The endpoint secret makes the local name unpredictable for other users.
// It is not an access credential: same-user checks still decide admission.
constexpr qsizetype SecretSize = 40;
QString secretPath(const QString& root) { return QDir(root).filePath(QStringLiteral("hakone-authority.endpoint")); }
bool validSecret(const QByteArray& bytes)
{
    return bytes.size() == SecretSize && bytes.first(8) == QByteArrayLiteral("HAKONEE1")
        && bytes.sliced(8) != QByteArray(SecretSize - 8, '\0');
}
#ifdef Q_OS_WIN
QByteArray readSecret(const QString& path, bool* missing)
{
    *missing = false;
    Handle file(CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        *missing = error == ERROR_FILE_NOT_FOUND;
        return {};
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(file.value) != FILE_TYPE_DISK || !GetFileInformationByHandle(file.value, &info)
        || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        || info.nFileSizeHigh || info.nFileSizeLow != DWORD(SecretSize) || info.nNumberOfLinks != 1) return {};
    QByteArray bytes(SecretSize, '\0'); DWORD read = 0;
    if (!ReadFile(file.value, bytes.data(), DWORD(SecretSize), &read, nullptr) || read != DWORD(SecretSize)) return {};
    return bytes;
}
bool publishSecret(const QString& path, const QByteArray& bytes)
{
    const QString temporary = path + QLatin1Char('.') + QString::fromLatin1(QByteArray::number(QRandomGenerator::system()->generate64(), 16)) + QStringLiteral(".tmp");
    const auto name = reinterpret_cast<LPCWSTR>(temporary.utf16());
    const HANDLE file = CreateFileW(name, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool complete = WriteFile(file, bytes.constData(), DWORD(bytes.size()), &written, nullptr)
        && written == DWORD(bytes.size()) && FlushFileBuffers(file);
    const bool closed = CloseHandle(file);
    // No MOVEFILE_REPLACE_EXISTING: a concurrent first publication wins.
    const bool moved = complete && closed && MoveFileExW(name, reinterpret_cast<LPCWSTR>(path.utf16()), MOVEFILE_WRITE_THROUGH);
    if (!moved) DeleteFileW(name);
    return moved;
}
#else
QByteArray readSecret(const QString& path, bool* missing)
{
    *missing = false;
    const auto name = QFile::encodeName(path);
    const int file = ::open(name.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (file < 0) { *missing = errno == ENOENT; return {}; }
    struct stat info{};
    QByteArray bytes(SecretSize, '\0');
    const bool valid = fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_size == SecretSize
        && info.st_nlink == 1 && info.st_uid == geteuid() && (info.st_mode & 0077) == 0
        && ::read(file, bytes.data(), size_t(SecretSize)) == SecretSize;
    ::close(file);
    return valid ? bytes : QByteArray{};
}
bool publishSecret(const QString& path, const QByteArray& bytes)
{
    const auto final = QFile::encodeName(path);
    const QByteArray temporary = final + '.' + QByteArray::number(QRandomGenerator::system()->generate64(), 16) + ".tmp";
    const int file = ::open(temporary.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file < 0) return false;
    const bool written = ::write(file, bytes.constData(), size_t(bytes.size())) == bytes.size() && ::fsync(file) == 0;
    const bool closed = ::close(file) == 0;
    // link() never replaces an existing name: a concurrent first publication wins.
    const bool linked = written && closed && ::link(temporary.constData(), final.constData()) == 0;
    ::unlink(temporary.constData());
    return linked;
}
#endif
// Filesystem work. The first caller for a profile creates the secret; every
// later caller, including a concurrent one, reads that same published file.
QByteArray endpointSecret(const QString& root)
{
    const auto path = secretPath(root);
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool missing = false;
        const auto existing = readSecret(path, &missing);
        if (validSecret(existing)) return existing;
        // A damaged secret is not replaced here. Other processes can still
        // use its name; replacement belongs to whole-profile recovery.
        if (!missing || attempt) return {};
        quint32 random[(SecretSize - 8) / 4] = {};
        QRandomGenerator::system()->fillRange(random);
        auto created = QByteArrayLiteral("HAKONEE1");
        created.append(reinterpret_cast<const char*>(random), sizeof(random));
        if (!validSecret(created)) return {};
        publishSecret(path, created);
    }
    return {};
}
#ifndef Q_OS_WIN
QString runtimeParent(const QString& requested, const QByteArray& digest)
{
    if (!requested.isEmpty()) return privateDirectory(requested) ? requested : QString{};
    const auto xdg = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (QDir::isAbsolutePath(xdg) && privateDirectory(xdg, false)) {
        const auto child = QDir(xdg).filePath(QStringLiteral("hakone"));
        if (privateDirectory(child)) return child;
    }
#ifdef Q_OS_DARWIN
    char temporary[1024] = {};
    const auto length = confstr(_CS_DARWIN_USER_TEMP_DIR, temporary, sizeof(temporary));
    if (length > 0 && length <= sizeof(temporary)) {
        const auto user = QFile::decodeName(temporary);
        if (QDir::isAbsolutePath(user) && privateDirectory(QDir::cleanPath(user), false)) {
            const auto child = QDir(QDir::cleanPath(user)).filePath(QStringLiteral("hakone"));
            if (privateDirectory(child)) return child;
        }
    }
#endif
    // Last fallback. The secret-derived name cannot be created in advance
    // by another user, and privateDirectory() still refuses a foreign one.
    const auto fallback = QDir(QStringLiteral("/tmp")).canonicalPath() + QStringLiteral("/hakone-")
        + QString::fromLatin1(digest.toHex().sliced(32, 16));
    return privateDirectory(fallback) ? fallback : QString{};
}
#endif
struct Descriptor {
    qintptr value;
    explicit Descriptor(qintptr fd) : value(fd) {}
    ~Descriptor() { if (value != -1) closeDescriptor(value); }
    qintptr release() { return std::exchange(value, qintptr(-1)); }
};
}
std::optional<StoreProfileEndpoint> StoreProfileEndpoint::forProfile(const QString& directory, const QString& runtime)
{
    if (!QDir::isAbsolutePath(directory) || directory.size() > 32000 || directory.contains(QChar::Null)) return {};
    const QFileInfo profile(directory); if (!profile.isDir()) return {};
    auto canonical = profile.canonicalFilePath(); if (canonical.isEmpty()) return {};
#ifdef Q_OS_WIN
    canonical = canonical.toCaseFolded(); const auto identity = processUser(GetCurrentProcess());
#else
    const auto identity = QByteArray::number(qulonglong(geteuid()));
#endif
    if (identity.isEmpty()) return {};
    const auto secret = endpointSecret(profile.canonicalFilePath());
    if (!validSecret(secret)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("HAKONE-IPC-2"));
    QByteArray length(4, '\0'); qToBigEndian<quint32>(quint32(identity.size()), length.data()); hash.addData(length); hash.addData(identity);
    hash.addData(secret.sliced(8));
    hash.addData(canonical.toUtf8());
    const auto digest = hash.result();
#ifdef Q_OS_WIN
    Q_UNUSED(runtime);
    QString address = QStringLiteral("hakone-profile-") + QString::fromLatin1(digest.toHex().first(32));
#else
    const auto parent = runtimeParent(runtime, digest);
    if (parent.isEmpty()) return {};
    const auto address = QDir(parent).filePath(QStringLiteral("hakone-") + QString::fromLatin1(digest.toHex().first(32)));
    if (QFile::encodeName(address).size() >= qsizetype(sizeof(sockaddr_un::sun_path))) return {};
#endif
    return StoreProfileEndpoint(std::move(address));
}
StoreProfileSocket::StoreProfileSocket()
{
    m_socket.setParent(this);
    m_socket.setReadBufferSize(StoreProfileProtocol::MaximumInput);
    m_deadline.setSingleShot(true); m_deadline.setInterval(5000);
    m_deadline.setParent(this); m_deadline.setObjectName(QStringLiteral("hakoneProfileConnectDeadline"));
    connect(&m_deadline, &QTimer::timeout, this, [this] { ioLost(); });
    connect(&m_socket, &QLocalSocket::connected, this, [this] {
        QMetaObject::invokeMethod(this, [this] { opened(); }, Qt::QueuedConnection);
    });
    connect(&m_socket, &QLocalSocket::readyRead, this, [this] { scheduleRead(); });
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] { ioLost(); });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](auto) { ioLost(); });
}
StoreProfileSocket::~StoreProfileSocket() { finish(false); }
bool StoreProfileSocket::setCallbacks(Callbacks callbacks)
{
    if (m_started || m_closed || !callbacks.revoke) return false;
    m_callbacks = std::move(callbacks); m_started = true; return true;
}
bool StoreProfileSocket::connectTo(const StoreProfileEndpoint& endpoint, Callbacks callbacks)
{
    if (m_accepted || !setCallbacks(std::move(callbacks))) return false;
    m_deadline.start(); QPointer<StoreProfileSocket> guard(this);
#ifdef Q_OS_WIN
    const auto name = pipeName(endpoint.address());
    // One local open attempt. Do not use WaitNamedPipe or Qt's busy-pipe wait.
    Handle handle(CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
    if (handle.value == INVALID_HANDLE_VALUE || !sameUser(reinterpret_cast<qintptr>(handle.value), false)) { finish(true); return false; }
    if (!m_socket.setSocketDescriptor(reinterpret_cast<qintptr>(handle.value), QLocalSocket::ConnectedState)) { finish(true); return false; }
    handle.release(); opened();
#else
    m_socket.connectToServer(endpoint.address());
#endif
    return guard && !m_closed;
}
std::unique_ptr<StoreProfileSocket> StoreProfileSocket::accept(qintptr descriptor)
{
    Descriptor owned(descriptor);
    if (!sameUser(descriptor, true)) return {};
    auto socket = std::unique_ptr<StoreProfileSocket>(new StoreProfileSocket);
    socket->m_accepted = true;
    if (!socket->m_socket.setSocketDescriptor(descriptor, QLocalSocket::ConnectedState)) return {};
    owned.release(); return socket;
}
bool StoreProfileSocket::startAccepted(Callbacks callbacks)
{
    if (!m_accepted || !setCallbacks(std::move(callbacks))) return false;
    QPointer<StoreProfileSocket> guard(this); opened(); return guard && !m_closed;
}
void StoreProfileSocket::opened()
{
    if (!m_started || m_closed || m_ready) return;
    // accept() has already checked an accepted client. Check the server here.
    if (!m_accepted && !sameUser(m_socket.socketDescriptor(), false)) { finish(true); return; }
    m_ready = true; m_deadline.stop(); QPointer<StoreProfileSocket> guard(this);
    const auto call = m_callbacks.opened;
    try { if (call) call(); } catch (...) { if (guard) finish(true); return; }
    if (guard && !m_closed) scheduleRead();
}
void StoreProfileSocket::closeAdmission()
{
    if (m_closed) return;
    m_closed = true; m_ready = false; m_deadline.stop();
    const auto revoke = m_callbacks.revoke;
    try { if (revoke) revoke(); } catch (...) {}
}
void StoreProfileSocket::ioLost()
{
    if (m_closed) return;
    closeAdmission();
    // A control callback may delete this object's QLocalSocket member. Run
    // it after Qt has left the low-level socket signal stack.
    QMetaObject::invokeMethod(this, [this] { finish(true); }, Qt::QueuedConnection);
}
void StoreProfileSocket::finish(bool notify)
{
    closeAdmission();
    const auto lost = notify && !m_lossDelivered ? m_callbacks.lost : std::function<void()>{};
    if (notify) m_lossDelivered = true;
    if (!m_ioClosed) { m_ioClosed = true; m_socket.abort(); }
    try { if (lost) lost(); } catch (...) {}
    // A control callback can destroy this object. Do not access it here.
}
void StoreProfileSocket::stop() { finish(true); }
bool StoreProfileSocket::send(const Message& message)
{
    if (!ready()) return false;
    const auto bytes = StoreProfileProtocol::encode(message);
    if (bytes.isEmpty() || m_socket.bytesToWrite() > MaximumQueuedBytes - bytes.size()) { finish(true); return false; }
    QPointer<StoreProfileSocket> guard(this); const auto written = m_socket.write(bytes);
    if (!guard) return false;
    if (written != bytes.size()) { finish(true); return false; }
    return !m_closed;
}
void StoreProfileSocket::scheduleRead()
{
    if (!ready() || m_readQueued || !m_socket.bytesAvailable()) return;
    m_readQueued = true;
    QMetaObject::invokeMethod(this, [this] { m_readQueued = false; readReady(); }, Qt::QueuedConnection);
}
void StoreProfileSocket::readReady()
{
    if (!ready()) return;
    const auto bytes = m_socket.read(StoreProfileProtocol::MaximumInput);
    if (m_closed || bytes.isEmpty()) return;
    const auto batch = m_decoder.feed(bytes);
    if (!batch.ok) { finish(true); return; }
    QPointer<StoreProfileSocket> guard(this);
    for (const auto& message : batch.messages) {
        const auto call = m_callbacks.received;
        try { if (call) call(message); } catch (...) { if (guard) finish(true); return; }
        if (!guard || m_closed) return;
    }
    scheduleRead();
}
#ifdef Q_OS_WIN
struct StoreProfileListener::Native {
    struct Pending {
        Handle pipe, event;
        OVERLAPPED operation{};
        QPointer<QWinEventNotifier> notifier;
        bool waiting = false, connected = false;
        ~Pending() {
            if (notifier) { notifier->setEnabled(false); QObject::disconnect(notifier.data(), nullptr, nullptr, nullptr); notifier->deleteLater(); }
            if (waiting && pipe.value != INVALID_HANDLE_VALUE) {
                CancelIoEx(pipe.value, &operation); DWORD bytes = 0;
                GetOverlappedResult(pipe.value, &operation, &bytes, TRUE);
            }
        }
    };
    StoreProfileListener* owner;
    const QString name;
    PSECURITY_DESCRIPTOR security = nullptr;
    std::unique_ptr<Pending> pending;
    Native(StoreProfileListener* listener, const QString& address) : owner(listener), name(pipeName(address)) {
        const auto sid = processUser(GetCurrentProcess()); LPWSTR text = nullptr;
        if (sid.isEmpty() || !ConvertSidToStringSidW(const_cast<char*>(sid.constData()), &text)) return;
        const QString sddl = QStringLiteral("O:") + QString::fromWCharArray(text) + QStringLiteral("D:P(A;;GA;;;") + QString::fromWCharArray(text) + QStringLiteral(")"); LocalFree(text);
        ConvertStringSecurityDescriptorToSecurityDescriptorW(reinterpret_cast<LPCWSTR>(sddl.utf16()), SDDL_REVISION_1, &security, nullptr);
    }
    ~Native() { pending.reset(); if (security) LocalFree(security); }
    bool arm(bool first) {
        if (!security) return false;
        auto next = std::make_unique<Pending>(); SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), security, FALSE};
        next->pipe.value = CreateNamedPipeW(reinterpret_cast<LPCWSTR>(name.utf16()), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 0, 0, 0, &attributes);
        if (next->pipe.value == INVALID_HANDLE_VALUE) return false;
        next->event.value = CreateEventW(nullptr, TRUE, FALSE, nullptr); if (!next->event.value) return false;
        next->operation.hEvent = next->event.value;
        if (ConnectNamedPipe(next->pipe.value, &next->operation)) next->connected = true;
        else {
            const auto error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) next->connected = true;
            else if (error == ERROR_IO_PENDING) next->waiting = true;
            else return false;
        }
        next->notifier = new QWinEventNotifier(next->event.value, owner);
        QObject::connect(next->notifier.data(), &QWinEventNotifier::activated, owner, [this](HANDLE) { available(); });
        if (next->connected) SetEvent(next->event.value);
        pending = std::move(next); return true;
    }
    void available() {
        auto old = std::move(pending); if (!old) return;
        old->notifier->setEnabled(false); ResetEvent(old->event.value); DWORD bytes = 0;
        const bool connected = old->connected || GetOverlappedResult(old->pipe.value, &old->operation, &bytes, FALSE);
        if (!connected && GetLastError() == ERROR_IO_INCOMPLETE) { pending = std::move(old); pending->notifier->setEnabled(true); return; }
        old->waiting = false;
        if (!arm(false)) { owner->close(true); return; }
        if (connected) owner->queue(reinterpret_cast<qintptr>(old->pipe.release()));
    }
};
#else
struct StoreProfileListener::Native : QLocalServer {
    StoreProfileListener* owner;
    explicit Native(StoreProfileListener* listener, const QString&) : QLocalServer(listener), owner(listener) {
        // Bind the exact path. Qt's access-option path can rename a temporary
        // socket over an existing endpoint. The parent is already private.
        setSocketOptions(NoOptions); setMaxPendingConnections(int(MaximumConnections));
    }
    void incomingConnection(quintptr descriptor) override { owner->queue(qintptr(descriptor)); }
};
#endif
StoreProfileListener::StoreProfileListener(Accepted accepted) : m_accepted(std::move(accepted)) {}
StoreProfileListener::~StoreProfileListener() { close(false); }
bool StoreProfileListener::listen(const StoreProfileEndpoint& endpoint)
{
    if (m_listening || !m_accepted || m_serial == std::numeric_limits<quint64>::max()) return false;
    ++m_serial;
    auto native = std::make_unique<Native>(this, endpoint.address());
#ifdef Q_OS_WIN
    if (!native->arm(true)) return false;
#else
    if (!native->listen(endpoint.address())) return false;
    if (::chmod(QFile::encodeName(endpoint.address()).constData(), 0600) != 0) return false;
#endif
    m_native = std::move(native); m_listening = true; return true;
}
void StoreProfileListener::queue(qintptr descriptor)
{
    auto held = std::make_shared<Descriptor>(descriptor);
    // StoreProfileSocket::accept() performs the one server-side user check.
    if (!m_listening || m_pending + m_sockets.size() >= MaximumConnections) return;
    ++m_pending;
    const auto serial = m_serial;
    QMetaObject::invokeMethod(this, [this, held, serial] {
        --m_pending;
        if (serial == m_serial && m_listening) deliver(held->release());
    }, Qt::QueuedConnection);
}
void StoreProfileListener::deliver(qintptr descriptor)
{
    if (!m_listening) { closeDescriptor(descriptor); return; }
    auto socket = StoreProfileSocket::accept(descriptor); if (!socket) return;
    auto* raw = socket.get(); m_sockets.insert(raw);
    QObject::connect(raw, &QObject::destroyed, this, [this, raw] { m_sockets.remove(raw); });
    const auto accepted = m_accepted;
    try { accepted(std::move(socket)); } catch (...) {}
    // The receiver can destroy this listener.
}
void StoreProfileListener::close(bool notify)
{
    m_listening = false;
    if (m_serial != std::numeric_limits<quint64>::max()) ++m_serial;
    QList<QPointer<StoreProfileSocket>> sockets; for (auto* socket : std::as_const(m_sockets)) sockets.push_back(socket);
    m_sockets.clear();
    for (const auto& socket : sockets) if (socket) socket->closeAdmission();
    m_native.reset();
    for (const auto& socket : sockets) if (socket) socket->finish(notify);
    // Control callbacks can destroy this listener. Only local state is used.
}
void StoreProfileListener::stop() { close(true); }
}
