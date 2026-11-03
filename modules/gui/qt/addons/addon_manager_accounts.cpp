// SPDX-License-Identifier: GPL-2.0-or-later
// Account recipes, picked file and folder handles, and session settings.
#include "addon_manager.hpp"
#include "addon_package_configuration.hpp"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QScopeGuard>
#include <QUuid>

namespace vlc::addons {
namespace {
using packageConfiguration::secretField;
using packageConfiguration::valueValid;

QString refreshField(const QString& field) { return field + QStringLiteral(".refresh"); }
QString key(const QString& id, const QString& field) { return id + QLatin1Char('\n') + field; }
bool accountField(const QJsonObject& field)
{ return secretField(field) && field.value("x-hakone-control").toString() == QLatin1String("account"); }
QStringList origins(const InstalledPackage& package)
{
    QStringList result;
    for (const auto& value : package.manifest.permissionScope(QStringLiteral("network.http")).value("origins").toArray())
        result.append(value.toString());
    return result;
}
}

std::optional<DeviceRecipe> AddonManager::accountRecipe(const QString& id, const QString& field) const
{
    InstalledPackage package;
    if (!m_store.installed(id, &package)) return std::nullopt;
    const auto property = schema(package).value("properties").toObject().value(field).toObject();
    if (!accountField(property)) return std::nullopt;
    return DeviceRecipe::from(property.value("x-hakone-account").toObject(), origins(package));
}

bool AddonManager::connectAccount(const QString& id, const QString& field, const DeviceTokens& tokens, bool sessionOnly,
                                  QString* error)
{ return writeNow(accountWrite(id, field, tokens, sessionOnly, error, false)); }
QString AddonManager::requestConnectAccount(const QString& id, const QString& field, const DeviceTokens& tokens, bool sessionOnly)
{ return queueWrite(accountWrite(id, field, tokens, sessionOnly, nullptr, true)); }
AddonManager::StoreWrite AddonManager::accountWrite(const QString& id, const QString& field, const DeviceTokens& tokens,
                                                     bool sessionOnly, QString* error, bool report)
{
    struct State {
        std::shared_ptr<SecretsCopy> secrets;
        DeviceTokens tokens;
        QStringList created, previous;
        QString error;
        bool success = false;
    };
    const auto state = std::make_shared<State>();
    state->tokens = tokens;
    auto store = m_store;
    // The synchronous call reports through error; a request through errorMessage.
    const auto refuse = [this, error, report](const QString& message) {
        if (error) *error = message;
        return report ? changed(false, message) : false;
    };
    return {[this, state, id, refuse] {
        if (m_retiring || m_removingPackages.contains(id) || m_store.packageRetiring(id)) return refuse(tr("The add-on removal is in progress."));
        state->secrets = std::make_shared<SecretsCopy>(m_secrets);
        return true;
    }, [state, store, id, field, sessionOnly]() mutable {
        auto& secrets = state->secrets->store;
        auto& tokens = state->tokens;
        const auto wipe = qScopeGuard([&tokens] { tokens.accessToken.fill('\0'); tokens.refreshToken.fill('\0'); });
        const auto discard = [&] { for (const auto& reference : state->created) secrets.remove(id, reference); state->created.clear(); };
        InstalledPackage package;
        if (!store.installed(id, &package, &state->error)) return;
        const auto property = schema(package).value("properties").toObject().value(field).toObject();
        if (!accountField(property) || !DeviceRecipe::from(property.value("x-hakone-account").toObject(), origins(package))
            || tokens.accessToken.isEmpty()) { state->error = tr("The app cannot use this account."); return; }
        QJsonObject values; quint64 revision = 0;
        if (!store.settings(id, &values, &revision, &state->error)) return;
        const QString stamp = QString::fromLatin1(package.digest) + QLatin1Char(':') + QString::number(package.generation)
            + QLatin1Char(':') + QString::number(revision);
        auto references = values.value("_credentials").toObject();
        const QStringList previous{references.value(field).toString(), references.value(refreshField(field)).toString()};
        const auto access = secrets.create(id, field, tokens.accessToken, sessionOnly);
        if (access.reference.isEmpty()) {
            state->error = tr("The secure credential store is not available. Select temporary storage to keep the account until the app closes.");
            return;
        }
        state->created.append(access.reference);
        references.insert(field, access.reference);
        if (!tokens.refreshToken.isEmpty()) {
            const auto refresh = secrets.create(id, refreshField(field), tokens.refreshToken, !access.persistent);
            if (refresh.reference.isEmpty()) { discard(); state->error = tr("The app cannot keep this account."); return; }
            state->created.append(refresh.reference);
            references.insert(refreshField(field), refresh.reference);
        } else references.remove(refreshField(field));
        // A list, so that no ordinary settings key repeats a secret field name.
        QJsonArray accounts;
        for (const auto& value : values.value("_accounts").toArray())
            if (value.toObject().value("field").toString() != field) accounts.append(value);
        accounts.append(QJsonObject{{"field", field}, {"expiresAt", tokens.expiresIn > 0
            ? double(QDateTime::currentMSecsSinceEpoch() + tokens.expiresIn * 1000) : 0.0}});
        values.insert("_credentials", references);
        values.insert("_accounts", accounts);
        if (!store.change([&](QString* e) { return store.saveSettings(id, values, revision, stamp, e); },
                [&](const StoreProfileWriteContext& c, QString* e) { return store.saveSettings(c, id, values, revision, stamp, e); },
                &state->error)) { discard(); return; }
        // A new sign-in replaces the tokens; older settings generations that
        // named them are no longer usable for this account.
        for (const auto& reference : previous)
            if (!reference.isEmpty()) { secrets.remove(id, reference); state->previous.append(reference); }
        state->success = true;
    }, [this, state, id, field, refuse] {
        if (!state->success) return refuse(state->error);
        // The saved settings name the new keys, so keep them before anything can refuse.
        m_secrets.adopt(state->secrets->store, id, state->created);
        for (const auto& reference : state->previous) m_secrets.forget(id, reference);
        if (!withdraw(id)) return refuse(tr("The add-on removal is in progress."));
        m_accountExpiry.remove(key(id, field));
        changed(true);
        emit accountsChanged(id);
        return true;
    }};
}

DevicePost AddonManager::accountTransport()
{
    if (!m_accountPost) {
        m_accountNetwork = std::make_unique<QNetworkAccessManager>();
        m_accountPost = devicePost(m_accountNetwork.get());
    }
    return m_accountPost;
}

void AddonManager::setAccountTransport(DevicePost post) { m_accountPost = std::move(post); }

void AddonManager::requestAccountRecipe(const QString& id, const QString& field, std::function<void(std::optional<DeviceRecipe>)> done)
{
    // The package and its schema are read on background work.
    auto recipe = std::make_shared<std::optional<DeviceRecipe>>();
    auto store = m_store;
    StoreWrite read{[] { return true; }, [recipe, store, id, field] {
        InstalledPackage package;
        if (!store.installed(id, &package)) return;
        const auto property = schema(package).value("properties").toObject().value(field).toObject();
        if (accountField(property)) *recipe = DeviceRecipe::from(property.value("x-hakone-account").toObject(), origins(package));
    }, [recipe, done = std::move(done)] { done(*recipe); return true; }};
    if (m_inlineStore) writeNow(std::move(read));
    else queueRead(std::move(read));
}

void AddonManager::refreshAccounts(const QString& id, bool expiredOnly)
{
    if (m_retiring) return;
    // The package, its settings and the refresh tokens are read on
    // background work; the exchange starts here.
    struct Account { QString field; QJsonObject references; QByteArray token; std::optional<DeviceRecipe> recipe; double expiresAt = 0; };
    struct Read { CredentialStore::Reader secrets; QList<Account> accounts; ~Read() { for (auto& account : accounts) account.token.fill('\0'); } };
    auto found = std::make_shared<Read>();
    auto store = m_store;
    StoreWrite read{[this, found] { found->secrets = m_secrets.snapshotReader(); return true; }, [found, store, id] {
        InstalledPackage package;
        if (!store.installed(id, &package)) return;
        const auto properties = schema(package).value("properties").toObject();
        QJsonObject saved; store.settings(id, &saved);
        const auto references = saved.value("_credentials").toObject();
        QHash<QString, double> expiry;
        for (const auto& value : saved.value("_accounts").toArray())
            expiry.insert(value.toObject().value("field").toString(), value.toObject().value("expiresAt").toDouble());
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            if (!accountField(it.value().toObject())) continue;
            const auto reference = references.value(refreshField(it.key())).toString();
            if (reference.isEmpty()) continue;
            auto recipe = DeviceRecipe::from(it.value().toObject().value("x-hakone-account").toObject(), origins(package));
            auto token = found->secrets(id, reference);
            if (!recipe || token.isEmpty()) continue;
            found->accounts.append({it.key(), references, std::move(token), std::move(recipe), expiry.value(it.key())});
        }
        found->secrets = {};
    }, [this, found, id, expiredOnly] {
        if (m_retiring) return true;
        if (!m_accountClock.isValid()) m_accountClock.start();
        for (const auto& account : std::as_const(found->accounts)) {
            const auto field = account.field, entry = key(id, field);
            if (m_accountRefreshes.contains(entry)) continue;
            const double expiresAt = m_accountExpiry.contains(entry) ? double(m_accountExpiry.value(entry)) : account.expiresAt;
            if (expiredOnly && (expiresAt <= 0 || expiresAt > QDateTime::currentMSecsSinceEpoch())) continue;
            const auto tried = m_accountRefreshTried.constFind(entry);
            if (tried != m_accountRefreshTried.cend() && m_accountClock.elapsed() - *tried < 60000) continue;
            m_accountRefreshTried.insert(entry, m_accountClock.elapsed());
            const QPointer<AddonManager> owner(this);
            const auto references = account.references;
            // The callback can run inside post() for a synchronous transport.
            auto done = std::make_shared<bool>(false);
            auto cancel = DeviceAuthorization::refresh(*account.recipe, account.token, accountTransport(),
                [owner, id, field, entry, references, done](std::optional<DeviceTokens> tokens) {
                    *done = true;
                    if (!owner) return;
                    owner->m_accountRefreshes.remove(entry);
                    if (!tokens || owner->m_retiring) return;
                    owner->replaceAccountTokens(id, field, references, *tokens);
                });
            if (!*done) m_accountRefreshes.insert(entry, std::move(cancel));
        }
        return true;
    }};
    if (m_inlineStore) writeNow(std::move(read));
    else queueRead(std::move(read));
}
void AddonManager::replaceAccountTokens(const QString& id, const QString& field, const QJsonObject& references, const DeviceTokens& tokens)
{
    // The same references keep the account identity, so cached answers and
    // queued items of this account stay valid. The platform store is written
    // on background work.
    const auto access = references.value(field).toString(), refresh = references.value(refreshField(field)).toString();
    struct Change { std::shared_ptr<SecretsCopy> secrets; DeviceTokens tokens; bool replaced = false;
        ~Change() { tokens.accessToken.fill('\0'); tokens.refreshToken.fill('\0'); } };
    auto change = std::make_shared<Change>();
    change->tokens = tokens;
    StoreWrite write{[this, change] { change->secrets = std::make_shared<SecretsCopy>(m_secrets); return true; },
        [change, id, access, refresh] {
            auto& secrets = change->secrets->store;
            change->replaced = secrets.replace(id, access, change->tokens.accessToken);
            if (change->replaced && !change->tokens.refreshToken.isEmpty()) secrets.replace(id, refresh, change->tokens.refreshToken);
        }, [this, change, id, field, access, refresh] {
            if (m_retiring || !change->replaced || !change->secrets) return true;
            m_secrets.adopt(change->secrets->store, id, {access, refresh});
            m_accountExpiry.insert(key(id, field), change->tokens.expiresIn > 0
                ? QDateTime::currentMSecsSinceEpoch() + change->tokens.expiresIn * 1000 : 0);
            emit accountsChanged(id);
            return true;
        }};
    if (m_inlineStore) writeNow(std::move(write));
    else queueRead(std::move(write));
}

