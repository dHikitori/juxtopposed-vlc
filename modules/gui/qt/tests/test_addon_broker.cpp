// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonArray>
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
    void publicAddressClassification_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("allowed");
        for (const auto& address : {"0.0.0.0", "0.0.0.1", "10.0.0.1", "100.64.0.1", "100.127.255.254",
                 "127.0.0.1", "169.254.169.254", "172.16.0.1", "172.31.255.254", "192.0.0.1",
                 "192.0.2.1", "192.168.1.1", "198.18.0.1", "198.51.100.1", "203.0.113.1",
                 "224.0.0.1", "239.255.255.255", "255.255.255.255", "::", "::1", "::ffff:127.0.0.1",
                 "::ffff:8.8.8.8", "fe80::1", "fc00::1", "fd00::1", "ff02::1", "2001:db8::1",
                 "2001::1", "2002:0808:0808::1"})
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
