// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QPointer>
#include <atomic>
#include <thread>
#include <vlc_common.h>
#include <vlc_source_backend.h>
#include <vlc_source_owner.h>
#include <vlc_input_policy.h>
#include "lib/libvlc_internal.h"
#include "addons/addon_native_source_resolver.hpp"
#include "addons/addon_source_admissions.hpp"

using namespace vlc::addons;
namespace {
using Resolver = AddonNativeSourceResolver;
using Binding = AddonSourceConsent::Binding;
using Reason = AddonSourceConsent::IntentReason;
struct Transport {
    std::shared_ptr<std::atomic_bool> active = std::make_shared<std::atomic_bool>(true);
    std::atomic_int canceled{0}, destroyed{0}, io{0};
    Resolver::Current current;
    Resolver::Started started;
    std::function<void()> atStart;
    bool inlineStart = true;
};
struct Lease {
    std::shared_ptr<Transport> value;
    static void cancel(void* opaque) {
        auto& value = static_cast<Lease*>(opaque)->value;
        value->active->store(false, std::memory_order_release); ++value->canceled;
    }
    static void destroy(void* opaque) {
        auto* value = static_cast<Lease*>(opaque); ++value->value->destroyed; delete value;
    }
};
std::unique_ptr<Resolver::Prepared> dormant(const QString&, const std::shared_ptr<Transport>& value,
                                           QByteArray proxy = "http://127.0.0.1:32123/native_token/main")
{
    auto* policy = vlc_input_policy_NewProxy(32123, "/native_token/", nullptr, false);
    static const vlc_source_lease_operations ops{Lease::cancel, Lease::destroy};
    auto* lease = vlc_source_lease_New(&ops, new Lease{value});
    return Resolver::Prepared::adopt(std::move(proxy), policy, lease, value->active,
        [value](Resolver::Current current, Resolver::Started done) -> Resolver::Cancel {
            value->current = current; value->started = done;
            if (current()) { ++value->io; if (value->atStart) value->atStart(); }
            if (value->inlineStart) done(VLC_SUCCESS);
            return {};
        });
}
struct NativeJob {
    vlc_source_job_t* value = nullptr;
    std::atomic_int ended{0}, status{VLC_EGENERIC};
    std::function<void()> terminal;
    static void end(vlc_source_job_t*, int result, void* opaque) {
        auto* state = static_cast<NativeJob*>(opaque); state->status = result; ++state->ended;
        if (state->terminal) state->terminal();
    }
    NativeJob(vlc_object_t* instance, const QString& uri, quint64 generation) {
        auto* owner = vlc_source_owner_Hold(instance);
        const auto bytes = uri.toUtf8(); const vlc_source_request request{bytes.constData(), generation};
        value = vlc_source_job_New(owner, &request, end, this);
        if (owner) vlc_source_backend_Release(owner);
    }
    ~NativeJob() { if (value) { vlc_source_job_Cancel(value); vlc_source_job_Release(value); } }
    int start() {
        int result = VLC_EGENERIC;
        std::thread native([&] { result = value ? vlc_source_job_Start(value) : VLC_EGENERIC; }); native.join(); return result;
    }
};
struct Fixture {
    QTemporaryDir profile;
    libvlc_int_t* instance = libvlc_InternalCreate();
    Binding binding{AddonSourceConsent::SourceKind::Git, QStringLiteral("https://example.org/radio.git"),
        QStringLiteral("org.example.radio/source"), QStringLiteral("account-1")};
    std::atomic_bool enabled{true};
    std::atomic_uint revision{1};
    std::atomic_int admitted{0}, preparations{0}, preparationCanceled{0};
    std::shared_ptr<AddonSourceAdmissions> admissions;
    std::shared_ptr<AddonSourceConsent> consent;
    std::unique_ptr<Resolver> owner;
    std::function<Resolver::Cancel(const Resolver::Admission&, Resolver::Current, Resolver::PreparedCallback)> prepare;
    Fixture() {
        admissions = std::make_shared<AddonSourceAdmissions>(profile.filePath(QStringLiteral("admissions")),
            [this](const QVariantMap& value) { return enabled.load() && value.value("source").toString() == binding.contribution; });
        consent = std::make_shared<AddonSourceConsent>(profile.path(), AddonSourceConsent::Checks{
            [this](const Binding& value) { return enabled.load() && value.identity == binding.identity && value.contribution == binding.contribution; },
            [this](const Binding&, const QString& uri) { return !admissions->currentReference(uri).isEmpty(); }});
    }
    ~Fixture() { owner.reset(); if (instance) libvlc_InternalDestroy(instance); }
    vlc_object_t* root() const { return VLC_OBJECT(instance); }
    QString uri(const QString& account = QStringLiteral("account-1"), const QString& item = QStringLiteral("station")) {
        const QVariantMap value{{"format", "hakone-addon-source-v1"}, {"source", binding.contribution}, {"accountReference", account},
            {"itemId", item}, {"snapshot", QVariantMap{{"title", "Station"}, {"mediaKind", "audio"}}}};
        QStringList result; return admissions->admit({value}, &result) ? result.first() : QString();
    }
    bool approve(const QString& account = QStringLiteral("account-1")) {
        auto value = binding; value.accountReference = account;
        const auto ticket = consent->capture(value); return ticket && consent->grant(ticket->id);
    }
    Resolver::Backend backend() {
        return {[this](const QString& uri, quint64 generation) -> std::optional<Resolver::Admission> {
            ++admitted; const auto ref = admissions->currentReference(uri); if (ref.isEmpty()) return {};
            auto value = binding; value.accountReference = ref.value("accountReference").toString();
            const auto token = consent->mintIntent(value, uri, generation, Reason::PrimaryPlayback);
            const auto authorization = consent->consumeIntent(token, value, uri, generation, Reason::PrimaryPlayback);
            if (!authorization) return {};
            const auto captured = revision.load();
            return Resolver::Admission{consent, *authorization, [this, captured] { return revision.load() == captured; }};
        }, [this](const auto& admission, auto current, auto done) -> Resolver::Cancel {
            ++preparations; return prepare ? prepare(admission, std::move(current), std::move(done)) : Resolver::Cancel{};
        }};
    }
    void create(Resolver::Limits limits = {}) { owner = std::make_unique<Resolver>(root(), backend(), limits); }
    void automatic(const QString& uri, const std::shared_ptr<Transport>& transport) {
        prepare = [uri, transport](const auto&, auto, auto done) -> Resolver::Cancel { done(dormant(uri, transport)); return {}; };
    }
};
}
class TestAddonNativeSourceResolver : public QObject
{
    Q_OBJECT
private slots:
    void nativeOwnerHasOneBindingAndClosedOwnersCanBeReplaced() {
        Fixture f; QVERIFY(f.instance); f.create(); QVERIFY(f.owner->bound());
        Resolver second(f.root(), f.backend()); QVERIFY(!second.bound());
        f.owner->close(); QVERIFY(!f.owner->bound()); Resolver replacement(f.root(), f.backend()); QVERIFY(replacement.bound());
    }
    void inlinePreparationAndStartupKeepTheIssuedGuardCurrent() {
        Fixture f; const auto uri = f.uri(); QVERIFY(!uri.isEmpty()); QVERIFY(f.approve());
        auto transport = std::make_shared<Transport>(); f.automatic(uri, transport); f.create();
        NativeJob job(f.root(), uri, 1); QCOMPARE(job.start(), VLC_SUCCESS); QCOMPARE(f.preparations.load(), 0);
        QTRY_COMPARE(job.ended.load(), 1); QCOMPARE(job.status.load(), VLC_SUCCESS); QCOMPARE(transport->io.load(), 1);
        QVERIFY(transport->current); QVERIFY(transport->current()); QCOMPARE(f.owner->issuedCount(), 1u);
        const auto* result = vlc_source_job_GetResult(job.value); QVERIFY(result && result->policy && result->lease);
        QVERIFY(vlc_input_policy_IsProxy(result->policy, result->proxy_uri));
        QVERIFY(!vlc_source_job_Cancel(job.value)); QCOMPARE(transport->canceled.load(), 1); QVERIFY(!transport->current());
        QCOMPARE(f.owner->issuedCount(), 0u);
    }
    void missingConsentAndUnknownAdmissionCannotStartPreparation() {
        Fixture f; const auto uri = f.uri(); f.create(); NativeJob noConsent(f.root(), uri, 1);
        QCOMPARE(noConsent.start(), VLC_SUCCESS); QTRY_COMPARE(noConsent.ended.load(), 1); QCOMPARE(noConsent.status.load(), -EACCES);
        QVERIFY(f.approve()); NativeJob unknown(f.root(), QStringLiteral("hakone-addon-source:v1:unknown"), 2);
        QCOMPARE(unknown.start(), VLC_SUCCESS); QTRY_COMPARE(unknown.ended.load(), 1); QCOMPARE(unknown.status.load(), -EACCES);
        QCOMPARE(f.preparations.load(), 0);
    }
    void cancelBeforeTheQtHopEndsTheNativeWaitWithoutAdmission() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); f.create(); NativeJob job(f.root(), uri, 1);
        QCOMPARE(job.start(), VLC_SUCCESS);
        std::thread native([&] { vlc_source_job_Cancel(job.value); }); native.join();
        QCOMPARE(job.ended.load(), 1); QCOMPARE(job.status.load(), -EINTR); QCOMPARE(f.admitted.load(), 0);
        QCoreApplication::sendPostedEvents(); QCOMPARE(f.admitted.load(), 0);
    }
    void latePreparedTransportClosesWithoutStartingIo() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); Resolver::PreparedCallback done;
        f.prepare = [&](const auto&, auto, auto callback) -> Resolver::Cancel { done = std::move(callback); return [&] { ++f.preparationCanceled; }; };
        f.create(); NativeJob job(f.root(), uri, 1); QCOMPARE(job.start(), VLC_SUCCESS); QTRY_COMPARE(f.preparations.load(), 1);
        vlc_source_job_Cancel(job.value); QCOMPARE(job.ended.load(), 1);
        auto transport = std::make_shared<Transport>(); done(dormant(uri, transport));
        QTRY_COMPARE(transport->canceled.load(), 1); QCOMPARE(transport->io.load(), 0); QTRY_COMPARE(f.preparationCanceled.load(), 1);
        done = {}; QTRY_COMPARE(transport->destroyed.load(), 1);
    }
    void leaseIsAttachedBeforeTheFirstIoOperation() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); auto transport = std::make_shared<Transport>();
        f.automatic(uri, transport); f.create(); NativeJob job(f.root(), uri, 1);
        bool canceledAtStart = false; transport->atStart = [&] { vlc_source_job_Cancel(job.value); canceledAtStart = transport->canceled.load() == 1; };
        QCOMPARE(job.start(), VLC_SUCCESS); QTRY_COMPARE(job.ended.load(), 1);
        QVERIFY(canceledAtStart); QCOMPARE(job.status.load(), -EINTR); QCOMPARE(transport->io.load(), 1); QVERIFY(!transport->current());
    }
    void changedConfigurationRejectsPreparedAndStartedResults() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); Resolver::PreparedCallback done;
        f.prepare = [&](const auto&, auto, auto callback) -> Resolver::Cancel { done = std::move(callback); return {}; };
        f.create(); NativeJob stale(f.root(), uri, 1); QCOMPARE(stale.start(), VLC_SUCCESS); QTRY_COMPARE(f.preparations.load(), 1);
        auto first = std::make_shared<Transport>(); ++f.revision; done(dormant(uri, first));
        QTRY_COMPARE(stale.ended.load(), 1); QCOMPARE(stale.status.load(), -EACCES); QCOMPARE(first->io.load(), 0);
        auto second = std::make_shared<Transport>(); second->inlineStart = false; f.automatic(uri, second);
        NativeJob afterIo(f.root(), uri, 2); QCOMPARE(afterIo.start(), VLC_SUCCESS); QTRY_COMPARE(second->io.load(), 1);
        ++f.revision; QVERIFY(second->current); QVERIFY(!second->current()); QVERIFY(second->started); second->started(VLC_SUCCESS);
        QTRY_COMPARE(afterIo.ended.load(), 1); QCOMPARE(afterIo.status.load(), -EACCES); QCOMPARE(second->canceled.load(), 1);
    }
    void coreCloseRevokesIssuedAndPendingWorkWithoutQtEvents() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); auto transport = std::make_shared<Transport>();
        f.automatic(uri, transport); f.create(); NativeJob issued(f.root(), uri, 1); QCOMPARE(issued.start(), VLC_SUCCESS); QTRY_COMPARE(issued.ended.load(), 1);
        Resolver::PreparedCallback done; f.prepare = [&](const auto&, auto, auto callback) -> Resolver::Cancel { done = std::move(callback); return {}; };
        NativeJob pending(f.root(), uri, 2); QCOMPARE(pending.start(), VLC_SUCCESS); QTRY_COMPARE(f.preparations.load(), 2);
        auto* backend = vlc_source_owner_Hold(f.root()); QVERIFY(backend);
        std::thread native([&] { vlc_source_backend_Close(backend); }); native.join(); vlc_source_backend_Release(backend);
        QCOMPARE(pending.ended.load(), 1); QCOMPARE(pending.status.load(), -EINTR); QCOMPARE(transport->canceled.load(), 1);
        QVERIFY(transport->current); QVERIFY(!transport->current()); QVERIFY(!f.owner->bound());
        auto late = std::make_shared<Transport>(); done(dormant(uri, late)); QTRY_COMPARE(late->canceled.load(), 1); QCOMPARE(late->io.load(), 0);
    }
    void sourceWithdrawalSelectsOnlyTheExactAccount() {
        Fixture f; const auto firstUri = f.uri(), secondUri = f.uri(QStringLiteral("account-2")); QVERIFY(f.approve()); QVERIFY(f.approve(QStringLiteral("account-2")));
        auto first = std::make_shared<Transport>(), second = std::make_shared<Transport>();
        f.automatic(firstUri, first); f.create(); NativeJob a(f.root(), firstUri, 1); QCOMPARE(a.start(), VLC_SUCCESS); QTRY_COMPARE(a.ended.load(), 1);
        f.automatic(secondUri, second); NativeJob b(f.root(), secondUri, 2); QCOMPARE(b.start(), VLC_SUCCESS); QTRY_COMPARE(b.ended.load(), 1);
        f.owner->withdraw(f.binding); QCOMPARE(first->canceled.load(), 1); QCOMPARE(second->canceled.load(), 0); QVERIFY(second->current); QVERIFY(second->current());
        QCOMPARE(f.owner->issuedCount(), 1u);
    }
    void hostLimitsRejectWorkAndPruneStoppedTransports() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); Resolver::PreparedCallback done;
        f.prepare = [&](const auto&, auto, auto callback) -> Resolver::Cancel { done = std::move(callback); return {}; };
        f.create({1,1}); NativeJob a(f.root(), uri, 1); QCOMPARE(a.start(), VLC_SUCCESS); QTRY_COMPARE(f.owner->pendingCount(), 1u);
        NativeJob b(f.root(), uri, 2); QCOMPARE(b.start(), VLC_SUCCESS); QTRY_COMPARE(b.ended.load(), 1); QCOMPARE(b.status.load(), VLC_ENOMEM);
        auto first = std::make_shared<Transport>(); done(dormant(uri, first)); QTRY_COMPARE(a.ended.load(), 1);
        NativeJob c(f.root(), uri, 3); QCOMPARE(c.start(), VLC_SUCCESS); QTRY_COMPARE(c.ended.load(), 1); QCOMPARE(c.status.load(), VLC_ENOMEM);
        vlc_source_job_Cancel(a.value); QCOMPARE(f.owner->issuedCount(), 0u);
        auto next = std::make_shared<Transport>(); f.automatic(uri, next); NativeJob d(f.root(), uri, 4);
        QCOMPARE(d.start(), VLC_SUCCESS); QTRY_COMPARE(d.ended.load(), 1); QCOMPARE(d.status.load(), VLC_SUCCESS);
    }
    void malformedProxyIsRejectedBeforeIo() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); auto transport = std::make_shared<Transport>();
        f.prepare = [&](const auto&, auto, auto done) -> Resolver::Cancel { done(dormant(uri, transport, "https://example.org/uncontained")); return {}; };
        f.create(); NativeJob job(f.root(), uri, 1); QCOMPARE(job.start(), VLC_SUCCESS); QTRY_COMPARE(job.ended.load(), 1);
        QCOMPARE(job.status.load(), -EACCES); QCOMPARE(transport->io.load(), 0); QCOMPARE(transport->canceled.load(), 1);
    }
    void destructionDuringPrepareCancelsTheReturnedWork() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); Resolver::PreparedCallback done;
        f.prepare = [&](const auto&, auto, auto callback) -> Resolver::Cancel { done = std::move(callback); f.owner.reset(); return [&] { ++f.preparationCanceled; }; };
        f.create(); NativeJob job(f.root(), uri, 1); QCOMPARE(job.start(), VLC_SUCCESS); QTRY_COMPARE(job.ended.load(), 1);
        QVERIFY(!f.owner); QCOMPARE(f.preparationCanceled.load(), 1); QCOMPARE(job.status.load(), -EINTR);
        auto late = std::make_shared<Transport>(); done(dormant(uri, late)); QCOMPARE(late->canceled.load(), 1); QCOMPARE(late->io.load(), 0);
    }
    void destructionDuringStartupCancelsTheReturnedWork() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); auto transport = std::make_shared<Transport>();
        f.prepare = [&](const auto&, auto, auto done) -> Resolver::Cancel {
            static const vlc_source_lease_operations ops{Lease::cancel, Lease::destroy};
            done(Resolver::Prepared::adopt("http://127.0.0.1:32123/native_token/main",
                vlc_input_policy_NewProxy(32123,"/native_token/",nullptr,false),
                vlc_source_lease_New(&ops,new Lease{transport}),transport->active,
                [&](auto, auto) -> Resolver::Cancel { f.owner.reset(); return [&] { ++f.preparationCanceled; }; })); return {};
        };
        f.create(); NativeJob job(f.root(), uri, 1); QCOMPARE(job.start(), VLC_SUCCESS); QTRY_COMPARE(job.ended.load(), 1);
        QVERIFY(!f.owner); QCOMPARE(job.status.load(), -EINTR); QCOMPARE(transport->canceled.load(), 1); QCOMPARE(f.preparationCanceled.load(), 1);
    }
    void closeDuringPrepareCancelsTheReturnedWork() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve());
        f.prepare = [&](const auto&, auto, auto) -> Resolver::Cancel { f.owner->close(); return [&] { ++f.preparationCanceled; }; };
        f.create(); NativeJob job(f.root(), uri, 1); QCOMPARE(job.start(), VLC_SUCCESS); QTRY_COMPARE(job.ended.load(), 1);
        QVERIFY(f.owner && !f.owner->bound()); QCOMPARE(job.status.load(), -EINTR); QCOMPARE(f.preparationCanceled.load(), 1);
    }
    void duplicateCallbacksDoNotStartASecondTransport() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); auto first = std::make_shared<Transport>(), duplicate = std::make_shared<Transport>();
        f.prepare = [&](const auto&, auto, auto done) -> Resolver::Cancel { done(dormant(uri, first)); done(dormant(uri, duplicate)); return {}; };
        f.create(); NativeJob job(f.root(), uri, 1); QCOMPARE(job.start(), VLC_SUCCESS); QTRY_COMPARE(job.ended.load(), 1);
        QCOMPARE(job.status.load(), VLC_SUCCESS); QCOMPARE(first->io.load(), 1); QCOMPARE(duplicate->io.load(), 0); QCOMPARE(duplicate->canceled.load(), 1);
        first->started(VLC_EGENERIC); QCoreApplication::sendPostedEvents(); QCOMPARE(job.ended.load(), 1); QCOMPARE(first->canceled.load(), 0);
    }
    void terminalCallbackCanDestroyTheOwnerAndRevokeTheResult() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); auto transport = std::make_shared<Transport>();
        f.automatic(uri, transport); f.create(); NativeJob job(f.root(), uri, 1);
        job.terminal = [&] { f.owner.reset(); };
        QCOMPARE(job.start(), VLC_SUCCESS); QTRY_COMPARE(job.ended.load(), 1); QVERIFY(!f.owner);
        QCOMPARE(job.status.load(), VLC_SUCCESS); QCOMPARE(transport->canceled.load(), 1);
        const auto* result = vlc_source_job_GetResult(job.value); QVERIFY(!result);
        QVERIFY(!transport->current());
    }
    void canceledDispatchFloodNeedsNoQtOrUnboundedNativeWait() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve()); f.create();
        for (quint64 generation = 1; generation <= 300; ++generation) {
            NativeJob job(f.root(), uri, generation); QCOMPARE(job.start(), VLC_SUCCESS); vlc_source_job_Cancel(job.value); QCOMPARE(job.ended.load(), 1);
        }
        QCOMPARE(f.admitted.load(), 0); f.owner.reset(); QCoreApplication::sendPostedEvents(); QCOMPARE(f.preparations.load(), 0);
    }
    void saturatedCancellationQueueCannotKeepAStalePendingJob() {
        Fixture f; const auto uri = f.uri(); QVERIFY(f.approve());
        f.prepare = [&](const auto&, auto, auto) -> Resolver::Cancel { return [&] { ++f.preparationCanceled; }; };
        f.create({1,1}); NativeJob active(f.root(), uri, 1); QCOMPARE(active.start(), VLC_SUCCESS); QTRY_COMPARE(f.preparations.load(), 1);
        for (quint64 generation = 2; generation <= 200; ++generation) {
            NativeJob job(f.root(), uri, generation); QCOMPARE(job.start(), VLC_SUCCESS); vlc_source_job_Cancel(job.value); QCOMPARE(job.ended.load(), 1);
        }
        vlc_source_job_Cancel(active.value); QCOMPARE(active.ended.load(), 1);
        QCoreApplication::sendPostedEvents();
        NativeJob next(f.root(), uri, 201); QCOMPARE(next.start(), VLC_SUCCESS); QTRY_COMPARE(f.preparations.load(), 2);
        QCOMPARE(f.preparationCanceled.load(), 1); QCOMPARE(f.owner->pendingCount(), 1u);
    }
};
QTEST_GUILESS_MAIN(TestAddonNativeSourceResolver)
#include "test_addon_native_source_resolver.moc"
