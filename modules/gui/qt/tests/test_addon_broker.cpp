// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QBuffer>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <thread>
#include <cstring>
#include "addons/addon_network.hpp"
#include "addons/addon_worker.hpp"

using namespace vlc::addons;

class TestAddonBroker : public QObject
{
    Q_OBJECT
    static Manifest manifest()
    {
        Manifest result;
        result.id = QStringLiteral("org.example.broker-test");
        result.requiredPermissions = {{"network.http", QJsonObject{
            {"origins", QJsonArray{"https://api.example.org", "https://files.example.org"}},
            {"methods", QJsonArray{"GET"}}
        }}};
        Contribution contribution;
        contribution.id = QStringLiteral("search");
        contribution.type = QStringLiteral("metadata.provider");
        contribution.operations = {QStringLiteral("searchAssets")};
        contribution.requires = {QStringLiteral("network.http")};
        result.contributions.append(contribution);
        return result;
    }
    static InstalledPackage package(const QString& directory, const QString& mode)
    {
        InstalledPackage result;
        result.manifest = manifest();
        result.manifest.runtimeEntry = mode;
        result.grants = result.manifest.requiredPermissions;
        result.directory = directory;
        result.enabled = true;
        return result;
    }
    static QString fixture(const QString& directory)
    {
#ifdef Q_OS_UNIX
        const auto path = QDir(directory).filePath(QStringLiteral("fixture-worker"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        const auto sourcePath = QFINDTESTDATA("fixtures/addon-worker-fixture.py");
        QFile source(sourcePath);
        if (!source.open(QIODevice::ReadOnly)) return {};
        const auto bytes = source.readAll();
        if (file.write(bytes) != bytes.size()) return {};

        file.close();
        if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return {};
        return path;
#else
        Q_UNUSED(directory);
        return {};
#endif
    }
private slots:
    void idleBrokerHasNoNetworkTransport()
    {
        NetworkBroker broker;
        QVERIFY(broker.findChildren<QNetworkAccessManager*>().isEmpty());
        QVERIFY(broker.findChildren<QTimer*>().isEmpty());
        QTest::qWait(50);
        QVERIFY(broker.findChildren<QNetworkAccessManager*>().isEmpty());
        QVERIFY(broker.findChildren<QTimer*>().isEmpty());
    }
    void refusedAndCanceledRequestsHaveNoNetworkTransport()
    {
        NetworkBroker broker;
        auto declaration = manifest();
        declaration.requiredPermissions = {{"network.http", QJsonObject{
            {"origins", QJsonArray{"https://127.0.0.1", "https://93.184.216.34"}},
            {"methods", QJsonArray{"GET"}}}}};
        QStringList errors;
        const auto completed = [&](const NetworkBroker::Response& response) { errors.append(response.error); };
        broker.fetch(declaration, declaration.requiredPermissions, {{"url", "http://127.0.0.1"}}, {}, completed);
        broker.fetch(declaration, declaration.requiredPermissions, {{"url", "https://127.0.0.1"}}, {}, completed);
        broker.fetch(declaration, declaration.requiredPermissions, {{"url", "https://93.184.216.34"}}, {}, completed,
            512 * 1024, NetworkBroker::RequestKind::Document, [] { return false; });
        const auto canceled = broker.fetch(declaration, declaration.requiredPermissions,
            {{"url", "https://93.184.216.34"}}, {}, completed);
        broker.cancel(canceled);
        QTRY_COMPARE(errors.size(), 3);
        for (const auto& error : errors) QVERIFY(!error.isEmpty());
        QTest::qWait(20);
        QCOMPARE(errors.size(), 3);
        QVERIFY(broker.findChildren<QNetworkAccessManager*>().isEmpty());
    }
    void injectedTransportIsOwnedAndReusedWithoutAnExtraManager()
    {
        class Reply final : public QNetworkReply {
        public:
            Reply(QNetworkAccessManager::Operation operation, const QNetworkRequest& request, QObject* parent)
                : QNetworkReply(parent)
            {
                setOperation(operation); setRequest(request); setUrl(request.url());
                setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
                setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
                open(QIODevice::ReadOnly | QIODevice::Unbuffered);
                QTimer::singleShot(0, this, [this] {
                    setFinished(true); emit readyRead(); emit finished();
                });
            }
            void abort() override { setFinished(true); }
            bool isSequential() const override { return true; }
            qint64 bytesAvailable() const override { return 2 - m_offset + QNetworkReply::bytesAvailable(); }
        protected:
            qint64 readData(char* data, qint64 maximum) override
            {
                if (m_offset == 2) return -1;
                const auto count = std::min(maximum, qint64(2 - m_offset));
                std::memcpy(data, "{}" + m_offset, size_t(count)); m_offset += count;
                return count;
            }
        private:
            qint64 m_offset = 0;
        };
        class Transport final : public QNetworkAccessManager {
        public:
            int requests = 0;
            QNetworkRequest lastRequest;
            Operation lastOperation = UnknownOperation;
        protected:
            QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice*) override
            {
                ++requests; lastRequest = request; lastOperation = operation;
                return new Reply(operation, request, this);
            }
        };
        auto* transport = new Transport;
        QPointer<QNetworkAccessManager> retained(transport);
        std::unique_ptr<QNetworkAccessManager> owned(transport);
        auto broker = NetworkBroker::withTransport(owned);
        QVERIFY(broker); QVERIFY(!owned);
        QCOMPARE(transport->parent(), broker.get());
        QCOMPARE(transport->proxy().type(), QNetworkProxy::NoProxy);
        QCOMPARE(broker->findChildren<QNetworkAccessManager*>().size(), 1);
        auto declaration = manifest();
        declaration.requiredPermissions = {{"network.http", QJsonObject{
            {"origins", QJsonArray{"https://93.184.216.34"}}, {"methods", QJsonArray{"GET"}}}}};
        int completed = 0;
        for (int attempt = 1; attempt <= 2; ++attempt) {
            broker->fetch(declaration, declaration.requiredPermissions, {{"url", "https://93.184.216.34/data"}}, {},
                [&](const NetworkBroker::Response& response) {
                    QCOMPARE(response.status, 200); QCOMPARE(response.body, QByteArray("{}"));
                    QVERIFY(response.error.isEmpty()); ++completed;
                });
            QTRY_COMPARE(completed, attempt);
            QCOMPARE(transport->requests, attempt);
            QCOMPARE(transport->lastOperation, QNetworkAccessManager::GetOperation);
            QCOMPARE(transport->lastRequest.peerVerifyName(), QStringLiteral("93.184.216.34"));
            QCOMPARE(transport->lastRequest.rawHeader("Host"), QByteArray("93.184.216.34"));
            QCOMPARE(broker->findChildren<QNetworkAccessManager*>().size(), 1);
        }
        broker.reset(); QVERIFY(retained.isNull());
    }
    void manifestLimitsPacingAndRedirectProfiles()
    {
        QFile file(QFINDTESTDATA("../../../../contrib-addons/examples/thesubtitledb/addon.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto original = QJsonDocument::fromJson(file.readAll()).object();
        for (const auto& interval : {QJsonValue(0), QJsonValue(1000), QJsonValue(60000), QJsonValue(-1), QJsonValue(60001), QJsonValue(1.5), QJsonValue(true)}) {
            auto document = original; auto permissions = document.value("permissions").toObject(); auto required = permissions.value("required").toObject();
            auto network = required.value("network.http").toObject(); network.insert("minimumIntervalMs", interval);
            network.insert("assetRedirectProfiles", QJsonArray{"internet-archive-images@1"});
            required.insert("network.http", network); permissions.insert("required", required); document.insert("permissions", permissions);
            const bool valid = interval.isDouble() && interval.toDouble() >= 0 && interval.toDouble() <= 60000 && interval.toDouble() == int(interval.toDouble());
            Manifest parsed; QString error;
            QCOMPARE(Manifest::parse(QJsonDocument(document).toJson(), &parsed, &error), valid);
            if (valid) {
                network.insert("assetRedirectProfiles", QJsonArray{"arbitrary-redirects@1"}); required.insert("network.http", network);
                permissions.insert("required", required); document.insert("permissions", permissions);
                QVERIFY(!Manifest::parse(QJsonDocument(document).toJson(), &parsed, &error));
            }
        }
    }

    void originPacingCoversParallelPackagesAndReloads()
    {
        OriginRequestPacer pacer;
        QVERIFY(pacer.enqueue(101, "https://API.example.org:443", 1000, 100));
        QVERIFY(pacer.enqueue(201, "https://api.example.org", 0, 100));
        QVERIFY(pacer.admit(101, 100).valid); QCOMPARE(pacer.admit(201, 100).delayMs, qint64(1000));
        pacer.release(101, 200);
        QCOMPARE(pacer.admit(201, 200).delayMs, qint64(900));
        QCOMPARE(pacer.admit(201, 1100).delayMs, qint64(0));
        QVERIFY(pacer.enqueue(202, "https://api.example.org", 0, 1100));
        QCOMPARE(pacer.admit(202, 1100).delayMs, qint64(1000));
        pacer.release(201, 1100); pacer.release(202, 1100);
        QVERIFY(!pacer.admit(202, 1200).valid);
        QVERIFY(pacer.enqueue(301, "https://api.example.org", 0, 1200));
        QCOMPARE(pacer.admit(301, 1200).delayMs, qint64(900));
        QVERIFY(pacer.enqueue(401, "https://files.example.org", 0, 1200));
        QCOMPARE(pacer.admit(401, 1200).delayMs, qint64(0));
    }
    void originPacingHasNoConcurrentBurst()
    {
        OriginRequestPacer pacer;
        std::atomic_int registered{0}, admitted{0}; std::thread threads[16];
        for (quint64 ticket = 1; ticket <= 16; ++ticket) threads[ticket - 1] = std::thread([&, ticket] {
            if (!pacer.enqueue(ticket, "https://api.example.org", 1000, 0)) return;
            ++registered;
            const auto admission = pacer.admit(ticket, 0);
            if (admission.valid && admission.delayMs == 0) ++admitted;
            pacer.release(ticket, 1);
        });
        for (auto& thread : threads) thread.join();
        QCOMPARE(registered.load(), 16);
        QCOMPARE(admitted.load(), 1);
        QVERIFY(pacer.enqueue(17, "https://api.example.org", 1000, 500));
        QCOMPARE(pacer.admit(17, 500).delayMs, qint64(500));
    }
    void originPacingCancellationRetainsQueuedLimitsAndHasBounds()
    {
        OriginRequestPacer pacer;
        QVERIFY(pacer.enqueue(1, "https://api.example.org", 60000, 0));
        QVERIFY(pacer.enqueue(2, "https://api.example.org", 100, 0));
        QCOMPARE(pacer.admit(1, 0).delayMs, qint64(0)); pacer.release(1, 1);
        QCOMPARE(pacer.admit(2, 1).delayMs, qint64(59999)); pacer.release(2, 1);
        QVERIFY(!pacer.admit(2, 60000).valid);
        QVERIFY(!pacer.enqueue(0, "https://api.example.org", 100, 0));
        QVERIFY(!pacer.enqueue(3, "http://api.example.org", 100, 0));
        QVERIFY(!pacer.enqueue(3, "https://api.example.org", 60001, 0));
        OriginRequestPacer bounded;
        for (quint64 ticket = 1; ticket <= 1024; ++ticket)
            QVERIFY(bounded.enqueue(ticket, QStringLiteral("https://host-%1.example.org").arg(ticket), 1000, 0));
        QVERIFY(!bounded.enqueue(1025, "https://extra.example.org", 1000, 0));
        bounded.release(1, 0); QVERIFY(bounded.enqueue(1025, "https://extra.example.org", 1000, 0));
    }
    void bodyPrefixUsesOnlyTheByteBudget()
    {
        QBuffer large; large.setData(QByteArray(200000, 'x')); QVERIFY(large.open(QIODevice::ReadOnly));
        QByteArray output;
        QCOMPARE(NetworkBroker::readBody(&large, output, 4096, true), NetworkBroker::BodyRead::PrefixComplete);
        QCOMPARE(output.size(), 4096); QCOMPARE(large.pos(), qint64(4096));
        QBuffer shortResponse; shortResponse.setData("short"); QVERIFY(shortResponse.open(QIODevice::ReadOnly)); output.clear();
        QCOMPARE(NetworkBroker::readBody(&shortResponse, output, 4096, true), NetworkBroker::BodyRead::More);
        QCOMPARE(output, QByteArray("short")); QVERIFY(shortResponse.atEnd());
        large.seek(0); output.clear();
        QCOMPARE(NetworkBroker::readBody(&large, output, 4096, false), NetworkBroker::BodyRead::TooLarge);
        QCOMPARE(output.size(), 4097);
    }
    void bodyPrefixStopsAnEndlessSource()
    {
        class Endless : public QIODevice {
        public:
            qint64 readBytes = 0, maximumRead = 0;
            bool isSequential() const override { return true; }
            qint64 bytesAvailable() const override { return 1024 * 1024; }
            qint64 readData(char* data, qint64 size) override { maximumRead = std::max(maximumRead, size); readBytes += size; std::memset(data, 'x', size); return size; }
            qint64 writeData(const char*, qint64) override { return -1; }
        } source;
        QVERIFY(source.open(QIODevice::ReadOnly | QIODevice::Unbuffered)); QByteArray output;
        QCOMPARE(NetworkBroker::readBody(&source, output, 512 * 1024, true), NetworkBroker::BodyRead::PrefixComplete);
        QCOMPARE(output.size(), 512 * 1024); QCOMPARE(source.readBytes, qint64(512 * 1024));
        QVERIFY(source.maximumRead <= 64 * 1024);
        QVERIFY(NetworkBroker::textPrefixContentType("Text/HTML; charset=utf-8"));
        QVERIFY(NetworkBroker::textPrefixContentType("text/plain"));
        for (const auto* type : {"application/json", "image/png", "video/mp4", "application/octet-stream", ""})
            QVERIFY(!NetworkBroker::textPrefixContentType(QString::fromLatin1(type)));
    }
    void prefixOptionsRejectInvalidLimitsBinaryAssetsAndCancellation()
    {
        NetworkBroker broker; const auto declaration = manifest();
        for (const auto& limit : {QJsonValue(0), QJsonValue(-1), QJsonValue(524289), QJsonValue(1.5), QJsonValue(true), QJsonValue("64")}) {
            bool finished = false; QString error;
            broker.fetch(declaration, declaration.requiredPermissions, {{"url", "https://api.example.org"}, {"responseBytes", limit}, {"allowTruncated", true}}, {},
                [&](const NetworkBroker::Response& response) { finished = true; error = response.error; }, 512 * 1024, NetworkBroker::RequestKind::Document);
            QTRY_VERIFY(finished); QVERIFY(!error.isEmpty());
        }
        for (const auto kind : {NetworkBroker::RequestKind::BinaryAsset, NetworkBroker::RequestKind::ImageAsset}) {
            bool finished = false;
            broker.fetch(declaration, declaration.requiredPermissions, {{"url", "https://api.example.org"}, {"responseBytes", 64}, {"allowTruncated", true}}, {},
                [&](const NetworkBroker::Response& response) { finished = !response.error.isEmpty(); }, 512 * 1024, kind);
            QTRY_VERIFY(finished);
        }
        bool finished = false;
        const auto ticket = broker.fetch(declaration, declaration.requiredPermissions, {{"url", "https://api.example.org"}, {"responseBytes", 64}, {"allowTruncated", true}}, {},
            [&](const NetworkBroker::Response&) { finished = true; }, 512 * 1024, NetworkBroker::RequestKind::Document);
        broker.cancel(ticket); QTest::qWait(50); QVERIFY(!finished);
    }
    void archiveProfileAppliesOnlyToApprovedImageRedirects()
    {
        auto declaration = manifest(); auto scope = declaration.requiredPermissions.value("network.http").toObject();
        scope.insert("assetRedirectProfiles", QJsonArray{"internet-archive-images@1"}); declaration.requiredPermissions.insert("network.http", scope);
        auto grants = declaration.requiredPermissions;
        for (const auto* address : {"https://archive.org/image.jpg", "https://dn711103.ca.archive.org/image.jpg"}) {
            const QUrl url(QString::fromLatin1(address));
            const auto image = NetworkBroker::authorize(declaration, grants, url, NetworkBroker::RequestKind::ImageAsset, 1);
            QVERIFY(image.permitted); QVERIFY(image.stripCredentials);
            QVERIFY(!NetworkBroker::authorize(declaration, grants, url, NetworkBroker::RequestKind::ImageAsset, 0).permitted);
            QVERIFY(!NetworkBroker::authorize(declaration, grants, url, NetworkBroker::RequestKind::Document, 1).permitted);
            QVERIFY(!NetworkBroker::authorize(declaration, grants, url, NetworkBroker::RequestKind::BinaryAsset, 1).permitted);
        }
        for (const auto* address : {"http://archive.org/image.jpg", "https://archive.org:444/image.jpg", "https://archive.org.attacker.invalid/image.jpg",
                 "https://evilarchive.org/image.jpg", "https://archive.org@127.0.0.1/image.jpg", "https://archive.org/image.jpg#fragment"})
            QVERIFY(!NetworkBroker::permittedImageRedirect(declaration, grants, QUrl(QString::fromLatin1(address))));
        QVERIFY(!NetworkBroker::permittedImageRedirect(declaration, {}, QUrl("https://archive.org/image.jpg")));
        scope.remove("assetRedirectProfiles"); grants.insert("network.http", scope);
        QVERIFY(!NetworkBroker::permittedImageRedirect(declaration, grants, QUrl("https://archive.org/image.jpg")));
        QVERIFY(!NetworkBroker::publicAddress(QHostAddress("127.0.0.1")));
        QVERIFY(!NetworkBroker::publicAddress(QHostAddress("192.168.1.1")));
    }

    void workerUsesTrustedHostContextAndCancelsPendingServices()
    {
#ifndef Q_OS_UNIX
        QSKIP("The process fixture needs the Unix test interpreter.");
#else
        QTemporaryDir directory; const auto executable = fixture(directory.path()); QVERIFY(!executable.isEmpty());
        auto installed = package(directory.path(), QStringLiteral("host-service"));
        installed.grants.insert("playback.observe", QJsonObject{});
        NetworkBroker broker; AddonHostServices services; QObject owner;
        AddonHostServices::Completion late; int calls = 0, aborts = 0; QString packageId, contributionId;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), &owner,
            [&](const AddonHostServices::Context& context, const QJsonObject&, auto done) -> AddonHostServices::Cancel {
                ++calls; packageId = context.package; contributionId = context.contribution; late = done;
                return [&] { ++aborts; };
            }));
        AddonWorker worker(installed, &broker); worker.setExecutable(executable); worker.setHostServices(&services);
        QSignalSpy completed(&worker, &AddonWorker::completed);
        auto invocation = worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {});
        QTRY_COMPARE(calls, 1); QCOMPARE(packageId, installed.manifest.id);
        QCOMPARE(contributionId, QString(installed.manifest.id + QStringLiteral("/search")));
        late({{{"positionUs", 123}}, {}}); QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first()[0].toString(), invocation);
        QCOMPARE(completed.first()[2].toJsonObject().value("response").toObject().value("positionUs").toInt(), 123);
        completed.clear(); worker.stop();
        // The same worker object starts a new host-call ID namespace.
        invocation = worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {}); QTRY_COMPARE(calls, 2);
        worker.cancel(invocation); QCOMPARE(aborts, 1); late({{{"positionUs", 999}}, {}}); QTest::qWait(50); QVERIFY(completed.isEmpty());
