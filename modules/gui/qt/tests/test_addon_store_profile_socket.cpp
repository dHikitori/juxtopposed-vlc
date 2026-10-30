// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_store_profile_socket.hpp"
#include "addons/addon_store_authority.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QLocalServer>
#include <QScopeGuard>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <atomic>
#include <thread>
#include <vector>
#include <cstdio>
#ifdef Q_OS_WIN
#include <windows.h>
#include <aclapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
using namespace vlc::addons;
using S = StoreProfileSocket;
using L = StoreProfileListener;
using E = StoreProfileEndpoint;
using P = StoreProfileProtocol;
using K = P::Kind;
using G = StoreProfileGeneration;
namespace {
QByteArray nonce(char c) { return QByteArray(16, c); }
P::Message frame() { return {K::Join, nonce('c'), {}, nonce('j'), 0}; }
G::Control control() { return {[] { return true; }, G::now, G::now() + 20 * 1000 * 1000}; }
#ifdef Q_OS_LINUX
const QString CrossUserImage = QStringLiteral("registry.videolan.org/vlc-debian-win64-posix:20260611225331");
// The cross-user cases need a second account. Docker supplies one without
// changing the host. Hosts without Docker or the image skip these cases.
QString crossUserUnavailable() {
    if (QStandardPaths::findExecutable(QStringLiteral("docker")).isEmpty()) return QStringLiteral("docker is not installed");
    QProcess inspect; inspect.start(QStringLiteral("docker"), {QStringLiteral("image"), QStringLiteral("inspect"), CrossUserImage});
    if (!inspect.waitForFinished(10000)) { inspect.kill(); inspect.waitForFinished(1000); return QStringLiteral("docker does not respond"); }
    if (inspect.exitStatus() != QProcess::NormalExit || inspect.exitCode() != 0) return QStringLiteral("docker cannot reach the cross-user image (daemon stopped or image missing)");
    return {};
}
#define REQUIRE_CROSS_USER() do { const auto reason = crossUserUnavailable(); if (!reason.isEmpty()) QSKIP(qPrintable(reason)); } while (false)
#endif
QString secretFile(const QString& profile) { return QDir(profile).filePath(QStringLiteral("hakone-authority.endpoint")); }
bool rawConnect(QLocalSocket& socket, const E& endpoint) {
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("\\\\.\\pipe\\") + endpoint.address();
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(name.utf16()), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    return handle != INVALID_HANDLE_VALUE && socket.setSocketDescriptor(reinterpret_cast<qintptr>(handle), QLocalSocket::ConnectedState);
#else
    socket.connectToServer(endpoint.address()); return true;
#endif
}
struct Fixture {
    QTemporaryDir root, runtime;
    std::optional<E> endpoint = E::forProfile(root.path(), runtime.path());
    unsigned serverOpened = 0, clientOpened = 0, serverRevoked = 0, clientRevoked = 0, serverLost = 0, clientLost = 0, received = 0;
    std::vector<std::unique_ptr<S>> servers;
    std::unique_ptr<L> listener;
    std::unique_ptr<S> client;
    std::function<void(const P::Message&)> onMessage;
    Fixture() {
        listener = std::make_unique<L>([this](std::unique_ptr<S> socket) {
            auto* raw = socket.get(); servers.push_back(std::move(socket));
            raw->startAccepted({[this] { ++serverRevoked; }, [this] { ++serverOpened; },
                [this](const auto& value) { ++received; if (onMessage) onMessage(value); }, [this] { ++serverLost; }});
        });
    }
    ~Fixture() { listener.reset(); client.reset(); servers.clear(); }
    bool listen() { return endpoint && listener->listen(*endpoint); }
    bool connect() {
        client = std::make_unique<S>(); return client->connectTo(*endpoint, {[this] { ++clientRevoked; }, [this] { ++clientOpened; }, {}, [this] { ++clientLost; }});
    }
};
int child(QCoreApplication& app, const QStringList& args) {
    const auto endpoint = E::forProfile(args.at(2), args.at(3)); if (!endpoint) return 2;
    StoreAuthority authority(args.at(2)); auto peer = authority.attachPeer(); if (!peer) return 3;
    const auto stamp = peer->close(); if (!stamp || !peer->drain(stamp)) return 4;
    S socket; QByteArray connection; G::PreparedPeer prepared; G::Epoch offered; StoreAuthorityToken token; StoreAuthorityWatch watch;
    std::atomic<quint64> closed{0}; std::atomic_uint calls{0}; std::atomic_bool okay{false}; std::thread drain;
    const auto finish = [&] { QMetaObject::invokeMethod(&app, [&] { app.quit(); }, Qt::QueuedConnection); };
    socket.connectTo(*endpoint, {
        [close = peer->closeHandle(), &closed] { closed.store(close()); }, {},
        [&](const P::Message& message) {
            if (message.kind == K::Welcome) { connection = message.connection; socket.send({K::Join, connection, {}, nonce('j'), 0}); }
            else if (message.kind == K::Candidate) {
                if (message.connection != connection || message.request != nonce('j')) { socket.stop(); return; }
                offered = message.epoch; G::Status status; auto current = control(); current.current = peer->pending(stamp);
                prepared = G::prepareJoin(args.at(2), offered, current, &status); if (!prepared.fence) { socket.stop(); return; }
                auto confirm = message; confirm.kind = K::Confirm; socket.send(confirm);
            } else if (message.kind == K::Ready) {
                if (message.connection != connection || message.request != nonce('j') || message.epoch != offered
                    || !peer->admit(stamp, std::move(prepared.fence))) { socket.stop(); return; }
                token = authority.capture(); watch = token.onInvalidated([&] { ++calls; });
                if (!token.current() || !bool(watch)) { socket.stop(); return; }
                std::puts("ready"); std::fflush(stdout);
            } else socket.stop();
        },
        [&] {
            const bool immediate = !token.current() && !authority.capture().current(); prepared.fence.reset();
            drain = std::thread([&, immediate] {
                okay.store(immediate && peer->drain(closed.load()) && calls.load() == 1);
                std::puts("closed"); std::fflush(stdout); finish();
            });
        }});
    app.exec(); socket.stop(); if (drain.joinable()) drain.join(); peer.reset(); return okay.load() ? 0 : 5;
}
}
class TestAddonStoreProfileSocket : public QObject {
    Q_OBJECT
private slots:
#ifndef Q_OS_WIN
    void aDeepRuntimeDirectoryFallsBackToAFittingOne() {
        QTemporaryDir profile, base;
        QVERIFY(profile.isValid() && base.isValid());
        const auto saved = qgetenv("XDG_RUNTIME_DIR");
        const auto restore = qScopeGuard([saved] {
            if (saved.isNull()) qunsetenv("XDG_RUNTIME_DIR"); else qputenv("XDG_RUNTIME_DIR", saved);
        });
        // A private runtime directory whose socket path cannot fit sun_path.
        auto deep = base.path();
        while (QFile::encodeName(deep).size() < 120) deep += QStringLiteral("/runtime-directory");
        QVERIFY(QDir().mkpath(deep));
        for (auto path = deep; path.size() > base.path().size(); path = QFileInfo(path).path())
            QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(deep));
        const auto fallback = E::forProfile(profile.path());
        QVERIFY(fallback);
        QVERIFY2(fallback->address().startsWith(QDir(QStringLiteral("/tmp")).canonicalPath() + QStringLiteral("/hakone-")),
                 qPrintable(fallback->address()));
        // A short runtime directory is still the first choice.
        QTemporaryDir shortRuntime; QVERIFY(shortRuntime.isValid());
        QVERIFY(QFile::setPermissions(shortRuntime.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(shortRuntime.path()));
        const auto preferred = E::forProfile(profile.path());
        QVERIFY(preferred);
        QVERIFY(preferred->address().startsWith(QDir(shortRuntime.path()).filePath(QStringLiteral("hakone")) + QLatin1Char('/')));
    }
#endif
    void endpointIsCanonicalAndProfileScoped() {
        QTemporaryDir root, other, runtime;
        const auto first = E::forProfile(root.path(), runtime.path()), alias = E::forProfile(root.path() + QStringLiteral("/."), runtime.path()), second = E::forProfile(other.path(), runtime.path());
        QVERIFY(first && alias && second); QCOMPARE(first->address(), alias->address()); QVERIFY(first->address() != second->address());
        QVERIFY(!first->address().contains(QStringLiteral("\\\\")));
    }
    void endpointSecretIsCreatedOnceAndOwnerOnly() {
        QTemporaryDir root, runtime; QVERIFY(!QFileInfo::exists(secretFile(root.path())));
        const auto first = E::forProfile(root.path(), runtime.path()); QVERIFY(first);
        QFile secret(secretFile(root.path())); QVERIFY(secret.open(QIODevice::ReadOnly));
        const auto bytes = secret.readAll(); QCOMPARE(bytes.size(), qsizetype(40)); QVERIFY(bytes.startsWith("HAKONEE1"));
        QCOMPARE(QDir(root.path()).entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral("hakone-authority.endpoint")});
#ifndef Q_OS_WIN
        struct stat info{}; QVERIFY(::stat(QFile::encodeName(secret.fileName()).constData(), &info) == 0); QCOMPARE(info.st_mode & 0777, mode_t(0600));
#endif
        const auto again = E::forProfile(root.path(), runtime.path()); QVERIFY(again); QCOMPARE(again->address(), first->address());
        QFile reread(secret.fileName()); QVERIFY(reread.open(QIODevice::ReadOnly)); QCOMPARE(reread.readAll(), bytes);
    }
    void concurrentFirstCallsShareOneSecret() {
        QTemporaryDir root, runtime; constexpr int count = 16; std::vector<QString> results(count); std::vector<std::thread> threads;
        std::atomic_int ready{0};
        for (int i = 0; i < count; ++i) threads.emplace_back([&, i] { ++ready; while (ready.load() < count) std::this_thread::yield(); const auto endpoint = E::forProfile(root.path(), runtime.path()); if (endpoint) results[i] = endpoint->address(); });
        for (auto& thread : threads) thread.join();
        for (const auto& result : results) { QVERIFY(!result.isEmpty()); QCOMPARE(result, results.front()); }
        QCOMPARE(QDir(root.path()).entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral("hakone-authority.endpoint")});
    }
    void theSecretChangesTheName() {
        QTemporaryDir first, runtime; const auto before = E::forProfile(first.path(), runtime.path()); QVERIFY(before);
        QVERIFY(QFile::remove(secretFile(first.path()))); const auto after = E::forProfile(first.path(), runtime.path()); QVERIFY(after);
        QVERIFY(before->address() != after->address());
    }
    void aDamagedSecretIsRefusedAndKept_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("short") << QByteArray("HAKONEE1"); QTest::newRow("magic") << QByteArray(40, 'x');
        QTest::newRow("zero") << (QByteArray("HAKONEE1") + QByteArray(32, '\0')); QTest::newRow("long") << (QByteArray("HAKONEE1") + QByteArray(33, 'a'));
    }
    void aDamagedSecretIsRefusedAndKept() {
        QFETCH(QByteArray, bytes); QTemporaryDir root, runtime; QFile file(secretFile(root.path()));
        QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), qint64(bytes.size())); file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
        QVERIFY(!E::forProfile(root.path(), runtime.path()));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
    }
