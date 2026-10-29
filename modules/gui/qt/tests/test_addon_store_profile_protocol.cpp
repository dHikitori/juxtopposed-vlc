// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_store_profile_protocol.hpp"
#include <QTest>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QLockFile>
#include <QtEndian>
#include <limits>
using namespace vlc::addons;
using P = StoreProfileProtocol;
using A = StoreProfileArbiter;
using G = StoreProfileGeneration;
using K = P::Kind;
using M = P::Message;
namespace {
QByteArray nonce(char c) { return QByteArray(16, c); }
M message(K kind) {
    M value{kind, nonce('c'), {}, {}, 0};
    if (kind == K::Welcome || kind == K::Recover || kind == K::Available) return value;
    value.request = nonce('r');
    if (kind == K::Join || kind == K::Hold) return value;
    value.epoch = {nonce('g'), (quint64(1) << 63) + 17};
    if (kind != K::Candidate && kind != K::Confirm && kind != K::Ready) value.deadline = 1000;
    return value;
}
G::Control control() { return {[] { return true; }, G::now, G::now() + 20 * 1000 * 1000}; }
struct Fixture {
    A arbiter{nonce('i')};
    const G::Epoch epoch;
    explicit Fixture(G::Epoch value = {nonce('g'), 0}) : epoch(std::move(value)) { arbiter.recovered(epoch); }
    QByteArray peer(bool joined = true) {
        auto welcome = arbiter.accept(); if (!welcome.accepted || welcome.send.size() != 1) return {};
        const auto id = welcome.send.first().connection;
        M join{K::Join, id, {}, nonce('j'), 0}; const auto candidate = arbiter.receive(id, join, 100);
        if (!candidate.accepted || candidate.send.size() != 1 || candidate.send.first().kind != K::Candidate) return {};
        if (joined) {
            auto confirm = candidate.send.first(); confirm.kind = K::Confirm;
            const auto ready = arbiter.receive(id, confirm, 100);
            if (!ready.accepted || ready.send.size() != 1 || ready.send.first().kind != K::Ready) return {};
        }
        return id;
    }
    M begin(const QByteArray& id) const { return {K::Begin, id, epoch, nonce('w'), 1000}; }
    M ack(const QByteArray& id) const { auto value = begin(id); value.kind = K::Revoked; return value; }
    M complete(const QByteArray& id) const { auto value = begin(id); value.kind = K::Complete; ++value.epoch.revision; return value; }
};
bool save(const QString& path, const QByteArray& bytes) { QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(); }
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
}
class TestAddonStoreProfileProtocol : public QObject {
    Q_OBJECT
private slots:
    void everyMessageKeepsItsExactFields_data() {
        QTest::addColumn<int>("kind"); for (int i = 1; i <= 14; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void everyMessageKeepsItsExactFields() {
        QFETCH(int, kind); const auto expected = message(K(kind)); const auto bytes = P::encode(expected); QCOMPARE(bytes.size(), P::FrameBytes);
        const auto decoded = P::decode(bytes); QVERIFY(decoded); QCOMPARE(decoded->kind, expected.kind); QCOMPARE(decoded->connection, expected.connection);
        QCOMPARE(decoded->epoch, expected.epoch); QCOMPARE(decoded->request, expected.request); QCOMPARE(decoded->deadline, expected.deadline);
    }
    void wireLayoutHasNoFloatingPointRevision() {
        const auto bytes = P::encode(message(K::Begin));
        const auto expected = QByteArray::fromHex(QByteArrayLiteral(
            "48414b4f4e4550310600000000000000"
            "63636363636363636363636363636363"
            "67676767676767676767676767676767"
            "8000000000000011"
            "72727272727272727272727272727272"
            "00000000000003e8"
            "00000000000000000000000000000000"));
        QCOMPARE(bytes, expected);
        auto maximum = message(K::Begin); maximum.epoch.revision = std::numeric_limits<quint64>::max();
        maximum.deadline = std::numeric_limits<qint64>::max(); const auto decoded = P::decode(P::encode(maximum)); QVERIFY(decoded);
        QCOMPARE(decoded->epoch.revision, maximum.epoch.revision); QCOMPARE(decoded->deadline, maximum.deadline);
    }
    void reservedBytesAreStrict_data() {
        QTest::addColumn<int>("offset");
        for (int i = 9; i < 16; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
        for (int i = 80; i < 96; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void reservedBytesAreStrict() { QFETCH(int, offset); auto bytes = P::encode(message(K::Begin)); bytes[offset] = 1; QVERIFY(!P::decode(bytes)); }
    void malformedFramesAreRefused_data() {
        QTest::addColumn<QString>("kind");
        for (const auto* value : {"magic", "version", "kind zero", "kind extra", "kind high", "short", "long", "connection", "generation", "request", "deadline zero", "deadline sign"}) QTest::newRow(value) << QString::fromLatin1(value);
    }
    void malformedFramesAreRefused() {
        QFETCH(QString, kind); auto bytes = P::encode(message(K::Begin));
        if (kind == QLatin1String("magic")) bytes[0] = 'x';
        if (kind == QLatin1String("version")) bytes[7] = '2';
        if (kind == QLatin1String("kind zero")) bytes[8] = 0;
        if (kind == QLatin1String("kind extra")) bytes[8] = 15;
        if (kind == QLatin1String("kind high")) bytes[8] = char(255);
        if (kind == QLatin1String("short")) bytes.chop(1);
        if (kind == QLatin1String("long")) bytes.append('x');
        if (kind == QLatin1String("connection")) bytes.replace(16, 16, QByteArray(16, '\0'));
        if (kind == QLatin1String("generation")) bytes.replace(32, 16, QByteArray(16, '\0'));
        if (kind == QLatin1String("request")) bytes.replace(56, 16, QByteArray(16, '\0'));
        if (kind == QLatin1String("deadline zero")) bytes.replace(72, 8, QByteArray(8, '\0'));
        if (kind == QLatin1String("deadline sign")) bytes[72] = char(128);
        QVERIFY(!P::decode(bytes));
    }
    void forbiddenFieldsCannotCrossMessageKinds_data() {
        QTest::addColumn<int>("kind");
        for (auto value : {K::Welcome, K::Join, K::Candidate, K::Confirm, K::Ready, K::Hold, K::Recover, K::Available}) QTest::newRow(qPrintable(QString::number(int(value)))) << int(value);
    }
    void forbiddenFieldsCannotCrossMessageKinds() {
        QFETCH(int, kind); auto value = message(K(kind)); value.deadline = 1; QVERIFY(P::encode(value).isEmpty());
        auto bytes = P::encode(message(K(kind))); bytes[79] = 1; QVERIFY(!P::decode(bytes));
        if (kind == int(K::Join) || kind == int(K::Hold) || kind == int(K::Welcome) || kind == int(K::Recover) || kind == int(K::Available)) {
            bytes = P::encode(message(K(kind))); bytes[55] = 1; QVERIFY(!P::decode(bytes));
        }
    }
    void everyFragmentBoundaryWorks() {
        const auto bytes = P::encode(message(K::Grant));
        for (qsizetype split = 1; split < P::FrameBytes; ++split) {
            P::Decoder decoder; auto first = decoder.feed(QByteArrayView(bytes).first(split)); QVERIFY(first.ok && first.messages.isEmpty()); QCOMPARE(decoder.bufferedBytes(), split);
            const auto last = decoder.feed(QByteArrayView(bytes).sliced(split)); QVERIFY(last.ok); QCOMPARE(last.messages.size(), 1); QCOMPARE(last.messages.first().kind, K::Grant); QCOMPARE(decoder.bufferedBytes(), 0);
        }
        P::Decoder decoder;
        for (qsizetype i = 0; i < bytes.size(); ++i) { auto batch = decoder.feed(QByteArrayView(bytes).sliced(i, 1)); QVERIFY(batch.ok); QCOMPARE(batch.messages.size(), i == bytes.size() - 1 ? 1 : 0); }
    }
    void batchAndCarryStayBounded() {
        const auto bytes = P::encode(message(K::Candidate)); P::Decoder decoder;
        QVERIFY(decoder.feed(QByteArrayView(bytes).first(95)).ok);
        auto next = bytes.last(1); next.append(bytes.repeated(P::MaximumBatch - 1)); next.append(bytes.first(95));
        const auto batch = decoder.feed(next); QVERIFY(batch.ok); QCOMPARE(batch.messages.size(), P::MaximumBatch); QCOMPARE(decoder.bufferedBytes(), 95);
        auto end = decoder.feed(QByteArrayView(bytes).last(1)); QVERIFY(end.ok); QCOMPARE(end.messages.size(), 1); QCOMPARE(decoder.bufferedBytes(), 0);
    }
    void badBatchHasNoPartialEffectsAndNeverRecovers_data() { QTest::addColumn<bool>("oversize"); QTest::newRow("bad tail") << false; QTest::newRow("oversize") << true; }
    void badBatchHasNoPartialEffectsAndNeverRecovers() {
        QFETCH(bool, oversize); const auto good = P::encode(message(K::Grant)); auto bytes = good.repeated(oversize ? P::MaximumBatch : 2);
        if (oversize) bytes.append('x'); else bytes[96 + 9] = 1;
        P::Decoder decoder; const auto batch = decoder.feed(bytes); QVERIFY(!batch.ok && batch.messages.isEmpty()); QVERIFY(decoder.failed()); QCOMPARE(decoder.bufferedBytes(), 0);
        const auto later = decoder.feed(good); QVERIFY(!later.ok && later.messages.isEmpty());
    }
    void connectionsAreAssignedBoundedAndNeverReused() {
        A arbiter(nonce('i')); QList<QByteArray> ids;
        for (qsizetype i = 0; i < A::MaximumPeers; ++i) { auto effect = arbiter.accept(); QVERIFY(effect.accepted); QCOMPARE(effect.send.size(), 1); const auto id = effect.send.first().connection; QVERIFY(!ids.contains(id)); ids.push_back(id); }
        QVERIFY(!arbiter.accept().accepted); QCOMPARE(arbiter.peers(), A::MaximumPeers);
        QVERIFY(arbiter.disconnected(ids.first()).accepted); const auto next = arbiter.accept(); QVERIFY(next.accepted); QVERIFY(!ids.contains(next.send.first().connection));
        A bad(QByteArray(16, '\0')); QVERIFY(!bad.accept().accepted); QVERIFY(!bad.recovered({nonce('g'), 0}).accepted);
    }
    void startupRecoveryKeepsJoiningPeersClosed() {
        A arbiter(nonce('i')); const auto id = arbiter.accept().send.first().connection;
        const auto held = arbiter.receive(id, {K::Join, id, {}, nonce('j'), 0}, 100); QCOMPARE(held.send.first().kind, K::Hold);
        QVERIFY(!arbiter.recovered({nonce('g'), 1}).accepted);
        const auto available = arbiter.recovered({nonce('g'), 0}); QCOMPARE(available.send.first().kind, K::Available);
        const auto candidate = arbiter.receive(id, {K::Join, id, {}, nonce('k'), 0}, 100); QCOMPARE(candidate.send.first().kind, K::Candidate);
    }
    void aSecondMutationCannotReplaceThePendingWriter() {
        Fixture f; const auto writer = f.peer(), other = f.peer(); QVERIFY(!writer.isEmpty() && !other.isEmpty());
        f.arbiter.receive(writer, f.begin(writer), 100);
        const auto refused = f.arbiter.receive(other, f.begin(other), 100); QVERIFY(!refused.accepted && refused.recoveryRequired);
        QCOMPARE(f.arbiter.phase(), A::Phase::Recovering);
        for (const auto& sent : refused.send) QVERIFY(sent.kind != K::Grant);
    }
    void aJoinConfirmationMustMatchItsCandidate_data() {
        QTest::addColumn<int>("kind"); QTest::newRow("request") << 0; QTest::newRow("generation") << 1; QTest::newRow("revision") << 2;
    }
    void aJoinConfirmationMustMatchItsCandidate() {
        QFETCH(int, kind); Fixture f; const auto id = f.peer(false); QVERIFY(!id.isEmpty());
        M confirm{K::Confirm, id, f.epoch, nonce('j'), 0};
        if (kind == 0) confirm.request = nonce('x');
        if (kind == 1) confirm.epoch.generation = nonce('x');
        if (kind == 2) ++confirm.epoch.revision;
        const auto refused = f.arbiter.receive(id, confirm, 100); QVERIFY(!refused.accepted && refused.send.isEmpty()); QCOMPARE(f.arbiter.phase(), A::Phase::Ready);
    }
    void completionMustMatchTheGrantedOperation_data() {
        QTest::addColumn<int>("kind"); QTest::newRow("request") << 0; QTest::newRow("generation") << 1; QTest::newRow("revision") << 2; QTest::newRow("deadline") << 3; QTest::newRow("connection") << 4;
    }
    void completionMustMatchTheGrantedOperation() {
        QFETCH(int, kind); Fixture f; const auto id = f.peer(); QVERIFY(!id.isEmpty());
        f.arbiter.receive(id, f.begin(id), 100); f.arbiter.receive(id, f.ack(id), 100); auto completion = f.complete(id);
        if (kind == 0) completion.request = nonce('x');
        if (kind == 1) completion.epoch.generation = nonce('x');
        if (kind == 2) ++completion.epoch.revision;
        if (kind == 3) ++completion.deadline;
        if (kind == 4) completion.connection = nonce('x');
        const auto refused = f.arbiter.receive(id, completion, 100); QVERIFY(!refused.accepted && !refused.check && refused.recoveryRequired); QCOMPARE(f.arbiter.phase(), A::Phase::Recovering);
    }
    void aSavedEpochDoesNotSkipJoinConfirmation() {
        Fixture f; const auto id = f.peer(false); QVERIFY(!id.isEmpty());
        const auto rejected = f.arbiter.receive(id, f.begin(id), 100); QVERIFY(!rejected.accepted); QCOMPARE(rejected.close, QList<QByteArray>{id}); QCOMPARE(f.arbiter.phase(), A::Phase::Ready);
    }
    void joiningAndJoinedPeersMustBothDrain() {
        Fixture f; const auto writer = f.peer(), joining = f.peer(false); QVERIFY(!writer.isEmpty() && !joining.isEmpty());
        const auto begin = f.arbiter.receive(writer, f.begin(writer), 100); QCOMPARE(begin.send.size(), 2); QCOMPARE(f.arbiter.phase(), A::Phase::Revoking);
        for (const auto& item : begin.send) QCOMPARE(item.kind, K::Revoke);
        M confirm{K::Confirm, joining, f.epoch, nonce('j'), 0}; const auto late = f.arbiter.receive(joining, confirm, 100); QVERIFY(late.accepted && late.send.isEmpty());
        auto ack = f.arbiter.receive(writer, f.ack(writer), 100); QVERIFY(ack.send.isEmpty()); QCOMPARE(f.arbiter.phase(), A::Phase::Revoking);
        ack = f.arbiter.receive(joining, f.ack(joining), 100); QCOMPARE(ack.send.size(), 1); QCOMPARE(ack.send.first().kind, K::Grant); QCOMPARE(ack.send.first().connection, writer); QCOMPARE(f.arbiter.phase(), A::Phase::Granted);
        QVERIFY(f.arbiter.receive(joining, f.ack(joining), 100).send.isEmpty());
    }
    void newPeersCannotJoinDuringAMutation() {
        Fixture f; const auto writer = f.peer(); QVERIFY(!writer.isEmpty()); f.arbiter.receive(writer, f.begin(writer), 100);
        const auto id = f.arbiter.accept().send.first().connection;
        const auto held = f.arbiter.receive(id, {K::Join, id, {}, nonce('n'), 0}, 100); QVERIFY(held.accepted); QCOMPARE(held.send.size(), 1); QCOMPARE(held.send.first().kind, K::Hold);
        const auto grant = f.arbiter.receive(writer, f.ack(writer), 100); QCOMPARE(grant.send.size(), 1); QCOMPARE(grant.send.first().kind, K::Grant);
    }
    void completionNeedsAnExactNativeCheck() {
        Fixture f; const auto writer = f.peer(); QVERIFY(!writer.isEmpty()); f.arbiter.receive(writer, f.begin(writer), 100); f.arbiter.receive(writer, f.ack(writer), 100);
        auto receipt = f.complete(writer); const auto check = f.arbiter.receive(writer, receipt, 100); QVERIFY(check.check); QVERIFY(check.send.isEmpty()); QCOMPARE(f.arbiter.phase(), A::Phase::Checking);
        const auto duplicate = f.arbiter.receive(writer, receipt, 100); QVERIFY(!duplicate.check && duplicate.send.isEmpty());
        ++receipt.epoch.revision; QVERIFY(!f.arbiter.checked(receipt, true, 100).accepted); QCOMPARE(f.arbiter.phase(), A::Phase::Checking);
        const auto ready = f.arbiter.checked(*check.check, true, 100); QCOMPARE(ready.send.size(), 1); QCOMPARE(ready.send.first().kind, K::Available); QCOMPARE(f.arbiter.phase(), A::Phase::Ready); QCOMPARE(f.arbiter.epoch().revision, quint64(1));
        const auto id = writer; M join{K::Join, id, {}, nonce('k'), 0}; const auto candidate = f.arbiter.receive(id, join, 100); QCOMPARE(candidate.send.first().epoch.revision, quint64(1));
        auto confirm = candidate.send.first(); confirm.kind = K::Confirm; const auto joined = f.arbiter.receive(id, confirm, 100); QCOMPARE(joined.send.first().kind, K::Ready);
    }
    void failedChecksAndLateChecksRequireRecovery_data() { QTest::addColumn<bool>("late"); QTest::newRow("incoherent") << false; QTest::newRow("expired during check") << true; }
    void failedChecksAndLateChecksRequireRecovery() {
        QFETCH(bool, late); Fixture f; const auto writer = f.peer(); QVERIFY(!writer.isEmpty()); f.arbiter.receive(writer, f.begin(writer), 100); f.arbiter.receive(writer, f.ack(writer), 100);
        const auto check = f.arbiter.receive(writer, f.complete(writer), 100); QVERIFY(check.check);
        const auto failed = f.arbiter.checked(*check.check, late, late ? 1000 : 100); QVERIFY(failed.recoveryRequired); QCOMPARE(f.arbiter.phase(), A::Phase::Recovering);
        QVERIFY(!f.arbiter.recovered(f.epoch).accepted); auto next = f.epoch; next.generation = nonce('h'); ++next.revision; QVERIFY(!f.arbiter.recovered(next).accepted);
        next.revision = 0; QVERIFY(f.arbiter.recovered(next).accepted); QVERIFY(!f.arbiter.checked(*check.check, true, 1000).accepted);
    }
    void deadlinesDoNotGrantAfterTimeout_data() { QTest::addColumn<bool>("granted"); QTest::newRow("revoking") << false; QTest::newRow("granted") << true; }
    void deadlinesDoNotGrantAfterTimeout() {
        QFETCH(bool, granted); Fixture f; const auto writer = f.peer(); QVERIFY(!writer.isEmpty()); f.arbiter.receive(writer, f.begin(writer), 100); if (granted) f.arbiter.receive(writer, f.ack(writer), 100);
        QVERIFY(!f.arbiter.advance(999).recoveryRequired); const auto expired = f.arbiter.advance(1000); QVERIFY(expired.recoveryRequired); QCOMPARE(expired.send.first().kind, K::Recover);
        QVERIFY(!f.arbiter.advance(1001).recoveryRequired); const auto late = f.arbiter.receive(writer, f.ack(writer), 1001); QVERIFY(!late.accepted); QCOMPARE(f.arbiter.phase(), A::Phase::Recovering);
    }
    void invalidMutationBoundsCannotStartRevocation_data() {
        QTest::addColumn<QString>("kind"); for (const auto* k : {"expired", "too long", "old epoch", "wrong revision"}) QTest::newRow(k) << QString::fromLatin1(k);
    }
    void invalidMutationBoundsCannotStartRevocation() {
        QFETCH(QString, kind); Fixture f; const auto writer = f.peer(); QVERIFY(!writer.isEmpty()); auto begin = f.begin(writer);
        if (kind == QLatin1String("expired")) begin.deadline = 100;
        if (kind == QLatin1String("too long")) begin.deadline = G::MaximumDuration + 101;
        if (kind == QLatin1String("old epoch")) begin.epoch.generation = nonce('x');
        if (kind == QLatin1String("wrong revision")) ++begin.epoch.revision;
        const auto result = f.arbiter.receive(writer, begin, 100); QVERIFY(!result.accepted && result.send.isEmpty()); QCOMPARE(f.arbiter.phase(), A::Phase::Ready);
    }
    void aConnectionCannotClaimAnotherPeer() {
        Fixture f; const auto one = f.peer(), two = f.peer(); QVERIFY(!one.isEmpty() && !two.isEmpty());
        const auto result = f.arbiter.receive(two, f.begin(one), 100); QVERIFY(!result.accepted); QCOMPARE(result.close, QList<QByteArray>{two}); QCOMPARE(f.arbiter.peers(), qsizetype(1));
        const auto begin = f.arbiter.receive(one, f.begin(one), 100); QCOMPARE(begin.send.size(), 1); QCOMPARE(begin.send.first().connection, one);
    }
    void wrongAcknowledgementsNeverGrant_data() {
        QTest::addColumn<QString>("kind"); for (const auto* k : {"request", "epoch", "deadline", "connection", "kind"}) QTest::newRow(k) << QString::fromLatin1(k);
    }
    void wrongAcknowledgementsNeverGrant() {
        QFETCH(QString, kind); Fixture f; const auto writer = f.peer(); QVERIFY(!writer.isEmpty()); f.arbiter.receive(writer, f.begin(writer), 100); auto ack = f.ack(writer);
        if (kind == QLatin1String("request")) ack.request = nonce('x');
        if (kind == QLatin1String("epoch")) ++ack.epoch.revision;
        if (kind == QLatin1String("deadline")) ++ack.deadline;
        if (kind == QLatin1String("connection")) ack.connection = nonce('x');
        if (kind == QLatin1String("kind")) ack.kind = K::Grant;
        const auto result = f.arbiter.receive(writer, ack, 100); QVERIFY(!result.accepted && result.recoveryRequired); QCOMPARE(f.arbiter.phase(), A::Phase::Recovering);
        for (const auto& sent : result.send) QVERIFY(sent.kind != K::Grant);
    }
    void losingAWriterOrAnUnacknowledgedPeerRequiresRecovery_data() {
        QTest::addColumn<bool>("writerLost"); QTest::newRow("writer") << true; QTest::newRow("peer before ack") << false;
    }
    void losingAWriterOrAnUnacknowledgedPeerRequiresRecovery() {
        QFETCH(bool, writerLost); Fixture f; const auto writer = f.peer(), other = f.peer(); QVERIFY(!writer.isEmpty() && !other.isEmpty()); f.arbiter.receive(writer, f.begin(writer), 100);
        QVERIFY(f.arbiter.disconnected(writerLost ? writer : other).recoveryRequired); QCOMPARE(f.arbiter.phase(), A::Phase::Recovering);
    }
    void aDrainedPeerCanDisconnectWithoutCancellingTheWriter() {
        Fixture f; const auto writer = f.peer(), other = f.peer(); QVERIFY(!writer.isEmpty() && !other.isEmpty()); f.arbiter.receive(writer, f.begin(writer), 100);
        f.arbiter.receive(other, f.ack(other), 100); QVERIFY(!f.arbiter.disconnected(other).recoveryRequired);
        const auto granted = f.arbiter.receive(writer, f.ack(writer), 100); QCOMPARE(granted.send.first().kind, K::Grant);
    }
    void aNonWriterCannotCompleteTheTransaction() {
        Fixture f; const auto writer = f.peer(), other = f.peer(); QVERIFY(!writer.isEmpty() && !other.isEmpty()); f.arbiter.receive(writer, f.begin(writer), 100);
        f.arbiter.receive(other, f.ack(other), 100); f.arbiter.receive(writer, f.ack(writer), 100);
        const auto forged = f.arbiter.receive(other, f.complete(other), 100); QVERIFY(!forged.accepted && !forged.check && !forged.recoveryRequired); QCOMPARE(f.arbiter.phase(), A::Phase::Granted);
        QVERIFY(f.arbiter.receive(writer, f.complete(writer), 100).check);
    }
    void abortsNeverReopenTheOldEpoch_data() { QTest::addColumn<bool>("granted"); QTest::newRow("before grant") << false; QTest::newRow("after grant") << true; }
    void abortsNeverReopenTheOldEpoch() {
        QFETCH(bool, granted); Fixture f; const auto writer = f.peer(); QVERIFY(!writer.isEmpty()); f.arbiter.receive(writer, f.begin(writer), 100); if (granted) f.arbiter.receive(writer, f.ack(writer), 100);
        auto abort = f.begin(writer); abort.kind = K::Abort; QVERIFY(f.arbiter.receive(writer, abort, 100).recoveryRequired); QVERIFY(!f.arbiter.recovered(f.epoch).accepted);
        const auto held = f.arbiter.receive(writer, {K::Join, writer, {}, nonce('n'), 0}, 100); QCOMPARE(held.send.first().kind, K::Hold);
        QVERIFY(f.arbiter.recovered({nonce('h'), 0}).accepted); const auto old = f.arbiter.receive(writer, f.complete(writer), 100); QVERIFY(!old.accepted && !old.check); QCOMPARE(f.arbiter.epoch().generation, nonce('h'));
    }
    void badClocksStayClosed_data() { QTest::addColumn<int>("kind"); QTest::newRow("negative clock") << 0; QTest::newRow("clock reversed") << 1; }
    void badClocksStayClosed() {
        QFETCH(int, kind); Fixture f; const auto writer = f.peer(); QVERIFY(!writer.isEmpty());
        const auto result = f.arbiter.advance(kind == 0 ? -1 : 99);
        QVERIFY(result.recoveryRequired); QCOMPARE(f.arbiter.phase(), A::Phase::Recovering);
    }
    void realWriterAndJoiningFenceFollowTheProtocol() {
        QTemporaryDir root; G::Status status; auto owner = G::acquireCoordinator(root.path(), &status); QVERIFY(owner);
        const auto initial = owner->recover(control(), [](const auto& context, auto*) { return context.current(); }); QCOMPARE(initial.status, G::Status::Ready);
        Fixture f(initial.epoch); const auto writerId = f.peer(), joiningId = f.peer(false); QVERIFY(!writerId.isEmpty() && !joiningId.isEmpty());
        auto one = G::prepareJoin(root.path(), initial.epoch, control(), &status), two = G::prepareJoin(root.path(), initial.epoch, control(), &status); QVERIFY(one.fence && two.fence);
        qint64 tick = 100; bool ticketLive = false; G::Control preparation{[] { return true; }, [&] { return tick; }, 1000};
        auto writer = G::prepareWriter(root.path(), preparation, &status); QVERIFY(writer);
        QCOMPARE(f.arbiter.receive(writerId, f.begin(writerId), tick).send.size(), 2);
        one.fence.reset(); QVERIFY(f.arbiter.receive(writerId, f.ack(writerId), tick).send.isEmpty());
        StoreProcessFence::Status fenceStatus; QVERIFY(!StoreProcessFence::acquire(root.filePath(QStringLiteral("hakone-authority.fence")), StoreProcessFence::Mode::Replacement, &fenceStatus)); QCOMPARE(fenceStatus, StoreProcessFence::Status::Busy);
        two.fence.reset(); const auto grant = f.arbiter.receive(joiningId, f.ack(joiningId), tick); QCOMPARE(grant.send.first().kind, K::Grant); ticketLive = true;
        G::Ticket ticket{initial.epoch, nonce('w'), {[&] { return ticketLive; }, [&] { return tick; }, 1000}};
        const auto written = writer->write(ticket, [&](const auto& context, auto*) { return context.current() && save(root.filePath(QStringLiteral("value")), "new") ? G::Change::Applied : G::Change::NeedsRecovery; });
        QCOMPARE(written.status, G::Status::Ready); ticketLive = false;
        auto completion = f.complete(writerId); completion.epoch = written.epoch; const auto check = f.arbiter.receive(writerId, completion, tick); QVERIFY(check.check);
        auto confirmed = G::prepareJoin(root.path(), written.epoch, preparation, &status); QVERIFY(confirmed.fence); QCOMPARE(status, G::Status::Ready);
        const auto ready = f.arbiter.checked(*check.check, true, tick); QCOMPARE(ready.send.size(), 2); QCOMPARE(f.arbiter.epoch(), written.epoch); QCOMPARE(read(root.filePath(QStringLiteral("value"))), QByteArray("new"));
    }
    void anAcknowledgementCannotBypassARetainedNativeFence() {
        QTemporaryDir root; G::Status status; auto owner = G::acquireCoordinator(root.path(), &status); QVERIFY(owner);
        const auto initial = owner->recover(control(), [](const auto& context, auto*) { return context.current(); }); QCOMPARE(initial.status, G::Status::Ready);
        Fixture f(initial.epoch); const auto id = f.peer(); QVERIFY(!id.isEmpty()); auto retained = G::prepareJoin(root.path(), initial.epoch, control(), &status); QVERIFY(retained.fence);
        qint64 tick = 100; G::Control current{[] { return true; }, [&] { return tick; }, 1000}; auto writer = G::prepareWriter(root.path(), current, &status); QVERIFY(writer);
        f.arbiter.receive(id, f.begin(id), tick); QCOMPARE(f.arbiter.receive(id, f.ack(id), tick).send.first().kind, K::Grant);
        bool called = false; const auto result = writer->write({initial.epoch, nonce('w'), current}, [&](const auto&, auto*) { called = true; return G::Change::Applied; });
        QCOMPARE(result.status, G::Status::Busy); QVERIFY(!called); auto abort = f.begin(id); abort.kind = K::Abort; QVERIFY(f.arbiter.receive(id, abort, tick).recoveryRequired);
        QCOMPARE(owner->recover(control(), [](const auto&, auto*) { return true; }).status, G::Status::Busy);
        retained.fence.reset(); const auto restored = owner->recover(control(), [](const auto&, auto*) { return true; }); QCOMPARE(restored.status, G::Status::Ready); QVERIFY(f.arbiter.recovered(restored.epoch).accepted);
    }
    void recoveryCannotPassALiveWriterAndAnOldGrantCannotReturn() {
        QTemporaryDir root; G::Status status; auto owner = G::acquireCoordinator(root.path(), &status); QVERIFY(owner);
        const auto initial = owner->recover(control(), [](const auto& context, auto*) { return context.current(); }); QCOMPARE(initial.status, G::Status::Ready);
        Fixture f(initial.epoch); const auto id = f.peer(); QVERIFY(!id.isEmpty()); qint64 tick = 100; G::Control current{[] { return true; }, [&] { return tick; }, 1000};
        auto writer = G::prepareWriter(root.path(), current, &status); QVERIFY(writer); f.arbiter.receive(id, f.begin(id), tick); const auto grant = f.arbiter.receive(id, f.ack(id), tick); QCOMPARE(grant.send.first().kind, K::Grant);
        auto abort = f.begin(id); abort.kind = K::Abort; QVERIFY(f.arbiter.receive(id, abort, tick).recoveryRequired); QCOMPARE(owner->recover(control(), [](const auto&, auto*) { return true; }).status, G::Status::Busy);
        writer.reset(); const auto restored = owner->recover(control(), [](const auto&, auto*) { return true; }); QCOMPARE(restored.status, G::Status::Ready); QVERIFY(restored.epoch.generation != initial.epoch.generation); QVERIFY(f.arbiter.recovered(restored.epoch).accepted);
        writer = G::prepareWriter(root.path(), current, &status); QVERIFY(writer); bool called = false;
        QCOMPARE(writer->write({initial.epoch, nonce('w'), current}, [&](const auto&, auto*) { called = true; return G::Change::Applied; }).status, G::Status::Stale); QVERIFY(!called);
    }
};
QTEST_GUILESS_MAIN(TestAddonStoreProfileProtocol)
#include "test_addon_store_profile_protocol.moc"