QVariantMap AddonManager::registerPickedPath(const QString& id, const QString& field, const QString& localPath)
{
    if (localPath.isEmpty()) return {};
    const auto property = querySchema(id).value("properties").toObject().value(field).toObject();
    const auto kind = property.value("x-hakone-control").toString();
    if ((kind != QLatin1String("file") && kind != QLatin1String("folder")) || property.value("type") != QStringLiteral("string"))
        return {};
    // Bounded: a settings window can pick many times before Save.
    if (m_pickedPaths.size() >= 64) m_pickedPaths.clear();
    const QString handle = QStringLiteral("handle:") + QUuid::createUuid().toString(QUuid::Id128);
    const auto name = QFileInfo(QDir::cleanPath(localPath)).fileName().left(255);
    m_pickedPaths.insert(handle, {id, field, kind, QDir::cleanPath(localPath), name.isEmpty() ? QDir::cleanPath(localPath).left(255) : name});
    return {{"handle", handle}, {"name", m_pickedPaths.value(handle).name}};
}

bool AddonManager::pickSettingsPath(const QString& id, const QString& field)
{
    if (!m_pathPicker) return false;
    const auto kind = querySchema(id).value("properties").toObject().value(field).toObject().value("x-hakone-control").toString();
    if (kind != QLatin1String("file") && kind != QLatin1String("folder")) return false;
    const QPointer<AddonManager> owner(this);
    m_pathPicker(kind, [owner, id, field](const QString& path) {
        if (!owner || path.isEmpty()) return;
        const auto picked = owner->registerPickedPath(id, field, path);
        if (!picked.isEmpty())
            emit owner->settingsPathPicked(id, field, picked.value("handle").toString(), picked.value("name").toString());
    });
    return true;
}