#ifndef Q_OS_WIN
    void anUnsafeSecretFileIsRefused_data() { QTest::addColumn<int>("kind"); QTest::newRow("symlink") << 0; QTest::newRow("group readable") << 1; QTest::newRow("hard link") << 2; }
    void anUnsafeSecretFileIsRefused() {
        QFETCH(int, kind); QTemporaryDir root, runtime, other; QVERIFY(E::forProfile(other.path(), runtime.path()));
        const auto source = secretFile(other.path()), target = secretFile(root.path());
        if (kind == 0) QVERIFY(QFile::link(source, target));
        if (kind == 1) { QVERIFY(QFile::copy(source, target)); QVERIFY(::chmod(QFile::encodeName(target).constData(), 0640) == 0); }
        if (kind == 2) QVERIFY(::link(QFile::encodeName(source).constData(), QFile::encodeName(target).constData()) == 0);
        QVERIFY(!E::forProfile(root.path(), runtime.path()));
    }
    void theDefaultRuntimeUsesAPrivateXdgDirectory() {
        QTemporaryDir root, xdg; const auto saved = qgetenv("XDG_RUNTIME_DIR");
        const auto restore = qScopeGuard([&] { if (saved.isNull()) qunsetenv("XDG_RUNTIME_DIR"); else qputenv("XDG_RUNTIME_DIR", saved); });
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(xdg.path()));
        const auto endpoint = E::forProfile(root.path()); QVERIFY(endpoint);
        const auto parent = QFileInfo(endpoint->address()).absolutePath();
        QCOMPARE(parent, QDir(xdg.path()).filePath(QStringLiteral("hakone")));
        struct stat info{}; QVERIFY(::lstat(QFile::encodeName(parent).constData(), &info) == 0);
        QVERIFY(S_ISDIR(info.st_mode)); QCOMPARE(info.st_mode & 0777, mode_t(0700));
        L listener([](std::unique_ptr<S> socket) { socket.release()->deleteLater(); }); QVERIFY(listener.listen(*endpoint));
        S client; bool opened = false;
        QVERIFY(client.connectTo(*endpoint, {[] {}, [&] { opened = true; }, {}, {}})); QTRY_VERIFY(opened);
    }
    void anUnsafeXdgDirectoryIsNotUsedOrCreated() {
        QTemporaryDir root, parent; const auto saved = qgetenv("XDG_RUNTIME_DIR");
        const auto restore = qScopeGuard([&] { if (saved.isNull()) qunsetenv("XDG_RUNTIME_DIR"); else qputenv("XDG_RUNTIME_DIR", saved); });
        const auto missing = parent.filePath(QStringLiteral("missing")); qputenv("XDG_RUNTIME_DIR", QFile::encodeName(missing));
        const auto endpoint = E::forProfile(root.path()); QVERIFY(endpoint); QVERIFY(!QFileInfo::exists(missing));
        QVERIFY(!endpoint->address().startsWith(missing));
        const auto fallback = QFileInfo(endpoint->address()).absolutePath();
        const auto cleanup = qScopeGuard([&] { if (fallback.startsWith(QStringLiteral("/tmp/hakone-"))) QDir(fallback).removeRecursively(); });
#ifndef Q_OS_DARWIN
        QVERIFY(fallback.startsWith(QDir(QStringLiteral("/tmp")).canonicalPath() + QStringLiteral("/hakone-")));
        QVERIFY(!fallback.contains(QStringLiteral("hakone-ipc-")));
#endif
        QVERIFY(::chmod(QFile::encodeName(parent.path()).constData(), 0755) == 0); qputenv("XDG_RUNTIME_DIR", QFile::encodeName(parent.path()));
        const auto open = E::forProfile(root.path()); QVERIFY(open); QVERIFY(!open->address().startsWith(parent.path()));
    }
