// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_package_store.hpp"
#include "addons/addon_distribution_bootstrap.hpp"
#include "addons/addon_store_profile_generation.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <functional>
using namespace vlc::addons;
using G = StoreProfileGeneration;
namespace {
// Every ordinary (unguarded) write method of PackageStore. Each must refuse
// in a shared profile. A new write method must be added here and guarded.
const QSet<QString> OrdinaryWrites = {
    QStringLiteral("install"), QStringLiteral("ensureDistributionPackage"), QStringLiteral("setEnabled"),
    QStringLiteral("saveProviderOrder"), QStringLiteral("saveBuiltinProviderEnabled"), QStringLiteral("rollback"),
    QStringLiteral("uninstall"), QStringLiteral("saveSettings"), QStringLiteral("recover"),
    QStringLiteral("setSafeMode"), QStringLiteral("setDistributionSuppressed"),
};
QString source(const char* file) {
    QFile input(QStringLiteral(VLC_ADDON_SOURCE_ROOT "/modules/gui/qt/addons/") + QLatin1String(file));
    return input.open(QIODevice::ReadOnly) ? QString::fromUtf8(input.readAll()) : QString();
}
QStringList listing(const QString& root) {
    QStringList files;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) files << it.next();
    files.sort();
    return files;
}
G::Control control() { return {[] { return true; }, G::now, G::now() + 20 * 1000 * 1000}; }
}
class TestAddonStoreProfileWrites : public QObject
{
    Q_OBJECT
private slots:
    void everyOrdinaryWriteRefusesInASharedProfile() {
        QTemporaryDir root; PackageStore store(root.path());
        QVERIFY(!store.sharedProfile());
        // An unshared profile keeps ordinary writes.
        QString error;
        QVERIFY2(store.setSafeMode(true, &error), qPrintable(error));
        StoreAuthority authority(root.path());
        auto peer = authority.attachPeer(); QVERIFY(peer);
        QVERIFY(store.sharedProfile());
        const auto before = listing(root.path());
        const auto refused = [](const QString& message) {
            return message.contains(QLatin1String("profile coordinator"));
        };
        const QList<QPair<QString, std::function<bool(QString*)>>> writes = {
            {"install", [&](QString* e) { return store.install(PackageCandidate{}, {}, e); }},
            {"ensureDistributionPackage", [&](QString* e) { DistributionInstallResult result; return store.ensureDistributionPackage(root.path(), Manifest{}, &result, e); }},
            {"setEnabled", [&](QString* e) { return store.setEnabled(QStringLiteral("org.example.a"), true, e); }},
            {"saveProviderOrder", [&](QString* e) { return store.saveProviderOrder(QStringLiteral("subtitles"), {}, nullptr, e); }},
            {"saveBuiltinProviderEnabled", [&](QString* e) { return store.saveBuiltinProviderEnabled(QStringLiteral("hakone.builtin.a"), false, nullptr, e); }},
            {"rollback", [&](QString* e) { return store.rollback(QStringLiteral("org.example.a"), e); }},
            {"uninstall", [&](QString* e) { return store.uninstall(QStringLiteral("org.example.a"), true, e); }},
            {"saveSettings", [&](QString* e) { return store.saveSettings(QStringLiteral("org.example.a"), {}, 0, e); }},
            {"saveSettings with a stamp", [&](QString* e) { return store.saveSettings(QStringLiteral("org.example.a"), {}, 0, QStringLiteral("stamp"), e); }},
            {"recover", [&](QString* e) { return store.recover(QStringLiteral("org.example.a"), e); }},
            {"setSafeMode", [&](QString* e) { return store.setSafeMode(false, e); }},
            {"setDistributionSuppressed", [&](QString* e) { return store.setDistributionSuppressed(QStringLiteral("org.example.a"), true, e); }},
        };
        for (const auto& write : writes) {
            QString message;
            QVERIFY2(!write.second(&message), qPrintable(write.first));
            QVERIFY2(refused(message), qPrintable(write.first + QStringLiteral(": ") + message));
        }
        QCOMPARE(listing(root.path()), before);
        QVERIFY(store.safeMode());
    }
    void aGuardedWriteStillWorksInsideAPermittedWrite() {
        QTemporaryDir root; PackageStore store(root.path());
        G::Epoch epoch;
        {
            G::Status status; auto owner = G::acquireCoordinator(root.path(), &status); QVERIFY(owner);
            epoch = owner->recover(control(), [](const StoreProfileWriteContext& context, QString*) { return context.current(); }).epoch;
            QVERIFY(!epoch.generation.isEmpty());
        }
        StoreAuthority authority(root.path());
        auto peer = authority.attachPeer(); QVERIFY(peer);
        QString error;
        QVERIFY(!store.setSafeMode(true, &error));
        const auto result = G::write(root.path(), {epoch, QByteArray(16, 'r'), control()},
            [&](const StoreProfileWriteContext& context, QString* message) { return store.setSafeMode(context, true, message); });
        QVERIFY2(result.status == G::Status::Ready && result.applied, qPrintable(result.error));
        QVERIFY(store.safeMode());
    }
    void theOrdinaryWriteListMatchesTheStoreApi() {
        const auto header = source("addon_package_store.hpp");
        const auto body = source("addon_package_store.cpp");
        QVERIFY(!header.isEmpty() && !body.isEmpty());
        const auto start = header.indexOf(QLatin1String("class PackageStore\n"));
        const auto end = header.indexOf(QLatin1String("\nprivate:"), start);
        QVERIFY(start >= 0 && end > start);
        // Each public declaration, joined across lines up to its semicolon.
        QSet<QString> writes;
        QString declaration;
        for (auto line : header.mid(start, end - start).split(QLatin1Char('\n'))) {
            line = line.trimmed();
            if (line.startsWith(QLatin1String("//"))) continue;
            declaration += QLatin1Char(' ') + line;
            if (!line.endsWith(QLatin1Char(';')) && !line.endsWith(QLatin1Char('}'))) continue;
            static const QRegularExpression write(QStringLiteral("^\\s*bool\\s+(\\w+)\\s*\\((.*)\\)\\s*;$"));
            const auto match = write.match(declaration);
            // Non-static, non-const methods returning bool change the store.
            if (match.hasMatch() && !declaration.contains(QLatin1String("static "))) writes.insert(match.captured(1));
            declaration.clear();
        }
        QCOMPARE(writes, OrdinaryWrites);
        // Each definition refuses in a shared profile, or delegates to one that does.
        static const QRegularExpression definition(QStringLiteral("\\nbool PackageStore::(\\w+)\\("));
        int checked = 0;
        for (auto it = definition.globalMatch(body); it.hasNext();) {
            const auto match = it.next();
            if (!OrdinaryWrites.contains(match.captured(1))) continue;
            const auto open = body.indexOf(QLatin1Char('{'), match.capturedEnd());
            const auto close = body.indexOf(QLatin1String("\n}"), open);
            const auto inline_ = body.indexOf(QLatin1Char('}'), open);
            const auto text = body.mid(open, (inline_ < close ? inline_ : close) - open);
            const bool guarded = text.contains(QLatin1String("ordinaryWriteAllowed(error)"))
                || text.contains(QStringLiteral("return %1(").arg(match.captured(1)));
            QVERIFY2(guarded, qPrintable(match.captured(1)));
            ++checked;
        }
        QCOMPARE(checked, 12);
    }
};
QTEST_GUILESS_MAIN(TestAddonStoreProfileWrites)
#include "test_addon_store_profile_writes.moc"
