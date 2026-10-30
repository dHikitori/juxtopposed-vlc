// SPDX-License-Identifier: GPL-2.0-or-later
// Account recipes, picked file and folder handles, and session settings.
#include "addon_manager.hpp"
#include "addon_package_configuration.hpp"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QNetworkAccessManager>
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
{
    const auto refuse = [error](const QString& message) { if (error) *error = message; return false; };
    if (m_retiring || m_removingPackages.contains(id) || m_store.packageRetiring(id)) return refuse(tr("The add-on is being removed."));
    InstalledPackage package; QString reason;
    if (!m_store.installed(id, &package, &reason)) return refuse(reason);
    if (!accountRecipe(id, field) || tokens.accessToken.isEmpty()) return refuse(tr("The app cannot use this account."));
    QJsonObject values; quint64 revision = 0;
    if (!m_store.settings(id, &values, &revision, &reason)) return refuse(reason);
    const QString stamp = QString::fromLatin1(package.digest) + QLatin1Char(':') + QString::number(package.generation)
        + QLatin1Char(':') + QString::number(revision);
    auto references = values.value("_credentials").toObject();
    const QStringList previous{references.value(field).toString(), references.value(refreshField(field)).toString()};
    QStringList created;
    const auto discard = [&] { for (const auto& reference : created) m_secrets.remove(id, reference); };
    const auto access = m_secrets.create(id, field, tokens.accessToken, sessionOnly);
    if (access.reference.isEmpty())
        return refuse(tr("The secure credential store is not available. Select temporary storage to keep the account until the app closes."));
    created.append(access.reference);
    references.insert(field, access.reference);
    if (!tokens.refreshToken.isEmpty()) {
        const auto refresh = m_secrets.create(id, refreshField(field), tokens.refreshToken, !access.persistent);
        if (refresh.reference.isEmpty()) { discard(); return refuse(tr("The app cannot keep this account.")); }
        created.append(refresh.reference);
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
    if (!m_store.change([&](QString* e) { return m_store.saveSettings(id, values, revision, stamp, e); },
            [&](const StoreProfileWriteContext& c, QString* e) { return m_store.saveSettings(c, id, values, revision, stamp, e); },
            &reason)) { discard(); return refuse(reason); }
    if (!withdraw(id)) return refuse(tr("The add-on is being removed."));
    m_accountExpiry.remove(key(id, field));
    // A new sign-in replaces the tokens; older settings generations that
    // named them are no longer usable for this account.
    for (const auto& reference : previous) if (!reference.isEmpty()) m_secrets.remove(id, reference);
    changed(true);
    emit accountsChanged(id);
    return true;
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

void AddonManager::refreshAccounts(const QString& id, bool expiredOnly)
{
    if (m_retiring) return;
    InstalledPackage package;
    if (!m_store.installed(id, &package)) return;
    const auto properties = schema(package).value("properties").toObject();
    const auto saved = savedSettings(id);
    const auto references = saved.value("_credentials").toObject();
    QHash<QString, double> expiry;
    for (const auto& value : saved.value("_accounts").toArray())
        expiry.insert(value.toObject().value("field").toString(), value.toObject().value("expiresAt").toDouble());
    if (!m_accountClock.isValid()) m_accountClock.start();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (!accountField(it.value().toObject())) continue;
        const auto field = it.key(), entry = key(id, field);
        const auto reference = references.value(refreshField(field)).toString();
        if (reference.isEmpty() || m_accountRefreshes.contains(entry)) continue;
        const double expiresAt = m_accountExpiry.contains(entry) ? double(m_accountExpiry.value(entry))
            : expiry.value(field);
        if (expiredOnly && (expiresAt <= 0 || expiresAt > QDateTime::currentMSecsSinceEpoch())) continue;
        const auto tried = m_accountRefreshTried.constFind(entry);
        if (tried != m_accountRefreshTried.cend() && m_accountClock.elapsed() - *tried < 60000) continue;
        const auto recipe = accountRecipe(id, field);
        const auto token = m_secrets.read(id, reference);
        if (!recipe || token.isEmpty()) continue;
        m_accountRefreshTried.insert(entry, m_accountClock.elapsed());
        const QPointer<AddonManager> owner(this);
        // The callback can run inside post() for a synchronous transport.
        auto done = std::make_shared<bool>(false);
        auto cancel = DeviceAuthorization::refresh(*recipe, token, accountTransport(),
            [owner, id, field, entry, references, done](std::optional<DeviceTokens> tokens) {
                *done = true;
                if (!owner) return;
                owner->m_accountRefreshes.remove(entry);
                if (!tokens || owner->m_retiring) return;
                // The same references keep the account identity, so cached
                // answers and queued items of this account stay valid.
                const auto access = references.value(field).toString(), refresh = references.value(refreshField(field)).toString();
                if (!owner->m_secrets.replace(id, access, tokens->accessToken)) return;
                if (!tokens->refreshToken.isEmpty()) owner->m_secrets.replace(id, refresh, tokens->refreshToken);
                owner->m_accountExpiry.insert(entry, tokens->expiresIn > 0
                    ? QDateTime::currentMSecsSinceEpoch() + tokens->expiresIn * 1000 : 0);
                emit owner->accountsChanged(id);
            });
        if (!*done) m_accountRefreshes.insert(entry, std::move(cancel));
    }
}

QVariantMap AddonManager::registerPickedPath(const QString& id, const QString& field, const QString& localPath)
{
    InstalledPackage package;
    if (localPath.isEmpty() || !m_store.installed(id, &package)) return {};
    const auto property = schema(package).value("properties").toObject().value(field).toObject();
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
    InstalledPackage package;
    if (!m_pathPicker || !m_store.installed(id, &package)) return false;
    const auto kind = schema(package).value("properties").toObject().value(field).toObject().value("x-hakone-control").toString();
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

QVariantMap AddonManager::settingsHandleNames(const QString& id) const
{
    QVariantMap names;
    const auto handles = savedSettings(id).value("_handles").toObject();
    for (auto it = handles.begin(); it != handles.end(); ++it) names.insert(it.key(), it.value().toObject().value("name").toString());
    for (auto it = m_pickedPaths.cbegin(); it != m_pickedPaths.cend(); ++it)
        if (it->packageId == id) names.insert(it.key(), it->name);
    return names;
}

bool AddonManager::setSessionSettings(const QString& id, const QVariantMap& input)
{
    InstalledPackage package;
    if (m_retiring || m_removingPackages.contains(id) || !m_store.installed(id, &package)) return false;
    const auto properties = schema(package).value("properties").toObject();
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