#endif
    void badProfileNamesAreRefused_data() {
        QTest::addColumn<int>("kind"); QTest::newRow("relative") << 0; QTest::newRow("missing") << 1; QTest::newRow("null") << 2; QTest::newRow("too long") << 3;
    }
    void badProfileNamesAreRefused() {
        QFETCH(int, kind); QTemporaryDir root; auto path = root.path();
        if (kind == 0) path = QStringLiteral("relative");
        if (kind == 1) path += QStringLiteral("/missing");
        if (kind == 2) path += QChar::Null;
        if (kind == 3) path = QLatin1Char('/') + QString(32001, QLatin1Char('a'));
        QVERIFY(!E::forProfile(path));
    }
#ifndef Q_OS_WIN
    void unsafeRuntimeDirectoriesAreRefused_data() { QTest::addColumn<bool>("link"); QTest::newRow("permissions") << false; QTest::newRow("symlink") << true; }
    void unsafeRuntimeDirectoriesAreRefused() {
        QFETCH(bool, link); QTemporaryDir profile, runtime, parent;
        if (link) { const auto name = parent.filePath(QStringLiteral("link")); QVERIFY(QFile::link(runtime.path(), name)); QVERIFY(!E::forProfile(profile.path(), name)); }
        else { QVERIFY(::chmod(QFile::encodeName(runtime.path()).constData(), 0755) == 0); QVERIFY(!E::forProfile(profile.path(), runtime.path())); }
    }
