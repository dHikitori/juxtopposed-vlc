// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_candidate_check.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_package_store.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
using namespace vlc::addons;
namespace {
QString example(const char* name)
{
    return QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/examples/") + QLatin1String(name);
}
bool candidate(const QString& directory, PackageCandidate* result)
{
    Manifest manifest; QString error;
    if (!Manifest::loadDirectory(directory, &manifest, nullptr, &error)) return false;
    ReleaseIdentity identity; identity.source = manifest.source.toString(); identity.tag = QStringLiteral("v") + manifest.version;
    identity.developerSource = true;
    return PackageStore::inspectDirectory(directory, identity, result, &error);
}
QString worker() { return qEnvironmentVariable("VLC_ADDON_WORKER"); }
}
class TestAddonCandidateCheck : public QObject
{
    Q_OBJECT
private slots:
    void init() { if (worker().isEmpty()) QSKIP("VLC_ADDON_WORKER names no worker"); }
    void aDeclaredSelfTestRunsOffline() {
        PackageCandidate tvmaze; QVERIFY(candidate(example("tvmaze"), &tvmaze));
        QVERIFY(tvmaze.manifest.runtimeSelfTest);
        QString error;
        QVERIFY2(AddonCandidateCheck::run(worker(), tvmaze, &error), qPrintable(error));
    }
    void aModuleThatDoesNotStartIsRefused() {
        PackageCandidate broken; QVERIFY(candidate(example("discovery-fixture"), &broken));
        broken.contents.insert(broken.manifest.runtimeEntry, QByteArrayLiteral("not a Wasm module"));
        QString error;
        QVERIFY(!AddonCandidateCheck::run(worker(), broken, &error));
        QVERIFY(error.contains(QLatin1String("did not start")));
    }
    void anUnansweredSelfTestIsRefused() {
        // The discovery fixture does not handle the self-test message.
        PackageCandidate fixture; QVERIFY(candidate(example("discovery-fixture"), &fixture));
        QString error;
        QVERIFY2(AddonCandidateCheck::run(worker(), fixture, &error), qPrintable(error));
        fixture.manifest.runtimeSelfTest = true;
        QElapsedTimer clock; clock.start();
        QVERIFY(!AddonCandidateCheck::run(worker(), fixture, &error));
        QVERIFY(error.contains(QLatin1String("did not answer")));
        QVERIFY(clock.elapsed() < 5000);
    }
    void aDataOnlyPackageNeedsNoWorker() {
        PackageCandidate theme; QVERIFY(candidate(example("midnight"), &theme));
        QVERIFY(theme.manifest.runtimeKind.isEmpty());
        QString error;
        QVERIFY(AddonCandidateCheck::run(QStringLiteral("/no/such/worker"), theme, &error));
    }
    void cancellationStopsTheCheck() {
        PackageCandidate tvmaze; QVERIFY(candidate(example("tvmaze"), &tvmaze));
        QString error;
        QVERIFY(!AddonCandidateCheck::run(worker(), tvmaze, &error, [] { return true; }));
        QVERIFY(error.contains(QLatin1String("canceled")));
    }
};
QTEST_GUILESS_MAIN(TestAddonCandidateCheck)
#include "test_addon_candidate_check.moc"
