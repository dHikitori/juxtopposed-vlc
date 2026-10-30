// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_account_setup.hpp"
#include "addons/addon_device_authorization.hpp"
#include "addons/addon_manager.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>
using namespace vlc::addons;

namespace {
// A sign-in service: each URL answers from its own queue.
struct FakeService {
    struct Request { QUrl url; QUrlQuery form; };
    QList<Request> requests;
    QHash<QString, QList<std::pair<int, QByteArray>>> replies;
    int canceled = 0;
    void add(const QString& url, int status, const QJsonObject& body)
    { replies[url].append({status, QJsonDocument(body).toJson(QJsonDocument::Compact)}); }
    DevicePost post() {
        return [this](const QUrl& url, const QByteArray& form, std::function<void(int, const QByteArray&)> reply) -> std::function<void()> {
            requests.append({url, QUrlQuery(QString::fromLatin1(form))});
            auto queue = replies.find(url.toString());
            const auto answer = queue != replies.end() && !queue->isEmpty() ? queue->takeFirst() : std::pair<int, QByteArray>{0, {}};
            auto live = std::make_shared<bool>(true);
            QTimer::singleShot(0, [live, reply, answer] { if (*live) reply(answer.first, answer.second); });
            return [this, live] { *live = false; ++canceled; };
        };
    }
};
const QString Device = QStringLiteral("https://auth.example.org/device");
const QString Token = QStringLiteral("https://auth.example.org/token");
DeviceRecipe recipe()
{
    return *DeviceRecipe::from({{"recipe", "oauth-device"}, {"deviceAuthorizationUrl", Device}, {"tokenUrl", Token},
        {"clientId", "vlc-test"}, {"scopes", QJsonArray{"read", "write"}}}, {QStringLiteral("https://auth.example.org")});
}
QJsonObject code(int interval = 1)
{
    return {{"device_code", "device-1"}, {"user_code", "WDJB-MJHT"}, {"verification_uri", "https://example.org/device"},
        {"verification_uri_complete", "https://example.org/device?code=WDJB-MJHT"}, {"expires_in", 600}, {"interval", interval}};
}
QJsonObject tokens(const QString& access = QStringLiteral("access-1"), const QString& refresh = QStringLiteral("refresh-1"))
{
    QJsonObject result{{"access_token", access}, {"token_type", "Bearer"}, {"expires_in", 3600}};
    if (!refresh.isEmpty()) result.insert("refresh_token", refresh);
    return result;
}
}

