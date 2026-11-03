// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_registry_types.hpp"
#include "addon_manifest.hpp"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <cmath>

namespace vlc::addons {
namespace {
bool fail(QString* error, const QString& message)
{ if (error) *error = message; return false; }

bool packageId(const QString& id)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+\\z"));
    return id.size() <= 128 && pattern.match(id).hasMatch();
}
bool hex(const QString& value, qsizetype size)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]+\\z"));
    return value.size() == size && pattern.match(value).hasMatch();
}

// Reads one object's fields. The first wrong field is kept for the error.
struct Check
{
    bool ok = true;
    QString field;
    bool allowHttpLinks = false;

    void fail(const QString& name) { if (ok) { ok = false; field = name; } }
    static bool missing(const QJsonValue& value) { return value.isNull() || value.isUndefined(); }

    // Control characters become spaces; multiline text keeps its newlines.
    QString text(const QJsonObject& object, const QString& key, qsizetype maximum,
                 bool required = true, bool multiline = false)
    {
        const auto value = object.value(key);
        if (missing(value)) { if (required) fail(key); return {}; }
        if (!value.isString() || value.toString().size() > maximum) { fail(key); return {}; }
        QString result = value.toString();
        for (auto& character : result) {
            const auto code = character.unicode();
            if ((code < 0x20 && !(multiline && code == '\n')) || (code >= 0x7f && code < 0xa0))
                character = QLatin1Char(' ');
        }
        return result;
    }
    bool boolean(const QJsonObject& object, const QString& key)
    {
        const auto value = object.value(key);
        if (!value.isBool()) fail(key);
        return value.toBool();
    }
    qint64 integer(const QJsonObject& object, const QString& key, qint64 maximum)
    {
        const auto value = object.value(key);
        const double number = value.toDouble(-1);
        if (!value.isDouble() || number < 0 || number > double(maximum) || std::floor(number) != number) { fail(key); return 0; }
        return qint64(number);
    }
    QString version(const QJsonObject& object, const QString& key, bool required = true)
    {
        const auto value = text(object, key, 128, required);
        if (!value.isEmpty() && !SemanticVersion::parse(value, nullptr)) fail(key);
        return value;
    }
    QString range(const QJsonObject& object, const QString& key)
    {
        const auto value = text(object, key, 256);
        if (ok && value.trimmed().isEmpty()) fail(key);
        return value;
    }
    QDateTime time(const QJsonObject& object, const QString& key, bool required = true)
    {
        const auto value = text(object, key, 64, required);
        if (value.isEmpty()) return {};
        const auto result = QDateTime::fromString(value, Qt::ISODate);
        if (!result.isValid()) fail(key);
        return result;
    }
    QJsonObject jsonObject(const QJsonObject& object, const QString& key, bool required = true)
    {
        const auto value = object.value(key);
        if (missing(value)) { if (required) fail(key); return {}; }
        if (!value.isObject()) fail(key);
        return value.toObject();
    }
    // A map of names to version ranges, such as {"metadata": "^1.0.0"}.
    QJsonObject ranges(const QJsonObject& object, const QString& key, bool required = true)
    {
        const auto result = jsonObject(object, key, required);
        if (result.size() > 64) fail(key);
        for (auto it = result.begin(); it != result.end(); ++it)
            if (it.key().size() > 64 || !it.value().isString() || it.value().toString().size() > 256) fail(key);
        return result;
    }
    QStringList strings(const QJsonObject& object, const QString& key, qsizetype maximumItems, qsizetype maximumLength)
    {
        const auto value = object.value(key);
        if (!value.isArray() || value.toArray().size() > maximumItems) { fail(key); return {}; }
        QStringList result;
        for (const auto& item : value.toArray()) {
            if (!item.isString() || item.toString().isEmpty() || item.toString().size() > maximumLength) { fail(key); return {}; }
            result.append(item.toString());
        }
        return result;
    }
    // A link to open in the browser. A wrong link becomes empty: it never
    // makes the answer wrong.
    QString link(const QJsonObject& object, const QString& key)
    {
        const auto value = object.value(key);
        if (!value.isString() || value.toString().size() > 2048) return {};
        const QUrl url(value.toString(), QUrl::StrictMode);
        const bool scheme = url.scheme() == QLatin1String("https") || (allowHttpLinks && url.scheme() == QLatin1String("http"));
        if (!url.isValid() || !scheme || url.host().isEmpty() || !url.userInfo().isEmpty()) return {};
        return url.toString();
    }
};

