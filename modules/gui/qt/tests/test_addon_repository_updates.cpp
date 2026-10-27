// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "addons/addon_repository_updates.hpp"
using namespace vlc::addons;

class TestAddonRepositoryUpdates : public QObject
{
    Q_OBJECT
    static QByteArray tag(const char* name, char object = 'a')
    { return QByteArray(40, object) + "\trefs/tags/" + name + '\n'; }
private slots:
    void objectFormatsMustBeConsistentAcrossTheAdvertisement()
    {
        QList<RepositoryTag> tags; QString error;
        const QByteArray sha256 = QByteArray(64, 'a') + "\trefs/tags/v1.1.0\n";
        QVERIFY(parseRepositoryTags(sha256, &tags, &error)); QCOMPARE(tags.first().objectId.size(), 64);
        QVERIFY(!parseRepositoryTags(tag("v1.0.0") + sha256, &tags, &error));
        QVERIFY(error.contains(QStringLiteral("object format")));
        QVERIFY(!parseRepositoryTags(sha256 + tag("notes"), &tags, &error));
    }
    void selectionUsesSemanticVersionAndTheChosenRange()
    {
        QList<RepositoryTag> tags; QString error; RepositoryTag selected;
        QVERIFY(parseRepositoryTags(tag("v1.9.0") + tag("v1.10.0") + tag("v2.0.0") + tag("notes"), &tags, &error));
        QCOMPARE(tags.size(), 3);
        QVERIFY(selectRepositoryUpdate(tags, QStringLiteral("1.0.0"), QStringLiteral("^1.0.0"), false, &selected, &error));
        QCOMPARE(selected.name, QStringLiteral("v1.10.0"));
        QVERIFY(selectRepositoryUpdate(tags, QStringLiteral("2.0.0"), QStringLiteral("^2.0.0"), false, &selected, &error));
        QVERIFY(selected.name.isEmpty()); QVERIFY(error.isEmpty());
    }
    void prereleaseNeedsAnExplicitPolicy()
    {
        QList<RepositoryTag> tags; RepositoryTag selected;
        QVERIFY(parseRepositoryTags(tag("v1.1.0-beta.2"), &tags));
        QVERIFY(selectRepositoryUpdate(tags, QStringLiteral("1.0.0"), QStringLiteral("^1.0.0"), false, &selected));
        QVERIFY(selected.name.isEmpty());
        QVERIFY(selectRepositoryUpdate(tags, QStringLiteral("1.0.0"), QStringLiteral("^1.0.0"), true, &selected));
        QCOMPARE(selected.name, QStringLiteral("v1.1.0-beta.2"));
        QVERIFY(selectRepositoryUpdate(tags, QStringLiteral("1.0.0"), QStringLiteral(">=1.0.0 <1.1.0"), true, &selected));
        QCOMPARE(selected.name, QStringLiteral("v1.1.0-beta.2"));
    }
    void aliasesMustHaveTheSameLockedObject()
    {
        QList<RepositoryTag> tags; RepositoryTag selected; QString error;
        QVERIFY(parseRepositoryTags(tag("1.1.0") + tag("v1.1.0"), &tags));
        QVERIFY(selectRepositoryUpdate(tags, QStringLiteral("1.0.0"), QStringLiteral("^1.0.0"), false, &selected));
        QCOMPARE(selected.name, QStringLiteral("v1.1.0"));
        QVERIFY(parseRepositoryTags(tag("1.1.0") + tag("v1.1.0", 'b'), &tags));
        QVERIFY(!selectRepositoryUpdate(tags, QStringLiteral("1.0.0"), QStringLiteral("^1.0.0"), false, &selected, &error));
        QVERIFY(!error.isEmpty());
    }
    void malformedDuplicateAndOversizedAdvertisementsFail()
    {
        QList<RepositoryTag> tags; RepositoryTag selected;
        QVERIFY(!parseRepositoryTags("not-an-object\trefs/tags/v1.0.0\n", &tags));
        QVERIFY(!parseRepositoryTags(tag("v1.0.0") + tag("v1.0.0"), &tags));
        QVERIFY(!parseRepositoryTags(QByteArray(1024 * 1024 + 1, 'x'), &tags));
        QByteArray many;
        for (int index = 0; index < 4097; ++index) many += tag("notes");
        QVERIFY(!parseRepositoryTags(many, &tags));
        QVERIFY(!selectRepositoryUpdate({}, QStringLiteral("1.0.0"), QStringLiteral("latest"), false, &selected));
    }
};
QTEST_GUILESS_MAIN(TestAddonRepositoryUpdates)
#include "test_addon_repository_updates.moc"
