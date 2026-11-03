// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "addon_test_application_work.hpp"
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QThreadPool>
#include <QSemaphore>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QScopeGuard>
#include "addons/addon_storage.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_storage_data.hpp"
#ifdef Q_OS_UNIX
# include <fcntl.h>
# include <sys/stat.h>
# include <unistd.h>
#endif
using namespace vlc::addons;

#ifdef Q_OS_UNIX
namespace {
class StateRead final {
public:
    explicit StateRead(const QString& profile)
        : path(QDir(profile).filePath(QStringLiteral("packages/org.example.storage/state.json")))
    {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly)) return;
        bytes=file.readAll(); file.close();
        if(bytes.isEmpty() || bytes.size()>4096 || !QFile::remove(path)) return;
        replaced=true; ready=::mkfifo(QFile::encodeName(path).constData(),0600)==0;
    }
    ~StateRead() { release(); }
    bool valid() const { return ready; }
    bool entered() {
        if(writer<0) writer=::open(QFile::encodeName(path).constData(),O_WRONLY|O_NONBLOCK);
        return writer>=0;
    }
    bool release() {
        if(!replaced) return restored;
        if(writer<0 && ready) writer=::open(QFile::encodeName(path).constData(),O_RDWR|O_NONBLOCK);
        QFile::remove(path); QFile file(path);
        restored=file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size(); file.close();
        if(writer>=0) {
            const auto count=::write(writer,bytes.constData(),size_t(bytes.size()));
            restored=restored && count==bytes.size(); ::close(writer); writer=-1;
        }
        replaced=false; return restored;
    }
private:
    QString path;
    QByteArray bytes;
    int writer=-1;
    bool replaced=false,ready=false,restored=false;
};
}
#endif

class TestAddonStorage : public QObject
{
    Q_OBJECT
    static QString account(int index = 1) { return QString::number(index, 16).rightJustified(64, QLatin1Char('0')); }
    static QJsonObject snapshot(const QJsonObject& entries = {{"name", "old"}}) {
        auto value = emptyPrivateStorage(1); value.insert("namespaces", QJsonObject{{account(), entries}}); return value;
    }
    static Manifest manifest() {
        Manifest result; result.files = {QStringLiteral("data/1-2.json"), QStringLiteral("data/2-3.json")};
        result.requiredPermissions = {{"storage.private", QJsonObject{{"quotaBytes", 1000000}}}};
        result.document = {{"storage", QJsonObject{{"version", 3}, {"migrations", QJsonArray{
            QJsonObject{{"from", 1}, {"to", 2}, {"resource", "data/1-2.json"}},
            QJsonObject{{"from", 2}, {"to", 3}, {"resource", "data/2-3.json"}}}}}}};
        return result;
    }
    static QMap<QString, QByteArray> programs() {
        return {{QStringLiteral("data/1-2.json"), R"({"formatVersion":1,"operations":[{"op":"rename","from":"name","to":"title"}]})"},
            {QStringLiteral("data/2-3.json"), R"({"formatVersion":1,"operations":[{"op":"default","key":"language","value":"en"},{"op":"remove","key":"unused"}]})"}};
    }
    static bool package(const QString& directory, PackageCandidate* output, QString* error,
                        const QString& release = QStringLiteral("1.0.0"), int schema = 1, bool migration = false,
                        const QString& repository = QStringLiteral("https://example.org/storage.git"), bool executable = false, int quota = 1048576) {
        QJsonObject data{{"formatVersion", 1}, {"id", "org.example.storage"}, {"name", "Storage"},
            {"version", release}, {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", repository},
            {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"theme", "^1.0.0"}, {"storage", "^1.0.0"}}}}},
            {"files", QJsonArray{"addon.json", "LICENSE", "theme.json"}},
            {"permissions", QJsonObject{{"required", QJsonObject{{"storage.private", QJsonObject{{"quotaBytes", quota}}}}}}},
            {"storage", QJsonObject{{"version", schema}}},
            {"contributions", QJsonArray{QJsonObject{{"id", "theme"}, {"type", "theme"}, {"contract", "theme@1"}, {"resource", "theme.json"}}}}};
        QMap<QString, QByteArray> files{{QStringLiteral("LICENSE"), "test"}, {QStringLiteral("theme.json"), "{}"}};
        if (executable) {
            data.insert("api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"metadata", "^1.0.0"}, {"storage", "^1.0.0"}}}});
            data.insert("runtime", QJsonObject{{"kind", "wasm-message-v1"}, {"entry", "storage-host"}});
            data.insert("files", QJsonArray{"addon.json", "LICENSE", "storage-host"}); files.remove(QStringLiteral("theme.json"));
            files.insert(QStringLiteral("storage-host"), QByteArray("\0asm\1\0\0\0", 8));
            data.insert("contributions", QJsonArray{QJsonObject{{"id", "storage"}, {"type", "metadata.provider"}, {"contract", "metadata@1"},
                {"features", QJsonArray{"text.lyrics"}}, {"mediaKinds", QJsonArray{"audio"}}, {"operations", QJsonArray{"searchAssets"}}, {"requires", QJsonArray{"storage.private"}}}});
        }
        if (migration) {
            auto declaration = data.value("storage").toObject();
            declaration.insert("migrations", QJsonArray{QJsonObject{{"from", 1}, {"to", 2}, {"resource", "migrate.json"}}});
            data.insert("storage", declaration); auto inventory = data.value("files").toArray(); inventory.append("migrate.json"); data.insert("files", inventory);
            files.insert(QStringLiteral("migrate.json"), R"({"formatVersion":1,"operations":[{"op":"rename","from":"old","to":"new"}]})");
        }
        files.insert(QStringLiteral("addon.json"), QJsonDocument(data).toJson());
        if (!QDir().mkpath(directory)) return false;
        for (auto it = files.begin(); it != files.end(); ++it) {
            QFile file(QDir(directory).filePath(it.key()));
            if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size()) return false;
        }
        ReleaseIdentity identity; identity.source = data.value("source").toString(); identity.tag = QStringLiteral("v") + release; identity.developerSource = true;
        return PackageStore::inspectDirectory(directory, identity, output, error);
    }
    static PrivateStorageContext capture(const PackageStore& store, const QString& account = QStringLiteral("public:first")) {
        PrivateStorageContext result; QString error;
        if (!store.installed(QStringLiteral("org.example.storage"), &result.package, &error)
            || !store.networkConfiguration(result.package, &result.settings, &result.settingsRevision)) return {};
        result.accountReference = account; return result;
    }
    static bool operation(const PackageStore& store, const PrivateStorageContext& context, const QString& kind,
                          const QJsonObject& request, QJsonObject* output = nullptr, QString* error = nullptr) {
        return store.privateStorage(context, kind, request, output, [] { return true; }, [] { return true; }, error);
    }