#endif
    void socketExchangesFramesAndHasNoIdleDeadline() {
        Fixture f; QVERIFY(f.listen() && f.connect()); QTRY_VERIFY(f.client->ready() && f.serverOpened == 1);
        QVERIFY(f.client->send(frame())); QTRY_COMPARE(f.received, 1u);
        const auto timer = f.client->findChild<QTimer*>(QStringLiteral("hakoneProfileConnectDeadline")); QVERIFY(timer && !timer->isActive());
        QCOMPARE(f.clientOpened, 1u); QCOMPARE(f.clientRevoked, 0u); QCOMPARE(f.serverRevoked, 0u);
    }
    void aSecondListenerCannotReplaceALiveEndpoint() {
        Fixture f; QVERIFY(f.listen()); L other([](std::unique_ptr<S>) {}); QVERIFY(!other.listen(*f.endpoint)); QVERIFY(f.listener->listening());
        QVERIFY(f.connect()); QTRY_VERIFY(f.serverOpened == 1 && f.client->ready());
    }
    void connectionRefusalClosesNativeStateOnce() {
        Fixture f; QVERIFY(f.endpoint); f.connect(); QTRY_COMPARE(f.clientLost, 1u); QCOMPARE(f.clientRevoked, 1u);
        QVERIFY(!f.client->ready()); f.client->stop(); f.client.reset(); QCOMPARE(f.clientRevoked, 1u); QCOMPARE(f.clientLost, 1u);
    }
    void eofRevokesBeforeItsControlCallback() {
        Fixture f; bool closedBeforeLost = false;
        QVERIFY(f.listen()); f.client = std::make_unique<S>();
        QVERIFY(f.client->connectTo(*f.endpoint, {[&] { ++f.clientRevoked; }, {}, {}, [&] { closedBeforeLost = f.clientRevoked == 1 && !f.client->ready(); ++f.clientLost; }}));
        QTRY_VERIFY(f.serverOpened == 1 && f.client->ready()); f.listener->stop(); QTRY_COMPARE(f.clientLost, 1u); QVERIFY(closedBeforeLost); QCOMPARE(f.serverRevoked, 1u);
    }
    void destructionRevokesWithoutAControlReentry() {
        Fixture f; QVERIFY(f.listen() && f.connect()); QTRY_VERIFY(f.serverOpened == 1 && f.client->ready());
        f.client.reset(); QCOMPARE(f.clientRevoked, 1u); QCOMPARE(f.clientLost, 0u); QTRY_COMPARE(f.serverLost, 1u);
        f.listener.reset(); QCOMPARE(f.serverRevoked, 1u);
    }
    void malformedDataClosesBeforeMessageDispatch() {
        Fixture f; QVERIFY(f.listen()); QLocalSocket raw; QVERIFY(rawConnect(raw, *f.endpoint)); QTRY_COMPARE(raw.state(), QLocalSocket::ConnectedState); QTRY_COMPARE(f.serverOpened, 1u);
        auto bytes = P::encode(frame()).repeated(2); bytes[9] = 1; QCOMPARE(raw.write(bytes), qint64(bytes.size()));
        QTRY_COMPARE(f.serverLost, 1u); QCOMPARE(f.serverRevoked, 1u); QCOMPARE(f.received, 0u);
    }
    void splitMessagesAreDeliveredOnce() {
        Fixture f; QVERIFY(f.listen()); QLocalSocket raw; QVERIFY(rawConnect(raw, *f.endpoint)); QTRY_COMPARE(raw.state(), QLocalSocket::ConnectedState); QTRY_COMPARE(f.serverOpened, 1u);
        const auto bytes = P::encode(frame()); QCOMPARE(raw.write(bytes.first(95)), qint64(95)); raw.flush(); QTest::qWait(20); QCOMPARE(f.received, 0u);
        QCOMPARE(raw.write(bytes.last(1)), qint64(1)); QTRY_COMPARE(f.received, 1u);
    }
    void outgoingBackpressureRefusesUnlimitedQueueing() {
        Fixture f; QVERIFY(f.listen() && f.connect()); QTRY_VERIFY(f.serverOpened == 1 && f.client->ready());
        unsigned sent = 0; while (sent < 20000 && f.client->send(frame())) ++sent;
        QVERIFY(sent < 20000); QCOMPARE(f.clientRevoked, 1u); QVERIFY(!f.client->ready());
    }
    void invalidOutgoingFramesCloseTheConnection() {
        Fixture f; QVERIFY(f.listen() && f.connect()); QTRY_VERIFY(f.serverOpened == 1 && f.client->ready()); auto invalid = frame(); invalid.connection.clear();
        QVERIFY(!f.client->send(invalid)); QCOMPARE(f.clientRevoked, 1u); QCOMPARE(f.clientLost, 1u);
    }
    void aMessageCallbackCanDestroyItsSocket() {
        Fixture f; QVERIFY(f.listen() && f.connect()); QTRY_VERIFY(f.serverOpened == 1 && f.client->ready());
        f.onMessage = [&](const auto&) { f.servers.clear(); }; QVERIFY(f.client->send(frame())); QTRY_COMPARE(f.received, 1u); QTRY_COMPARE(f.clientLost, 1u); QCOMPARE(f.serverRevoked, 1u);
    }
    void receivedCallbacksRunAfterTheNativeSignal() {
        Fixture f; QVERIFY(f.listen() && f.connect()); QTRY_VERIFY(f.serverOpened == 1 && f.client->ready());
        auto* native = f.servers.front()->findChild<QLocalSocket*>(); QVERIFY(native);
        unsigned observed = 0; bool deferred = false;
        connect(native, &QLocalSocket::readyRead, this, [&] { ++observed; });
        f.onMessage = [&](const auto&) { deferred = observed != 0; };
        QVERIFY(f.client->send(frame())); QTRY_COMPARE(f.received, 1u); QVERIFY(deferred);
    }
    void anOpenedCallbackCanDestroyItsSocket() {
        Fixture f; QVERIFY(f.listen()); f.client = std::make_unique<S>(); auto* raw = f.client.get();
        raw->connectTo(*f.endpoint, {[&] { ++f.clientRevoked; }, [&] { f.client.reset(); }, {}, {}}); QTRY_VERIFY(!f.client); QCOMPARE(f.clientRevoked, 1u);
    }
    void aLostCallbackCanDestroyItsSocket() {
        Fixture f; QVERIFY(f.listen()); f.client = std::make_unique<S>();
        QVERIFY(f.client->connectTo(*f.endpoint, {[&] { ++f.clientRevoked; }, {}, {}, [&] { f.client.reset(); }}));
        QTRY_VERIFY(f.client->ready() && f.serverOpened == 1); f.listener->stop(); QTRY_VERIFY(!f.client); QCOMPARE(f.clientRevoked, 1u);
    }
    void aReceiverExceptionClosesItsNativeState() {
        Fixture f; QVERIFY(f.listen() && f.connect()); QTRY_VERIFY(f.serverOpened == 1 && f.client->ready());
        f.onMessage = [](const auto&) { throw 1; }; QVERIFY(f.client->send(frame())); QTRY_COMPARE(f.serverLost, 1u); QCOMPARE(f.serverRevoked, 1u);
    }
    void stopBeforeQueuedAcceptanceDoesNotDeliverIt() {
        Fixture f; QVERIFY(f.listen()); QLocalSocket raw; QVERIFY(rawConnect(raw, *f.endpoint));
        f.listener->stop(); QCoreApplication::processEvents(); QCOMPARE(f.serverOpened, 0u);
    }
    void acceptedConnectionsStayBounded() {
        Fixture f; QVERIFY(f.listen()); std::vector<std::unique_ptr<S>> clients; unsigned lost = 0;
        for (qsizetype i = 0; i < L::MaximumConnections; ++i) {
            auto client = std::make_unique<S>(); QVERIFY(client->connectTo(*f.endpoint, {[] {}, {}, {}, {}})); clients.push_back(std::move(client));
            QTRY_COMPARE(f.serverOpened, unsigned(i + 1));
        }
        S extra; extra.connectTo(*f.endpoint, {[] {}, {}, {}, [&] { ++lost; }}); QTRY_COMPARE(lost, 1u);
        QCOMPARE(f.servers.size(), size_t(L::MaximumConnections)); QVERIFY(f.listener->listening());
    }
