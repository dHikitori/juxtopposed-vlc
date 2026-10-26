// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_manifest.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vlc::addons {
namespace {
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
bool keys(const QJsonObject& object, const QSet<QString>& allowed, QString* error, const QString& context)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!allowed.contains(it.key())) return fail(error, context + QStringLiteral(": unknown field ") + it.key());
    return true;
}
bool text(const QJsonObject& object, const QString& key, QString* output, QString* error, bool required = true)
{
    const auto value = object.value(key);
    if (!required && value.isUndefined()) return true;
    if (!value.isString() || value.toString().isEmpty() || value.toString().size() > 4096 || value.toString().contains(QChar::Null))
        return fail(error, QStringLiteral("Invalid text field: ") + key);
    *output = value.toString();
    return true;
}
bool strings(const QJsonValue& value, QStringList* output, QString* error, const QString& context, bool required = false)
{
    if (value.isUndefined() && !required) return true;
    if (!value.isArray() || value.toArray().size() > 256) return fail(error, context + QStringLiteral(": expected bounded array"));
    QSet<QString> seen;
    for (const auto& item : value.toArray()) {
        if (!item.isString() || item.toString().isEmpty() || item.toString().size() > 4096 || seen.contains(item.toString()))
            return fail(error, context + QStringLiteral(": invalid or duplicate value"));
        seen.insert(item.toString()); output->append(item.toString());
    }
    return true;
}
bool positiveInteger(const QJsonValue& value, int* output)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (number < 1 || number > std::numeric_limits<int>::max() || std::floor(number) != number) return false;
    *output = int(number); return true;
}
bool safeSource(const QUrl& url)
{
    return url.isValid() && url.scheme() == QStringLiteral("https") && !url.host().isEmpty()
        && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment();
}
QString originForUrl(const QUrl& url)
{
    QUrl result;
    result.setScheme(url.scheme()); result.setHost(url.host().toLower());
    if (url.port() != -1 && url.port() != 443) result.setPort(url.port());
    return result.toString(QUrl::FullyEncoded);
}
bool permissions(const QJsonObject& value, QString* error)
{
    static const QMap<QString, QSet<QString>> fields = {
        {"metadata.context", {"fields"}}, {"media.fingerprint", {"algorithms"}},
        {"network.http", {"origins", "methods"}}, {"network.websocket", {"origins"}},
        {"credentials.use", {"bindings"}}, {"storage.private", {"quotaBytes"}},
        {"library.read", {"fields", "collections"}}, {"queue.read", {}},
        {"queue.modify", {"operations"}}, {"playback.observe", {"fields"}},
        {"playback.control", {"operations"}}, {"audio.analysis", {"channels"}},
        {"ui.contribute", {"hooks"}}, {"session.background", {}}
    };
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!fields.contains(it.key()) || !it.value().isObject()) return fail(error, QStringLiteral("Unsupported permission: ") + it.key());
        const auto scope = it.value().toObject();
        if (!keys(scope, fields.value(it.key()), error, it.key())) return false;
        for (auto item = scope.begin(); item != scope.end(); ++item) {
            if (item.key() == QStringLiteral("quotaBytes")) {
                const auto n = item.value().toDouble(-1);
                if (n < 0 || n > 100 * 1024 * 1024 || std::floor(n) != n) return fail(error, QStringLiteral("Invalid storage quota"));
            } else if (item.key() != QStringLiteral("bindings")) {
                QStringList list;
                if (!strings(item.value(), &list, error, it.key() + QLatin1Char('.') + item.key(), true)) return false;
                if (item.key() == QStringLiteral("origins")) {
                    if (list.isEmpty()) return fail(error, QStringLiteral("Network permission needs explicit origins"));
                    for (const auto& origin : list) {
                        if (it.key() == QStringLiteral("network.websocket")) {
                            QUrl url(origin); const auto scheme = url.scheme(); url.setScheme(QStringLiteral("https"));
                            if (scheme != QStringLiteral("wss") || exactHttpsOrigin(url.toString()).isEmpty()) return fail(error, QStringLiteral("Invalid WSS origin"));
                        } else if (exactHttpsOrigin(origin).isEmpty()) return fail(error, QStringLiteral("Invalid HTTPS origin"));
                    }
                }
                if (item.key() == QStringLiteral("methods"))
                    for (const auto& method : list)
                        if (method != QStringLiteral("GET") && method != QStringLiteral("POST"))
                            return fail(error, QStringLiteral("Unsupported HTTP method"));
            }
        }
        if ((it.key().startsWith(QStringLiteral("network."))) && !scope.contains(QStringLiteral("origins")))
            return fail(error, QStringLiteral("Network permission needs explicit origins"));
        if (it.key() == QStringLiteral("credentials.use")) {
            if (!scope.value(QStringLiteral("bindings")).isArray() || scope.value(QStringLiteral("bindings")).toArray().size() > 16)
                return fail(error, QStringLiteral("Invalid credential bindings"));
            QSet<QString> names;
            for (const auto& item : scope.value(QStringLiteral("bindings")).toArray()) {
                if (!item.isObject()) return fail(error, QStringLiteral("Invalid credential binding"));
                const auto binding = item.toObject(); QString field, origin, header;
                if (!keys(binding, {"field", "origin", "header", "prefix"}, error, QStringLiteral("credential binding"))
                    || !text(binding, "field", &field, error) || !text(binding, "origin", &origin, error)
                    || !text(binding, "header", &header, error)) return false;
                if (!QRegularExpression(QStringLiteral("^[a-zA-Z][a-zA-Z0-9_-]{0,63}$")).match(field).hasMatch()
                    || exactHttpsOrigin(origin).isEmpty()
                    || !QRegularExpression(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]{1,128}$")).match(header).hasMatch()
                    || names.contains(field + QLatin1Char('/') + origin + QLatin1Char('/') + header.toLower()))
                    return fail(error, QStringLiteral("Invalid or duplicate credential binding"));
                const auto protectedHeaders = QSet<QString>{"host", "connection", "content-length", "transfer-encoding", "proxy-authorization"};
                if (protectedHeaders.contains(header.toLower())) return fail(error, QStringLiteral("Forbidden credential header"));
                if (binding.contains("prefix")) {
                    const auto prefix = binding.value("prefix");
                    if (!prefix.isString() || prefix.toString().size() > 32
                        || prefix.toString().contains(QRegularExpression(QStringLiteral("[^\\x20-\\x7e]"))))
                        return fail(error, QStringLiteral("Invalid credential prefix"));
                }
                names.insert(field + QLatin1Char('/') + origin + QLatin1Char('/') + header.toLower());
            }
        }
    }
    return true;
}
bool requiredResource(const QString& value, const QStringList& files, QString* error)
{
    return value.isEmpty() || (portablePackagePath(value) && files.contains(value))
        || fail(error, QStringLiteral("Resource not declared in files: ") + value);
}
} // namespace

