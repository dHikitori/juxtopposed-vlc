// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_package_store.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
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
using Change = Generation::Change;
using Status = Generation::Status;
#ifdef HAKONE_PROFILE_SAVEFILE_WRAP
namespace { std::atomic<std::atomic_bool*> cancelAfterBackup{nullptr}; }
extern "C" bool __real__ZN9QSaveFile6commitEv(QSaveFile*);
extern "C" bool __wrap__ZN9QSaveFile6commitEv(QSaveFile* file)
{
    const bool committed = __real__ZN9QSaveFile6commitEv(file);
    if (committed && file->fileName().endsWith(QStringLiteral("state.json.backup")))
        if (auto* current = cancelAfterBackup.load()) current->store(false);
    return committed;
}
#endif
namespace {
enum Operation { Disable, Enable, Rollback, Settings, Recovery };
const auto id = QStringLiteral("org.hakone.profile-fixture");
Generation::Control control() { return {[] { return true; }, Generation::now, Generation::now() + 20 * 1000 * 1000}; }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool save(const QString& path, const QByteArray& bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool save(const QString& path, const QJsonObject& object) { return save(path, QJsonDocument(object).toJson(QJsonDocument::Compact)); }
void operations() {
    QTest::addColumn<int>("operation");
    QTest::newRow("disable") << int(Disable); QTest::newRow("enable") << int(Enable);
    QTest::newRow("rollback") << int(Rollback); QTest::newRow("settings") << int(Settings);
    QTest::newRow("recovery") << int(Recovery);
}
struct Fixture {
    QTemporaryDir directory;
    const QString root = directory.filePath(QStringLiteral("profile"));
    PackageStore store{root};
    std::unique_ptr<Generation> owner;
    Generation::Epoch epoch;
    QString stamp, error;
    quint64 revision = 0;
    bool valid = false;
    const bool distribution;
    QString statePath() const { return QDir(root).filePath(QStringLiteral("packages/") + id + QStringLiteral("/state.json")); }
    QJsonObject state() const { return QJsonDocument::fromJson(read(statePath())).object(); }
    bool install(int release) {
        const QString version = QString::number(release) + QStringLiteral(".0.0");
        const auto source = directory.filePath(QStringLiteral("source-") + version);
        const QJsonObject manifest{{"formatVersion", 1}, {"id", id}, {"name", "Profile fixture"},
            {"version", version}, {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"},
            {"source", "https://github.com/hakone-org/profile-fixture.git"},
            {"api", QJsonObject{{"addon", ">=1.0.0 <2.0.0"}, {"contracts", QJsonObject{{"theme", "^1.0.0"}, {"settings", "^1.0.0"}}}}},
            {"files", QJsonArray{"addon.json", "LICENSE", "theme.json", "settings/schema.json"}},
            {"settings", QJsonObject{{"version", 1}, {"schema", "settings/schema.json"}}},
            {"contributions", QJsonArray{QJsonObject{{"id", "theme"}, {"type", "theme"}, {"contract", "theme@1"}, {"resource", "theme.json"}}}}};
        const QJsonObject schema{{"type", "object"}, {"properties", QJsonObject{{"label", QJsonObject{{"type", "string"}}}}},
            {"required", QJsonArray{"label"}}, {"additionalProperties", false}};
        if (!save(QDir(source).filePath(QStringLiteral("addon.json")), manifest)
            || !save(QDir(source).filePath(QStringLiteral("settings/schema.json")), schema)
            || !save(QDir(source).filePath(QStringLiteral("LICENSE")), QByteArray("Fixture license.\n"))
            || !save(QDir(source).filePath(QStringLiteral("theme.json")), QJsonObject{{"contract", "theme@1"}})) return false;
        ReleaseIdentity identity; identity.source = manifest.value("source").toString();
        identity.tag = QLatin1Char('v') + version; identity.tagObjectId = identity.commitId = QString(40, QChar('a' + release));
        if (distribution) { identity.distributionSource = true; identity.tagObjectId.clear(); identity.commitId.clear(); }
        PackageCandidate candidate;
        return PackageStore::inspectDirectory(source, identity, &candidate, &error) && store.install(candidate, {}, &error, distribution);
    }
    explicit Fixture(bool enabled = true, bool bundled = false) : distribution(bundled) {
        if (!install(1) || !store.saveSettings(id, {{"label", "Before"}}, 0, &error)
            || !store.setEnabled(id, enabled, &error) || !install(2)) return;
        refreshStamp(); Status status; owner = Generation::acquireCoordinator(root, &status, &error);
        if (!owner) return;
        // This fixture has one package and no other saved policy files.
        const auto result = owner->recover(control(), [&](const auto& context, auto* message) {
            InstalledPackage package; return context.current() && store.installed(id, &package, message) && !package.recoveryRequired;
        });
        epoch = result.epoch; valid = result.status == Status::Ready;
    }
    void refreshStamp() {
        const auto value = state(), current = value.value("current").toObject();
        revision = quint64(current.value("settingsRevision").toDouble());
        stamp = current.value("digest").toString() + QLatin1Char(':') + QString::number(quint64(value.value("generation").toDouble()))
            + QLatin1Char(':') + QString::number(revision);
    }
    Generation::Ticket ticket() const { return {epoch, QByteArray(16, 'p'), control()}; }
    Change apply(PackageStore& target, const StoreProfileWriteContext& context, int operation, QString* message) const {
        switch (operation) {
            case Disable: return target.setEnabled(context, id, false, message);
            case Enable: return target.setEnabled(context, id, true, message);
            case Rollback: return target.rollback(context, id, message);
            case Settings: return target.saveSettings(context, id, {{"label", "After"}}, revision, stamp, message);
            default: return target.recover(context, id, message);
        }
    }
    Generation::Result write(int operation) {
        return Generation::write(root, ticket(), [&](const auto& context, auto* message) { return apply(store, context, operation, message); });
    }
};
}
class TestAddonStoreProfilePackages : public QObject {
    Q_OBJECT
private slots:
    void ordinaryPackageMethodsKeepTheirBehavior_data() { operations(); }
    void ordinaryPackageMethodsKeepTheirBehavior() {
        QFETCH(int, operation); Fixture fixture(operation != Enable); QVERIFY(fixture.valid);
        const auto before = fixture.state(); const auto token = fixture.store.authorityToken(); bool saved = false;
        switch (operation) {
            case Disable: saved = fixture.store.setEnabled(id, false); break;
            case Enable: saved = fixture.store.setEnabled(id, true); break;
            case Rollback: saved = fixture.store.rollback(id); break;
            case Settings: saved = fixture.store.saveSettings(id, {{"label", "After"}}, fixture.revision, fixture.stamp); break;
            default: saved = fixture.store.recover(id); break;
        }
        QVERIFY(saved); QVERIFY(!token.current());
        const auto state = fixture.state(), current = state.value("current").toObject();
        QCOMPARE(state.value("generation").toDouble(), before.value("generation").toDouble() + (operation == Settings ? 0 : 1));
        QCOMPARE(state.value("enabled").toBool(), operation != Disable && operation != Recovery);
        QCOMPARE(current.value("version").toString(), operation == Rollback ? QStringLiteral("1.0.0") : QStringLiteral("2.0.0"));
        QCOMPARE(current.value("settings").toObject().value("label").toString(), operation == Settings ? QStringLiteral("After") : QStringLiteral("Before"));
        QCOMPARE(QJsonDocument::fromJson(read(fixture.statePath() + QStringLiteral(".backup"))).object(), before);
    }
    void realPackageChangesUseOneGuardedCommit_data() { operations(); }
    void realPackageChangesUseOneGuardedCommit() {
        QFETCH(int, operation); Fixture fixture(operation != Enable); QVERIFY2(fixture.valid, qPrintable(fixture.error));
        const auto before = fixture.state(); const auto token = fixture.store.authorityToken(); std::atomic_uint cancelled{0};
        const auto watch = token.onInvalidated([&] { ++cancelled; }); QVERIFY(watch);
        const auto result = fixture.write(operation); QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied);
        QCOMPARE(result.epoch.revision, quint64(1)); QCOMPARE(result.epoch.generation, fixture.epoch.generation);
        QCOMPARE(cancelled.load(), 1u); QVERIFY(!token.current());
        const auto state = fixture.state(), current = state.value("current").toObject();
        QCOMPARE(state.value("generation").toDouble(), before.value("generation").toDouble() + (operation == Settings ? 0 : 1));
        QCOMPARE(current.value("version").toString(), operation == Rollback ? QStringLiteral("1.0.0") : QStringLiteral("2.0.0"));
        QCOMPARE(current.value("settings").toObject().value("label").toString(), operation == Settings ? QStringLiteral("After") : QStringLiteral("Before"));
        QCOMPARE(current.value("settingsRevision").toDouble(), before.value("current").toObject().value("settingsRevision").toDouble() + (operation == Settings ? 1 : 0));
        QCOMPARE(state.value("enabled").toBool(), operation != Disable && operation != Recovery);
        QCOMPARE(QJsonDocument::fromJson(read(fixture.statePath() + QStringLiteral(".backup"))).object(), before);
        Status status; QVERIFY(Generation::prepareJoin(fixture.root, result.epoch, control(), &status).fence);
        InstalledPackage package; QVERIFY(fixture.store.installed(id, &package)); QVERIFY(!package.recoveryRequired);
    }
    void aPackageWriteCannotUseAnotherProfile_data() { operations(); }
    void aPackageWriteCannotUseAnotherProfile() {
        QFETCH(int, operation); Fixture fixture, other; QVERIFY(fixture.valid && other.valid);
        const auto before = read(other.statePath()), backup = read(other.statePath() + QStringLiteral(".backup"));
        const auto token = other.store.authorityToken();
        const auto result = Generation::write(fixture.root, fixture.ticket(), [&](const auto& context, auto* message) {
            return fixture.apply(other.store, context, operation, message);
        });
        QCOMPARE(result.status, Status::Ready); QVERIFY(!result.applied); QVERIFY(token.current());
        QCOMPARE(read(other.statePath()), before); QCOMPARE(read(other.statePath() + QStringLiteral(".backup")), backup);
    }
    void anUnknownPackageDoesNotRequireProfileRecovery_data() { operations(); }
    void anUnknownPackageDoesNotRequireProfileRecovery() {
        QFETCH(int, operation); Fixture fixture; QVERIFY(fixture.valid); const auto before = read(fixture.statePath());
        const auto unknown = QStringLiteral("org.hakone.missing"); const auto token = fixture.store.authorityToken();
        const auto result = Generation::write(fixture.root, fixture.ticket(), [&](const auto& context, auto* message) {
            switch (operation) {
                case Disable: return fixture.store.setEnabled(context, unknown, false, message);
                case Enable: return fixture.store.setEnabled(context, unknown, true, message);
                case Rollback: return fixture.store.rollback(context, unknown, message);
                case Settings: return fixture.store.saveSettings(context, unknown, {{"label", "After"}}, fixture.revision, fixture.stamp, message);
                default: return fixture.store.recover(context, unknown, message);
            }
        });
        QCOMPARE(result.status, Status::Ready); QVERIFY(!result.applied); QVERIFY(!result.error.isEmpty());
        QCOMPARE(read(fixture.statePath()), before); QVERIFY(token.current());
    }
    void aSuppressedBundleNeedsFreshApprovalAfterRollback() {
        Fixture fixture(false, true); QVERIFY2(fixture.valid, qPrintable(fixture.error));
        const auto withdrawn = Generation::write(fixture.root, fixture.ticket(), [&](const auto& context, auto* message) {
            if (fixture.store.setDistributionSuppressed(context, id, true, message) != Change::Applied) return Change::NeedsRecovery;
            return fixture.store.rollback(context, id, message);
        });
        QCOMPARE(withdrawn.status, Status::Ready); QVERIFY(withdrawn.applied); fixture.epoch = withdrawn.epoch;
        const auto before = read(fixture.statePath()); const auto token = fixture.store.authorityToken();
        const auto result = fixture.write(Enable); QCOMPARE(result.status, Status::Ready); QVERIFY(!result.applied);
        QVERIFY(!result.error.isEmpty()); QCOMPARE(read(fixture.statePath()), before); QVERIFY(token.current());
    }
    void cancellationDrainsBeforePrimaryOrBackupPublication_data() { operations(); }
    void cancellationDrainsBeforePrimaryOrBackupPublication() {
        QFETCH(int, operation); Fixture fixture(operation != Enable); QVERIFY(fixture.valid);
        const auto before = read(fixture.statePath()), backup = read(fixture.statePath() + QStringLiteral(".backup"));
        const auto token = fixture.store.authorityToken();
        std::mutex mutex; std::condition_variable changed; bool entered = false, released = false;
        const auto watch = token.onInvalidated([&] {
            std::unique_lock<std::mutex> lock(mutex); entered = true; changed.notify_all(); changed.wait(lock, [&] { return released; });
        }); QVERIFY(watch);
        Generation::Result result; std::thread writer([&] { result = fixture.write(operation); });
        const auto cleanup = qScopeGuard([&] {
            { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
            if (writer.joinable()) writer.join();
        });
        { std::unique_lock<std::mutex> lock(mutex); QVERIFY(changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; })); }
        QVERIFY(!token.current()); QVERIFY(!fixture.store.authorityToken().current());
        QCOMPARE(read(fixture.statePath()), before); QCOMPARE(read(fixture.statePath() + QStringLiteral(".backup")), backup);
        QLockFile probe(QDir(fixture.root).filePath(QStringLiteral("store.lock"))); QVERIFY(!probe.tryLock(0));
        { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
        writer.join(); QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied);
    }
    void cancellationDuringDrainKeepsBothFiles_data() { operations(); }
    void cancellationDuringDrainKeepsBothFiles() {
        QFETCH(int, operation); Fixture fixture; QVERIFY(fixture.valid);
        const auto before = read(fixture.statePath()), backup = read(fixture.statePath() + QStringLiteral(".backup"));
        std::atomic_bool current{true}; auto ticket = fixture.ticket(); ticket.control.current = [&] { return current.load(); };
        const auto token = fixture.store.authorityToken(); const auto watch = token.onInvalidated([&] { current.store(false); }); QVERIFY(watch);
        const auto result = Generation::write(fixture.root, ticket, [&](const auto& context, auto* message) {
            return fixture.apply(fixture.store, context, operation, message);
        });
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QCOMPARE(read(fixture.statePath()), before); QCOMPARE(read(fixture.statePath() + QStringLiteral(".backup")), backup);
    }
    void aFailedBackupCannotPublishThePrimary_data() { operations(); }
    void aFailedBackupCannotPublishThePrimary() {
        QFETCH(int, operation); Fixture fixture; QVERIFY(fixture.valid); const auto before = read(fixture.statePath());
        const auto backup = fixture.statePath() + QStringLiteral(".backup"); QVERIFY(QFile::remove(backup)); QVERIFY(QDir().mkdir(backup));
        const auto result = fixture.write(operation); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QCOMPARE(read(fixture.statePath()), before); QVERIFY(QFileInfo(backup).isDir());
    }
#ifdef HAKONE_PROFILE_SAVEFILE_WRAP
    void cancellationAfterBackupCannotPublishThePrimary_data() { operations(); }
    void cancellationAfterBackupCannotPublishThePrimary() {
        QFETCH(int, operation); Fixture fixture; QVERIFY(fixture.valid);
        const auto before = read(fixture.statePath()); const auto backup = fixture.statePath() + QStringLiteral(".backup");
        QVERIFY(read(backup) != before); std::atomic_bool current{true};
        const auto cleanup = qScopeGuard([] { cancelAfterBackup.store(nullptr); });
        cancelAfterBackup.store(&current); auto ticket = fixture.ticket(); ticket.control.current = [&] { return current.load(); };
        const auto result = Generation::write(fixture.root, ticket, [&](const auto& context, auto* message) {
            return fixture.apply(fixture.store, context, operation, message);
        });
        QVERIFY(!current.load()); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QCOMPARE(read(fixture.statePath()), before); QCOMPARE(read(backup), before);
    }
#endif
    void settingsRejectAnOldStampAfterReleaseRollbackAndReturn() {
        Fixture fixture; QVERIFY(fixture.valid); const auto oldStamp = fixture.stamp;
        const auto changed = Generation::write(fixture.root, fixture.ticket(), [&](const auto& context, auto* message) {
            if (fixture.store.rollback(context, id, message) != Change::Applied) return Change::NeedsRecovery;
            return fixture.store.rollback(context, id, message);
        });
        QCOMPARE(changed.status, Status::Ready); QVERIFY(changed.applied); fixture.epoch = changed.epoch;
        const auto before = read(fixture.statePath()); fixture.refreshStamp(); QVERIFY(fixture.stamp != oldStamp);
        fixture.stamp = oldStamp; const auto token = fixture.store.authorityToken();
        const auto result = fixture.write(Settings); QCOMPARE(result.status, Status::Ready); QVERIFY(!result.applied);
        QVERIFY(!result.error.isEmpty()); QCOMPARE(read(fixture.statePath()), before); QVERIFY(token.current());
    }
    void invalidSettingsCannotChangeState_data() {
        QTest::addColumn<QJsonObject>("settings"); QTest::addColumn<bool>("emptyStamp");
        QTest::newRow("wrong-type") << QJsonObject{{"label", true}} << false;
        QTest::newRow("unknown-field") << QJsonObject{{"label", "After"}, {"extra", "Value"}} << false;
        QTest::newRow("bad-secret-reference") << QJsonObject{{"label", "After"}, {"_credentials", QJsonObject{{"apiKey", "not-a-reference"}}}} << false;
        QTest::newRow("missing-stamp") << QJsonObject{{"label", "After"}} << true;
    }
    void invalidSettingsCannotChangeState() {
        QFETCH(QJsonObject, settings); QFETCH(bool, emptyStamp); Fixture fixture; QVERIFY(fixture.valid);
        const auto before = read(fixture.statePath()); const auto token = fixture.store.authorityToken();
        const auto result = Generation::write(fixture.root, fixture.ticket(), [&](const auto& context, auto* message) {
            return fixture.store.saveSettings(context, id, settings, fixture.revision, emptyStamp ? QString{} : fixture.stamp, message);
        });
        QCOMPARE(result.status, Status::Ready); QVERIFY(!result.applied); QVERIFY(!result.error.isEmpty());
        QCOMPARE(read(fixture.statePath()), before); QVERIFY(token.current());
    }
    void corruptSavedSettingsRequireRecovery_data() {
        QTest::addColumn<QString>("field");
        for (const auto* field : {"digest", "settings", "identity", "settingsRevision"})
            QTest::newRow(field) << QString::fromLatin1(field);
    }
    void corruptSavedSettingsRequireRecovery() {
        QFETCH(QString, field); Fixture fixture; QVERIFY(fixture.valid); auto state = fixture.state();
        auto current = state.value("current").toObject();
        current.insert(field, field == QLatin1String("settingsRevision") ? QJsonValue(0.5) : QJsonValue(false));
        state.insert("current", current); QVERIFY(save(fixture.statePath(), state)); const auto before = read(fixture.statePath());
        const auto result = fixture.write(Settings); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QCOMPARE(read(fixture.statePath()), before);
    }
    void recoveryUsesTheRetainedReleaseAndKeepsItDisabled() {
        Fixture fixture; QVERIFY(fixture.valid); InstalledPackage current; QVERIFY(fixture.store.installed(id, &current));
        const auto path = QDir(current.directory).filePath(QStringLiteral("theme.json"));
        QVERIFY(QFile::setPermissions(current.directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        QVERIFY(save(path, QByteArray("invalid")));
        const auto result = fixture.write(Recovery); QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied);
        InstalledPackage restored; QVERIFY(fixture.store.installed(id, &restored));
        QCOMPARE(restored.manifest.version, QStringLiteral("1.0.0")); QVERIFY(!restored.enabled); QVERIFY(!restored.recoveryRequired);
    }
    void aDamagedPrimaryNeedsExplicitPackageRecovery() {
        Fixture fixture; QVERIFY(fixture.valid); QVERIFY(save(fixture.statePath(), QByteArray("invalid")));
        const auto refused = fixture.write(Enable); QCOMPARE(refused.status, Status::NeedsRecovery); QVERIFY(!refused.applied);
        const auto result = fixture.owner->recover(control(), [&](const auto& context, auto* message) {
            return fixture.store.recover(context, id, message) == Change::Applied;
        });
        QCOMPARE(result.status, Status::Ready); QVERIFY(result.epoch.generation != fixture.epoch.generation);
        InstalledPackage restored; QVERIFY(fixture.store.installed(id, &restored)); QVERIFY(!restored.enabled && !restored.recoveryRequired);
    }
    void aFullGenerationRefusesAChangeWithoutOverflow() {
        Fixture fixture; QVERIFY(fixture.valid); auto state = fixture.state(); state.insert("generation", 9007199254740991.);
        QVERIFY(save(fixture.statePath(), state)); const auto before = read(fixture.statePath());
        const auto result = fixture.write(Disable); QCOMPARE(result.status, Status::Ready); QVERIFY(!result.applied);
        QCOMPARE(read(fixture.statePath()), before);
    }
#ifdef Q_OS_UNIX
    void linkedPackageParentsCannotRedirectStateWrites() {
        Fixture fixture, other; QVERIFY(fixture.valid && other.valid); const auto before = read(other.statePath());
        const auto folder = QDir(fixture.root).filePath(QStringLiteral("packages/") + id); QVERIFY(QDir(folder).removeRecursively());
        QVERIFY(QFile::link(QDir(other.root).filePath(QStringLiteral("packages/") + id), folder));
        const auto result = fixture.write(Disable); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QCOMPARE(read(other.statePath()), before);
    }
    void aFifoPrimaryCannotBlockAGuardedMutation() {
        Fixture fixture; QVERIFY(fixture.valid); QVERIFY(QFile::remove(fixture.statePath()));
        QCOMPARE(::mkfifo(QFile::encodeName(fixture.statePath()).constData(), 0600), 0);
        const auto result = fixture.write(Disable); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
    }
#endif
};
QTEST_GUILESS_MAIN(TestAddonStoreProfilePackages)
#include "test_addon_store_profile_packages.moc"
