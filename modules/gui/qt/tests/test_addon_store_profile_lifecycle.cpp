// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_package_store.hpp"
#include "addons/addon_distribution_bootstrap.hpp"
#include "addons/addon_storage.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
using namespace vlc::addons;
using Generation = StoreProfileGeneration;
using Status = Generation::Status;
using Change = Generation::Change;
#ifdef HAKONE_PROFILE_SAVEFILE_WRAP
namespace { std::atomic<std::atomic_bool*> cancelAfterPrimary{nullptr}; std::atomic_uint primaryCommits{0}; std::atomic_bool firstEnabled{false}, obstructPrimary{false}; }
extern "C" bool __real__ZN9QSaveFile6commitEv(QSaveFile*);
extern "C" bool __wrap__ZN9QSaveFile6commitEv(QSaveFile* file) {
    if (obstructPrimary.load() && file->fileName().endsWith(QStringLiteral("/state.json"))) {
        obstructPrimary.store(false);
        QDir().mkdir(file->fileName());
    }
    const bool committed = __real__ZN9QSaveFile6commitEv(file);
    if (committed && file->fileName().endsWith(QStringLiteral("/state.json"))) {
        if (primaryCommits.fetch_add(1) == 0) {
            QFile state(file->fileName());
            firstEnabled.store(state.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(state.readAll()).object().value("enabled").toBool());
        }
        if (auto* current = cancelAfterPrimary.load()) current->store(false);
    }
    return committed;
}
#endif
namespace {
enum Operation { Install, Update, KeepData, DeleteData, Bootstrap };
const auto id = QStringLiteral("org.hakone.lifecycle-fixture");
Generation::Control control() { return {[] { return true; }, Generation::now, Generation::now() + 20 * 1000 * 1000}; }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool save(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
void operations() {
    QTest::addColumn<int>("operation"); QTest::newRow("install") << int(Install); QTest::newRow("update") << int(Update);
    QTest::newRow("keep-data") << int(KeepData); QTest::newRow("delete-data") << int(DeleteData); QTest::newRow("bootstrap") << int(Bootstrap);
}
struct Offer { PackageCandidate candidate; QString directory; };
struct Fixture {
    QTemporaryDir temporary;
    const QString root = temporary.filePath(QStringLiteral("profile"));
    PackageStore store{root};
    std::unique_ptr<Generation> owner;
    Generation::Epoch epoch;
    Offer target;
    DistributionInstallResult output;
    QString error;
    bool valid = false;
    QString path(const QString& relative) const { return QDir(root).filePath(relative); }
    QString statePath() const { return path(QStringLiteral("packages/") + id + QStringLiteral("/state.json")); }
    QJsonObject state() const { return QJsonDocument::fromJson(read(statePath())).object(); }
    Offer make(int release, bool optional = false) {
        Offer result; const QString version = QString::number(release) + QStringLiteral(".0.0");
        result.directory = temporary.filePath(QStringLiteral("source-") + version + (optional ? QStringLiteral("-optional") : QString{}));
        const QJsonObject permission{{"storage.private", QJsonObject{{"quotaBytes", 4096}}}};
        const QJsonObject manifest{{"formatVersion", 1}, {"id", id}, {"name", "Lifecycle fixture"}, {"publisher", "Tests"},
            {"version", version}, {"license", "GPL-2.0-or-later"}, {"source", "https://github.com/hakone-org/lifecycle-fixture.git"},
            {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"theme", "^1.0.0"}, {"settings", "^1.0.0"}, {"storage", "^1.0.0"}}}}},
            {"files", QJsonArray{"addon.json", "LICENSE", "theme.json", "schema.json"}}, {"storage", QJsonObject{{"version", 1}}},
            {"permissions", QJsonObject{{optional ? QStringLiteral("optional") : QStringLiteral("required"), permission}}},
            {"settings", QJsonObject{{"version", 1}, {"schema", "schema.json"}}},
            {"contributions", QJsonArray{QJsonObject{{"id", "theme"}, {"type", "theme"}, {"contract", "theme@1"}, {"resource", "theme.json"}}}}};
        const QJsonObject schema{{"type", "object"}, {"properties", QJsonObject{{"label", QJsonObject{{"type", "string"}}}}}, {"additionalProperties", false}};
        if (!save(QDir(result.directory).filePath(QStringLiteral("addon.json")), QJsonDocument(manifest).toJson())
            || !save(QDir(result.directory).filePath(QStringLiteral("schema.json")), QJsonDocument(schema).toJson())
            || !save(QDir(result.directory).filePath(QStringLiteral("LICENSE")), "Fixture license.\n")
            || !save(QDir(result.directory).filePath(QStringLiteral("theme.json")), "{\"contract\":\"theme@1\"}")) return result;
        ReleaseIdentity identity; identity.source = manifest.value("source").toString(); identity.tag = QLatin1Char('v') + version;
        identity.commitId = identity.tagObjectId = QString(40, QChar('a' + release));
        PackageStore::inspectDirectory(result.directory, identity, &result.candidate, &error); return result;
    }
    explicit Fixture(bool installed = false) {
        if (!QDir().mkpath(root)) return;
        if (installed) {
            auto initial = make(1);
            if (!store.install(initial.candidate, initial.candidate.manifest.requiredPermissions, &error)
                || !store.saveSettings(id, {{"label", "Kept"}}, 0, &error) || !store.setEnabled(id, true, &error)
                || !storage(QStringLiteral("storage.set"), {{"key", "name"}, {"value", "Stored"}}, nullptr)) return;
        }
        target = make(installed ? 2 : 1); if (target.candidate.digest.isEmpty()) return;
        Status status; owner = Generation::acquireCoordinator(root, &status, &error); if (!owner) return;
        const auto recovered = owner->recover(control(), [&](const auto& context, auto* message) {
            if (!context.current()) return false;
            InstalledPackage package; return !installed || store.installed(id, &package, message);
        });
        epoch = recovered.epoch; valid = recovered.status == Status::Ready;
    }
    bool storage(const QString& operation, const QJsonObject& request, QJsonObject* output) {
        PrivateStorageContext context; context.accountReference = QStringLiteral("public:lifecycle");
        if (!store.installed(id, &context.package, &error)
            || !store.networkConfiguration(context.package, &context.settings, &context.settingsRevision)) return false;
        return store.privateStorage(context, operation, request, output, [] { return true; }, [] { return true; }, &error);
    }
    Generation::Ticket ticket() const { return {epoch, QByteArray(16, 'l'), control()}; }
    template<class Work> Generation::Result write(Work work) {
        auto result = Generation::write(root, ticket(), work); if (result.status == Status::Ready) epoch = result.epoch; return result;
    }
    Change apply(const StoreProfileWriteContext& context, int operation, QString* message) {
        if (operation == KeepData || operation == DeleteData) return store.uninstall(context, id, operation == DeleteData, message);
        if (operation == Bootstrap) return store.ensureDistributionPackage(context, target.directory, target.candidate.manifest, &output, message);
        return store.install(context, target.candidate, target.candidate.manifest.requiredPermissions, message);
    }
    Generation::Result run(int operation) { return write([&](const auto& context, auto* message) { return apply(context, operation, message); }); }
};
}
class TestAddonStoreProfileLifecycle : public QObject {
    Q_OBJECT
private slots:
    void realLifecycleOperationsKeepTheStoreCoherent_data() { operations(); }
    void realLifecycleOperationsKeepTheStoreCoherent() {
        QFETCH(int, operation); Fixture fixture(operation != Install && operation != Bootstrap); QVERIFY2(fixture.valid, qPrintable(fixture.error));
        const auto token = fixture.store.authorityToken(); std::atomic_uint cancelled{0}; const auto watch = token.onInvalidated([&] { ++cancelled; }); QVERIFY(watch);
        const auto result = fixture.run(operation); QCOMPARE(result.status, Status::Ready); QVERIFY2(result.applied, qPrintable(result.error));
        QCOMPARE(result.epoch.revision, quint64(1)); QVERIFY(!token.current()); QCOMPARE(cancelled.load(), 1u);
        if (operation == KeepData || operation == DeleteData) {
            QVERIFY(!QFileInfo::exists(fixture.statePath()));
            QCOMPARE(QFileInfo::exists(fixture.path(QStringLiteral("retained-settings/") + id + QStringLiteral(".json"))), operation == KeepData);
            QCOMPARE(QFileInfo::exists(fixture.path(QStringLiteral("private-storage/") + id)), operation == KeepData);
        } else {
            InstalledPackage package; QVERIFY(fixture.store.installed(id, &package)); QVERIFY(!package.recoveryRequired);
            QCOMPARE(package.manifest.version, operation == Update ? QStringLiteral("2.0.0") : QStringLiteral("1.0.0"));
            QCOMPARE(package.enabled, operation != Install);
            if (operation == Update) {
                QJsonObject response; QVERIFY(fixture.storage(QStringLiteral("storage.get"), {{"key", "name"}}, &response));
                QCOMPARE(response.value("value").toString(), QStringLiteral("Stored"));
            }
        }
        Status status; QVERIFY(Generation::prepareJoin(fixture.root, fixture.epoch, control(), &status).fence);
    }
    void nativeCancellationEndsBeforeLifecycleWrites_data() { operations(); }
    void nativeCancellationEndsBeforeLifecycleWrites() {
        QFETCH(int, operation); Fixture fixture(operation != Install && operation != Bootstrap); QVERIFY(fixture.valid);
        const auto before = read(fixture.statePath()), backup = read(fixture.statePath() + QStringLiteral(".backup"));
        std::mutex mutex; std::condition_variable changed; bool entered = false, released = false;
        const auto token = fixture.store.authorityToken(); const auto watch = token.onInvalidated([&] {
            std::unique_lock<std::mutex> lock(mutex); entered = true; changed.notify_all(); changed.wait(lock, [&] { return released; });
        }); QVERIFY(watch);
        Generation::Result result; std::thread writer([&] { result = fixture.run(operation); });
        const auto cleanup = qScopeGuard([&] { { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); } if (writer.joinable()) writer.join(); });
        { std::unique_lock<std::mutex> lock(mutex); QVERIFY(changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; })); }
        QVERIFY(!token.current()); QVERIFY(!fixture.store.authorityToken().current());
        QCOMPARE(read(fixture.statePath()), before); QCOMPARE(read(fixture.statePath() + QStringLiteral(".backup")), backup);
        QVERIFY(!QFileInfo::exists(fixture.path(QStringLiteral("retained-settings/") + id + QStringLiteral(".json"))));
        { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
        writer.join(); QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied);
    }
    void cancellationDuringDrainCannotStartLifecycleWrites_data() { operations(); }
    void cancellationDuringDrainCannotStartLifecycleWrites() {
        QFETCH(int, operation); Fixture fixture(operation != Install && operation != Bootstrap); QVERIFY(fixture.valid);
        const auto before = read(fixture.statePath()), backup = read(fixture.statePath() + QStringLiteral(".backup"));
        std::atomic_bool current{true}; auto ticket = fixture.ticket(); ticket.control.current = [&] { return current.load(); };
        const auto token = fixture.store.authorityToken(); const auto watch = token.onInvalidated([&] { current.store(false); }); QVERIFY(watch);
        const auto result = Generation::write(fixture.root, ticket, [&](const auto& context, auto* message) { return fixture.apply(context, operation, message); });
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QCOMPARE(read(fixture.statePath()), before); QCOMPARE(read(fixture.statePath() + QStringLiteral(".backup")), backup);
    }
    void keptConfigurationAndPrivateDataSurviveReinstall() {
        Fixture fixture(true); QVERIFY(fixture.valid); QCOMPARE(fixture.run(KeepData).status, Status::Ready);
        QCOMPARE(fixture.run(Install).status, Status::Ready);
        const auto enabled = fixture.write([&](const auto& context, auto* message) { return fixture.store.setEnabled(context, id, true, message); });
        QCOMPARE(enabled.status, Status::Ready); QVERIFY(enabled.applied);
        QJsonObject settings, value; QVERIFY(fixture.store.settings(id, &settings)); QCOMPARE(settings.value("label").toString(), QStringLiteral("Kept"));
        QVERIFY(fixture.storage(QStringLiteral("storage.get"), {{"key", "name"}}, &value)); QCOMPARE(value.value("value").toString(), QStringLiteral("Stored"));
    }
    void damagedPrimaryAndBackupAreKeptOnRemoval() {
        Fixture fixture(true); QVERIFY(fixture.valid); const auto backup = read(fixture.statePath() + QStringLiteral(".backup"));
        QVERIFY(save(fixture.statePath(), "invalid")); const auto result = fixture.run(KeepData); QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied);
        const QDir retained(fixture.path(QStringLiteral("retained-corrupt/") + id)); const auto entries = retained.entryList(QDir::Dirs | QDir::NoDotAndDotDot); QCOMPARE(entries.size(), 1);
        QCOMPARE(read(retained.filePath(entries.first() + QStringLiteral("/state.json"))), QByteArray("invalid"));
        QCOMPARE(read(retained.filePath(entries.first() + QStringLiteral("/state.json.backup"))), backup);
    }
    void savedDataCanBeDeletedAfterThePackageIsRemoved() {
        Fixture fixture(true); QVERIFY(fixture.valid); QVERIFY(fixture.run(KeepData).applied);
        const auto result = fixture.run(DeleteData); QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied);
        QVERIFY(!fixture.store.savedDataIds().contains(id));
    }
    void aFailedSettingsArchiveKeepsTheInstalledPackage() {
        Fixture fixture(true); QVERIFY(fixture.valid); const auto before = read(fixture.statePath());
        QVERIFY(QDir().mkpath(fixture.path(QStringLiteral("retained-settings/") + id + QStringLiteral(".json"))));
        const auto result = fixture.run(KeepData); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QCOMPARE(read(fixture.statePath()), before); QJsonObject value;
        QVERIFY(fixture.storage(QStringLiteral("storage.get"), {{"key", "name"}}, &value));
        QCOMPARE(value.value("value").toString(), QStringLiteral("Stored"));
    }
    void invalidAndRetaggedCandidatesCannotChangeTheActiveRelease() {
        Fixture fixture(true); QVERIFY(fixture.valid); const auto before = read(fixture.statePath()); auto bad = fixture.target.candidate; bad.digest = QByteArray(64, 'f');
        const auto invalid = fixture.write([&](const auto& context, auto* message) { return fixture.store.install(context, bad, bad.manifest.requiredPermissions, message); });
        QCOMPARE(invalid.status, Status::Ready); QVERIFY(!invalid.applied); QCOMPARE(read(fixture.statePath()), before);
        auto retagged = fixture.make(1); retagged.candidate.contents[QStringLiteral("theme.json")] = "{\"contract\":\"theme@1\",\"changed\":true}";
        retagged.candidate.digest = packageDigest(retagged.candidate.contents);
        const auto rejected = fixture.write([&](const auto& context, auto* message) { return fixture.store.install(context, retagged.candidate, retagged.candidate.manifest.requiredPermissions, message); });
        QCOMPARE(rejected.status, Status::Ready); QVERIFY(!rejected.applied); QCOMPARE(read(fixture.statePath()), before);
    }
    void theSameReleaseCanRevokeOptionalStoragePermission_data() { QTest::addColumn<bool>("guarded"); QTest::newRow("guarded") << true; QTest::newRow("ordinary") << false; }
    void theSameReleaseCanRevokeOptionalStoragePermission() {
        QFETCH(bool, guarded); Fixture fixture; QVERIFY(fixture.valid); const auto offer = fixture.make(1, true);
        const QJsonObject grants{{"storage.private", QJsonObject{{"quotaBytes", 4096}}}};
        QVERIFY(fixture.write([&](const auto& context, auto* message) { return fixture.store.install(context, offer.candidate, grants, message); }).applied);
        bool applied = false;
        if (guarded) applied = fixture.write([&](const auto& context, auto* message) { return fixture.store.install(context, offer.candidate, {}, message); }).applied;
        else applied = fixture.store.install(offer.candidate, {}, &fixture.error);
        QVERIFY2(applied, qPrintable(fixture.error)); InstalledPackage package; QVERIFY(fixture.store.installed(id, &package)); QVERIFY(package.grants.isEmpty());
        QCOMPARE(fixture.state().value("generation").toInt(), 2);
    }
    void bootstrapKeepsDisabledAndSuppressedChoices() {
        Fixture fixture; QVERIFY(fixture.valid); auto installed = fixture.run(Bootstrap); QVERIFY(installed.applied);
        QVERIFY(fixture.write([&](const auto& context, auto* message) { return fixture.store.setEnabled(context, id, false, message); }).applied);
        const auto before = read(fixture.statePath()); QVERIFY(fixture.run(Bootstrap).applied); QCOMPARE(fixture.output.state, DistributionInstallResult::State::Existing); QCOMPARE(read(fixture.statePath()), before);
        QVERIFY(fixture.write([&](const auto& context, auto* message) { return fixture.store.setDistributionSuppressed(context, id, true, message); }).applied);
        QVERIFY(fixture.run(Bootstrap).applied); QCOMPARE(fixture.output.state, DistributionInstallResult::State::Suppressed); QCOMPARE(read(fixture.statePath()), before);
    }
    void bootstrapRefusesAnIncompletePackageEvenWhenSuppressed() {
        Fixture fixture; QVERIFY(fixture.valid); QVERIFY(fixture.store.setDistributionSuppressed(id, true));
        QVERIFY(QDir().mkpath(fixture.path(QStringLiteral("packages/") + id + QStringLiteral("/releases"))));
        const auto result = fixture.run(Bootstrap); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
    }
    void aLinkedReleaseParentCannotReceiveAnUpdate() {
        Fixture fixture(true); QVERIFY(fixture.valid); const auto before = read(fixture.statePath());
        const auto releases = fixture.path(QStringLiteral("packages/") + id + QStringLiteral("/releases"));
        const auto outside = fixture.temporary.filePath(QStringLiteral("outside-releases"));
        QVERIFY(QDir().rename(releases, outside));
#ifdef Q_OS_WIN
        QProcess command; command.start(QStringLiteral("cmd.exe"), {QStringLiteral("/D"), QStringLiteral("/C"),
            QStringLiteral("mklink"), QStringLiteral("/J"), QDir::toNativeSeparators(releases), QDir::toNativeSeparators(outside)});
        QVERIFY(command.waitForStarted(3000)); QVERIFY(command.waitForFinished(3000)); QCOMPARE(command.exitCode(), 0);
        QVERIFY(QFileInfo(releases).isJunction());
        const auto cleanup = qScopeGuard([&] { QDir().rmdir(releases); });
#else
        QVERIFY(QFile::link(outside, releases));
#endif
        const auto original = QDir(outside).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        const auto result = fixture.run(Update); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QCOMPARE(read(fixture.statePath()), before); QCOMPARE(QDir(outside).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), original);
    }