// The Git identity the app checks after its own fetch.
RegistryInstall install(const QJsonObject& object, const QString& version, const QString& source, Check& check)
{
    RegistryInstall result;
    result.source = source.isEmpty() ? check.text(object, QStringLiteral("source"), 2048) : source;
    if (check.ok && !httpsSource(result.source)) check.fail(QStringLiteral("source"));
    result.tag = check.text(object, QStringLiteral("tag"), 128);
    QString tagVersion = result.tag;
    if (tagVersion.startsWith(QLatin1Char('v'))) tagVersion.remove(0, 1);
    if (check.ok && (result.tag.contains(QLatin1Char('/')) || result.tag.startsWith(QLatin1Char('-')) || tagVersion != version))
        check.fail(QStringLiteral("tag"));
    result.objectFormat = check.text(object, QStringLiteral("objectFormat"), 16);
    const qsizetype width = result.objectFormat == QLatin1String("sha1") ? 40 : result.objectFormat == QLatin1String("sha256") ? 64 : 0;
    if (check.ok && !width) check.fail(QStringLiteral("objectFormat"));
    result.tagObjectId = check.text(object, QStringLiteral("tagObjectId"), 64);
    if (check.ok && !hex(result.tagObjectId, width)) check.fail(QStringLiteral("tagObjectId"));
    result.commitId = check.text(object, QStringLiteral("commitId"), 64);
    if (check.ok && !hex(result.commitId, width)) check.fail(QStringLiteral("commitId"));
    const auto digest = check.text(object, QStringLiteral("digest"), 64);
    if (check.ok && !hex(digest, 64)) check.fail(QStringLiteral("digest"));
    result.digest = digest.toLatin1();
    return result;
}

void prereleaseAgrees(RegistryRelease& release, Check& check)
{
    SemanticVersion parsed;
    if (check.ok && (!SemanticVersion::parse(release.version, &parsed) || release.prerelease != !parsed.prerelease.isEmpty()))
        check.fail(QStringLiteral("prerelease"));
}

// The compact release in /v1/index: version, identity and compatibility.
RegistryRelease indexRelease(const QJsonObject& object, const QString& id, const QString& source, Check& check)
{
    RegistryRelease result;
    result.addonId = id;
    result.state = QStringLiteral("published");
    result.version = check.version(object, QStringLiteral("version"));
    result.prerelease = check.boolean(object, QStringLiteral("prerelease"));
    result.addonApi = check.range(object, QStringLiteral("addonApi"));
    result.contracts = check.ranges(object, QStringLiteral("contracts"));
    result.install = install(object, result.version, source, check);
    prereleaseAgrees(result, check);
    return result;
}

RegistryRelease fullRelease(const QJsonObject& object, Check& check)
{
    RegistryRelease result;
    result.addonId = check.text(object, QStringLiteral("addonId"), 128);
    if (check.ok && !packageId(result.addonId)) check.fail(QStringLiteral("addonId"));
    result.version = check.version(object, QStringLiteral("version"));
    result.prerelease = check.boolean(object, QStringLiteral("prerelease"));
    result.state = check.text(object, QStringLiteral("state"), 16);
    if (check.ok && result.state != QLatin1String("published") && result.state != QLatin1String("yanked"))
        check.fail(QStringLiteral("state"));
    result.yankReason = check.text(object, QStringLiteral("yankReason"), 280, false);
    result.publishedAt = check.time(object, QStringLiteral("publishedAt"), false);
    result.install = install(check.jsonObject(object, QStringLiteral("install")), result.version, {}, check);
    result.addonApi = check.range(object, QStringLiteral("addonApi"));
    result.contracts = check.ranges(object, QStringLiteral("contracts"));
    result.runtime = check.boolean(object, QStringLiteral("runtime"));
    result.permissions = check.jsonObject(object, QStringLiteral("permissions"));
    const auto contributions = object.value(QStringLiteral("contributions"));
    if (!contributions.isArray() || contributions.toArray().size() > 256) check.fail(QStringLiteral("contributions"));
    for (const auto& item : contributions.toArray()) {
        const auto contribution = item.toObject();
        QJsonObject kept{{QStringLiteral("id"), check.text(contribution, QStringLiteral("id"), 128)},
                         {QStringLiteral("type"), check.text(contribution, QStringLiteral("type"), 64)}};
        const auto name = check.text(contribution, QStringLiteral("name"), 128, false);
        if (!name.isEmpty()) kept.insert(QStringLiteral("name"), name);
        result.contributions.append(kept);
    }
    result.locales = check.strings(object, QStringLiteral("locales"), 128, 35);
    result.packageBytes = check.integer(object, QStringLiteral("packageBytes"), qint64(64) * 1024 * 1024);
    result.changelog = check.text(object, QStringLiteral("changelog"), 65536, false, true);
    result.sourceUrl = check.link(object, QStringLiteral("sourceUrl"));
    result.webUrl = check.link(object, QStringLiteral("webUrl"));
    prereleaseAgrees(result, check);
    return result;
}

