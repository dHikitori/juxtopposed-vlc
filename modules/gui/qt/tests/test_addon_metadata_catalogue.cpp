// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <functional>
#include "addons/addon_stream_lifetime.hpp"
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
#ifdef HAKONE_CATALOGUE_OPEN_PROBE
#include <fcntl.h>
#include <cstdarg>
namespace { std::function<void(const char*)> catalogueOpenProbe; }
extern "C" int __real_open(const char*, int, ...);
extern "C" int __wrap_open(const char* path, int flags, ...)
{
    if (catalogueOpenProbe) catalogueOpenProbe(path);
    if (flags & O_CREAT) {
        va_list arguments; va_start(arguments, flags); const auto mode = va_arg(arguments, int); va_end(arguments);
        return __real_open(path, flags, mode);
    }
    return __real_open(path, flags);
}
#endif
#include "addons/addon_package_store.hpp"

using namespace vlc::addons;
class TestMetadataCatalogue final : public QObject
{
    Q_OBJECT
    QByteArray m_safeMode;
    static bool write(const QString& name, const QByteArray& bytes)
    {
        if (!QDir().mkpath(QFileInfo(name).absolutePath())) return false;
        QFile file(name);
        if (file.exists() && !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return false;
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static bool document(const QString& name, const QJsonObject& value)
    { return write(name, QJsonDocument(value).toJson(QJsonDocument::Compact)); }
    static QJsonObject read(const QString& name)
    { QFile file(name); if (!file.open(QIODevice::ReadOnly)) return {}; return QJsonDocument::fromJson(file.readAll()).object(); }
    static QString statePath(const QString& root, const QString& id)
    { return QDir(root).filePath(QStringLiteral("packages/") + id + QStringLiteral("/state.json")); }
    static bool candidate(const QString& directory, const QString& id, PackageCandidate* output,
                          unsigned contributions = 1, const QString& version = QStringLiteral("1.0.0"),
                          const QString& source = QStringLiteral("https://github.com/hakone-org/catalogue-fixture"),
                          const QString& type = QStringLiteral("metadata.provider"))
    {
        QJsonArray declarations;
        for (unsigned i = 0; i < contributions; ++i)
            declarations.append(QJsonObject{{"id", QStringLiteral("provider%1").arg(i)}, {"type", type},
                {"contract", "metadata@1"}, {"features", QJsonArray{i % 2 ? "video.details" : "artwork"}},
                {"mediaKinds", QJsonArray{i % 2 ? "video" : "audio"}}, {"operations", QJsonArray{"enrich", "identify"}}});
        const QJsonObject manifest{{"formatVersion", 1}, {"id", id}, {"name", "Catalogue fixture"},
            {"version", version}, {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", source},
            {"api", QJsonObject{{"addon", ">=1.0.0 <2.0.0"}, {"contracts", QJsonObject{{"metadata", "^1.0.0"}, {"settings", "^1.0.0"}, {"theme", "^1.0.0"}}}}},
            {"runtime", QJsonObject{{"kind", "wasm-message-v1"}, {"entry", "worker.wasm"}}},
            {"files", QJsonArray{"addon.json", "LICENSE", "settings.json", "worker.wasm"}}, {"contributions", declarations},
            {"settings", QJsonObject{{"version", 1}, {"schema", "settings.json"}, {"accountFields", QJsonArray{"account"}}}}};
        const QJsonObject schema{{"type", "object"}, {"properties", QJsonObject{
            {"account", QJsonObject{{"type", "string"}}}, {"blob", QJsonObject{{"type", "string"}}}}},
            {"required", QJsonArray{"account"}}, {"additionalProperties", false}};
        if (!document(QDir(directory).filePath("addon.json"), manifest)
            || !document(QDir(directory).filePath("settings.json"), schema)
            || !write(QDir(directory).filePath("LICENSE"), "fixture license\n")
            || !write(QDir(directory).filePath("worker.wasm"), QByteArray::fromHex("0061736d01000000"))) return false;
        ReleaseIdentity identity; identity.source = source; identity.tag = QStringLiteral("v") + version;
        identity.tagObjectId = QString(40, QLatin1Char('a')); identity.commitId = identity.tagObjectId;
        return PackageStore::inspectDirectory(directory, identity, output);
    }
    static bool protectedCandidate(const QString& directory, const QString& id, PackageCandidate* output)
    {
        PackageCandidate ordinary;
        if (!candidate(directory, id, &ordinary)) return false;
        auto manifest = read(QDir(directory).filePath(QStringLiteral("addon.json")));
        const QJsonObject binding{{QStringLiteral("field"), QStringLiteral("apiKey")},
            {QStringLiteral("origin"), QStringLiteral("https://api.example.org")}, {QStringLiteral("header"), QStringLiteral("X-Api-Key")}};
        const QJsonObject network{{QStringLiteral("origins"), QJsonArray{QStringLiteral("https://api.example.org")}},
            {QStringLiteral("methods"), QJsonArray{QStringLiteral("GET")}}};
        const QJsonObject required{{QStringLiteral("network.http"), network},
            {QStringLiteral("credentials.use"), QJsonObject{{QStringLiteral("bindings"), QJsonArray{binding}}}}};
        manifest.insert(QStringLiteral("permissions"), QJsonObject{{QStringLiteral("required"), required}});
        const QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("apiKey"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("x-hakone-secret"), true}}},
            {QStringLiteral("password"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("format"), QStringLiteral("password")}}},
            {QStringLiteral("token"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("writeOnly"), true}}},
            {QStringLiteral("account"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
            {QStringLiteral("blob"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
            {QStringLiteral("additionalProperties"), false}};
        if (!document(QDir(directory).filePath(QStringLiteral("settings.json")), schema)) return false;
        if (!document(QDir(directory).filePath(QStringLiteral("addon.json")), manifest)) return false;
        return PackageStore::inspectDirectory(directory, ordinary.identity, output);
    }
    static bool install(PackageStore& store, const QString& id, unsigned count = 1)
    {
        QTemporaryDir source; PackageCandidate value;
        return candidate(source.path(), id, &value, count) && store.install(value, {});
    }
    static void empty(const MetadataCatalogueSnapshot& value)
    {
        QVERIFY(value.packages.isEmpty()); QVERIFY(value.providerPreferences.isEmpty());
        QVERIFY(!value.queuedAuthority.current()); QVERIFY(!value.authority.current());
        QVERIFY(value.digest.isEmpty()); QCOMPARE(value.retainedBytes, qsizetype(0));
    }
private slots:
    void initTestCase() { m_safeMode = qgetenv("VLC_ADDON_SAFE_MODE"); qputenv("VLC_ADDON_SAFE_MODE", "0"); }
    void cleanupTestCase() { if (m_safeMode.isNull()) qunsetenv("VLC_ADDON_SAFE_MODE"); else qputenv("VLC_ADDON_SAFE_MODE", m_safeMode); }
    void emptyAndNullOutputHaveDifferentResults()
    {
        QTemporaryDir dir; PackageStore store(dir.path());
        const auto reader = store.metadataCatalogueReader(); MetadataCatalogueSnapshot captured;
        QVERIFY(!reader(nullptr, nullptr)); QVERIFY(reader(&captured, nullptr));
        QVERIFY(captured.current()); QVERIFY(captured.packages.isEmpty()); QCOMPARE(captured.candidateCount, qsizetype(0));
        QCOMPARE(captured.digest.size(), qsizetype(32)); QVERIFY(captured.retainedBytes > 0);
    }
    void ownerCaptureDoesNoFileIoAndReaderRunsAsJoinedWork()
    {
        QTemporaryDir dir; const auto root = dir.filePath("store"); MetadataCatalogueReader reader;
        const auto ownerThread = QThread::currentThreadId();
        {
            PackageStore store(root); reader = store.metadataCatalogueReader();
            QVERIFY(!QFileInfo::exists(root));
        }
        AddonStreamLifetime lifetime; AddonStreamLifetime::NetworkTicket ticket;
        MetadataCatalogueSnapshot value; bool captured = false; std::atomic_bool worker{false};
        QVERIFY(lifetime.handle().startQtWork([&] {
            worker.store(QThread::currentThreadId() != ownerThread);
            captured = reader(&value, nullptr);
        }, [] {}, [&](auto registered) { ticket = registered; }));
        QVERIFY(ticket.waitJoined(std::chrono::seconds(5))); QVERIFY(worker.load());
        QVERIFY(captured); QVERIFY(value.current()); QVERIFY(QFileInfo::exists(root));
    }
    void factoryDoesNoReadAndKeepsThePrequeueToken()
    {
        QTemporaryDir dir; PackageStore store(dir.path());
        const auto before = store.metadataCatalogueReader();
        QVERIFY(store.saveProviderOrder(QStringLiteral("artwork"), {QStringLiteral("org.hakone.first/provider0")}));
        MetadataCatalogueSnapshot captured;
        QVERIFY(!before(&captured, nullptr)); empty(captured);
        const auto fresh = store.metadataCatalogueReader(); QVERIFY(fresh(&captured, nullptr));
        QVERIFY(captured.current()); QCOMPARE(captured.providerPreferences.value("artwork").toArray(), QJsonArray{"org.hakone.first/provider0"});
        QVERIFY(store.saveBuiltinProviderEnabled(QStringLiteral("hakone.builtin.example"), false));
        QVERIFY(!captured.current()); QVERIFY(!fresh(&captured, nullptr)); empty(captured);
    }
    void peerPreferenceAwayAndBackCannotRebaseAQueuedRead()
    {
        QTemporaryDir dir; PackageStore first(dir.path()), second(dir.path());
        QVERIFY(first.saveProviderOrder(QStringLiteral("artwork"), {QStringLiteral("org.hakone.one/provider0")}));
        const auto reader = first.metadataCatalogueReader();
        QVERIFY(second.saveProviderOrder(QStringLiteral("artwork"), {QStringLiteral("org.hakone.two/provider0")}));
        QVERIFY(second.saveProviderOrder(QStringLiteral("artwork"), {QStringLiteral("org.hakone.one/provider0")}));
        MetadataCatalogueSnapshot value; QVERIFY(!reader(&value, nullptr)); empty(value);
        QVERIFY(first.metadataCatalogueReader()(&value, nullptr)); QVERIFY(value.current());
    }
    void exactStoreLockIsUsedWithoutNestedPreferenceLocking()
    {
        QTemporaryDir dir; PackageStore store(dir.path()); const auto reader = store.metadataCatalogueReader();
        QLockFile lock(dir.filePath("store.lock")); QVERIFY(lock.tryLock(0));
        MetadataCatalogueSnapshot value; QElapsedTimer elapsed; elapsed.start();
        QVERIFY(!reader(&value, nullptr)); QVERIFY(elapsed.elapsed() < 1000); empty(value);
        lock.unlock(); QVERIFY(reader(&value, nullptr)); QVERIFY(value.current());
    }
#ifdef HAKONE_CATALOGUE_OPEN_PROBE
    void oneLockCoversFreshPreferencesAndEveryPackageRead()
    {
        QTemporaryDir dir; PackageStore store(dir.path()); QVERIFY(install(store, QStringLiteral("org.hakone.first")));
        QVERIFY(store.saveProviderOrder(QStringLiteral("artwork"), {QStringLiteral("org.hakone.previous/provider0")}));
        const auto reader = store.metadataCatalogueReader(); unsigned preferences = 0, resources = 0; bool locked = true;
        QVERIFY(document(dir.filePath("provider-order.json"), {{"artwork", QJsonArray{"org.hakone.first/provider0"}}}));
        catalogueOpenProbe = [&](const char* native) {
            const auto path = QFile::decodeName(native); QLockFile lock(dir.filePath("store.lock"));
            if (lock.tryLock(0)) locked = false;
            if (path.endsWith(QLatin1String("provider-order.json"))) ++preferences;
            if (path.endsWith(QLatin1String("LICENSE"))) ++resources;
        };
        MetadataCatalogueSnapshot value; const auto captured = reader(&value, nullptr); catalogueOpenProbe = {};
        QVERIFY(captured); QVERIFY(locked); QCOMPARE(preferences, 1u); QCOMPARE(resources, 1u);
        QCOMPARE(value.providerPreferences.value("artwork").toArray(), QJsonArray{"org.hakone.first/provider0"});
    }
    void changedFileTypeCannotBlockOrFollowALink_data()
    {
        QTest::addColumn<bool>("link"); QTest::newRow("fifo") << false; QTest::newRow("symlink") << true;
    }
    void changedFileTypeCannotBlockOrFollowALink()
    {
        QFETCH(bool, link); QTemporaryDir dir; PackageStore store(dir.path()); const auto id = QStringLiteral("org.hakone.first");
        QVERIFY(install(store, id)); const auto path = statePath(dir.path(), id); const auto before = read(path);
        const auto target = dir.filePath("saved-state"); QVERIFY(document(target, before)); bool changed = false;
        catalogueOpenProbe = [&](const char* native) {
            if (QFile::decodeName(native) != path) return;
            changed = QFile::remove(path) && (link ? QFile::link(target, path) : mkfifo(native, 0600) == 0);
        };
        QElapsedTimer elapsed; elapsed.start(); MetadataCatalogueSnapshot value;
        const auto captured = store.metadataCatalogueReader()(&value, nullptr); catalogueOpenProbe = {};
        QVERIFY(changed); QVERIFY(!captured); QVERIFY(elapsed.elapsed() < 1000); empty(value);
    }
#endif
    void disabledAndUnconfiguredDeclarationsRemainRaw()
    {
        QTemporaryDir dir; PackageStore store(dir.path()); QVERIFY(install(store, QStringLiteral("org.hakone.first"), 2));
        QVERIFY(install(store, QStringLiteral("org.hakone.second")));
        QVERIFY(store.setEnabled(QStringLiteral("org.hakone.second"), true));
        MetadataCatalogueSnapshot value; QVERIFY(store.metadataCatalogueReader()(&value, nullptr));
        QCOMPARE(value.packages.size(), qsizetype(2)); QCOMPARE(value.packages[0].contributions.size(), qsizetype(2));
        QVERIFY(!value.packages[0].savedEnabled); QVERIFY(!value.packages[0].package.enabled);
        QVERIFY(value.packages[1].savedEnabled); QVERIFY(value.packages[1].package.enabled);
        QVERIFY(value.packages[1].settings.isEmpty()); QCOMPARE(value.packages[1].schema.value("required").toArray(), QJsonArray{"account"});
        QCOMPARE(value.packages[0].contributions[1].features, QStringList{QStringLiteral("video.details")});
        QCOMPARE(value.packages[0].contributions[1].contract, QStringLiteral("metadata@1"));
        QCOMPARE(value.packages[0].contributions[0].operations, (QStringList{QStringLiteral("enrich"), QStringLiteral("identify")}));
    }
    void packagesWithoutMetadataContributionsRemainRaw()
    {
        QTemporaryDir dir, source; PackageStore store(dir.path()); PackageCandidate theme;
        QVERIFY(candidate(source.path(), QStringLiteral("org.hakone.theme"), &theme, 1, QStringLiteral("1.0.0"),
            QStringLiteral("https://github.com/hakone-org/catalogue-fixture"), QStringLiteral("theme")));
        QVERIFY(store.install(theme, {})); MetadataCatalogueSnapshot value;
        QVERIFY(store.metadataCatalogueReader()(&value, nullptr)); QCOMPARE(value.candidateCount, qsizetype(1));
        QCOMPARE(value.packages.size(), qsizetype(1)); QCOMPARE(value.rawContributionCount, qsizetype(1));
        QCOMPARE(value.packages[0].contributions[0].type, QStringLiteral("theme"));
        QVERIFY(write(QDir(value.packages[0].package.directory).filePath("LICENSE"), "corrupt\n"));
        QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
    }
    void sourceSettingsSchemaAndReleasePartitionStayExact()
    {
        QTemporaryDir dir, source; PackageStore store(dir.path()); const auto id = QStringLiteral("org.hakone.account");
        PackageCandidate original; QVERIFY(candidate(source.path(), id, &original)); QVERIFY(store.install(original, {}));
        QVERIFY(store.saveSettings(id, {{"account", "A"}}, 0));
        MetadataCatalogueSnapshot first; QVERIFY(store.metadataCatalogueReader()(&first, nullptr));
        const auto& row = first.packages[0]; QCOMPARE(row.settings.value("account").toString(), QStringLiteral("A"));
        QCOMPARE(row.settingsRevision, quint64(1)); QCOMPARE(row.package.generation, quint64(1));
        QCOMPARE(row.package.digest, original.digest); QCOMPARE(row.package.identity.source, original.identity.source);
        QCOMPARE(row.package.manifest.accountFields, QStringList{QStringLiteral("account")});
        const auto queued = store.metadataCatalogueReader();
        QVERIFY(store.saveSettings(id, {{"account", "B"}}, 1)); QVERIFY(!first.current());
        QVERIFY(!queued(&first, nullptr)); empty(first);
        PackageCandidate next; QVERIFY(candidate(source.path(), id, &next, 1, QStringLiteral("1.1.0")));
        next.identity.tagObjectId = next.identity.commitId = QString(40, QLatin1Char('b'));
        QVERIFY(store.install(next, {}));
        MetadataCatalogueSnapshot current; QVERIFY(store.metadataCatalogueReader()(&current, nullptr));
        QCOMPARE(current.packages[0].settings.value("account").toString(), QStringLiteral("B"));
        QCOMPARE(current.packages[0].settingsRevision, quint64(2)); QCOMPARE(current.packages[0].package.generation, quint64(2));
        QCOMPARE(current.packages[0].package.digest, next.digest);
    }
    void corruptNonpreferredSourceAbortsTheWholeCatalogue()
    {
        QTemporaryDir dir; PackageStore store(dir.path());
        QVERIFY(install(store, QStringLiteral("org.hakone.first"))); QVERIFY(install(store, QStringLiteral("org.hakone.second")));
        QVERIFY(store.saveProviderOrder(QStringLiteral("artwork"), {QStringLiteral("org.hakone.first/provider0")}));
        MetadataCatalogueSnapshot value; QVERIFY(store.metadataCatalogueReader()(&value, nullptr));
        const auto path = QDir(value.packages[1].package.directory).filePath("LICENSE");
        QVERIFY(write(path, "corrupt\n")); QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
    }
    void brokenCurrentStateCannotSilentlyUseItsBackup()
    {
        QTemporaryDir dir; PackageStore store(dir.path()); const auto id = QStringLiteral("org.hakone.first");
        QVERIFY(install(store, id)); QVERIFY(store.setEnabled(id, true));
        QVERIFY(QFileInfo::exists(statePath(dir.path(), id) + QStringLiteral(".backup")));
        QVERIFY(write(statePath(dir.path(), id), "not-json"));
        MetadataCatalogueSnapshot value; QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
        InstalledPackage normal; QVERIFY(store.installed(id, &normal)); QVERIFY(normal.recoveryRequired);
    }
    void malformedGenerationRefusesWithoutFloatConversion_data()
    {
        QTest::addColumn<QJsonValue>("bad");
        QTest::newRow("negative") << QJsonValue(-1);
        QTest::newRow("fraction") << QJsonValue(.5);
        QTest::newRow("overflow") << QJsonValue(1e30);
        QTest::newRow("string") << QJsonValue(QStringLiteral("1"));
    }
    void malformedGenerationRefusesWithoutFloatConversion()
    {
        QFETCH(QJsonValue, bad); QTemporaryDir dir; PackageStore store(dir.path()); const auto id = QStringLiteral("org.hakone.first");
        QVERIFY(install(store, id)); auto state = read(statePath(dir.path(), id)); state.insert("generation", bad);
        QVERIFY(document(statePath(dir.path(), id), state)); MetadataCatalogueSnapshot value;
        QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
    }
    void rawPreferenceAliasOverflowCannotNormalizeIntoSuccess()
    {
        QTemporaryDir dir; PackageStore store(dir.path()); QJsonArray order;
        for (unsigned i = 0; i < 129; ++i) order.append(QStringLiteral("vlc.builtin.same/provider"));
        QVERIFY(document(dir.filePath("provider-order.json"), {{"artwork", order}}));
        MetadataCatalogueSnapshot value; QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
    }
    void candidateAndContributionBoundariesAreIndependent()
    {
        QTemporaryDir dir; PackageStore store(dir.path());
        for (unsigned i = 0; i < 4; ++i) QVERIFY(install(store, QStringLiteral("org.hakone.family%1").arg(i), 64));
        MetadataCatalogueSnapshot value; QVERIFY(store.metadataCatalogueReader()(&value, nullptr));
        QCOMPARE(value.packages.size(), qsizetype(4)); QCOMPARE(value.packages[3].contributions.size(), qsizetype(64));
        QCOMPARE(value.rawContributionCount, qsizetype(256));
        QTemporaryDir source; PackageCandidate theme;
        QVERIFY(candidate(source.path(), QStringLiteral("org.hakone.extra"), &theme, 1, QStringLiteral("1.0.0"),
            QStringLiteral("https://github.com/hakone-org/catalogue-fixture"), QStringLiteral("theme")));
        QVERIFY(store.install(theme, {}));
        QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
        QTemporaryDir many; PackageStore countStore(many.path());
        for (unsigned i = 0; i < 257; ++i) QVERIFY(QDir().mkpath(many.filePath(QStringLiteral("packages/org.hakone.entry%1").arg(i))));
        QVERIFY(!countStore.metadataCatalogueReader()(&value, nullptr)); empty(value);
    }
    void exactCandidateLimitKeepsEveryDisabledSource()
    {
        QTemporaryDir dir; PackageStore store(dir.path());
        for (unsigned i = 0; i < 256; ++i) QVERIFY(install(store, QStringLiteral("org.hakone.entry%1").arg(i)));
        MetadataCatalogueSnapshot value; QVERIFY(store.metadataCatalogueReader()(&value, nullptr));
        QCOMPARE(value.candidateCount, qsizetype(256)); QCOMPARE(value.packages.size(), qsizetype(256));
        QVERIFY(value.retainedBytes <= MetadataCatalogueSnapshot::MaximumBytes);
        for (const auto& row : value.packages) QVERIFY(!row.savedEnabled && !row.package.enabled);
        QVERIFY(QDir().mkpath(dir.filePath("packages/org.hakone.extra")));
        QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
    }
    void completeRetainedSnapshotHasOneMiBByteLimit()
    {
        QTemporaryDir dir; PackageStore store(dir.path());
        for (unsigned i = 0; i < 4; ++i) {
            const auto id = QStringLiteral("org.hakone.large%1").arg(i); QVERIFY(install(store, id));
            QVERIFY(store.saveSettings(id, {{"account", "A"}, {"blob", QString(240 * 1024, QLatin1Char('X'))}}, 0));
        }
        MetadataCatalogueSnapshot value; QVERIFY(store.metadataCatalogueReader()(&value, nullptr));
        QVERIFY(value.retainedBytes <= MetadataCatalogueSnapshot::MaximumBytes); QVERIFY(value.retainedBytes > 960 * 1024);
        QVERIFY(install(store, QStringLiteral("org.hakone.large4")));
        QVERIFY(store.saveSettings(QStringLiteral("org.hakone.large4"), {{"account", "A"}, {"blob", QString(240 * 1024, QLatin1Char('X'))}}, 0));
        QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
    }
    void savedSafeModeDisablesWithoutHidingTheRawSlot()
    {
        QTemporaryDir dir; PackageStore store(dir.path()); const auto id = QStringLiteral("org.hakone.first");
        QVERIFY(install(store, id)); QVERIFY(store.setEnabled(id, true)); QVERIFY(store.setSafeMode(true));
        MetadataCatalogueSnapshot value; QVERIFY(store.metadataCatalogueReader()(&value, nullptr));
        QVERIFY(value.safeMode); QCOMPARE(value.packages.size(), qsizetype(1));
        QVERIFY(value.packages[0].savedEnabled); QVERIFY(!value.packages[0].package.enabled);
    }
    void malformedSavedPolicyRefusesTheCompleteCapture_data()
    {
        QTest::addColumn<QString>("path"); QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("preferences") << QStringLiteral("provider-order.json") << QByteArray("not-json");
        QTest::newRow("safe-mode") << QStringLiteral("recovery.json") << QByteArray("{\"safeMode\":\"false\"}");
        QTest::newRow("distribution") << QStringLiteral("distribution-policy.json") << QByteArray("{\"formatVersion\":1,\"removed\":[]}");
    }
    void malformedSavedPolicyRefusesTheCompleteCapture()
    {
        QFETCH(QString, path); QFETCH(QByteArray, bytes); QTemporaryDir dir; PackageStore store(dir.path());
        QVERIFY(install(store, QStringLiteral("org.hakone.first"))); QVERIFY(write(dir.filePath(path), bytes));
        MetadataCatalogueSnapshot value; QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
    }
    void savedCredentialSettingsHaveTheSameSaveAndCaptureRules_data()
    {
        QTest::addColumn<QJsonObject>("settings"); QTest::addColumn<bool>("accepted");
        const auto reference = QStringLiteral("12345678-1234-1234-1234-123456789abc");
        const auto secret = QStringLiteral("fixture-protected-value");
        QTest::newRow("direct-secret") << QJsonObject{{QStringLiteral("apiKey"), secret}} << false;
        QTest::newRow("nested-secret") << QJsonObject{{QStringLiteral("group"), QJsonObject{{QStringLiteral("apiKey"), secret}}}} << false;
        QTest::newRow("array-secret") << QJsonObject{{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("apiKey"), secret}}}}} << false;
        QTest::newRow("deep-array-secret") << QJsonObject{{QStringLiteral("items"), QJsonArray{QJsonArray{QJsonObject{{QStringLiteral("apiKey"), secret}}}}}} << false;
        QTest::newRow("password-format") << QJsonObject{{QStringLiteral("password"), secret}} << false;
        QTest::newRow("write-only") << QJsonObject{{QStringLiteral("token"), secret}} << false;
        QTest::newRow("reference-string") << QJsonObject{{QStringLiteral("_credentials"), reference}} << false;
        QTest::newRow("reference-array") << QJsonObject{{QStringLiteral("_credentials"), QJsonArray{reference}}} << false;
        QTest::newRow("reference-null") << QJsonObject{{QStringLiteral("_credentials"), QJsonValue(QJsonValue::Null)}} << false;
        QTest::newRow("bad-reference") << QJsonObject{{QStringLiteral("_credentials"), QJsonObject{{QStringLiteral("apiKey"), secret}}}} << false;
        QTest::newRow("uppercase-reference") << QJsonObject{{QStringLiteral("_credentials"), QJsonObject{{QStringLiteral("apiKey"), reference.toUpper()}}}} << false;
        QTest::newRow("reference-number") << QJsonObject{{QStringLiteral("_credentials"), QJsonObject{{QStringLiteral("apiKey"), 1}}}} << false;
        QTest::newRow("unknown-reference-field") << QJsonObject{{QStringLiteral("_credentials"), QJsonObject{{QStringLiteral("other"), reference}}}} << false;
        QTest::newRow("nested-reference-key") << QJsonObject{{QStringLiteral("group"), QJsonObject{{QStringLiteral("_credentials"), QJsonObject{{QStringLiteral("apiKey"), reference}}}}}} << false;
        QTest::newRow("empty-settings") << QJsonObject{} << true;
        QTest::newRow("empty-reference-object") << QJsonObject{{QStringLiteral("_credentials"), QJsonObject{}}} << true;
        QTest::newRow("valid-reference") << QJsonObject{{QStringLiteral("_credentials"), QJsonObject{{QStringLiteral("apiKey"), reference}}}} << true;
        QTest::newRow("valid-schema-reference") << QJsonObject{{QStringLiteral("_credentials"), QJsonObject{{QStringLiteral("password"), reference}, {QStringLiteral("token"), reference}}}} << true;
        QTest::newRow("ordinary-opaque-string") << QJsonObject{{QStringLiteral("blob"), secret}} << true;
        QTest::newRow("unknown-ordinary-string") << QJsonObject{{QStringLiteral("other"), secret}} << true;
    }
    void savedCredentialSettingsHaveTheSameSaveAndCaptureRules()
    {
        QFETCH(QJsonObject, settings); QFETCH(bool, accepted);
        QTemporaryDir dir, source; PackageStore store(dir.path()); const auto id = QStringLiteral("org.hakone.zprotected");
        // A valid earlier row must never survive a later refusal as a partial catalogue.
        QVERIFY(install(store, QStringLiteral("org.hakone.first")));
        PackageCandidate package; QVERIFY(protectedCandidate(source.path(), id, &package));
        QString error; QVERIFY2(store.install(package, package.manifest.requiredPermissions, &error), qPrintable(error));
        const auto path = statePath(dir.path(), id); const auto before = read(path); const auto token = store.authorityToken();
        QCOMPARE(store.saveSettings(id, settings, 0, &error), accepted);
        QJsonObject saved; quint64 revision = 0; QVERIFY(store.settings(id, &saved, &revision));
        if (accepted) { QCOMPARE(saved, settings); QCOMPARE(revision, quint64(1)); QVERIFY(!token.current()); }
        else { QCOMPARE(read(path), before); QCOMPARE(revision, quint64(0)); QVERIFY(token.current()); }
        // Keep release bytes and identity exact. Change only the installed settings.
        auto state = read(path); auto current = state.value(QStringLiteral("current")).toObject();
        current.insert(QStringLiteral("settings"), settings); state.insert(QStringLiteral("current"), current);
        QVERIFY(document(path, state));
        InstalledPackage verified; QVERIFY2(store.installed(id, &verified, &error), qPrintable(error));
        QCOMPARE(verified.digest, package.digest);
        MetadataCatalogueSnapshot value;
        QCOMPARE(store.metadataCatalogueReader()(&value, &error), accepted);
        if (accepted) {
            QCOMPARE(value.packages.size(), 2); QVERIFY(value.current());
            QCOMPARE(value.packages.last().settings, settings);
        } else { QVERIFY(!error.isEmpty()); empty(value); }
    }
    void malformedIdentityCannotBecomeAValidDefault()
    {
        QTemporaryDir dir; PackageStore store(dir.path()); const auto id = QStringLiteral("org.hakone.first");
        QVERIFY(install(store, id)); auto state = read(statePath(dir.path(), id));
        auto current = state.value("current").toObject(); auto identity = current.value("identity").toObject();
        identity.insert("developerSource", QStringLiteral("false")); current.insert("identity", identity); state.insert("current", current);
        QVERIFY(document(statePath(dir.path(), id), state)); MetadataCatalogueSnapshot value;
        QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
    }
    void sharedContentsRefuseInvalidSchemaEvenWithMatchingDigest()
    {
        QTemporaryDir dir; PackageStore store(dir.path()); const auto id = QStringLiteral("org.hakone.first");
        QVERIFY(install(store, id)); MetadataCatalogueSnapshot value; QVERIFY(store.metadataCatalogueReader()(&value, nullptr));
        const auto oldDirectory = value.packages[0].package.directory; Manifest manifest; QMap<QString, QByteArray> contents;
        QVERIFY(Manifest::loadDirectory(oldDirectory, &manifest, &contents)); contents.insert("settings.json", "{\"type\":\"string\"}");
        const auto digest = packageDigest(contents); const auto nextDirectory = QFileInfo(oldDirectory).dir().filePath(QString::fromLatin1(digest));
        for (auto it = contents.begin(); it != contents.end(); ++it) QVERIFY(write(QDir(nextDirectory).filePath(it.key()), it.value()));
        auto state = read(statePath(dir.path(), id)); auto current = state.value("current").toObject();
        current.insert("digest", QString::fromLatin1(digest)); state.insert("current", current); QVERIFY(document(statePath(dir.path(), id), state));
        QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); empty(value);
    }
    void sharedCapturedContentsValidationMatchesDirectoryValidation()
    {
        QTemporaryDir source; PackageCandidate value; QVERIFY(candidate(source.path(), QStringLiteral("org.hakone.first"), &value));
        QVERIFY(Manifest::validateContents(value.manifest, value.contents));
        value.contents.insert("settings.json", "{\"type\":\"string\"}");
        QVERIFY(!Manifest::validateContents(value.manifest, value.contents));
        QVERIFY(write(source.filePath("settings.json"), value.contents.value("settings.json")));
        QVERIFY(!Manifest::loadDirectory(source.path(), nullptr));
    }