QVariantMap AddonManager::settingsHandleNames(const QString& id)
{
    QVariantMap names;
    const auto found = snapshot(id);
    const auto handles = found ? found->settings.value("_handles").toObject() : QJsonObject{};
    for (auto it = handles.begin(); it != handles.end(); ++it) names.insert(it.key(), it.value().toObject().value("name").toString());
    for (auto it = m_pickedPaths.cbegin(); it != m_pickedPaths.cend(); ++it)
        if (it->packageId == id) names.insert(it.key(), it->name);
    return names;
}

bool AddonManager::setSessionSettings(const QString& id, const QVariantMap& input)
{
    if (m_retiring || m_removingPackages.contains(id)) return false;
    const auto properties = querySchema(id).value("properties").toObject();
    if (properties.isEmpty() && !input.isEmpty()) return false;
    const auto values = QJsonObject::fromVariantMap(input);
    for (auto it = values.begin(); it != values.end(); ++it) {
        const auto field = properties.value(it.key()).toObject();
        const auto control = field.value("x-hakone-control").toString();
        // Credentials have their own session storage; a picked path must be saved.
        if (it.key().startsWith('_') || field.isEmpty() || secretField(field) || control == QLatin1String("file")
            || control == QLatin1String("folder") || !valueValid(it.value(), field)) return false;
    }
    if (input.isEmpty() ? !m_sessionSettings.contains(id) : m_sessionSettings.value(id) == input) return true;
    if (input.isEmpty()) m_sessionSettings.remove(id);
    else m_sessionSettings.insert(id, input);
    ++m_sessionGenerations[id];
    // Running workers keep their configuration; the next call starts again.
    if (!withdraw(id)) return false;
    return changed(true);
}

QVariantMap AddonManager::effectiveSettings(const QString& id) const
{
    auto values = settingsValues(id);
    const auto session = m_sessionSettings.value(id);
    for (auto it = session.cbegin(); it != session.cend(); ++it) values.insert(it.key(), it.value());
    return values;
}

QString AddonManager::sessionPartition(const QString& id) const
{
    return m_sessionSettings.contains(id) ? QStringLiteral(":session-") + QString::number(m_sessionGenerations.value(id)) : QString{};
}
}