#ifndef Q_OS_WIN
    void aQueuedOldAcceptanceCannotEnterARestartedListener() {
        Fixture f; QVERIFY(f.listen()); auto* native = f.listener->findChild<QLocalServer*>(); QVERIFY(native);
        QLocalSocket raw; QVERIFY(rawConnect(raw, *f.endpoint)); QVERIFY(raw.waitForConnected(3000)); native->waitForNewConnection(100);
        f.listener->stop(); QVERIFY(f.listener->listen(*f.endpoint)); QCoreApplication::processEvents(); QCOMPARE(f.serverOpened, 0u);
        QVERIFY(f.connect()); QTRY_COMPARE(f.serverOpened, 1u);
    }
#endif
#ifdef Q_OS_LINUX
    void aDifferentUserIsRefusedEvenWithPermissiveTestPaths() {
        REQUIRE_CROSS_USER();
        Fixture f; QVERIFY(f.listen());
        const auto dir = QFile::encodeName(f.runtime.path()), path = QFile::encodeName(f.endpoint->address());
        QVERIFY(::chmod(dir.constData(), 0711) == 0); QVERIFY(::chmod(path.constData(), 0666) == 0);
        const auto restore = qScopeGuard([&] { ::chmod(dir.constData(), 0700); });
        QProcess process; const auto file = QFileInfo(f.endpoint->address()).fileName();
        const auto script = QStringLiteral("import socket,os; assert os.getuid()==65534; s=socket.socket(socket.AF_UNIX); s.settimeout(5); s.connect('/peer/%1');\ntry:\n s.sendall(bytes.fromhex('%2')); data=s.recv(96); assert not data\nexcept (ConnectionResetError,BrokenPipeError): pass\n").arg(file, QString::fromLatin1(P::encode(frame()).toHex()));
        process.start(QStringLiteral("docker"), {QStringLiteral("run"), QStringLiteral("--rm"), QStringLiteral("--read-only"), QStringLiteral("--network=none"), QStringLiteral("--user=65534:65534"), QStringLiteral("--mount"), QStringLiteral("type=bind,source=") + f.runtime.path() + QStringLiteral(",target=/peer,readonly"), CrossUserImage, QStringLiteral("python3"), QStringLiteral("-c"), script});
        QVERIFY(process.waitForStarted(3000)); QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 10000);
        QCOMPARE(process.exitStatus(), QProcess::NormalExit); QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData()); QCOMPARE(f.serverOpened, 0u); QCOMPARE(f.received, 0u);
    }
    void aDifferentUserCannotSupplyTheServer() {
        REQUIRE_CROSS_USER();
        Fixture f; QVERIFY(f.endpoint); const auto dir = QFile::encodeName(f.runtime.path()); QVERIFY(::chmod(dir.constData(), 0777) == 0);
        const auto restore = qScopeGuard([&] { ::chmod(dir.constData(), 0700); });
        QProcess process; const auto file = QFileInfo(f.endpoint->address()).fileName();
        const auto script = QStringLiteral("import socket,os; assert os.getuid()==65534; s=socket.socket(socket.AF_UNIX); s.settimeout(5); s.bind('/peer/%1'); os.chmod('/peer/%1',0o666); s.listen(1); print('listening',flush=True); c,_=s.accept();\ntry:\n c.sendall(bytes.fromhex('%2')); c.recv(1)\nexcept (ConnectionResetError,BrokenPipeError): pass\n").arg(file, QString::fromLatin1(P::encode(frame()).toHex()));
        process.start(QStringLiteral("docker"), {QStringLiteral("run"), QStringLiteral("--rm"), QStringLiteral("--read-only"), QStringLiteral("--network=none"), QStringLiteral("--user=65534:65534"), QStringLiteral("--mount"), QStringLiteral("type=bind,source=") + f.runtime.path() + QStringLiteral(",target=/peer"), CrossUserImage, QStringLiteral("python3"), QStringLiteral("-c"), script});
        QVERIFY(process.waitForStarted(3000)); QTRY_VERIFY_WITH_TIMEOUT(process.canReadLine(), 10000); QCOMPARE(process.readLine().trimmed(), QByteArray("listening"));
        f.connect(); QTRY_COMPARE(f.clientLost, 1u); QCOMPARE(f.clientRevoked, 1u); QCOMPARE(f.clientOpened, 0u);
        QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 10000); QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    }
