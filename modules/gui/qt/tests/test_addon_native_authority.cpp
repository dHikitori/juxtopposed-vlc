// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QLockFile>
#include <atomic>
#include <thread>
#include "addons/addon_manager.hpp"
#ifdef Q_OS_UNIX
# include <fcntl.h>
# include <sys/stat.h>
# include <unistd.h>
#endif

using namespace vlc::addons;
namespace {
const QString packageId = QStringLiteral("org.hakone.thesubtitledb");
const QString contributionId = QStringLiteral("org.hakone.thesubtitledb/search");
std::optional<AddonNativeAuthority> capture(const AddonNativeAuthorityReader& reader)
{
    std::optional<AddonNativeAuthority> result;
    std::thread worker([&] { result = reader(); });
    worker.join(); return result;
}
QByteArray credential(const AddonNativeAuthority& authority, const QString& field)
{
    QByteArray result;
    std::thread worker([&] { result = authority.credentials(field); });
    worker.join(); return result;
}
bool readCatalogue(const MetadataCatalogueSource& source, MetadataCatalogueSnapshot* output, QString* error = nullptr)
{
    bool accepted = false;
    std::thread worker([&] { accepted = source.catalogueReader(output, error); });
    worker.join(); return accepted;
}
QByteArray readCredential(const MetadataCatalogueSource& source, const QString& reference)
{
    QByteArray value;
    std::thread worker([&] { value = source.credentialReader(packageId, reference); });
    worker.join(); return value;
}
QString referenceIn(const QString& directory)
{
    PackageStore store(directory); QJsonObject settings;
    if (!store.settings(packageId, &settings)) return {};
    return settings.value(QStringLiteral("_credentials")).toObject().value(QStringLiteral("apiKey")).toString();
}
bool saveObject(const QString& path, const QJsonObject& value)
{
    QSaveFile file(path); const auto bytes = QJsonDocument(value).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}
class TestAddonNativeAuthority : public QObject
{
    Q_OBJECT
    static QString example() { return QStringLiteral(VLC_ADDON_BUNDLED_ROOT "/thesubtitledb"); }
    static QVariantMap key(const QString& field = QStringLiteral("apiKey"), const QString& value = QStringLiteral("unit-native-authority"))
    { return {{field, QVariantMap{{"operation", "replace"}, {"value", value}, {"sessionOnly", true}}}}; }
    static bool setup(AddonManager& manager, const QString& source = example())
    {
        return manager.installLocalPackage(source, true)
            && manager.saveAddonSettings(packageId, {{"language", "en"}}, key())
            && manager.setEnabled(packageId, true);
    }
private slots:
    void credentialSnapshotsOwnAndSerializeTheirCache()
    {
        CredentialStore store;
        const auto first = store.create(packageId, QStringLiteral("apiKey"), QByteArray("unit-owned-first"), true);
        const auto second = store.create(packageId, QStringLiteral("apiKey"), QByteArray("unit-owned-second"), true);
        QVERIFY(!first.reference.isEmpty()); QVERIFY(!second.reference.isEmpty());
        const auto reader = store.snapshotReader(); store.clearSession();
        std::atomic_bool correct{true};
        const auto read = [&] {
            for (int i = 0; i < 100; ++i)
                if (reader(packageId, first.reference) != QByteArray("unit-owned-first")
                    || reader(packageId, second.reference) != QByteArray("unit-owned-second")
                    || !reader(QStringLiteral("org.hakone.other"), first.reference).isEmpty()) correct.store(false);
        };
        std::thread one(read), two(read); one.join(); two.join();
        QVERIFY(correct.load()); QVERIFY(store.presenceSnapshot().isEmpty());
    }
    void backgroundCaptureMatchesTheHostAccountAndCredentials()
    {
        QTemporaryDir directory; AddonManager manager(directory.path()); QVERIFY(setup(manager));
        const auto expected = manager.contributionsOfType(QStringLiteral("metadata.provider"), true).first().toMap().value("accountReference").toString();
        const auto authority = capture(manager.nativeAuthorityReader(contributionId)); QVERIFY(authority);
        QCOMPARE(authority->globalId, contributionId); QCOMPARE(authority->package.manifest.id, packageId);
        QCOMPARE(authority->contribution.id, QStringLiteral("search"));
        QCOMPARE(authority->contribution.type, QStringLiteral("metadata.provider"));
        QCOMPARE(authority->accountReference, expected); QVERIFY(expected.startsWith(QStringLiteral("account:")));
        QCOMPARE(authority->settings.value("language").toString(), QStringLiteral("en"));
        QVERIFY(authority->live()); QVERIFY(authority->current());
        QCOMPARE(credential(*authority, QStringLiteral("apiKey")), QByteArray("unit-native-authority"));
        QVERIFY(credential(*authority, QStringLiteral("other")).isEmpty());
    }
    void missingSetupDisabledSafeModeAndChangedFilesRefuseCapture()
    {
        QTemporaryDir directory; AddonManager manager(directory.path());
        QVERIFY(!capture(manager.nativeAuthorityReader(contributionId)));
        QVERIFY(manager.installLocalPackage(example(), true)); QVERIFY(manager.setEnabled(packageId, true));
        QVERIFY(!capture(manager.nativeAuthorityReader(contributionId)));
        QVERIFY(manager.saveAddonSettings(packageId, {{"language", "en"}}, key()));
        QVERIFY(!capture(manager.nativeAuthorityReader(packageId + QStringLiteral("/absent"))));
        QVERIFY(!capture(manager.nativeAuthorityReader(contributionId + QStringLiteral("/extra"))));
        auto authority = capture(manager.nativeAuthorityReader(contributionId)); QVERIFY(authority);
        QVERIFY(manager.setSafeMode(true)); QVERIFY(!authority->live());
        QVERIFY(!capture(manager.nativeAuthorityReader(contributionId)));
        QVERIFY(manager.setSafeMode(false)); QVERIFY(manager.setEnabled(packageId, false));
        QVERIFY(!capture(manager.nativeAuthorityReader(contributionId)));
        QVERIFY(manager.setEnabled(packageId, true));
        InstalledPackage installed; PackageStore store(directory.path()); QVERIFY(store.installed(packageId, &installed));
        const auto path = QDir(installed.directory).filePath(QStringLiteral("locales/en.json"));
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        QVERIFY(saveObject(path, {{"changed", true}}));
        QVERIFY(!capture(manager.nativeAuthorityReader(contributionId)));
    }
    void settingsKeysAndManagerDestructionRevokeCapturedReaders()
    {
        QTemporaryDir directory; auto manager = std::make_unique<AddonManager>(directory.path()); QVERIFY(setup(*manager));
        auto reader = manager->nativeAuthorityReader(contributionId);
        auto first = capture(reader); QVERIFY(first);
        std::atomic_uint revocations{0};
        const auto watch = first->onInvalidated([&revocations] { ++revocations; }); QVERIFY(bool(watch));
        QVERIFY(manager->saveAddonSettings(packageId, {{"language", "fr"}}, {}));
        QCOMPARE(revocations.load(), 1u); QVERIFY(!bool(watch));
        QVERIFY(!first->live()); QVERIFY(!first->current());
        QVERIFY(credential(*first, QStringLiteral("apiKey")).isEmpty()); QVERIFY(!capture(reader));
        auto second = capture(manager->nativeAuthorityReader(contributionId)); QVERIFY(second);
        QCOMPARE(second->accountReference, first->accountReference);
        QVERIFY(manager->saveAddonSettings(packageId, {{"language", "fr"}}, key(QStringLiteral("apiKey"), QStringLiteral("unit-rotated"))));
        QVERIFY(!second->live());
        reader = manager->nativeAuthorityReader(contributionId);
        auto third = capture(reader); QVERIFY(third); QVERIFY(third->accountReference != second->accountReference);
        manager.reset(); QVERIFY(!third->live()); QVERIFY(!third->current());
        QVERIFY(credential(*third, QStringLiteral("apiKey")).isEmpty()); QVERIFY(!capture(reader));
    }
    void nativeRevocationPrecedesManagerRetirementAndRefusesFreshReaders()
    {
        QTemporaryDir directory; auto manager = std::make_unique<AddonManager>(directory.path()); QVERIFY(setup(*manager));
        const auto authority = capture(manager->nativeAuthorityReader(contributionId)); QVERIFY(authority);
        std::atomic_bool revoked{false};
        const auto watch = authority->onInvalidated([&revoked] { revoked.store(true); }); QVERIFY(bool(watch));
        auto* native = manager.get(); bool retiring = false, destroyed = false, ordered = false, refused = false;
        connect(native, &AddonManager::retiring, this, [&] {
            retiring = true; ordered = revoked.load() && !destroyed && !authority->live();
            refused = !capture(native->nativeAuthorityReader(contributionId));
        });
        connect(native, &QObject::destroyed, this, [&] { destroyed = true; ordered = ordered && retiring; });
        manager.reset(); QVERIFY(retiring); QVERIFY(destroyed); QVERIFY(ordered); QVERIFY(refused); QVERIFY(!bool(watch));
    }
    void savedStateChangesRefuseCredentialsWithoutAMemoryWrite()
    {
        QTemporaryDir directory; AddonManager manager(directory.path()); QVERIFY(setup(manager));
        const auto authority = capture(manager.nativeAuthorityReader(contributionId)); QVERIFY(authority);
        const auto path = directory.filePath(QStringLiteral("packages/org.hakone.thesubtitledb/state.json"));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        auto state = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto current = state.value("current").toObject(), settings = current.value("settings").toObject();
        settings.insert("language", "fr"); current.insert("settings", settings);
        current.insert("settingsRevision", current.value("settingsRevision").toDouble() + 1); state.insert("current", current);
        QVERIFY(saveObject(path, state)); QVERIFY(authority->live()); QVERIFY(!authority->current());
        QVERIFY(credential(*authority, QStringLiteral("apiKey")).isEmpty());
    }
    void secretFieldsNeedAnApprovedBinding()
    {
        QTemporaryDir directory, source; Manifest manifest; QMap<QString, QByteArray> contents;
        QVERIFY(Manifest::loadDirectory(example(), &manifest, &contents));
        auto schema = QJsonDocument::fromJson(contents.value(manifest.settingsSchema)).object();
        auto fields = schema.value("properties").toObject();
        fields.insert("privateKey", QJsonObject{{"type", "string"}, {"maxLength", 128}, {"writeOnly", true}});
        fields.insert("retries", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 8}, {"default", 1}});
        schema.insert("properties", fields); contents.insert(manifest.settingsSchema, QJsonDocument(schema).toJson());
        for (auto it = contents.cbegin(); it != contents.cend(); ++it) {
            const auto path = source.filePath(it.key()); QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(it.value()), it.value().size());
        }
        AddonManager manager(directory.path()); QVERIFY(setup(manager, source.path()));
        QVERIFY(!manager.saveAddonSettings(packageId, {{"language", "en"}, {"retries", 1e30}}, {}));
        QVERIFY(manager.saveAddonSettings(packageId, {{"language", "en"}}, key(QStringLiteral("privateKey"), QStringLiteral("unit-unbound"))));
        const auto authority = capture(manager.nativeAuthorityReader(contributionId)); QVERIFY(authority);
        QCOMPARE(credential(*authority, QStringLiteral("apiKey")), QByteArray("unit-native-authority"));
        QVERIFY(credential(*authority, QStringLiteral("privateKey")).isEmpty());
    }
    void catalogueSourceCapturesOwnedMemory()
    {
        QTemporaryDir directory; AddonManager manager(directory.path()); QVERIFY(setup(manager));
        const auto reference = referenceIn(directory.path()); QVERIFY(!reference.isEmpty());
        const auto executable = directory.filePath(QStringLiteral("absent-worker"));
        manager.setWorkerExecutable(executable);
        const auto source = manager.metadataCatalogueSource(); QVERIFY(source); QVERIFY(source->current());
        QCOMPARE(source->workerExecutable, executable);
        QVERIFY(source->credentialPresence.value(packageId).value(reference));
        QVERIFY(manager.metaObject()->indexOfMethod("metadataCatalogueSource()") < 0);
        MetadataCatalogueSnapshot snapshot; QString error;
        QVERIFY2(readCatalogue(*source, &snapshot, &error), qPrintable(error)); QVERIFY(snapshot.current());
        QCOMPARE(snapshot.packages.size(), 1);
        QCOMPARE(snapshot.packages.first().package.manifest.id, packageId);
        QCOMPARE(snapshot.packages.first().settings.value(QStringLiteral("language")).toString(), QStringLiteral("en"));
        QCOMPARE(readCredential(*source, reference), QByteArray("unit-native-authority"));
        QByteArray other;
        std::thread worker([&] { other = source->credentialReader(QStringLiteral("org.hakone.other"), reference); });
        worker.join(); QVERIFY(other.isEmpty());
    }
    void catalogueSourceReadersRefuseSettingsChangeAndOwnerDestruction()
    {
        QTemporaryDir directory; auto manager = std::make_unique<AddonManager>(directory.path()); QVERIFY(setup(*manager));
        const auto reference = referenceIn(directory.path()); QVERIFY(!reference.isEmpty());
        const auto old = manager->metadataCatalogueSource(); QVERIFY(old);
        QCOMPARE(readCredential(*old, reference), QByteArray("unit-native-authority"));
        QVERIFY(manager->saveAddonSettings(packageId, {{"language", "fr"}}, {}));
        QVERIFY(!old->current()); QVERIFY(readCredential(*old, reference).isEmpty());
        MetadataCatalogueSnapshot snapshot; snapshot.candidateCount = 42; QString error;
        QVERIFY(!readCatalogue(*old, &snapshot, &error)); QVERIFY(!error.isEmpty());
        QCOMPARE(snapshot.candidateCount, 0); QVERIFY(snapshot.packages.isEmpty());
        const auto fresh = manager->metadataCatalogueSource(); QVERIFY(fresh);
        QVERIFY(readCatalogue(*fresh, &snapshot)); QCOMPARE(readCredential(*fresh, reference), QByteArray("unit-native-authority"));
        manager.reset(); QVERIFY(!fresh->current());
        QVERIFY(readCredential(*fresh, reference).isEmpty()); QVERIFY(!readCatalogue(*fresh, &snapshot));
        QVERIFY(snapshot.packages.isEmpty());
    }
    void catalogueSourceRefusesDuringManagerRetirement()
    {
        QTemporaryDir directory; auto manager = std::make_unique<AddonManager>(directory.path()); QVERIFY(setup(*manager));
        const auto source = manager->metadataCatalogueSource(); QVERIFY(source);
        auto* owner = manager.get(); bool retired = false, refused = false, revoked = false;
        connect(owner, &AddonManager::retiring, this, [&] {
            retired = true; revoked = !source->current(); refused = !owner->metadataCatalogueSource();
        });
        manager.reset(); QVERIFY(retired); QVERIFY(revoked); QVERIFY(refused);
    }
    void catalogueSourcePreparationDoesNotReadSavedStateOrProbeWorker()
    {
        QTemporaryDir directory; AddonManager manager(directory.path()); QVERIFY(setup(manager));
        const auto reference = referenceIn(directory.path()); QVERIFY(!reference.isEmpty());
        const auto executable = directory.filePath(QStringLiteral("absent-worker"));
        manager.setWorkerExecutable(executable);
        QLockFile lock(directory.filePath(QStringLiteral("store.lock"))); QVERIFY(lock.tryLock(0));
        const auto source = manager.metadataCatalogueSource(); QVERIFY(source); QVERIFY(source->current());
        QCOMPARE(source->workerExecutable, executable);
        QVERIFY(source->credentialPresence.value(packageId).value(reference));
        MetadataCatalogueSnapshot snapshot; QString error;
        QVERIFY(!readCatalogue(*source, &snapshot, &error)); QVERIFY(!error.isEmpty());
        QCOMPARE(readCredential(*source, reference), QByteArray("unit-native-authority"));
        std::optional<MetadataCatalogueSource> wrongThread;
        std::thread worker([&] { wrongThread = manager.metadataCatalogueSource(); }); worker.join();
        QVERIFY(!wrongThread);
        lock.unlock(); QVERIFY(readCatalogue(*source, &snapshot));
    }
    void catalogueSourceRefusesFromWithdrawalCallbacks()
    {
        QTemporaryDir directory; AddonManager manager(directory.path()); QVERIFY(setup(manager));
        manager.setWorkerExecutable(directory.filePath(QStringLiteral("absent-worker")));
        const auto source = manager.metadataCatalogueSource(); QVERIFY(source);
        const auto request = manager.invoke(contributionId, QStringLiteral("searchAssets"), {{"query", "Example"}});
        QVERIFY(!request.isEmpty()); bool observed = false, refused = false, revoked = false;
        connect(&manager, &AddonManager::operationFinished, this, [&](const QString& id, const QString&, const QString&, const QVariantMap&, const QString&) {
            if (id != request) return;
            observed = true; refused = !manager.metadataCatalogueSource(); revoked = !source->current();
        });
        QVERIFY(manager.setEnabled(packageId, false));
        QVERIFY(observed); QVERIFY(refused); QVERIFY(revoked);
        QVERIFY(manager.metadataCatalogueSource());
    }
    void catalogueSourceRefusesFromRemovalCallbacks()
    {
        QTemporaryDir directory; AddonManager manager(directory.path()); QVERIFY(setup(manager));
        bool observed = false, refused = false;
        connect(&manager, &AddonManager::packageWithdrawn, this, [&](const QString& id) {
            if (id != packageId) return;
            observed = true; refused = !manager.metadataCatalogueSource();
        });
        // The callback runs before the optional background deletion starts.
        manager.uninstall(packageId, false);
        QVERIFY(observed); QVERIFY(refused);
    }
    void preparationAndLiveChecksDoNotWaitForSavedState()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir directory; AddonManager manager(directory.path()); QVERIFY(setup(manager));
        const auto authority = capture(manager.nativeAuthorityReader(contributionId)); QVERIFY(authority);
        const auto path = directory.filePath(QStringLiteral("packages/org.hakone.thesubtitledb/state.json"));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto saved = file.readAll(); file.close();
        QVERIFY(QFile::remove(path)); QVERIFY(::mkfifo(QFile::encodeName(path).constData(), 0600) == 0);
        int writer = -1; bool released = false;
        auto release = [&] {
            if (released) return true;
            if (writer < 0) writer = ::open(QFile::encodeName(path).constData(), O_RDWR | O_NONBLOCK);
            QFile::remove(path); QFile restored(path);
            const bool restoredOk = restored.open(QIODevice::WriteOnly) && restored.write(saved) == saved.size(); restored.close();
            if (writer >= 0) { const auto count = ::write(writer, saved.constData(), size_t(saved.size())); Q_UNUSED(count); ::close(writer); writer = -1; }
            released = true; return restoredOk;
        };
        const auto restore = qScopeGuard([&] { if (QFileInfo(path).isFile()) return; release(); });
        const auto prepared = manager.nativeAuthorityReader(contributionId); QVERIFY(prepared);
        std::atomic_bool finished{false}, accepted{true};
        std::thread worker([&] { accepted.store(authority->current()); finished.store(true); });
        const auto joined = qScopeGuard([&] { release(); worker.join(); });
        const auto entered = [&] {
            if (writer < 0) writer = ::open(QFile::encodeName(path).constData(), O_WRONLY | O_NONBLOCK);
            return writer >= 0;
        };
        QTRY_VERIFY(entered());
        QVERIFY(!finished.load()); QVERIFY(authority->live());
        StoreAuthority invalidator(directory.path()); invalidator.invalidate();
        QVERIFY(!authority->live()); QVERIFY(!capture(prepared));
        QVERIFY(release()); QTRY_VERIFY(finished.load()); QVERIFY(!accepted.load());
#else
        QSKIP("This file-read check needs a native FIFO.");
#endif
    }
};
QTEST_GUILESS_MAIN(TestAddonNativeAuthority)
#include "test_addon_native_authority.moc"