#ifdef Q_OS_UNIX
    void nonregularOrOversizedResourcesAreRefusedBeforeRead_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("symlink") << QStringLiteral("symlink");
        QTest::newRow("fifo") << QStringLiteral("fifo");
        QTest::newRow("oversized") << QStringLiteral("oversized");
    }
    void nonregularOrOversizedResourcesAreRefusedBeforeRead()
    {
        QFETCH(QString, kind); QTemporaryDir dir; PackageStore store(dir.path());
        QVERIFY(install(store, QStringLiteral("org.hakone.first"))); MetadataCatalogueSnapshot value;
        QVERIFY(store.metadataCatalogueReader()(&value, nullptr)); const auto path = QDir(value.packages[0].package.directory).filePath("LICENSE");
        QVERIFY(QFile::remove(path));
        if (kind == QLatin1String("symlink")) {
            const auto target = dir.filePath("license"); QVERIFY(write(target, "fixture license\n")); QVERIFY(QFile::link(target, path));
        } else if (kind == QLatin1String("fifo")) QCOMPARE(mkfifo(QFile::encodeName(path).constData(), 0600), 0);
        else { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(Manifest::MaximumFileBytes + 1)); }
        QElapsedTimer elapsed; elapsed.start(); QVERIFY(!store.metadataCatalogueReader()(&value, nullptr));
        QVERIFY(elapsed.elapsed() < 1000); empty(value);
    }
    void nonregularCurrentStateIsRefusedBeforeOpen()
    {
        QTemporaryDir dir; PackageStore store(dir.path()); const auto id = QStringLiteral("org.hakone.first");
        QVERIFY(install(store, id)); const auto path = statePath(dir.path(), id); QVERIFY(QFile::remove(path));
        QCOMPARE(mkfifo(QFile::encodeName(path).constData(), 0600), 0);
        QElapsedTimer elapsed; elapsed.start(); MetadataCatalogueSnapshot value;
        QVERIFY(!store.metadataCatalogueReader()(&value, nullptr)); QVERIFY(elapsed.elapsed() < 1000); empty(value);
    }
#endif
};
QTEST_GUILESS_MAIN(TestMetadataCatalogue)
#include "test_addon_metadata_catalogue.moc"