private slots:
    void keysAndValuesHaveExactBounds() {
        QVERIFY(privateStorageKey(QStringLiteral("cache.item-1")));
        QVERIFY(!privateStorageKey(QStringLiteral("../outside")));
        QVERIFY(!privateStorageKey(QStringLiteral("key\n")));
        QVERIFY(!privateStorageKey(QStringLiteral("_credentials")));
        QVERIFY(!privateStorageKey(QString(129, QLatin1Char('a'))));
        QVERIFY(privateStorageValue(QString(PrivateStorageLimits::MaximumValueBytes - 2, QLatin1Char('x'))));
        QVERIFY(!privateStorageValue(QString(PrivateStorageLimits::MaximumValueBytes - 1, QLatin1Char('x'))));
        QJsonValue nested = true;
        for (int i = 0; i < 17; ++i) nested = QJsonArray{nested};
        QVERIFY(!privateStorageValue(nested));
        QJsonArray wide;
        for (int i = 0; i < 1025; ++i) wide.append(false);
        QVERIFY(!privateStorageValue(wide));
    }
    void quotaCountsAllAccountsAndKeys() {
        auto value = snapshot(); qint64 used = 0; QString error;
        QVERIFY(validatePrivateStorage(value, 1000000, &used, &error)); QVERIFY(used > 0);
        QVERIFY(validatePrivateStorage(value, used, nullptr, &error));
        QVERIFY(!validatePrivateStorage(value, used - 1, nullptr, &error));
        QVERIFY(validatePrivateStorage(emptyPrivateStorage(1), 0));
        auto accounts = value.value("namespaces").toObject();
        for (int i = 2; i <= 17; ++i) accounts.insert(account(i), QJsonObject{});
        value.insert("namespaces", accounts); QVERIFY(!validatePrivateStorage(value, 1000000));
        QJsonObject entries;
        for (int i = 0; i < 1024; ++i) entries.insert(QStringLiteral("key") + QString::number(i), false);
        value = snapshot(entries); QVERIFY(validatePrivateStorage(value, 1000000));
        entries.insert("extra", false); QVERIFY(!validatePrivateStorage(snapshot(entries), 1000000));
    }
    void immutableSnapshotsDetectCorruptionAndLinks() {
        QTemporaryDir folder; QVERIFY(folder.isValid()); QString hash, error; QJsonObject read;
        const auto original = snapshot();
        QVERIFY2(writePrivateStorage(folder.path(), original, &hash, &error), qPrintable(error));
        QVERIFY2(readPrivateStorage(folder.path(), hash, &read, &error), qPrintable(error)); QCOMPARE(read, original);
#ifdef Q_OS_UNIX
        QCOMPARE(QFileInfo(folder.filePath(hash + QStringLiteral(".json"))).permissions()
            & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther), QFile::Permissions{});
