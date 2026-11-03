// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QPointer>
#include "addons/addon_visualizer_worker.hpp"
using namespace vlc::addons;

class TestAddonVisualizerWorker : public QObject
{
    Q_OBJECT
    static QJsonObject scene()
    {
        return {{"nodes", QJsonArray{QJsonObject{{"type", "spectrum"}, {"id", "bars"}, {"count", 32}, {"gain", 1}}}},
            {"parameters", QJsonObject{{"gain", QJsonObject{{"node", "bars"}, {"property", "gain"}, {"minimum", 0}, {"maximum", 4}}}}}};
    }
    static QJsonObject result(double value) { return {{"parameters", QJsonObject{{"gain", value}}}}; }
    struct Fixture {
        qint64 now = 0;
        int canceled = 0, closed = 0;
        QList<QJsonObject> requests;
        QList<AddonVisualizerWorker::Completion> replies;
        std::unique_ptr<AddonVisualizerWorker> worker;
        Fixture()
        {
            worker = std::make_unique<AddonVisualizerWorker>(scene(), AddonVisualizerWorker::Backend{
                [this](const QJsonObject& context, AddonVisualizerWorker::Completion done) {
                    requests.append(context); replies.append(std::move(done)); return [this] { ++canceled; };
                }, [this] { ++closed; }}, [this] { return now; });
        }
        void deliver(int index, const QString& status, const QJsonObject& data)
        {
            replies[index](status, data);
            QCoreApplication::sendPostedEvents(worker.get(), QEvent::MetaCall);
        }
    };
private slots:
    void singleFlightAndRateLimit()
    {
        Fixture f; QVERIFY(f.worker->update({})); QVERIFY(!f.worker->update({}));
        QCOMPARE(f.requests.size(), 1); QCOMPARE(f.requests[0].value("contract").toString(), QStringLiteral("visualizer.parameters@1"));
        QVERIFY(!f.requests[0].value("session").toString().isEmpty());
        QCOMPARE(f.requests[0].value("parameterSchema"), scene().value("parameters"));
        f.deliver(0, QStringLiteral("match"), result(2)); QCOMPARE(f.worker->values().value("gain").toDouble(), 2);
        f.now = 99; QVERIFY(!f.worker->update({})); f.now = 100; QVERIFY(f.worker->update({}));
        f.deliver(1, QStringLiteral("partial"), result(3)); QCOMPARE(f.worker->values().value("gain").toDouble(), 3);
        QCOMPARE(f.canceled, 0);
    }
    void invalidInputDoesNotReachTheWorker()
    {
        Fixture f;
        QVERIFY(!f.worker->update({{"url", "https://example.invalid"}}));
        QVERIFY(!f.worker->update({{"theme", QString(49 * 1024, QLatin1Char('x'))}}));
        QVERIFY(!f.worker->update({{"reducedMotion", true}}));
        QCOMPARE(f.requests.size(), 0);
        QVERIFY(f.worker->update({{"reducedMotion", false}, {"clock", QJsonObject{{"timeUs", 2}}}}));
    }
    void invalidRepliesCannotChangeTheScene()
    {
        Fixture f;
        const QList<QJsonObject> replies{result(8), {{"parameters", QJsonObject{{"url", 2}}}},
            {{"parameters", QJsonObject{{"gain", 2}}}, {"scene", QJsonObject{}}}};
        for (int index = 0; index < replies.size(); ++index) {
            f.now = index * 100; QVERIFY(f.worker->update({})); f.deliver(index, QStringLiteral("match"), replies[index]);
            QVERIFY(f.worker->values().isEmpty());
        }
        QVERIFY(f.worker->suspended()); QVERIFY(!f.worker->errorMessage().isEmpty()); QCOMPARE(f.closed, 1);
        f.now = 1000; QVERIFY(!f.worker->update({}));
    }
    void unsupportedProtocolStatusCannotPublish()
    {
        Fixture f; QVERIFY(f.worker->update({})); f.deliver(0, QStringLiteral("ok"), result(2));
        QVERIFY(f.worker->values().isEmpty());
    }
    void busyUsesStaticParametersAndCanRetry()
    {
        Fixture f;
        for (int index = 0; index < 4; ++index) {
            f.now = index * 100; QVERIFY(f.worker->update({})); f.deliver(index, QStringLiteral("busy"), {});
        }
        QVERIFY(!f.worker->suspended()); QCOMPARE(f.closed, 0); QVERIFY(f.worker->values().isEmpty());
    }
    void timeoutCancelsAndLateRepliesCannotPublish()
    {
        Fixture f; QVERIFY(f.worker->update({})); f.now = 1999; f.worker->poll(); QCOMPARE(f.canceled, 0);
        f.now = 2000; f.worker->poll(); QCOMPARE(f.canceled, 1);
        f.deliver(0, QStringLiteral("match"), result(2)); QVERIFY(f.worker->values().isEmpty());
        QVERIFY(f.worker->update({})); f.now = 2499; f.worker->poll(); QCOMPARE(f.canceled, 1);
        f.now = 2500; f.worker->poll(); QCOMPARE(f.canceled, 2);
    }
    void aWorkerSlotRefusalKeepsTheFirstProcessDeadline()
    {
        Fixture f; QVERIFY(f.worker->update({})); f.deliver(0, QStringLiteral("busy"), {});
        f.now = 100; QVERIFY(f.worker->update({})); f.now = 2099; f.worker->poll(); QCOMPARE(f.canceled, 0);
        f.now = 2100; f.worker->poll(); QCOMPARE(f.canceled, 1);
    }
    void oldParametersExpire()
    {
        Fixture f; QSignalSpy changes(f.worker.get(), &AddonVisualizerWorker::parametersChanged);
        QVERIFY(f.worker->update({})); f.deliver(0, QStringLiteral("match"), result(2)); QCOMPARE(changes.count(), 1);
        f.now = 1999; f.worker->poll(); QVERIFY(!f.worker->values().isEmpty());
        f.now = 2000; f.worker->poll(); QVERIFY(f.worker->values().isEmpty()); QCOMPARE(changes.count(), 2);
    }
    void timelineChangeCancelsAndChangesGeneration()
    {
        Fixture f; QVERIFY(f.worker->update({})); const auto session = f.requests[0].value("session");
        const auto generation = f.requests[0].value("generation"); f.worker->resetTimeline(); QCOMPARE(f.canceled, 1);
        f.deliver(0, QStringLiteral("match"), result(4)); QVERIFY(f.worker->values().isEmpty());
        QVERIFY(f.worker->update({})); QCOMPARE(f.requests[1].value("session"), session);
        QVERIFY(f.requests[1].value("generation") != generation);
    }
    void closeCancelsAndRejectsLateReplies()
    {
        Fixture f; QVERIFY(f.worker->update({})); QSignalSpy changes(f.worker.get(), &AddonVisualizerWorker::parametersChanged);
        f.worker->close(); QCOMPARE(f.canceled, 1); QCOMPARE(f.closed, 1);
        f.deliver(0, QStringLiteral("match"), result(4)); QCOMPARE(changes.count(), 0);
        QVERIFY(!f.worker->update({})); f.worker->close(); QCOMPARE(f.closed, 1);
    }
    void inlineReplyIsDeliveredAfterInvokeReturns()
    {
        int canceled = 0;
        AddonVisualizerWorker worker(scene(), {[](const QJsonObject&, AddonVisualizerWorker::Completion done) {
            done(QStringLiteral("match"), result(2)); return AddonVisualizerWorker::Cancel{};
        }, [&] { ++canceled; }});
        QVERIFY(worker.update({})); QVERIFY(worker.values().isEmpty());
        QCoreApplication::sendPostedEvents(&worker, QEvent::MetaCall); QCOMPARE(worker.values().value("gain").toDouble(), 2);
        worker.close(); QCOMPARE(canceled, 1);
    }
    void invokeCanDestroyTheOwner()
    {
        int canceled = 0, closed = 0; AddonVisualizerWorker* worker = nullptr;
        worker = new AddonVisualizerWorker(scene(), {[&](const QJsonObject&, AddonVisualizerWorker::Completion done) {
            delete worker; worker = nullptr; done(QStringLiteral("match"), result(2)); return [&] { ++canceled; };
        }, [&] { ++closed; }});
        QVERIFY(!worker->update({})); QVERIFY(!worker); QCOMPARE(canceled, 1); QCOMPARE(closed, 1);
    }
    void cancelAndPublicationCanDestroyTheOwner()
    {
        AddonVisualizerWorker* worker = nullptr;
        worker = new AddonVisualizerWorker(scene(), {[&](const QJsonObject&, AddonVisualizerWorker::Completion) {
            return [&] { delete worker; worker = nullptr; };
        }, {}});
        QVERIFY(worker->update({})); worker->resetTimeline(); QVERIFY(!worker);
        Fixture f; const auto reply = [&f] { return f.replies[0]; };
        QVERIFY(f.worker->update({})); auto* ptr = f.worker.release(); QPointer<AddonVisualizerWorker> guard(ptr);
        connect(ptr, &AddonVisualizerWorker::parametersChanged, ptr, [ptr] { delete ptr; });
        reply()(QStringLiteral("match"), result(2)); QCoreApplication::sendPostedEvents(ptr, QEvent::MetaCall); QVERIFY(!guard);
    }
};
QTEST_GUILESS_MAIN(TestAddonVisualizerWorker)
#include "test_addon_visualizer_worker.moc"