#endif
    }
    void workerScopesReusedCallIdsToTheirExactInvocation()
    {
#ifndef Q_OS_UNIX
        QSKIP("The process fixture needs the Unix test interpreter.");
#else
        QTemporaryDir directory; auto installed=package(directory.path(),QStringLiteral("reuse-host-call"));
        installed.grants.insert("playback.observe",QJsonObject{}); NetworkBroker broker; AddonHostServices services; QObject owner;
        QMap<QString,AddonHostServices::Completion> replies; int aborts=0;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"),&owner,[&](const auto& context,const QJsonObject&,auto done)->AddonHostServices::Cancel {
            replies.insert(context.invocation,std::move(done)); return [&] { ++aborts; };
        }));
        AddonWorker worker(installed,&broker); worker.setExecutable(fixture(directory.path())); worker.setHostServices(&services);
        QSignalSpy completed(&worker,&AddonWorker::completed),unavailable(&worker,&AddonWorker::unavailable);
        const auto first=worker.invoke(QStringLiteral("search"),QStringLiteral("searchAssets"),{});
        const auto second=worker.invoke(QStringLiteral("search"),QStringLiteral("searchAssets"),{}); QTRY_COMPARE(replies.size(),2);
        worker.cancel(first); QCOMPARE(aborts,1);
        const auto third=worker.invoke(QStringLiteral("search"),QStringLiteral("searchAssets"),{}); QTRY_COMPARE(replies.size(),3);
        replies.value(first)({{{"marker",111}}, {}}); QTest::qWait(20); QVERIFY(completed.isEmpty());
        replies.value(second)({{{"marker",222}}, {}}); QTRY_COMPARE(completed.size(),1);
        QCOMPARE(completed.first()[0].toString(),second); QCOMPARE(completed.first()[2].toJsonObject().value("response").toObject().value("marker").toInt(),222);
        replies.value(third)({{{"marker",333}}, {}}); QTRY_COMPARE(completed.size(),2);
        QCOMPARE(completed.last()[0].toString(),third); QCOMPARE(completed.last()[2].toJsonObject().value("response").toObject().value("marker").toInt(),333);
        QTest::qWait(30); QCOMPARE(replies.size(),3); QVERIFY(unavailable.isEmpty());