bool SemanticVersion::parse(const QString& value, SemanticVersion* output)
{
    static const QRegularExpression expression(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?$"));
    const auto match = expression.match(value);
    if (!match.hasMatch() || value.size() > 128) return false;
    SemanticVersion parsed; bool ok = false;
    parsed.major = match.captured(1).toULongLong(&ok); if (!ok) return false;
    parsed.minor = match.captured(2).toULongLong(&ok); if (!ok) return false;
    parsed.patch = match.captured(3).toULongLong(&ok); if (!ok) return false;
    parsed.prerelease = match.captured(4);
    for (const auto& part : parsed.prerelease.split(QLatin1Char('.'))) {
        if (part.size() > 1 && part.startsWith(QLatin1Char('0')) && QRegularExpression(QStringLiteral("^[0-9]+$")).match(part).hasMatch()) return false;
    }
    if (output) *output = parsed;
    return true;
}
int SemanticVersion::compare(const SemanticVersion& other) const
{
    for (auto pair : {qMakePair(major, other.major), qMakePair(minor, other.minor), qMakePair(patch, other.patch)})
        if (pair.first != pair.second) return pair.first < pair.second ? -1 : 1;
    if (prerelease == other.prerelease) return 0;
    if (prerelease.isEmpty()) return 1;
    if (other.prerelease.isEmpty()) return -1;
    const auto a = prerelease.split(QLatin1Char('.')), b = other.prerelease.split(QLatin1Char('.'));
    const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    for (int i = 0; i < std::min(a.size(), b.size()); ++i) {
        if (a[i] == b[i]) continue;
        const bool an = digits.match(a[i]).hasMatch(), bn = digits.match(b[i]).hasMatch();
        if (an != bn) return an ? -1 : 1;
        if (an && a[i].size() != b[i].size()) return a[i].size() < b[i].size() ? -1 : 1;
        return a[i] < b[i] ? -1 : 1;
    }
    return a.size() < b.size() ? -1 : 1;
}
bool versionSatisfies(const QString& value, const QString& range)
{
    SemanticVersion version;
    if (!SemanticVersion::parse(value, &version) || range.isEmpty() || range.size() > 256) return false;
    const auto clauses = range.split(QStringLiteral("||"));
    if (clauses.size() > 8) return false;
    bool accepted = false;
    for (const auto& clause : clauses) {
        const auto terms = clause.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (terms.isEmpty() || terms.size() > 8) return false;
        bool matches = true, permitsPrerelease = false;
        for (auto term : terms) {
            QString op;
            for (const auto& candidate : {QStringLiteral(">="), QStringLiteral("<="), QStringLiteral(">"), QStringLiteral("<"), QStringLiteral("="), QStringLiteral("^")}) {
                if (term.startsWith(candidate)) { op = candidate; term.remove(0, candidate.size()); break; }
            }
            SemanticVersion target;
            if (!SemanticVersion::parse(term, &target)) return false;
            if (!target.prerelease.isEmpty() && target.major == version.major && target.minor == version.minor && target.patch == version.patch) permitsPrerelease = true;
            const int comparison = version.compare(target);
            if (op == QStringLiteral(">=")) matches &= comparison >= 0;
            else if (op == QStringLiteral("<=")) matches &= comparison <= 0;
            else if (op == QStringLiteral(">")) matches &= comparison > 0;
            else if (op == QStringLiteral("<")) matches &= comparison < 0;
            else if (op == QStringLiteral("^")) {
                SemanticVersion upper = target; upper.prerelease.clear();
                if (upper.major) { if (upper.major == std::numeric_limits<quint64>::max()) return false; ++upper.major; upper.minor = upper.patch = 0; }
                else if (upper.minor) { if (upper.minor == std::numeric_limits<quint64>::max()) return false; ++upper.minor; upper.patch = 0; }
                else { if (upper.patch == std::numeric_limits<quint64>::max()) return false; ++upper.patch; }
                matches &= comparison >= 0 && version.compare(upper) < 0;
            } else matches &= comparison == 0;
        }
        accepted |= matches && (version.prerelease.isEmpty() || permitsPrerelease);
    }
    return accepted;
}
bool portablePackagePath(const QString& path)
{
    if (path.isEmpty() || path.size() > 512 || path != path.normalized(QString::NormalizationForm_C)
        || path.startsWith(QLatin1Char('/')) || path.contains(QLatin1Char('\\')) || path.contains(QLatin1Char(':'))
        || path.contains(QChar::Null) || path.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")))) return false;
    const QRegularExpression reserved(QStringLiteral("^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\\.|$)"), QRegularExpression::CaseInsensitiveOption);
    for (const auto& part : path.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral("..") || part.endsWith(QLatin1Char(' '))
            || part.endsWith(QLatin1Char('.')) || part.size() > 255 || reserved.match(part).hasMatch()
            || part.contains(QRegularExpression(QStringLiteral("[<>\"|?*]"))) || part.compare(QStringLiteral(".git"), Qt::CaseInsensitive) == 0)
            return false;
    }
    return true;
}
QString exactHttpsOrigin(const QString& value)
{
    const QUrl url(value, QUrl::StrictMode);
    if (!safeSource(url) || (!url.path().isEmpty() && url.path() != QStringLiteral("/")) || url.port() == 0)
        return {};
    return originForUrl(url);
}
bool Manifest::parse(const QByteArray& bytes, Manifest* output, QString* error)
{
    if (bytes.isEmpty() || bytes.size() > MaximumManifestBytes) return fail(error, QStringLiteral("Manifest exceeds size limit"));
    QJsonParseError parseError;
    const auto parsed = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) return fail(error, QStringLiteral("Invalid manifest JSON"));
    const auto root = parsed.object(); Manifest result; result.document = root;
    if (!keys(root, {"formatVersion", "id", "name", "version", "publisher", "license", "source", "api", "runtime", "files", "contributions", "permissions", "settings", "locales", "icon", "extensions"}, error, "manifest")) return false;
    int format = 0;
    if (!positiveInteger(root.value("formatVersion"), &format) || format != 1) return fail(error, QStringLiteral("Unsupported manifest format"));
    QString source;
    if (!text(root,"id",&result.id,error) || !text(root,"name",&result.name,error) || !text(root,"version",&result.version,error)
        || !text(root,"publisher",&result.publisher,error) || !text(root,"license",&result.license,error) || !text(root,"source",&source,error)) return false;
    if (!QRegularExpression(QStringLiteral("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+$")).match(result.id).hasMatch() || result.id.size() > 128)
        return fail(error, QStringLiteral("Invalid package ID"));
    if (!SemanticVersion::parse(result.version, nullptr)) return fail(error, QStringLiteral("Invalid package version"));
    result.source = QUrl(source, QUrl::StrictMode);
    if (!safeSource(result.source)) return fail(error, QStringLiteral("Source must be an HTTPS repository without credentials"));
    if (!root.value("api").isObject()) return fail(error, QStringLiteral("Missing API requirements"));
    const auto api = root.value("api").toObject();
    if (!keys(api, {"addon", "contracts", "optionalContracts"}, error, "api") || !api.value("addon").isString()
        || !versionSatisfies(QStringLiteral("1.0.0"), api.value("addon").toString()) || !api.value("contracts").isObject())
        return fail(error, QStringLiteral("Incompatible add-on API"));
    const auto supported = QSet<QString>{"metadata", "settings", "ui", "theme", "discovery", "visualizer", "session"};
    const auto contracts = api.value("contracts").toObject();
    for (auto it = contracts.begin(); it != contracts.end(); ++it)
        if (!supported.contains(it.key()) || !it.value().isString() || !versionSatisfies("1.0.0", it.value().toString()))
            return fail(error, QStringLiteral("Unsupported required contract: ") + it.key());
    if (api.contains("optionalContracts") && !api.value("optionalContracts").isObject()) return fail(error, QStringLiteral("Invalid optional contracts"));
    if (!strings(root.value("files"), &result.files, error, "files", true) || result.files.isEmpty() || result.files.size() > MaximumFiles
        || !result.files.contains("addon.json") || !result.files.contains("LICENSE")) return fail(error, QStringLiteral("Invalid package file inventory"));
    QSet<QString> folded;
    for (const auto& path : result.files) {
        const auto normalized = path.toCaseFolded();
        if (!portablePackagePath(path) || folded.contains(normalized)) return fail(error, QStringLiteral("Unsafe or colliding package path"));
        folded.insert(normalized);
    }
    if (root.contains("runtime")) {
        if (!root.value("runtime").isObject()) return fail(error, QStringLiteral("Invalid runtime declaration"));
        const auto runtime = root.value("runtime").toObject();
        if (!keys(runtime, {"kind", "entry"}, error, "runtime") || !text(runtime,"kind",&result.runtimeKind,error)
            || !text(runtime,"entry",&result.runtimeEntry,error) || result.runtimeKind != QStringLiteral("wasm-message-v1")
            || !requiredResource(result.runtimeEntry, result.files, error)) return fail(error, QStringLiteral("Unsupported runtime"));
    }
    if (root.contains("permissions")) {
        if (!root.value("permissions").isObject()) return fail(error, QStringLiteral("Invalid permissions"));
        const auto object = root.value("permissions").toObject();
        if (!keys(object, {"required", "optional"}, error, "permissions")) return false;
        for (const auto& key : {QStringLiteral("required"), QStringLiteral("optional")})
            if (object.contains(key) && !object.value(key).isObject()) return fail(error, QStringLiteral("Invalid permission group"));
        result.requiredPermissions = object.value("required").toObject(); result.optionalPermissions = object.value("optional").toObject();
        if (!permissions(result.requiredPermissions,error) || !permissions(result.optionalPermissions,error)) return false;
        for (auto it = result.requiredPermissions.begin(); it != result.requiredPermissions.end(); ++it)
            if (result.optionalPermissions.contains(it.key())) return fail(error, QStringLiteral("Permission cannot be both required and optional"));
    }
    if (!root.value("contributions").isArray() || root.value("contributions").toArray().isEmpty() || root.value("contributions").toArray().size() > 64)
        return fail(error, QStringLiteral("Invalid contribution inventory"));
    QSet<QString> identifiers;
    const auto types = QSet<QString>{"theme", "metadata.provider", "discovery.source", "ui.command", "ui.route", "ui.modal", "ui.section", "ui.replacement", "visualizer", "session.controller"};
    for (const auto& value : root.value("contributions").toArray()) {
        if (!value.isObject()) return fail(error, QStringLiteral("Invalid contribution"));
        Contribution contribution; contribution.document = value.toObject(); const auto item = contribution.document;
        if (!keys(item, {"id", "type", "contract", "features", "mediaKinds", "operations", "requires", "name", "label", "icon", "resource", "view", "theme", "scene", "hook", "route", "command", "parameters", "state", "qualityTiers", "analysisChannels", "extensions"}, error, "contribution")
            || !text(item,"id",&contribution.id,error) || !text(item,"type",&contribution.type,error) || !text(item,"contract",&contribution.contract,error)) return false;
        if (!QRegularExpression(QStringLiteral("^[a-z][a-z0-9.-]{0,63}$")).match(contribution.id).hasMatch() || identifiers.contains(contribution.id)
            || !types.contains(contribution.type)) return fail(error, QStringLiteral("Invalid or duplicate contribution ID/type"));
        identifiers.insert(contribution.id);
        const auto parts = contribution.contract.split(QLatin1Char('@'));
        if (parts.size() != 2 || parts[1] != QStringLiteral("1") || !supported.contains(parts[0]) || !contracts.contains(parts[0]))
            return fail(error, QStringLiteral("Contribution requires an undeclared contract"));
        if (!strings(item.value("features"), &contribution.features,error,"features") || !strings(item.value("mediaKinds"),&contribution.mediaKinds,error,"mediaKinds")
            || !strings(item.value("operations"),&contribution.operations,error,"operations") || !strings(item.value("requires"),&contribution.requires,error,"requires")) return false;
        for (const auto& capability : contribution.requires)
            if (!result.permissionDeclared(capability)) return fail(error, QStringLiteral("Contribution requires undeclared permission"));
        for (const auto& key : {QStringLiteral("resource"), QStringLiteral("view"), QStringLiteral("theme"), QStringLiteral("scene"), QStringLiteral("icon")})
            if (item.contains(key) && (!item.value(key).isString() || !requiredResource(item.value(key).toString(),result.files,error))) return false;
        if ((contribution.type == QStringLiteral("metadata.provider") || contribution.type == QStringLiteral("discovery.source") || contribution.type == QStringLiteral("session.controller")) && result.runtimeEntry.isEmpty())
            return fail(error, QStringLiteral("Executable contribution needs a runtime"));
        result.contributions.append(contribution);
    }
    if (root.contains("settings")) {
        if (!root.value("settings").isObject()) return fail(error, QStringLiteral("Invalid settings declaration"));
        const auto settings = root.value("settings").toObject();
        if (!keys(settings,{"version","schema","ui","migrations"},error,"settings") || !positiveInteger(settings.value("version"),&result.settingsVersion)
            || !text(settings,"schema",&result.settingsSchema,error) || !text(settings,"ui",&result.settingsUi,error,false)
            || !requiredResource(result.settingsSchema,result.files,error) || !requiredResource(result.settingsUi,result.files,error)
            || !contracts.contains("settings")) return fail(error, QStringLiteral("Invalid settings resources"));
    }
    if (root.contains("locales")) {
        if (!root.value("locales").isObject()) return fail(error, QStringLiteral("Invalid locales"));
        const auto locales = root.value("locales").toObject(); QString fallback;
        if (!keys(locales,{"default","files"},error,"locales") || !text(locales,"default",&fallback,error) || !locales.value("files").isObject()) return false;
        const auto resources = locales.value("files").toObject();
        if (!resources.contains(fallback)) return fail(error, QStringLiteral("Missing fallback locale"));
        for (auto it = resources.begin(); it != resources.end(); ++it)
            if (!it.value().isString() || !requiredResource(it.value().toString(),result.files,error)) return false;
    }
    if (root.contains("icon") && (!root.value("icon").isString() || !requiredResource(root.value("icon").toString(),result.files,error))) return false;
    for (const auto& contribution : result.contributions)
        if (contribution.type == QStringLiteral("visualizer") && (result.permissionDeclared("network.http") || result.permissionDeclared("network.websocket")))
            return fail(error, QStringLiteral("Portable visualizer packages cannot request network access"));
    const auto bindingScope = result.permissionScope(QStringLiteral("credentials.use"));
    const auto network = result.permissionScope(QStringLiteral("network.http")).value("origins").toArray();
    for (const auto& binding : bindingScope.value("bindings").toArray()) {
        const auto origin = exactHttpsOrigin(binding.toObject().value("origin").toString()); bool allowed = false;
        for (const auto& item : network) allowed |= origin == exactHttpsOrigin(item.toString());
        if (!allowed || result.settingsSchema.isEmpty()) return fail(error, QStringLiteral("Credential binding needs a settings field and declared HTTP origin"));
    }
    if (output) *output = result;
    if (error) error->clear();
    return true;
}
bool Manifest::loadDirectory(const QString& directory, Manifest* output, QMap<QString,QByteArray>* contents, QString* error)
{
    const QFileInfo root(directory);
    if (!root.isDir() || root.isSymLink()) return fail(error, QStringLiteral("Invalid package directory"));
    QFile manifestFile(QDir(directory).filePath(QStringLiteral("addon.json")));
    if (QFileInfo(manifestFile).isSymLink() || !manifestFile.open(QIODevice::ReadOnly) || manifestFile.size() > MaximumManifestBytes)
        return fail(error, QStringLiteral("Cannot read manifest"));
    Manifest manifest;
    if (!parse(manifestFile.readAll(),&manifest,error)) return false;
    QMap<QString,QByteArray> data; qint64 total = 0;
    const QString rootPath = root.canonicalFilePath() + QDir::separator();
    for (const auto& path : manifest.files) {
        QString checked = directory;
        for (const auto& component : path.split(QLatin1Char('/'))) {
            checked = QDir(checked).filePath(component);
            if (QFileInfo(checked).isSymLink()) return fail(error,QStringLiteral("Symbolic links are not package resources"));
        }
        QFile file(checked); const QFileInfo info(file);
        if (!info.isFile() || !info.canonicalFilePath().startsWith(rootPath) || info.size() > MaximumFileBytes || !file.open(QIODevice::ReadOnly))
            return fail(error,QStringLiteral("Missing or oversized package resource: ") + path);
        const auto bytes = file.read(MaximumFileBytes + 1);
        total += bytes.size();
        if (bytes.size() > MaximumFileBytes || total > MaximumPackageBytes || bytes.size() != info.size())
            return fail(error,QStringLiteral("Package resource changed or exceeds limits"));
        data.insert(path,bytes);
    }
    // Validate secret-binding declarations against the protected input schema.
    if (!manifest.settingsSchema.isEmpty()) {
        QJsonParseError schemaError;
        const auto schema = QJsonDocument::fromJson(data.value(manifest.settingsSchema),&schemaError);
        if (schemaError.error != QJsonParseError::NoError || !schema.isObject() || schema.object().value("type") != QStringLiteral("object"))
            return fail(error,QStringLiteral("Invalid settings schema"));
        const auto properties = schema.object().value("properties").toObject();
        for (const auto& binding : manifest.permissionScope("credentials.use").value("bindings").toArray()) {
            const auto property = properties.value(binding.toObject().value("field").toString()).toObject();
            if (!property.value("x-vlc-secret").toBool() || property.value("type") != QStringLiteral("string"))
                return fail(error,QStringLiteral("Credential binding does not reference a secret field"));
        }
    }
    if (output) *output = manifest;
    if (contents) *contents = data;
    if (error) error->clear();
    return true;
}
bool Manifest::permissionDeclared(const QString& capability) const
{
    return requiredPermissions.contains(capability) || optionalPermissions.contains(capability);
}
QJsonObject Manifest::permissionScope(const QString& capability) const
{
    return requiredPermissions.contains(capability) ? requiredPermissions.value(capability).toObject() : optionalPermissions.value(capability).toObject();
}
const Contribution* Manifest::contribution(const QString& id) const
{
    for (const auto& item : contributions) if (item.id == id) return &item;
    return nullptr;
}
QByteArray packageDigest(const QMap<QString,QByteArray>& files)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("vlc-addon-package-v1\n"));
    for (auto it = files.begin(); it != files.end(); ++it) {
        const auto path = it.key().toUtf8();
        const QByteArray framing = QByteArray::number(path.size()) + ':' + path + ':'
            + QByteArray::number(it.value().size()) + ':';
        hash.addData(framing);
        hash.addData(QCryptographicHash::hash(it.value(),QCryptographicHash::Sha256));
        hash.addData(QByteArrayLiteral("\n"));
    }
    return hash.result().toHex();
}
} // namespace vlc::addons