#endif
    void actualPeerAdmissionClosesOnSocketLoss() {
        QTemporaryDir root, runtime; G::Status status; auto coordinator = G::acquireCoordinator(root.path(), &status); QVERIFY(coordinator);
        const auto recovered = coordinator->recover(control(), [](const auto& current, auto*) { return current.current(); }); QCOMPARE(recovered.status, G::Status::Ready);
        StoreProfileArbiter arbiter(nonce('i')); QVERIFY(arbiter.recovered(recovered.epoch).accepted);
        const auto endpoint = E::forProfile(root.path(), runtime.path()); QVERIFY(endpoint);
        std::unique_ptr<S> accepted; QByteArray connection;
        L listener([&](std::unique_ptr<S> socket) {
            accepted = std::move(socket); const auto welcome = arbiter.accept(); connection = welcome.send.first().connection;
            accepted->startAccepted({[] {}, [&, welcome] { accepted->send(welcome.send.first()); }, [&](const auto& message) {
                const auto effect = arbiter.receive(connection, message, G::now()); for (const auto& output : effect.send) accepted->send(output);
            }, {}});
        });
        QVERIFY(listener.listen(*endpoint)); QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hakone-profile-socket-child"), root.path(), runtime.path()}); QVERIFY(process.waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(process.canReadLine(), 5000); QCOMPARE(process.readLine().trimmed(), QByteArray("ready"));
        QCOMPARE(coordinator->recover(control(), [](const auto&, auto*) { return true; }).status, G::Status::Busy);
        listener.stop(); QTRY_COMPARE_WITH_TIMEOUT(process.state(), QProcess::NotRunning, 5000); QCOMPARE(process.exitStatus(), QProcess::NormalExit); QCOMPARE(process.exitCode(), 0);
        QCOMPARE(process.readLine().trimmed(), QByteArray("closed")); QVERIFY(process.readAllStandardError().isEmpty());
        QCOMPARE(coordinator->recover(control(), [](const auto&, auto*) { return true; }).status, G::Status::Ready);
    }