#ifdef HAKONE_PROFILE_SAVEFILE_WRAP
    void aFailedFirstStateCommitLeavesStagedBytesInRecovery() {
        Fixture fixture; QVERIFY(fixture.valid); const auto cleanup = qScopeGuard([] { obstructPrimary.store(false); });
        obstructPrimary.store(true); const auto result = fixture.run(Install);
        QVERIFY(!obstructPrimary.load()); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QVERIFY(QFileInfo(fixture.statePath()).isDir());
        QVERIFY(QFileInfo::exists(fixture.path(QStringLiteral("packages/") + id + QStringLiteral("/releases/") + QString::fromLatin1(fixture.target.candidate.digest))));
        Status status; QVERIFY(!Generation::prepareJoin(fixture.root, fixture.epoch, control(), &status).fence); QCOMPARE(status, Status::NeedsRecovery);
    }
    void bootstrapPublishesEnabledStateInOneCommit() {
        Fixture fixture; QVERIFY(fixture.valid); primaryCommits.store(0); firstEnabled.store(false);
        const auto result = fixture.run(Bootstrap); QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied);
        QCOMPARE(primaryCommits.load(), 1u); QVERIFY(firstEnabled.load());
    }
    void cancellationAfterBootstrapCommitLeavesRecoveryClosed() {
        Fixture fixture; QVERIFY(fixture.valid); std::atomic_bool current{true};
        const auto cleanup = qScopeGuard([] { cancelAfterPrimary.store(nullptr); }); cancelAfterPrimary.store(&current);
        auto ticket = fixture.ticket(); ticket.control.current = [&] { return current.load(); };
        const auto result = Generation::write(fixture.root, ticket, [&](const auto& context, auto* message) { return fixture.apply(context, Bootstrap, message); });
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied); QVERIFY(fixture.state().value("enabled").toBool());
        Status status; QVERIFY(!Generation::prepareJoin(fixture.root, fixture.epoch, control(), &status).fence); QCOMPARE(status, Status::NeedsRecovery);
    }
#endif
#ifdef Q_OS_UNIX
    void anUnsafePackageTreeCannotBeRemovedOrMadeReady() {
        Fixture fixture(true); QVERIFY(fixture.valid); const auto target = fixture.path(QStringLiteral("packages/") + id + QStringLiteral("/outside"));
        QVERIFY(QFile::link(fixture.target.directory, target)); const auto result = fixture.run(DeleteData);
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied); QVERIFY(QFileInfo::exists(fixture.statePath()));
    }
#endif
};
QTEST_GUILESS_MAIN(TestAddonStoreProfileLifecycle)
#include "test_addon_store_profile_lifecycle.moc"