#endif
        QString same; QVERIFY(writePrivateStorage(folder.path(), original, &same)); QCOMPARE(same, hash);
        QVERIFY(!readPrivateStorage(folder.path(), QStringLiteral("../outside"), &read));
        QFile file(folder.filePath(hash + QStringLiteral(".json"))); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{}"); file.close(); QVERIFY(!readPrivateStorage(folder.path(), hash, &read));
        QVERIFY(!writePrivateStorage(folder.path(), original, &same));
#ifdef Q_OS_UNIX
        QTemporaryDir target; QVERIFY(target.isValid());
        const auto linked = folder.filePath(QStringLiteral("linked")); QVERIFY(QFile::link(target.path(), linked));
        QVERIFY(!writePrivateStorage(linked, original, &same));
#endif
    }
    void migrationsUseACompleteBoundedChain() {
        const auto original = snapshot({{"name", "old"}, {"unused", true}}); QJsonObject output; QString error;
        const auto next = manifest(); const auto contents = programs();
        QVERIFY2(migratePrivateStorage(next, contents, 1, original, 1000000, &output, &error), qPrintable(error));
        QCOMPARE(output.value("schemaVersion").toInt(), 3);
        const auto entries = output.value("namespaces").toObject().value(account()).toObject();
        QCOMPARE(entries, (QJsonObject{{"title", "old"}, {"language", "en"}}));
        QVERIFY(original.value("namespaces").toObject().value(account()).toObject().contains("name"));
        auto missing = contents; missing.remove(QStringLiteral("data/2-3.json"));
        QVERIFY(!migratePrivateStorage(next, missing, 1, original, 1000000, &output, &error));
        QVERIFY(!migratePrivateStorage(next, contents, 1, snapshot({{"name", "a"}, {"title", "b"}}), 1000000, &output));
        QVERIFY(!migratePrivateStorage(next, contents, 1, original, 1, &output));
        auto invalid = contents; invalid[QStringLiteral("data/1-2.json")] = R"({"formatVersion":1,"operations":[{"op":"script","code":"ignored"}]})";
        QVERIFY(!validatePrivateStorageDeclaration(next, invalid, true));
        auto undeclared = next; undeclared.files.removeAll(QStringLiteral("data/1-2.json"));
        QVERIFY(!validatePrivateStorageDeclaration(undeclared, {}, false));
    }
    void realStoreSeparatesAccountsAndRejectsStaleConfiguration() {
        QTemporaryDir source, profile; PackageCandidate candidate; QString error;
        QVERIFY2(package(source.path(), &candidate, &error), qPrintable(error)); PackageStore store(profile.path());
        QVERIFY2(store.install(candidate, candidate.manifest.requiredPermissions, &error), qPrintable(error));
        QVERIFY(store.setEnabled(candidate.manifest.id, true)); auto first = capture(store); QJsonObject result;
        const auto authority = captureStoreAuthority(profile.path());
        QVERIFY2(operation(store, first, QStringLiteral("storage.set"), {{"key", "item"}, {"value", "first"}}, &result, &error), qPrintable(error));
        QCOMPARE(result.value("revision").toInt(), 1);
        QVERIFY(operation(store, first, QStringLiteral("storage.get"), {{"key", "item"}}, &result)); QCOMPARE(result.value("value").toString(), QStringLiteral("first"));
        const auto second = capture(store, QStringLiteral("account:second"));
        QVERIFY(operation(store, second, QStringLiteral("storage.get"), {{"key", "item"}}, &result)); QVERIFY(!result.value("found").toBool());
        QVERIFY(operation(store, second, QStringLiteral("storage.set"), {{"key", "item"}, {"value", "second"}}));
        QVERIFY(authority.current());
        QVERIFY(operation(store, first, QStringLiteral("storage.get"), {{"key", "item"}}, &result)); QCOMPARE(result.value("value").toString(), QStringLiteral("first"));
        QVERIFY(!operation(store, first, QStringLiteral("storage.set"), {{"key", "item"}, {"value", "lost"}, {"expectedRevision", 0}}));
        QVERIFY(!operation(store, first, QStringLiteral("storage.get"), {{"key", "item"}, {"account", "second"}}));
        QVERIFY(store.saveSettings(candidate.manifest.id, {{"language", "fr"}}, first.settingsRevision));
        QVERIFY(!authority.current());
        QVERIFY(!operation(store, first, QStringLiteral("storage.set"), {{"key", "item"}, {"value", "stale"}}));
        first = capture(store);
        QVERIFY(operation(store, first, QStringLiteral("storage.get"), {{"key", "item"}}, &result)); QCOMPARE(result.value("value").toString(), QStringLiteral("first"));
        QVERIFY(store.setSafeMode(true)); QVERIFY(!operation(store, first, QStringLiteral("storage.get"), {{"key", "item"}}));
    }
    void valueBoundsFitTheHostEnvelope() {
        QTemporaryDir source, profile; PackageCandidate candidate; QString error; QVERIFY(package(source.path(), &candidate, &error));
        PackageStore store(profile.path()); QVERIFY(store.install(candidate, candidate.manifest.requiredPermissions)); QVERIFY(store.setEnabled(candidate.manifest.id, true));
        QJsonObject wide;
        for (int field = 0; field < 4; ++field) {
            QJsonArray array; const int count = field == 3 ? 998 : 999;
            for (int item = 0; item < count; ++item) array.append(false);
            wide.insert(QString::number(field), array);
        }
        QVERIFY(privateStorageValue(wide));
        auto overflow = wide; auto array = overflow.value("3").toArray(); array.append(false); overflow.insert("3", array);
        QVERIFY(!privateStorageValue(overflow));
        QJsonValue deep = true;
        for (int level = 0; level < PrivateStorageLimits::MaximumValueDepth; ++level) deep = QJsonArray{deep};
        QVERIFY(privateStorageValue(deep)); QVERIFY(!privateStorageValue(QJsonArray{deep}));
        AddonHostServices services; AddonStorage storage(store, [&](const auto&, auto* output, QString*) { *output = capture(store); return true; });
        QVERIFY(storage.mount(&services)); int calls = 0;
        const AddonHostServices::Context host{candidate.manifest.id, candidate.manifest.id + QStringLiteral("/theme"), QStringLiteral("request"), candidate.manifest.requiredPermissions, {}};
        for (const auto& value : {QJsonValue(wide), deep}) {
            services.dispatch(host, QStringLiteral("storage.set"), {{"key", "bounded"}, {"value", value}}, [&](auto result) { ++calls; QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); });
            QTRY_COMPARE(calls, 1);
            services.dispatch(host, QStringLiteral("storage.get"), {{"key", "bounded"}}, [&, value](auto result) { ++calls; QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QCOMPARE(result.response.value("value"), value); });
            QTRY_COMPARE(calls, 2); calls = 0;
        }
    }
    void grantedQuotaAndFailedCommitKeepTheOldValue() {
        QTemporaryDir source, profile; PackageCandidate candidate; QString error; QVERIFY(package(source.path(), &candidate, &error));
        PackageStore store(profile.path()); const QJsonObject grants{{"storage.private", QJsonObject{{"quotaBytes", 128}}}};
        QVERIFY2(store.install(candidate, grants, &error), qPrintable(error)); QVERIFY(store.setEnabled(candidate.manifest.id, true));
        const auto context = capture(store); QJsonObject result;
        QVERIFY(operation(store, context, QStringLiteral("storage.set"), {{"key", "value"}, {"value", "old"}}, &result));
        QCOMPARE(result.value("quotaBytes").toInt(), 128);
        QVERIFY(!operation(store, context, QStringLiteral("storage.set"), {{"key", "value"}, {"value", QString(128, QLatin1Char('x'))}}));
        const auto backup = profile.filePath(QStringLiteral("packages/org.example.storage/state.json.backup"));
        bool admitted = false;
        QVERIFY(!store.privateStorage(context, QStringLiteral("storage.set"), {{"key", "value"}, {"value", "new"}}, &result,
            [] { return true; }, [&] { admitted = QFile::remove(backup) && QDir().mkdir(backup); return admitted; }, &error));
        QVERIFY(admitted);
        QVERIFY(QDir(backup).removeRecursively());
        QVERIFY(operation(store, context, QStringLiteral("storage.get"), {{"key", "value"}}, &result));
        QCOMPARE(result.value("value").toString(), QStringLiteral("old")); QCOMPARE(result.value("revision").toInt(), 1);
        const QDir objects(profile.filePath(QStringLiteral("private-storage/org.example.storage/objects")));
        QVERIFY(objects.entryList(QDir::Files).size() <= 2);
        QTemporaryDir largeSource, largeProfile; PackageCandidate large;
        QVERIFY(package(largeSource.path(), &large, &error, QStringLiteral("1.0.0"), 1, false, QStringLiteral("https://example.org/storage.git"), false, 2 * 1048576));
        PackageStore capped(largeProfile.path()); QVERIFY(capped.install(large, large.manifest.requiredPermissions)); QVERIFY(capped.setEnabled(large.manifest.id, true));
        QVERIFY(operation(capped, capture(capped), QStringLiteral("storage.get"), {{"key", "missing"}}, &result));
        QCOMPARE(result.value("quotaBytes").toInt(), int(PrivateStorageLimits::MaximumBytes));
    }
    void corruptCurrentDataRecoversThePreviousRelease() {
        QTemporaryDir firstSource, nextSource, profile; PackageCandidate first, next; QString error;
        QVERIFY(package(firstSource.path(), &first, &error)); QVERIFY(package(nextSource.path(), &next, &error, QStringLiteral("1.1.0"), 2, true));
        PackageStore store(profile.path()); QVERIFY(store.install(first, first.manifest.requiredPermissions)); QVERIFY(store.setEnabled(first.manifest.id, true));
        QVERIFY(operation(store, capture(store), QStringLiteral("storage.set"), {{"key", "old"}, {"value", "before"}}));
        QVERIFY(store.install(next, next.manifest.requiredPermissions));
        QVERIFY(operation(store, capture(store), QStringLiteral("storage.set"), {{"key", "new"}, {"value", "after"}}));
        QFile state(profile.filePath(QStringLiteral("packages/org.example.storage/state.json"))); QVERIFY(state.open(QIODevice::ReadOnly));
        const auto hash = QJsonDocument::fromJson(state.readAll()).object().value("current").toObject().value("storage").toObject().value("digest").toString(); state.close();
        QFile data(profile.filePath(QStringLiteral("private-storage/org.example.storage/objects/") + hash + QStringLiteral(".json")));
        QVERIFY(data.open(QIODevice::WriteOnly)); QCOMPARE(data.write("{}"), qint64(2)); data.close();
        QVERIFY(!operation(store, capture(store), QStringLiteral("storage.get"), {{"key", "new"}}));
        QVERIFY2(store.recover(first.manifest.id, &error), qPrintable(error)); QVERIFY(store.setEnabled(first.manifest.id, true));
        const auto recovered = capture(store); QCOMPARE(recovered.package.digest, first.digest); QJsonObject result;
        QVERIFY(operation(store, recovered, QStringLiteral("storage.get"), {{"key", "old"}}, &result)); QCOMPARE(result.value("value").toString(), QStringLiteral("before"));
    }
    void upgradeRollbackAndMissingMigrationKeepMatchingData() {
        QTemporaryDir oldSource, nextSource, missingSource, profile; PackageCandidate old, next, missing; QString error;
        QVERIFY(package(oldSource.path(), &old, &error)); QVERIFY(package(missingSource.path(), &missing, &error, QStringLiteral("1.1.0"), 2));
        QVERIFY(package(nextSource.path(), &next, &error, QStringLiteral("1.2.0"), 2, true)); PackageStore store(profile.path());
        QVERIFY(store.install(old, old.manifest.requiredPermissions)); QVERIFY(store.setEnabled(old.manifest.id, true)); const auto before = capture(store);
        QVERIFY(operation(store, before, QStringLiteral("storage.set"), {{"key", "old"}, {"value", "before"}}));
        QVERIFY(!store.install(missing, missing.manifest.requiredPermissions, &error));
        QJsonObject result; QVERIFY(operation(store, before, QStringLiteral("storage.get"), {{"key", "old"}}, &result)); QCOMPARE(result.value("value").toString(), QStringLiteral("before"));
        QVERIFY2(store.install(next, next.manifest.requiredPermissions, &error), qPrintable(error));
        QVERIFY(!operation(store, before, QStringLiteral("storage.set"), {{"key", "old"}, {"value", "stale"}}));
        const auto after = capture(store); QVERIFY(operation(store, after, QStringLiteral("storage.get"), {{"key", "new"}}, &result)); QCOMPARE(result.value("value").toString(), QStringLiteral("before"));
        QCOMPARE(result.value("revision").toInt(), 2);
        QVERIFY(!operation(store, after, QStringLiteral("storage.set"), {{"key", "new"}, {"value", "stale"}, {"expectedRevision", 1}}));
        QVERIFY(operation(store, after, QStringLiteral("storage.set"), {{"key", "new"}, {"value", "after"}}));
        QVERIFY2(store.rollback(old.manifest.id, &error), qPrintable(error));
        QVERIFY(operation(store, capture(store), QStringLiteral("storage.get"), {{"key", "old"}}, &result)); QCOMPARE(result.value("value").toString(), QStringLiteral("before"));
        QCOMPARE(result.value("revision").toInt(), 1);
        QVERIFY(operation(store, capture(store), QStringLiteral("storage.get"), {{"key", "new"}}, &result)); QVERIFY(!result.value("found").toBool());
    }
    void snapshotsAndAccountRotationHavePhysicalBounds() {
        QTemporaryDir source, profile; PackageCandidate candidate; QString error; QVERIFY(package(source.path(), &candidate, &error));
        PackageStore store(profile.path()); QVERIFY(store.install(candidate, candidate.manifest.requiredPermissions)); QVERIFY(store.setEnabled(candidate.manifest.id, true));
        for (int i = 0; i < 40; ++i) QVERIFY(operation(store, capture(store), QStringLiteral("storage.set"), {{"key", "counter"}, {"value", i}}));
        const QDir objects(profile.filePath(QStringLiteral("private-storage/org.example.storage/objects")));
        QVERIFY(objects.entryList(QDir::Files).size() <= 3);
        QFile partial(objects.filePath(QString(64, QLatin1Char('a')) + QStringLiteral(".json.abc123")));
        QVERIFY(partial.open(QIODevice::WriteOnly)); partial.write("partial"); partial.close();
        QVERIFY(operation(store, capture(store), QStringLiteral("storage.get"), {{"key", "counter"}})); QVERIFY(!partial.exists());
        for (int i = 1; i < 16; ++i) QVERIFY(operation(store, capture(store, QStringLiteral("account:") + QString::number(i)), QStringLiteral("storage.set"), {{"key", "value"}, {"value", i}}));
        QVERIFY(!operation(store, capture(store, QStringLiteral("account:overflow")), QStringLiteral("storage.set"), {{"key", "value"}, {"value", 17}}));
        QJsonObject result; QVERIFY(operation(store, capture(store), QStringLiteral("storage.get"), {{"key", "counter"}}, &result)); QCOMPARE(result.value("value").toInt(), 39);
    }
    void keepDataUninstallRestoresOnlyItsMatchingSource() {
        QTemporaryDir source, profile; PackageCandidate candidate; QString error; QVERIFY(package(source.path(), &candidate, &error));
        PackageStore store(profile.path()); QVERIFY(store.install(candidate, candidate.manifest.requiredPermissions)); QVERIFY(store.setEnabled(candidate.manifest.id, true));
        QVERIFY(operation(store, capture(store), QStringLiteral("storage.set"), {{"key", "saved"}, {"value", true}}));
        QVERIFY(store.uninstall(candidate.manifest.id, false)); QVERIFY(store.install(candidate, candidate.manifest.requiredPermissions)); QVERIFY(store.setEnabled(candidate.manifest.id, true));
        QJsonObject result; QVERIFY(operation(store, capture(store), QStringLiteral("storage.get"), {{"key", "saved"}}, &result)); QVERIFY(result.value("value").toBool());
        QVERIFY(store.uninstall(candidate.manifest.id, false));
        QTemporaryDir otherSource; PackageCandidate other;
        QVERIFY(package(otherSource.path(), &other, &error, QStringLiteral("1.1.0"), 1, false, QStringLiteral("https://example.org/other.git")));
        QVERIFY(store.install(other, other.manifest.requiredPermissions)); QVERIFY(store.setEnabled(other.manifest.id, true));
        QVERIFY(operation(store, capture(store), QStringLiteral("storage.get"), {{"key", "saved"}}, &result)); QVERIFY(!result.value("found").toBool());
        QVERIFY(store.uninstall(candidate.manifest.id, true)); QVERIFY(!QFileInfo::exists(profile.filePath(QStringLiteral("private-storage/org.example.storage"))));
        QVERIFY(store.install(candidate, candidate.manifest.requiredPermissions)); QVERIFY(store.setEnabled(candidate.manifest.id, true));
        QVERIFY(operation(store, capture(store), QStringLiteral("storage.get"), {{"key", "saved"}}, &result)); QVERIFY(!result.value("found").toBool());
    }
    void captureRefusalOrOwnerRetirementCannotStartStorageWork() {
        for(int action=0;action<3;++action) {
            QTemporaryDir source, profile; PackageCandidate candidate; QString error;
            QVERIFY(package(source.path(),&candidate,&error)); PackageStore store(profile.path());
            QVERIFY(store.install(candidate,candidate.manifest.requiredPermissions)); QVERIFY(store.setEnabled(candidate.manifest.id,true));
            const auto context=capture(store); AddonHostServices services; std::unique_ptr<AddonStorage> storage;
            storage=std::make_unique<AddonStorage>(store,[&](const auto&,auto* output,QString* failure) {
                *output=context;
                if(action==0) { *failure=QStringLiteral("The test refused the captured request."); return false; }
                if(action==1) storage->close();
                else storage.reset();
                return true;
            });
            QVERIFY(storage->mount(&services)); int calls=0;
            services.dispatch({candidate.manifest.id,candidate.manifest.id+QStringLiteral("/theme"),QStringLiteral("request"),candidate.manifest.requiredPermissions,{}},
                QStringLiteral("storage.set"),{{"key","refused"},{"value",true}},[&](auto result) { ++calls; QVERIFY(!result.error.isEmpty()); });
            QTRY_COMPARE(calls,1); QTRY_VERIFY(AddonStreamLifetime::applicationHandle().qtDrained());
            QJsonObject result; QVERIFY(operation(store,context,QStringLiteral("storage.get"),{{"key","refused"}},&result));
            QVERIFY(!result.value("found").toBool()); QCoreApplication::processEvents(); QCOMPARE(calls,1);
        }
    }
    void retiredStorageReadKeepsItsOwnJoinedWork_data() {
        QTest::addColumn<int>("action");
        QTest::newRow("cancel")<<0; QTest::newRow("close")<<1; QTest::newRow("destroy")<<2;
    }
    void retiredStorageReadKeepsItsOwnJoinedWork() {
#ifndef Q_OS_UNIX
        QSKIP("The controlled file read needs a Unix pipe.");
#else
        QFETCH(int,action); QTemporaryDir source,profile; PackageCandidate candidate; QString error;
        QVERIFY(package(source.path(),&candidate,&error)); PackageStore store(profile.path());
        QVERIFY(store.install(candidate,candidate.manifest.requiredPermissions)); QVERIFY(store.setEnabled(candidate.manifest.id,true));
        const auto context=capture(store); const auto lifetime=AddonStreamLifetime::applicationHandle(); QTRY_VERIFY(lifetime.qtDrained());
        AddonHostServices services; auto storage=std::make_unique<AddonStorage>(store,[context](const auto&,auto* output,QString*) { *output=context; return true; });
        QVERIFY(storage->mount(&services)); StateRead read(profile.path()); QVERIFY(read.valid()); int calls=0;
        const auto request=services.dispatch({candidate.manifest.id,candidate.manifest.id+QStringLiteral("/theme"),QStringLiteral("request"),candidate.manifest.requiredPermissions,{}},
            QStringLiteral("storage.set"),{{"key","canceled"},{"value",true}},[&](auto result) { ++calls; QVERIFY(!result.error.isEmpty()); });
        QTRY_VERIFY_WITH_TIMEOUT(read.entered(),3000); QVERIFY(lifetime.qtWorkCount()>0);
        QElapsedTimer elapsed; elapsed.start();
        if(action==0) services.cancel(request);
        else if(action==1) { storage->close(); services.cancel(request); }
        else storage.reset();
        QVERIFY(elapsed.elapsed()<100); QCOMPARE(calls,1); QVERIFY(!lifetime.qtDrained());
        QVERIFY(read.release()); QTRY_VERIFY(lifetime.qtDrained()); QCoreApplication::processEvents(); QCOMPARE(calls,1);
        QJsonObject result; QVERIFY(operation(store,context,QStringLiteral("storage.get"),{{"key","canceled"}},&result));
        QVERIFY(!result.value("found").toBool());
#endif
    }
    void completedStorageWorkDoesNotWaitForAnUnrelatedPoolTask() {
        QTemporaryDir source,profile; PackageCandidate candidate; QString error;
        QVERIFY(package(source.path(),&candidate,&error)); PackageStore store(profile.path());
        QVERIFY(store.install(candidate,candidate.manifest.requiredPermissions)); QVERIFY(store.setEnabled(candidate.manifest.id,true));
        const auto context=capture(store); const auto lifetime=AddonStreamLifetime::applicationHandle(); QTRY_VERIFY(lifetime.qtDrained());
        auto* pool=QThreadPool::globalInstance(); const int oldCount=pool->maxThreadCount(); pool->setMaxThreadCount(2);
        QSemaphore entered,resume; std::atomic_bool unrelatedEnded{false};
        const auto release=qScopeGuard([&] { resume.release(); pool->waitForDone(); pool->setMaxThreadCount(oldCount); });
        pool->start([&] { entered.release(); resume.acquire(); unrelatedEnded=true; }); QVERIFY(entered.tryAcquire(1,3000));
        AddonHostServices services; AddonStorage storage(store,[context](const auto&,auto* output,QString*) { *output=context; return true; });
        QVERIFY(storage.mount(&services)); int calls=0;
        services.dispatch({candidate.manifest.id,candidate.manifest.id+QStringLiteral("/theme"),QStringLiteral("request"),candidate.manifest.requiredPermissions,{}},
            QStringLiteral("storage.set"),{{"key","independent"},{"value",true}},[&](auto result) { ++calls; QVERIFY(result.error.isEmpty()); });
        QTRY_COMPARE(calls,1); QTRY_VERIFY(lifetime.qtDrained()); QVERIFY(!unrelatedEnded.load());
        QJsonObject result; QVERIFY(operation(store,context,QStringLiteral("storage.get"),{{"key","independent"}},&result));
        QVERIFY(result.value("found").toBool());
    }
    void realManagerRoutesGuestStorageCalls() {
#ifndef Q_OS_UNIX
        QSKIP("The process test needs the Unix test interpreter.");
#else
        QTemporaryDir source, profile, helper; PackageCandidate candidate; QString error;
        QVERIFY2(package(source.path(), &candidate, &error, QStringLiteral("1.0.0"), 1, false, QStringLiteral("https://example.org/storage.git"), true), qPrintable(error));
        QFile fixture(QFINDTESTDATA("fixtures/addon-worker-fixture.py")); QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto executable = helper.filePath(QStringLiteral("worker")); QFile worker(executable); QVERIFY(worker.open(QIODevice::WriteOnly));
        const auto bytes = fixture.readAll(); QCOMPARE(worker.write(bytes), bytes.size()); worker.close();
        QVERIFY(worker.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        AddonManager manager(profile.path()); manager.setWorkerExecutable(executable);
        QVERIFY2(manager.installLocalPackage(source.path(), true), qPrintable(manager.errorMessage())); QVERIFY(manager.setEnabled(candidate.manifest.id, true));
        QSignalSpy completed(&manager, &AddonManager::operationFinished);
        const auto invocation = manager.invoke(candidate.manifest.id + QStringLiteral("/storage"), QStringLiteral("searchAssets"), {});
        QTRY_COMPARE(completed.size(), 1); QCOMPARE(completed.first()[0].toString(), invocation); QCOMPARE(completed.first()[2].toString(), QStringLiteral("match"));
        const auto data = completed.first()[3].toMap(); QVERIFY(!data.value("denied").toBool());
        const auto response = data.value("response").toMap(); QCOMPARE(response.value("revision").toInt(), 1); QVERIFY(response.value("usedBytes").toInt() > 0);
        PackageStore store(profile.path()); QJsonObject settings;
        QVERIFY(store.settings(candidate.manifest.id, &settings)); QVERIFY(settings.isEmpty());
#endif
    }
    void aSettingsChangeDuringCaptureRefusesTheOldRequest() {
        QTemporaryDir source,profile; PackageCandidate candidate; QString error;
        QVERIFY(package(source.path(),&candidate,&error)); PackageStore store(profile.path());
        QVERIFY(store.install(candidate,candidate.manifest.requiredPermissions)); QVERIFY(store.setEnabled(candidate.manifest.id,true));
        const auto context=capture(store); AddonHostServices services;
        AddonStorage storage(store,[&](const auto&,auto* output,QString*) {
            *output=context;
            return store.saveSettings(candidate.manifest.id,{{"language","fr"}},context.settingsRevision);
        });
        QVERIFY(storage.mount(&services)); int calls=0;
        services.dispatch({candidate.manifest.id,candidate.manifest.id+QStringLiteral("/theme"),QStringLiteral("request"),candidate.manifest.requiredPermissions,{}},
            QStringLiteral("storage.set"),{{"key","changed"},{"value",true}},[&](auto result) { ++calls; QVERIFY(!result.error.isEmpty()); });
        QTRY_COMPARE(calls,1); QTRY_VERIFY(AddonStreamLifetime::applicationHandle().qtDrained());
        QJsonObject result; QVERIFY(operation(store,capture(store),QStringLiteral("storage.get"),{{"key","changed"}},&result));
        QVERIFY(!result.value("found").toBool());
    }
    void managerRejectsStorageWithoutALiveInvocation() {
        QTemporaryDir profile; AddonManager manager(profile.path()); QVERIFY(manager.capabilities().value("privateStorageAvailable").toBool());
        int calls = 0;
        manager.hostServices()->dispatch({QStringLiteral("org.example.storage"), QStringLiteral("org.example.storage/theme"), QStringLiteral("forged"), {{"storage.private", QJsonObject{{"quotaBytes", 1000}}}}, {}},
            QStringLiteral("storage.get"), {{"key", "private"}}, [&](auto result) { ++calls; QVERIFY(!result.error.isEmpty()); QVERIFY(result.response.isEmpty()); });
        QTRY_COMPARE(calls, 1);
    }
};
HAKONE_QTEST_MAIN(TestAddonStorage, QCoreApplication)
#include "test_addon_storage.moc"