#ifdef Q_OS_WIN
    void pipeAccessIsLimitedToTheCurrentUser() {
        Fixture f; QVERIFY(f.listen() && f.connect()); QTRY_VERIFY(f.serverOpened == 1 && f.client->ready());
        auto* native = f.client->findChild<QLocalSocket*>(); QVERIFY(native);
        PSECURITY_DESCRIPTOR descriptor = nullptr; PACL acl = nullptr;
        QCOMPARE(GetSecurityInfo(reinterpret_cast<HANDLE>(native->socketDescriptor()), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr, &descriptor), DWORD(ERROR_SUCCESS));
        const auto release = qScopeGuard([&] { LocalFree(descriptor); }); QVERIFY(acl && IsValidAcl(acl)); QCOMPARE(acl->AceCount, WORD(1));
        void* entry = nullptr; QVERIFY(GetAce(acl, 0, &entry)); const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(entry); QCOMPARE(ace->Header.AceType, BYTE(ACCESS_ALLOWED_ACE_TYPE));
        HANDLE token = nullptr; QVERIFY(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)); const auto close = qScopeGuard([&] { CloseHandle(token); });
        DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size); QVERIFY(size > 0 && size < 65536);
        std::vector<unsigned char> bytes(size); QVERIFY(GetTokenInformation(token, TokenUser, bytes.data(), size, &size));
        QVERIFY(EqualSid(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, const_cast<DWORD*>(&ace->SidStart)));
        SECURITY_DESCRIPTOR_CONTROL flags = 0; DWORD revision = 0; QVERIFY(GetSecurityDescriptorControl(descriptor, &flags, &revision)); QVERIFY(flags & SE_DACL_PROTECTED);
    }
#endif
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); const auto args = app.arguments();
    if (args.size() == 4 && args.at(1) == QLatin1String("--hakone-profile-socket-child")) return child(app, args);
    TestAddonStoreProfileSocket test; return QTest::qExec(&test, argc, argv);
}
#include "test_addon_store_profile_socket.moc"