RegistryAddon summary(const QJsonObject& object, Check& check)
{
    RegistryAddon result;
    result.id = check.text(object, QStringLiteral("id"), 128);
    if (check.ok && !packageId(result.id)) check.fail(QStringLiteral("id"));
    result.name = check.text(object, QStringLiteral("name"), 128);
    result.summary = check.text(object, QStringLiteral("summary"), 160, false);
    const auto publisher = check.jsonObject(object, QStringLiteral("publisher"));
    result.publisherName = check.text(publisher, QStringLiteral("name"), 128);
    result.publisherSlug = check.text(publisher, QStringLiteral("slug"), 128);
    result.publisherVerified = check.boolean(publisher, QStringLiteral("verified"));
    result.categories = check.strings(object, QStringLiteral("categories"), 32, 64);
    result.contributionTypes = check.strings(object, QStringLiteral("contributionTypes"), 32, 64);
    result.license = check.text(object, QStringLiteral("license"), 128);
    const auto latest = object.value(QStringLiteral("latest"));
    if (latest.isObject()) {
        const auto value = latest.toObject();
        result.latestVersion = check.version(value, QStringLiteral("version"));
        result.latestAddonApi = check.range(value, QStringLiteral("addonApi"));
        result.latestPrerelease = check.boolean(value, QStringLiteral("prerelease"));
    } else if (!latest.isNull()) {
        check.fail(QStringLiteral("latest"));
    }
    result.featured = check.boolean(object, QStringLiteral("featured"));
    result.updatedAt = check.time(object, QStringLiteral("updatedAt"), false);
    result.webUrl = check.link(object, QStringLiteral("webUrl"));
    return result;
}

std::optional<QJsonObject> root(const QByteArray& bytes, qsizetype maximum, QString* error)
{
    if (bytes.size() > maximum) { fail(error, QStringLiteral("The registry answer is too large.")); return std::nullopt; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        fail(error, QStringLiteral("The registry answer is not JSON."));
        return std::nullopt;
    }
    return document.object();
}
bool finish(const Check& check, QString* error)
{
    if (check.ok) { if (error) error->clear(); return true; }
    return fail(error, QStringLiteral("The registry answer is not correct (%1).").arg(check.field));
}
QVariantMap releaseVariant(const RegistryRelease& release)
{
    return {{"addonId", release.addonId}, {"version", release.version}, {"prerelease", release.prerelease},
            {"state", release.state}, {"yanked", release.yanked()}, {"yankReason", release.yankReason},
            {"publishedAt", release.publishedAt}, {"addonApi", release.addonApi},
            {"compatible", hostAcceptsApi(release.addonApi, release.contracts)},
            {"permissions", release.permissions.toVariantMap()}, {"contributions", release.contributions.toVariantList()},
            {"runtime", release.runtime}, {"packageBytes", release.packageBytes}, {"changelog", release.changelog},
            {"source", release.install.source}, {"tag", release.install.tag}, {"commitId", release.install.commitId},
            {"digest", QString::fromLatin1(release.install.digest)}, {"sourceUrl", release.sourceUrl}, {"webUrl", release.webUrl}};
}
} // namespace

QVariantMap RegistryRelease::toVariant() const { return releaseVariant(*this); }

QVariantMap RegistryAddon::toVariant() const
{
    QVariantMap result{{"id", id}, {"name", name}, {"summary", summary}, {"license", license},
        {"publisher", publisherName}, {"publisherVerified", publisherVerified}, {"categories", categories},
        {"contributionTypes", contributionTypes}, {"latestVersion", latestVersion}, {"latestPrerelease", latestPrerelease},
        {"compatible", !latestAddonApi.isEmpty() && versionSatisfies(QLatin1String(HostAddonApiVersion), latestAddonApi)},
        {"featured", featured}, {"updatedAt", updatedAt}, {"webUrl", webUrl}, {"source", source},
        {"homepageUrl", homepageUrl}, {"description", description}};
    if (latestRelease) result.insert(QStringLiteral("latestRelease"), latestRelease->toVariant());
    return result;
}