#endif
    }
    void workerReleasesSeenIdsAfterEachCompletedSlice()
    {
#ifndef Q_OS_UNIX
        QSKIP("The process fixture needs the Unix test interpreter.");
#else
        QTemporaryDir directory; NetworkBroker broker; AddonWorker worker(package(directory.path(),QStringLiteral("quota-complete")),&broker);
        worker.setExecutable(fixture(directory.path())); QSignalSpy completed(&worker,&AddonWorker::completed),unavailable(&worker,&AddonWorker::unavailable);
        for(int slice=0;slice<5;++slice) {
            const auto id=worker.invoke(QStringLiteral("search"),QStringLiteral("searchAssets"),{{"count",1024}});
            QTRY_COMPARE_WITH_TIMEOUT(completed.size(),slice+1,10000); QCOMPARE(completed.last()[0].toString(),id);
            QCOMPARE(completed.last()[1].toString(),QStringLiteral("match")); QCOMPARE(completed.last()[2].toJsonObject().value("count").toInt(),1024);
            QVERIFY(unavailable.isEmpty());
        }
#endif
    }
    void workerKeepsOneAggregateLimitAcrossActiveInvocations()
    {
#ifndef Q_OS_UNIX
        QSKIP("The process fixture needs the Unix test interpreter.");
#else
        QTemporaryDir directory; const auto mode=QStringLiteral("quota-active"); NetworkBroker broker; AddonWorker worker(package(directory.path(),mode),&broker);
        worker.setExecutable(fixture(directory.path())); QSignalSpy unavailable(&worker,&AddonWorker::unavailable);
        const auto run=[&] { return worker.invoke(QStringLiteral("search"),QStringLiteral("searchAssets"),{{"count",2048}}); };
        const auto drained=[&](const QString& id) { return QFileInfo::exists(directory.filePath(QString(mode+QLatin1Char('.')+id+QStringLiteral(".drained")))); };
        const auto first=run(); QTRY_VERIFY_WITH_TIMEOUT(drained(first),10000);
        const auto second=run(); QTRY_VERIFY_WITH_TIMEOUT(drained(second),10000); QVERIFY(unavailable.isEmpty());
        worker.cancel(first); const auto third=run(); QTRY_VERIFY_WITH_TIMEOUT(drained(third),10000); QVERIFY(unavailable.isEmpty());
        worker.invoke(QStringLiteral("search"),QStringLiteral("searchAssets"),{{"count",1}}); QTRY_COMPARE(unavailable.size(),1);
        QCOMPARE(unavailable.first()[0].toString(),QStringLiteral("The add-on sent too many requests."));
#endif
    }
    void workerSendsCancelAndIgnoresOnlyKnownLateCalls()
    {
#ifndef Q_OS_UNIX
        QSKIP("The process fixture needs the Unix test interpreter.");
#else
        QTemporaryDir directory; auto installed = package(directory.path(), QStringLiteral("cancel-late"));
        installed.grants.insert("playback.observe", QJsonObject{});
        NetworkBroker broker; AddonHostServices services; QObject owner; int calls = 0, aborts = 0;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), &owner,
            [&](const AddonHostServices::Context&, const QJsonObject&, auto) -> AddonHostServices::Cancel {
                ++calls; return [&] { ++aborts; };
            }));
        AddonWorker worker(installed, &broker); worker.setExecutable(fixture(directory.path())); worker.setHostServices(&services);
        QSignalSpy completed(&worker, &AddonWorker::completed), unavailable(&worker, &AddonWorker::unavailable);
        const auto first = worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {});
        QTRY_COMPARE(calls, 1); worker.cancel(first); QCOMPARE(aborts, 1);
        QTRY_VERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("cancel-late.cancelled"))));
        const auto next = worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {{"fast", true}});
        QTRY_COMPARE(completed.size(), 1); QCOMPARE(completed.first()[0].toString(), next);
        QCOMPARE(completed.first()[1].toString(), QStringLiteral("match"));
        QCOMPARE(calls, 1); QVERIFY(unavailable.isEmpty());