class TestAddonAccounts : public QObject
{
    Q_OBJECT
    QTemporaryDir m_profile, m_package;
    const QString m_id = QStringLiteral("org.example.account");
    bool writePackage(const QJsonObject& accountRecipe)
    {
        const auto manifest = QJsonDocument::fromJson("{\"formatVersion\":1,\"id\":\"org.example.account\",\"name\":\"Account\",\"version\":\"1.0.0\","
            "\"publisher\":\"Tests\",\"license\":\"GPL-2.0-or-later\",\"source\":\"https://git.example.org/account.git\","
            "\"api\":{\"addon\":\">=1.0.0 <2.0.0\",\"contracts\":{\"discovery\":\"^1.0.0\",\"settings\":\"^1.0.0\"}},"
            "\"runtime\":{\"kind\":\"wasm-message-v1\",\"entry\":\"worker.wasm\"},\"files\":[\"addon.json\",\"LICENSE\",\"worker.wasm\",\"schema.json\"],"
            "\"contributions\":[{\"id\":\"source\",\"type\":\"discovery.source\",\"contract\":\"discovery@1\",\"operations\":[\"roots\",\"resolve\"],\"requires\":[\"network.http\",\"credentials.use\"]}],"
            "\"permissions\":{\"required\":{\"network.http\":{\"origins\":[\"https://api.example.org\",\"https://auth.example.org\"],\"methods\":[\"GET\",\"POST\"]},"
            "\"credentials.use\":{\"bindings\":[{\"field\":\"account\",\"origin\":\"https://api.example.org\",\"header\":\"Authorization\",\"prefix\":\"Bearer \"}]}},\"optional\":{}},"
            "\"settings\":{\"version\":1,\"schema\":\"schema.json\"}}").object();
        QJsonObject properties{{"account", QJsonObject{{"type", "string"}, {"x-hakone-secret", true}, {"x-hakone-control", "account"},
                {"x-hakone-account", accountRecipe}}},
            {"language", QJsonObject{{"type", "string"}, {"default", "en"}}},
            {"folder", QJsonObject{{"type", "string"}, {"x-hakone-control", "folder"}}}};
        const QJsonObject schema{{"type", "object"}, {"properties", properties}, {"additionalProperties", false}};
        const auto write = [this](const QString& path, const QByteArray& data) {
            QFile file(m_package.filePath(path)); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        return write("addon.json", QJsonDocument(manifest).toJson()) && write("LICENSE", "test license\n")
            && write("worker.wasm", QByteArray::fromHex("0061736d01000000")) && write("schema.json", QJsonDocument(schema).toJson());
    }
    QJsonObject goodRecipe() const
    { return {{"recipe", "oauth-device"}, {"deviceAuthorizationUrl", Device}, {"tokenUrl", Token}, {"clientId", "vlc-test"}}; }

private slots:
    void aRecipeNeedsDeclaredHttpsEndpoints_data()
    {
        QTest::addColumn<QJsonObject>("recipe"); QTest::addColumn<bool>("valid");
        const QJsonObject good{{"recipe", "oauth-device"}, {"deviceAuthorizationUrl", Device}, {"tokenUrl", Token}, {"clientId", "vlc-test"}};
        QTest::newRow("good") << good << true;
        auto plain = good; plain["tokenUrl"] = "http://auth.example.org/token"; QTest::newRow("http") << plain << false;
        auto other = good; other["tokenUrl"] = "https://elsewhere.example.org/token"; QTest::newRow("undeclared origin") << other << false;
        auto user = good; user["deviceAuthorizationUrl"] = "https://user@auth.example.org/device"; QTest::newRow("user data") << user << false;
        auto client = good; client["clientId"] = "has space"; QTest::newRow("client") << client << false;
        auto extra = good; extra["clientSecret"] = "x"; QTest::newRow("extra key") << extra << false;
        auto scopes = good; scopes["scopes"] = QJsonArray{"a", "a"}; QTest::newRow("scopes") << scopes << false;
        auto kind = good; kind["recipe"] = "oauth-code"; QTest::newRow("recipe") << kind << false;
    }
    void aRecipeNeedsDeclaredHttpsEndpoints()
    {
        QFETCH(QJsonObject, recipe); QFETCH(bool, valid);
        QCOMPARE(DeviceRecipe::from(recipe, {QStringLiteral("https://auth.example.org")}).has_value(), valid);
    }
    void theDeviceFlowWaitsForTheUserThenConnects()
    {
        FakeService service;
        service.add(Device, 200, code());
        service.add(Token, 400, {{"error", "authorization_pending"}});
        service.add(Token, 200, tokens());
        DeviceAuthorization flow(recipe(), service.post());
        QSignalSpy connected(&flow, &DeviceAuthorization::connected);
        flow.start();
        QTRY_COMPARE(flow.state(), DeviceAuthorization::State::Waiting);
        QCOMPARE(flow.userCode(), QStringLiteral("WDJB-MJHT"));
        QCOMPARE(flow.completeVerificationUrl(), QUrl("https://example.org/device?code=WDJB-MJHT"));
        QCOMPARE(service.requests.first().form.queryItemValue("client_id"), QStringLiteral("vlc-test"));
        QCOMPARE(service.requests.first().form.queryItemValue("scope", QUrl::FullyDecoded), QStringLiteral("read write"));
        QTRY_COMPARE_WITH_TIMEOUT(connected.size(), 1, 5000);
        QCOMPARE(flow.state(), DeviceAuthorization::State::Connected);
        const auto result = connected.first().first().value<DeviceTokens>();
        QCOMPARE(result.accessToken, QByteArray("access-1"));
        QCOMPARE(result.refreshToken, QByteArray("refresh-1"));
        QCOMPARE(result.expiresIn, 3600);
        QCOMPARE(service.requests.size(), 3);
        QCOMPARE(service.requests.last().form.queryItemValue("grant_type", QUrl::FullyDecoded),
                 QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
        QCOMPARE(service.requests.last().form.queryItemValue("device_code"), QStringLiteral("device-1"));
    }
    void slowDownWidensThePollInterval()
    {
        FakeService service;
        service.add(Device, 200, code());
        service.add(Token, 400, {{"error", "slow_down"}});
        DeviceAuthorization flow(recipe(), service.post());
        flow.start();
        QTRY_COMPARE(flow.state(), DeviceAuthorization::State::Waiting);
        QTRY_COMPARE_WITH_TIMEOUT(flow.pollIntervalMs(), 6000, 3000);
        flow.cancel();
        QCOMPARE(flow.state(), DeviceAuthorization::State::Canceled);
    }
    void refusalsAndBadRepliesFailWithTheAppsOwnText_data()
    {
        QTest::addColumn<int>("deviceStatus"); QTest::addColumn<QJsonObject>("device");
        QTest::addColumn<QJsonObject>("token"); QTest::addColumn<QString>("message");
        const QJsonObject described{{"error", "access_denied"}, {"error_description", "<b>Server text</b>"}};
        QTest::newRow("declined") << 200 << code() << described << QStringLiteral("declined");
        QTest::newRow("expired") << 200 << code() << QJsonObject{{"error", "expired_token"}} << QStringLiteral("expired");
        QTest::newRow("other error") << 200 << code() << QJsonObject{{"error", "invalid_client"}} << QStringLiteral("refused");
        auto plain = code(); plain["verification_uri"] = "http://example.org/device";
        QTest::newRow("plain page") << 200 << plain << QJsonObject{} << QStringLiteral("cannot use");
        auto noCode = code(); noCode.remove("device_code");
        QTest::newRow("no code") << 200 << noCode << QJsonObject{} << QStringLiteral("cannot use");
        QTest::newRow("refused") << 401 << code() << QJsonObject{} << QStringLiteral("cannot use");
        QTest::newRow("unreachable") << 0 << QJsonObject{} << QJsonObject{} << QStringLiteral("could not reach");
    }
    void refusalsAndBadRepliesFailWithTheAppsOwnText()
    {
        QFETCH(int, deviceStatus); QFETCH(QJsonObject, device); QFETCH(QJsonObject, token); QFETCH(QString, message);
        FakeService service;
        if (deviceStatus) service.add(Device, deviceStatus, device);
        service.add(Token, 400, token);
        DeviceAuthorization flow(recipe(), service.post());
        flow.start();
        QTRY_COMPARE_WITH_TIMEOUT(flow.state(), DeviceAuthorization::State::Failed, 5000);
        QVERIFY2(flow.message().contains(message), qPrintable(flow.message()));
        QVERIFY(!flow.message().contains("Server text"));
    }
    void cancelStopsPolling()
    {
        FakeService service;
        service.add(Device, 200, code());
        DeviceAuthorization flow(recipe(), service.post());
        flow.start();
        QTRY_COMPARE(flow.state(), DeviceAuthorization::State::Waiting);
        flow.cancel();
        QTest::qWait(1300);
        QCOMPARE(service.requests.size(), 1);
        QCOMPARE(flow.state(), DeviceAuthorization::State::Canceled);
    }
    void onlyBearerTokensOfVisibleTextAreAccepted()
    {
        const auto reply = [](QJsonObject body) { return QJsonDocument(body).toJson(); };
        QVERIFY(DeviceAuthorization::tokens(200, reply(tokens())));
        auto mac = tokens(); mac["token_type"] = "mac"; QVERIFY(!DeviceAuthorization::tokens(200, reply(mac)));
        auto spaced = tokens(); spaced["access_token"] = "a b"; QVERIFY(!DeviceAuthorization::tokens(200, reply(spaced)));
        auto line = tokens(); line["access_token"] = "a\r\nX: y"; QVERIFY(!DeviceAuthorization::tokens(200, reply(line)));
        QVERIFY(!DeviceAuthorization::tokens(400, reply(tokens())));
        QVERIFY(!DeviceAuthorization::tokens(200, QByteArray(DeviceAuthorization::MaxReplyBytes + 1, ' ')));
    }
    void aManifestAccountFieldNeedsAValidRecipe()
    {
        AddonManager manager(m_profile.filePath("refused"));
        auto bad = goodRecipe(); bad["tokenUrl"] = "https://elsewhere.example.org/token";
        QVERIFY(writePackage(bad));
        QVERIFY(!manager.installLocalPackage(m_package.path(), true));
        QVERIFY(manager.errorMessage().contains("network origins"));
    }
    void theManagerKeepsRefreshesAndDisconnectsAnAccount()
    {
        QVERIFY(writePackage(goodRecipe()));
        AddonManager manager(m_profile.filePath("addons"));
        QVERIFY2(manager.installLocalPackage(m_package.path(), true), qPrintable(manager.errorMessage()));
        FakeService service; manager.setAccountTransport(service.post());
        QVERIFY(manager.accountRecipe(m_id, "account"));
        QVERIFY(!manager.accountRecipe(m_id, "language"));
        // A token cannot be typed in: it comes only from the sign-in.
        QVERIFY(!manager.saveAddonSettings(m_id, {{"language", "en"}},
            {{"account", QVariantMap{{"operation", "replace"}, {"value", "typed"}, {"sessionOnly", true}}}}));

        AddonAccountSetup setup; setup.setManager(&manager);
        setup.setProperty("packageId", m_id); setup.setProperty("field", "account"); setup.setProperty("sessionOnly", true);
        service.add(Device, 200, code());
        service.add(Token, 200, tokens());
        QSignalSpy connected(&setup, &AddonAccountSetup::connected);
        QVERIFY(setup.start());
        QTRY_VERIFY2_WITH_TIMEOUT(connected.size() == 1, qPrintable(setup.state() + QLatin1Char(' ') + setup.message()), 5000);
        QCOMPARE(setup.state(), QStringLiteral("connected"));
        auto status = manager.settingsSecretStatus(m_id).value("account").toMap();
        QVERIFY(status.value("present").toBool()); QVERIFY(status.value("account").toBool()); QVERIFY(status.value("sessionOnly").toBool());
        const auto partition = manager.contributionsOfType(QStringLiteral("discovery.source"), true).first().toMap().value("accountReference");

        // A provider asks for sign-in: the saved refresh token is exchanged once.
        service.add(Token, 200, tokens(QStringLiteral("access-2"), {}));
        const auto before = service.requests.size();
        QSignalSpy refreshed(&manager, &AddonManager::accountsChanged);
        manager.refreshAccounts(m_id);
        QTRY_COMPARE(service.requests.size(), before + 1);
        QCOMPARE(service.requests.last().form.queryItemValue("grant_type"), QStringLiteral("refresh_token"));
        QCOMPARE(service.requests.last().form.queryItemValue("refresh_token"), QStringLiteral("refresh-1"));
        QTRY_COMPARE(refreshed.size(), 1);
        // The account identity stays: queued items and cached answers remain valid.
        QCOMPARE(manager.contributionsOfType(QStringLiteral("discovery.source"), true).first().toMap().value("accountReference"), partition);
        QVERIFY(manager.settingsSecretStatus(m_id).value("account").toMap().value("present").toBool());
        manager.refreshAccounts(m_id);
        QTest::qWait(50);
        QCOMPARE(service.requests.size(), before + 1);

        // Disconnect removes the access and refresh tokens together.
        QVERIFY(manager.saveAddonSettings(m_id, {{"language", "en"}}, {{"account", QVariantMap{{"operation", "delete"}}}}));
        status = manager.settingsSecretStatus(m_id).value("account").toMap();
        QVERIFY(!status.value("present").toBool());
    }
    void aPickedFolderIsAHandleUntilSaved()
    {
        QVERIFY(writePackage(goodRecipe()));
        AddonManager manager(m_profile.filePath("pickers"));
        QVERIFY2(manager.installLocalPackage(m_package.path(), true), qPrintable(manager.errorMessage()));
        QVERIFY(manager.registerPickedPath(m_id, "language", "/tmp").isEmpty());
        const auto picked = manager.registerPickedPath(m_id, "folder", m_profile.path() + QStringLiteral("/My Music"));
        const auto handle = picked.value("handle").toString();
        QVERIFY(handle.startsWith("handle:")); QCOMPARE(picked.value("name").toString(), QStringLiteral("My Music"));
        QVERIFY(!manager.saveAddonSettings(m_id, {{"language", "en"}, {"folder", m_profile.path()}}, {}));
        QVERIFY(!manager.saveAddonSettings(m_id, {{"language", "en"}, {"folder", "handle:00000000000000000000000000000000"}}, {}));
        QVERIFY(manager.saveAddonSettings(m_id, {{"language", "en"}, {"folder", handle}}, {}));
        QCOMPARE(manager.settingsValues(m_id).value("folder").toString(), handle);
        QCOMPARE(manager.settingsHandleNames(m_id).value(handle).toString(), QStringLiteral("My Music"));
        QVERIFY(!QJsonDocument::fromVariant(manager.settingsValues(m_id)).toJson().contains("My Music"));
        // A saved handle stays valid; clearing removes it.
        QVERIFY(manager.saveAddonSettings(m_id, {{"language", "fr"}, {"folder", handle}}, {}));
        QVERIFY(manager.saveAddonSettings(m_id, {{"language", "fr"}, {"folder", ""}}, {}));
        QVERIFY(manager.settingsHandleNames(m_id).isEmpty());
        QVERIFY(!manager.saveAddonSettings(m_id, {{"language", "fr"}, {"folder", handle}}, {}));
    }
    void sessionSettingsOverlayTheSavedOnesUntilCleared()
    {
        QVERIFY(writePackage(goodRecipe()));
        AddonManager manager(m_profile.filePath("session"));
        QVERIFY2(manager.installLocalPackage(m_package.path(), true), qPrintable(manager.errorMessage()));
        QVERIFY(manager.saveAddonSettings(m_id, {{"language", "en"}}, {}));
        const auto partition = [&] { return manager.contributionsOfType(QStringLiteral("discovery.source"), true).first().toMap().value("cachePartition").toString(); };
        const auto saved = partition();
        QVERIFY(!manager.setSessionSettings(m_id, {{"account", "token"}}));
        QVERIFY(!manager.setSessionSettings(m_id, {{"folder", "/tmp"}}));
        QVERIFY(!manager.setSessionSettings(m_id, {{"language", 4}}));
        QVERIFY(manager.setSessionSettings(m_id, {{"language", "de"}}));
        QCOMPARE(manager.effectiveSettings(m_id).value("language").toString(), QStringLiteral("de"));
        QCOMPARE(manager.settingsValues(m_id).value("language").toString(), QStringLiteral("en"));
        QVERIFY(partition() != saved);
        QVERIFY(manager.setSessionSettings(m_id, {}));
        QCOMPARE(manager.effectiveSettings(m_id).value("language").toString(), QStringLiteral("en"));
        QCOMPARE(partition(), saved);
    }
};
QTEST_GUILESS_MAIN(TestAddonAccounts)
#include "test_addon_accounts.moc"