const RegistryIndexEntry* RegistryIndex::find(const QString& id) const
{
    for (const auto& entry : addons)
        if (entry.id == id) return &entry;
    return nullptr;
}

namespace registry {
bool parseIndex(const QByteArray& bytes, RegistryIndex* output, QString* error)
{
    const auto object = root(bytes, MaximumIndexBytes, error);
    if (!object) return false;
    Check check;
    RegistryIndex result;
    result.revision = check.integer(*object, QStringLiteral("revision"), qint64(1) << 53);
    const auto addons = object->value(QStringLiteral("addons"));
    if (!addons.isArray() || addons.toArray().size() > MaximumIndexEntries) check.fail(QStringLiteral("addons"));
    QSet<QString> seen;
    for (const auto& item : addons.toArray()) {
        if (!check.ok) break;
        const auto value = item.toObject();
        RegistryIndexEntry entry;
        entry.id = check.text(value, QStringLiteral("id"), 128);
        if (check.ok && (!packageId(entry.id) || seen.contains(entry.id))) check.fail(QStringLiteral("id"));
        seen.insert(entry.id);
        entry.source = check.text(value, QStringLiteral("source"), 2048);
        if (check.ok && !httpsSource(entry.source)) check.fail(QStringLiteral("source"));
        const auto releases = value.value(QStringLiteral("releases"));
        if (!releases.isArray() || releases.toArray().size() > MaximumIndexReleases) check.fail(QStringLiteral("releases"));
        for (const auto& release : releases.toArray())
            entry.releases.append(indexRelease(release.toObject(), entry.id, entry.source, check));
        entry.yanked = check.strings(value, QStringLiteral("yanked"), 1024, 128);
        result.addons.append(entry);
    }
    if (!finish(check, error)) return false;
    if (output) *output = std::move(result);
    return true;
}

bool parsePage(const QByteArray& bytes, RegistryPage* output, bool allowHttpLinks, QString* error)
{
    const auto object = root(bytes, MaximumResponseBytes, error);
    if (!object) return false;
    Check check; check.allowHttpLinks = allowHttpLinks;
    RegistryPage result;
    const auto items = object->value(QStringLiteral("items"));
    if (!items.isArray() || items.toArray().size() > MaximumPageItems) check.fail(QStringLiteral("items"));
    for (const auto& item : items.toArray()) result.items.append(summary(item.toObject(), check));
    result.next = check.text(*object, QStringLiteral("next"), 1024, false);
    if (!finish(check, error)) return false;
    if (output) *output = std::move(result);
    return true;
}

bool parseAddon(const QByteArray& bytes, RegistryAddon* output, bool allowHttpLinks, QString* error)
{
    const auto object = root(bytes, MaximumResponseBytes, error);
    if (!object) return false;
    Check check; check.allowHttpLinks = allowHttpLinks;
    RegistryAddon result = summary(*object, check);
    result.source = check.text(*object, QStringLiteral("source"), 2048);
    if (check.ok && !httpsSource(result.source)) check.fail(QStringLiteral("source"));
    result.homepageUrl = check.link(*object, QStringLiteral("homepageUrl"));
    result.description = check.text(*object, QStringLiteral("description"), 65536, false, true);
    result.state = check.text(*object, QStringLiteral("state"), 16);
    const auto latest = object->value(QStringLiteral("latestRelease"));
    if (latest.isObject()) {
        result.latestRelease = fullRelease(latest.toObject(), check);
        if (check.ok && (result.latestRelease->addonId != result.id || result.latestRelease->install.source != result.source))
            check.fail(QStringLiteral("latestRelease"));
    } else if (!latest.isNull()) {
        check.fail(QStringLiteral("latestRelease"));
    }
    if (!finish(check, error)) return false;
    if (output) *output = std::move(result);
    return true;
}

bool parseRelease(const QByteArray& bytes, RegistryRelease* output, bool allowHttpLinks, QString* error)
{
    const auto object = root(bytes, MaximumResponseBytes, error);
    if (!object) return false;
    Check check; check.allowHttpLinks = allowHttpLinks;
    RegistryRelease result = fullRelease(*object, check);
    if (!finish(check, error)) return false;
    if (output) *output = std::move(result);
    return true;
}

QString errorCode(const QByteArray& bytes)
{
    if (bytes.size() > MaximumResponseBytes) return {};
    const auto code = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("error")).toObject().value(QStringLiteral("code"));
    return code.isString() && code.toString().size() <= 64 ? code.toString() : QString{};
}
} // namespace registry
} // namespace vlc::addons