#endif
    }
    void workerProjectsCredentialPresenceOnly()
    {
#ifndef Q_OS_UNIX
        QSKIP("The process fixture needs the Unix test interpreter.");
#else
        QTemporaryDir directory; NetworkBroker broker;
        AddonWorker worker(package(directory.path(), QStringLiteral("credential-status")), &broker);
        worker.setExecutable(fixture(directory.path()));
        worker.setConfiguration({}, {{"apiKey", QJsonObject{{"present", true}, {"sessionOnly", true}, {"value", "do-not-forward"}}}}, {});
        QSignalSpy completed(&worker, &AddonWorker::completed);
        worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {}); QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first()[2].toJsonObject().value("credentials").toObject(), QJsonObject({{"apiKey", QJsonObject{{"present", true}}}}));
#endif
    }
    void nativeHandlerCanCancelDuringDispatch()
    {
#ifndef Q_OS_UNIX
        QSKIP("The process fixture needs the Unix test interpreter.");
#else
        QTemporaryDir directory; auto installed = package(directory.path(), QStringLiteral("host-service"));
        installed.grants.insert("playback.observe", QJsonObject{});
        NetworkBroker broker; AddonHostServices services; QObject owner; int calls = 0, aborts = 0;
        AddonWorker worker(installed, &broker); worker.setExecutable(fixture(directory.path())); worker.setHostServices(&services);
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), &owner,
            [&](const AddonHostServices::Context& context, const QJsonObject&, auto) -> AddonHostServices::Cancel {
                ++calls; worker.cancel(context.invocation); return [&] { ++aborts; };
            }));
        QSignalSpy completed(&worker, &AddonWorker::completed), unavailable(&worker, &AddonWorker::unavailable);
        worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {});
        QTRY_COMPARE(calls, 1); QTRY_COMPARE(aborts, 1);
        worker.stop(); QCOMPARE(aborts, 1); QVERIFY(completed.isEmpty()); QVERIFY(unavailable.isEmpty());
#endif
    }
    void publicAddressClassification_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("allowed");
        for (const auto& address : {"0.0.0.0", "0.0.0.1", "10.0.0.1", "100.64.0.1", "100.127.255.254",
                 "127.0.0.1", "169.254.169.254", "172.16.0.1", "172.31.255.254", "192.0.0.1",
                 "192.0.2.1", "192.168.1.1", "198.18.0.1", "198.51.100.1", "203.0.113.1",
                 "224.0.0.1", "239.255.255.255", "255.255.255.255", "::", "::1", "::ffff:127.0.0.1",
                 "::ffff:8.8.8.8", "fe80::1", "fc00::1", "fd00::1", "ff02::1", "2001:db8::1",
                 "2001::1", "2002:0808:0808::1", "3fff::1", "3fff:fff:ffff::1",
                 "192.88.99.0", "192.88.99.2", "192.88.99.255", "2606:4700:4700::1111%eth0"})
            QTest::newRow(address) << QString::fromLatin1(address) << false;
        for (const auto& address : {"1.1.1.1", "8.8.8.8", "9.9.9.9", "172.32.0.1", "192.169.1.1",
                 "2606:4700:4700::1111", "2001:4860:4860::8888", "2a00:1450:4001::200e"})
            QTest::newRow(address) << QString::fromLatin1(address) << true;
    }
    void publicAddressClassification()
    {
        QFETCH(QString, address);
        QFETCH(bool, allowed);
        QCOMPARE(NetworkBroker::publicAddress(QHostAddress(address)), allowed);
    }
    void destinationPolicyIntersection()
    {
        const auto declaration = manifest();
        auto grants = declaration.requiredPermissions;
        QVERIFY(NetworkBroker::permittedUrl(declaration, grants, QUrl("https://api.example.org/search?q=a")));
        QVERIFY(NetworkBroker::permittedUrl(declaration, grants, QUrl("https://API.example.org:443/search")));
        for (const auto& url : {"http://api.example.org/search", "file:///etc/passwd", "https://other.example.org",
                 "https://api.example.org.attacker.invalid", "https://api.example.org:444",
                 "https://user:secret@api.example.org/search", "https://api.example.org/search#fragment"})
            QVERIFY2(!NetworkBroker::permittedUrl(declaration, grants, QUrl(QString::fromLatin1(url))), url);
        grants = {};
        QVERIFY(!NetworkBroker::permittedUrl(declaration, grants, QUrl("https://api.example.org")));
        grants = {{"network.http", QJsonObject{{"origins", QJsonArray{"https://other.example.org"}}}}};
        QVERIFY(!NetworkBroker::permittedUrl(declaration, grants, QUrl("https://other.example.org")));
        QVERIFY(!NetworkBroker::permittedUrl(declaration, grants, QUrl("https://api.example.org")));
    }
    void privateDestinationsNeverOpenConnections()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto declaration = manifest();
        const auto origin = QStringLiteral("https://127.0.0.1:%1").arg(server.serverPort());
        declaration.requiredPermissions = {{"network.http", QJsonObject{{"origins", QJsonArray{origin}}}}};
        NetworkBroker broker;
        bool finished = false;
        QString error;
        broker.fetch(declaration, declaration.requiredPermissions, {{"url", QString(origin + "/private")}}, {},
            [&](const NetworkBroker::Response& response) { finished = true; error = response.error; });
        QTRY_VERIFY(finished);
        QVERIFY(!error.isEmpty());
        QVERIFY(!server.hasPendingConnections());
    }
    void deniedMethodAndCancellation()
    {
        NetworkBroker broker;
        const auto declaration = manifest();
        bool finished = false;
        broker.fetch(declaration, declaration.requiredPermissions,
            {{"url", "https://api.example.org"}, {"method", "POST"}}, {},
            [&](const NetworkBroker::Response& response) { finished = !response.error.isEmpty(); });
        QTRY_VERIFY(finished);
        finished = false;
        const auto job = broker.fetch(declaration, declaration.requiredPermissions, {{"url", "file:///private"}}, {},
            [&](const NetworkBroker::Response&) { finished = true; });
        broker.cancel(job);
        QTest::qWait(50);
        QVERIFY(!finished);
    }
    void workerRejectsMalformedOrForgedMessages_data()
    {
        QTest::addColumn<QString>("mode");
        for (const auto& mode : {"invalid-protocol", "oversized-message", "forged-invocation", "duplicate-host-call"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void workerRejectsMalformedOrForgedMessages()
    {
#ifndef Q_OS_UNIX
        QSKIP("The malicious process fixture requires the Unix test interpreter");
#else
        QFETCH(QString, mode);
        QTemporaryDir directory;
        const auto executable = fixture(directory.path());
        QVERIFY(!executable.isEmpty());
        NetworkBroker broker;
        AddonWorker worker(package(directory.path(), mode), &broker);
        worker.setExecutable(executable);
        QSignalSpy unavailable(&worker, &AddonWorker::unavailable);
        QSignalSpy completed(&worker, &AddonWorker::completed);
        const auto invocation = worker.invoke("search", "searchAssets", {});
        QTRY_COMPARE_WITH_TIMEOUT(unavailable.count(), 1, 5000);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(completed.first()[0].toString(), invocation);
        QCOMPARE(completed.first()[1].toString(), QStringLiteral("error"));
#endif
    }
    void workerCannotInvokeUndeclaredHostCapability()
    {
#ifndef Q_OS_UNIX
        QSKIP("The malicious process fixture requires the Unix test interpreter");
#else
        QTemporaryDir directory;
        NetworkBroker broker;
        AddonWorker worker(package(directory.path(), "denied-capability"), &broker);
        worker.setExecutable(fixture(directory.path()));
        QSignalSpy completed(&worker, &AddonWorker::completed);
        worker.invoke("search", "searchAssets", {});
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
        QCOMPARE(completed.first()[1].toString(), QStringLiteral("match"));
        QVERIFY(completed.first()[2].toJsonObject().value("denied").toBool());
#endif
    }
    void terminalResultsAreExactlyOnce()
    {
#ifndef Q_OS_UNIX
        QSKIP("The malicious process fixture requires the Unix test interpreter");
#else
        QTemporaryDir directory;
        NetworkBroker broker;
        AddonWorker worker(package(directory.path(), "duplicate-result"), &broker);
        worker.setExecutable(fixture(directory.path()));
        QSignalSpy completed(&worker, &AddonWorker::completed);
        worker.invoke("search", "searchAssets", {});
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 5000);
        QTest::qWait(50);
        QCOMPARE(completed.count(), 1);
#endif
    }
    void cancelledInvocationHasNoApplyAuthority()
    {
#ifndef Q_OS_UNIX
        QSKIP("The malicious process fixture requires the Unix test interpreter");
#else
        QTemporaryDir directory;
        NetworkBroker broker;
        AddonWorker worker(package(directory.path(), "late-result"), &broker);
        worker.setExecutable(fixture(directory.path()));
        QSignalSpy completed(&worker, &AddonWorker::completed);
        const auto invocation = worker.invoke("search", "searchAssets", {});
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(directory.filePath("late-result.received")), 5000);
        worker.cancel(invocation);
        QTest::qWait(300);
        QVERIFY(completed.isEmpty());
#endif
    }
};

QTEST_GUILESS_MAIN(TestAddonBroker)
#include "test_addon_broker.moc"
