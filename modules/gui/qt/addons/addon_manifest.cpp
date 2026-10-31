// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_manifest.hpp"
#include "addon_device_authorization.hpp"
#include "addon_guest_projections.hpp"
#include "addon_metadata_completion_policy.hpp"
#include "addon_settings_migrations.hpp"
#include "addon_storage_data.hpp"
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
bool settingsAnnotations(const QJsonValue& value, unsigned depth, unsigned& nodes, QString* error)
{
    if (depth > 32 || nodes == 0)
        return fail(error, QStringLiteral("The settings schema exceeds the annotation scan limit."));
    --nodes;
    if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key().startsWith(QLatin1String("x-vlc-")))
                return fail(error, QStringLiteral("Use x-hakone settings annotations."));
            if (!settingsAnnotations(it.value(), depth + 1, nodes, error)) return false;
        }
    } else if (value.isArray()) {
        for (const auto& child : value.toArray())
            if (!settingsAnnotations(child, depth + 1, nodes, error)) return false;
    }
    return true;
}
bool keys(const QJsonObject& object, const QSet<QString>& allowed, QString* error, const QString& context)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!allowed.contains(it.key())) return fail(error, context + QStringLiteral(": The following field is not supported: ") + it.key());
    return true;
}
bool text(const QJsonObject& object, const QString& key, QString* output, QString* error, bool required = true)
{
    const auto value = object.value(key);
    if (!required && value.isUndefined()) return true;
    if (!value.isString() || value.toString().isEmpty() || value.toString().size() > 4096 || value.toString().contains(QChar::Null))
        return fail(error, QStringLiteral("The text field is not correct: ") + key);
    *output = value.toString();
    return true;
}
bool strings(const QJsonValue& value, QStringList* output, QString* error, const QString& context, bool required = false)
{
    if (value.isUndefined() && !required) return true;
    if (!value.isArray() || value.toArray().size() > 256) return fail(error, context + QStringLiteral(": Use an array within the host limit."));
    QSet<QString> seen;
    for (const auto& item : value.toArray()) {
        if (!item.isString() || item.toString().isEmpty() || item.toString().size() > 4096 || seen.contains(item.toString()))
            return fail(error, context + QStringLiteral(": A value is not correct or occurs more than once."));
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
        && (url.userInfo().isEmpty() && !url.authority().contains(QLatin1Char('@'))) && !url.hasQuery() && !url.hasFragment();
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
        {"metadata.context", {"fields"}}, {"media.fingerprint", {"algorithms"}}, {"media.stream", {"profiles"}},
        {"network.http", {"origins", "methods", "minimumIntervalMs", "assetRedirectProfiles"}}, {"network.websocket", {"origins"}},
        {"credentials.use", {"bindings"}}, {"storage.private", {"quotaBytes"}},
        {"library.read", {"fields", "collections"}}, {"queue.read", {}},
        {"queue.modify", {"operations"}}, {"playback.observe", {"fields"}},
        {"playback.control", {"operations"}}, {"audio.analysis", {"channels"}},
        {"ui.contribute", {"hooks"}}, {"session.background", {}}
    };
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!fields.contains(it.key()) || !it.value().isObject()) return fail(error, QStringLiteral("The permission is not supported: ") + it.key());
        const auto scope = it.value().toObject();
        if (!keys(scope, fields.value(it.key()), error, it.key())) return false;
        if (it.key() == QStringLiteral("storage.private") && !scope.contains("quotaBytes"))
            return fail(error, QStringLiteral("Private storage needs a byte limit."));
        if (it.key() == QStringLiteral("media.stream")
            && scope.value(QStringLiteral("profiles")) != QJsonArray{QStringLiteral("public-https-media@1")})
            return fail(error, QStringLiteral("The media stream profile is not supported."));
        for (auto item = scope.begin(); item != scope.end(); ++item) {
            if (item.key() == QStringLiteral("minimumIntervalMs")) {
                const auto interval = item.value().toDouble(-1);
                if (!item.value().isDouble() || !std::isfinite(interval) || interval < 0 || interval > 60000 || std::floor(interval) != interval)
                    return fail(error, QStringLiteral("The network minimum interval is not correct."));
            } else if (item.key() == QStringLiteral("quotaBytes")) {
                const auto n = item.value().toDouble(-1);
                if (n < 0 || n > 100 * 1024 * 1024 || std::floor(n) != n) return fail(error, QStringLiteral("The storage quota is not correct."));
            } else if (item.key() != QStringLiteral("bindings")) {
                QStringList list;
                if (!strings(item.value(), &list, error, it.key() + QLatin1Char('.') + item.key(), true)) return false;
                // The host projects only these collections, fields and hashes.
                if (it.key() == QStringLiteral("library.read") && item.key() == QStringLiteral("collections")) {
                    for (const auto& name : list) if (!AddonGuestProjections::libraryFields().contains(name))
                        return fail(error, QStringLiteral("The library collection is not supported: ") + name);
                } else if (it.key() == QStringLiteral("library.read") && item.key() == QStringLiteral("fields")) {
                    QSet<QString> known;
                    for (const auto& names : AddonGuestProjections::libraryFields()) for (const auto& name : names) known.insert(name);
                    for (const auto& name : list) if (!known.contains(name))
                        return fail(error, QStringLiteral("The library field is not supported: ") + name);
                } else if (it.key() == QStringLiteral("media.fingerprint") && item.key() == QStringLiteral("algorithms")) {
                    for (const auto& name : list) if (!AddonGuestProjections::fingerprintAlgorithms().contains(name))
                        return fail(error, QStringLiteral("The fingerprint algorithm is not supported: ") + name);
                }
                if (item.key() == QStringLiteral("assetRedirectProfiles")) {
                    for (const auto& profile : list) if (profile != QStringLiteral("internet-archive-images@1"))
                        return fail(error, QStringLiteral("The image redirect profile is not supported."));
                } else if (item.key() == QStringLiteral("origins")) {
                    if (list.isEmpty()) return fail(error, QStringLiteral("The network permission needs a list of origins."));
                    for (const auto& origin : list) {
                        if (it.key() == QStringLiteral("network.websocket")) {
                            if (exactWssOrigin(origin).isEmpty()) return fail(error, QStringLiteral("The WSS origin is not correct."));
                        } else if (exactHttpsOrigin(origin).isEmpty()) return fail(error, QStringLiteral("The HTTPS origin is not correct."));
                    }
                }
                if (item.key() == QStringLiteral("methods"))
                    for (const auto& method : list)
                        if (method != QStringLiteral("GET") && method != QStringLiteral("POST"))
                            return fail(error, QStringLiteral("The HTTP method is not supported."));
            }
        }
        if ((it.key().startsWith(QStringLiteral("network."))) && !scope.contains(QStringLiteral("origins")))
            return fail(error, QStringLiteral("The network permission needs a list of origins."));
        if (it.key() == QStringLiteral("credentials.use")) {
            if (!scope.value(QStringLiteral("bindings")).isArray() || scope.value(QStringLiteral("bindings")).toArray().size() > 16)
                return fail(error, QStringLiteral("The credential bindings are not correct."));
            QSet<QString> names;
            for (const auto& item : scope.value(QStringLiteral("bindings")).toArray()) {
                if (!item.isObject()) return fail(error, QStringLiteral("The credential binding is not correct."));
                const auto binding = item.toObject(); QString field, origin, header;
                if (!keys(binding, {"field", "origin", "header", "prefix"}, error, QStringLiteral("credential binding"))
                    || !text(binding, "field", &field, error) || !text(binding, "origin", &origin, error)
                    || !text(binding, "header", &header, error)) return false;
                const bool websocket=QUrl(origin,QUrl::StrictMode).scheme()==QLatin1String("wss");
                if (!QRegularExpression(QStringLiteral("^[a-zA-Z][a-zA-Z0-9_-]{0,63}\\z")).match(field).hasMatch()
                    || (websocket ? exactWssOrigin(origin).isEmpty() : exactHttpsOrigin(origin).isEmpty())
                    || !QRegularExpression(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]{1,128}\\z")).match(header).hasMatch()
                    || names.contains(field + QLatin1Char('/') + origin + QLatin1Char('/') + header.toLower()))
                    return fail(error, QStringLiteral("A credential binding is not correct or occurs more than once."));
                const auto protectedHeaders = QSet<QString>{"host", "connection", "content-length", "transfer-encoding", "proxy-authorization"};
                if (protectedHeaders.contains(header.toLower())) return fail(error, QStringLiteral("The credential header is not permitted."));
                if (websocket && (header.toLower().startsWith(QStringLiteral("sec-websocket-"))
                    || QSet<QString>{"upgrade","origin","cookie"}.contains(header.toLower())))
                    return fail(error, QStringLiteral("The WSS credential header is not permitted."));
                if (binding.contains("prefix")) {
                    const auto prefix = binding.value("prefix");
                    if (!prefix.isString() || prefix.toString().size() > 32
                        || prefix.toString().contains(QRegularExpression(QStringLiteral("[^\\x20-\\x7e]"))))
                        return fail(error, QStringLiteral("The credential prefix is not correct."));
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
        || fail(error, QStringLiteral("The file list does not declare this resource: ") + value);
}
} // namespace

bool SemanticVersion::parse(const QString& value, SemanticVersion* output)
{
    static const QRegularExpression expression(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?\\z"));
    const auto match = expression.match(value);
    if (!match.hasMatch() || value.size() > 128) return false;
    SemanticVersion parsed; bool ok = false;
    parsed.major = match.captured(1).toULongLong(&ok); if (!ok) return false;
    parsed.minor = match.captured(2).toULongLong(&ok); if (!ok) return false;
    parsed.patch = match.captured(3).toULongLong(&ok); if (!ok) return false;
    parsed.prerelease = match.captured(4);
    for (const auto& part : parsed.prerelease.split(QLatin1Char('.'))) {
        if (part.size() > 1 && part.startsWith(QLatin1Char('0')) && QRegularExpression(QStringLiteral("^[0-9]+\\z")).match(part).hasMatch()) return false;
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
    const QRegularExpression digits(QStringLiteral("^[0-9]+\\z"));
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
{ return versionSatisfies(value, range, false); }
bool versionSatisfies(const QString& value, const QString& range, bool allowPrerelease)
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
        accepted |= matches && (allowPrerelease || version.prerelease.isEmpty() || permitsPrerelease);
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
QString exactWssOrigin(const QString& value)
{
    const QUrl url(value,QUrl::StrictMode);
    if (!url.isValid() || url.scheme()!=QLatin1String("wss") || url.host().isEmpty() || (!url.userInfo().isEmpty() || url.authority().contains(QLatin1Char('@')))
        || url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@')) || url.host().contains(QLatin1Char('%'))
        || url.port(443)!=443 || !url.path().isEmpty() || url.hasQuery() || url.hasFragment()) return {};
    const auto canonical=originForUrl(url);
    return value==canonical ? canonical : QString{};
}
bool Manifest::parse(const QByteArray& bytes, Manifest* output, QString* error)
{
    if (bytes.isEmpty() || bytes.size() > MaximumManifestBytes) return fail(error, QStringLiteral("The manifest is empty or too large."));
    QJsonParseError parseError;
    const auto parsed = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) return fail(error, QStringLiteral("The manifest JSON is not correct."));
    const auto root = parsed.object(); Manifest result; result.document = root;
    if (!keys(root, {"formatVersion", "id", "name", "version", "publisher", "license", "source", "api", "runtime", "files", "contributions", "permissions", "settings", "storage", "locales", "icon", "extensions"}, error, "manifest")) return false;
    int format = 0;
    if (!positiveInteger(root.value("formatVersion"), &format) || format != 1) return fail(error, QStringLiteral("The manifest format is not supported."));
    QString source;
    if (!text(root,"id",&result.id,error) || !text(root,"name",&result.name,error) || !text(root,"version",&result.version,error)
        || !text(root,"publisher",&result.publisher,error) || !text(root,"license",&result.license,error) || !text(root,"source",&source,error)) return false;
    if (!QRegularExpression(QStringLiteral("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+\\z")).match(result.id).hasMatch() || result.id.size() > 128)
        return fail(error, QStringLiteral("The package ID is not correct."));
    for (const auto& reserved : {QStringLiteral("hakone.builtin"), QStringLiteral("vlc.builtin")})
        if (result.id == reserved || result.id.startsWith(reserved + QLatin1Char('.')))
            return fail(error, QStringLiteral("This package ID is reserved for the host."));
    if (!SemanticVersion::parse(result.version, nullptr)) return fail(error, QStringLiteral("The package version is not correct."));
    result.source = QUrl(source, QUrl::StrictMode);
    if (!safeSource(result.source)) return fail(error, QStringLiteral("Use an HTTPS repository without credentials."));
    if (!root.value("api").isObject()) return fail(error, QStringLiteral("The API requirements are missing."));
    const auto api = root.value("api").toObject();
    if (!keys(api, {"addon", "contracts", "optionalContracts"}, error, "api") || !api.value("addon").isString()
        || !versionSatisfies(QStringLiteral("1.0.0"), api.value("addon").toString()) || !api.value("contracts").isObject())
        return fail(error, QStringLiteral("The add-on API version is not supported."));
    const auto supported = QSet<QString>{"metadata", "settings", "ui", "theme", "discovery", "visualizer", "session", "storage"};
    const auto contracts = api.value("contracts").toObject();
    for (auto it = contracts.begin(); it != contracts.end(); ++it)
        if (!supported.contains(it.key()) || !it.value().isString() || !versionSatisfies("1.0.0", it.value().toString()))
            return fail(error, QStringLiteral("The required contract is not supported: ") + it.key());
    if (api.contains("optionalContracts") && !api.value("optionalContracts").isObject()) return fail(error, QStringLiteral("The optional contracts are not correct."));
    if (!strings(root.value("files"), &result.files, error, "files", true) || result.files.isEmpty() || result.files.size() > MaximumFiles
        || !result.files.contains("addon.json") || !result.files.contains("LICENSE")) return fail(error, QStringLiteral("The package file list is not correct."));
    QSet<QString> folded;
    for (const auto& path : result.files) {
        const auto normalized = path.toCaseFolded();
        if (!portablePackagePath(path) || folded.contains(normalized)) return fail(error, QStringLiteral("A package path is not permitted or conflicts with another path."));
        folded.insert(normalized);
    }
    if (root.contains("runtime")) {
        if (!root.value("runtime").isObject()) return fail(error, QStringLiteral("The runtime declaration is not correct."));
        const auto runtime = root.value("runtime").toObject();
        if (runtime.contains("selfTest") && !runtime.value("selfTest").isBool())
            return fail(error, QStringLiteral("The runtime self-test declaration is not correct."));
        result.runtimeSelfTest = runtime.value("selfTest").toBool();
        if (!keys(runtime, {"kind", "entry", "selfTest"}, error, "runtime") || !text(runtime,"kind",&result.runtimeKind,error)
            || !text(runtime,"entry",&result.runtimeEntry,error) || result.runtimeKind != QStringLiteral("wasm-message-v1")
            || !requiredResource(result.runtimeEntry, result.files, error)) return fail(error, QStringLiteral("The runtime is not supported."));
    }
    if (root.contains("permissions")) {
        if (!root.value("permissions").isObject()) return fail(error, QStringLiteral("The permissions are not correct."));
        const auto object = root.value("permissions").toObject();
        if (!keys(object, {"required", "optional"}, error, "permissions")) return false;
        for (const auto& key : {QStringLiteral("required"), QStringLiteral("optional")})
            if (object.contains(key) && !object.value(key).isObject()) return fail(error, QStringLiteral("The permission group is not correct."));
        result.requiredPermissions = object.value("required").toObject(); result.optionalPermissions = object.value("optional").toObject();
        if (!permissions(result.requiredPermissions,error) || !permissions(result.optionalPermissions,error)) return false;
        for (auto it = result.requiredPermissions.begin(); it != result.requiredPermissions.end(); ++it)
            if (result.optionalPermissions.contains(it.key())) return fail(error, QStringLiteral("A permission cannot be required and optional."));
    }
    if (!root.value("contributions").isArray() || root.value("contributions").toArray().isEmpty() || root.value("contributions").toArray().size() > 64)
        return fail(error, QStringLiteral("The contribution list is not correct."));
    QSet<QString> identifiers;
    const auto types = QSet<QString>{"theme", "metadata.provider", "discovery.source", "ui.command", "ui.route", "ui.modal", "ui.section", "ui.replacement", "visualizer", "session.controller"};
    for (const auto& value : root.value("contributions").toArray()) {
        if (!value.isObject()) return fail(error, QStringLiteral("The contribution is not correct."));
        Contribution contribution; contribution.document = value.toObject(); const auto item = contribution.document;
        if (!keys(item, {"id", "type", "contract", "features", "mediaKinds", "operations", "requires", "name", "label", "icon", "resource", "view", "theme", "scene", "hook", "route", "command", "parameters", "state", "stateSchema", "resultSchema", "completionPolicy", "qualityTiers", "analysisChannels", "assets", "extensions"}, error, "contribution")
            || !text(item,"id",&contribution.id,error) || !text(item,"type",&contribution.type,error) || !text(item,"contract",&contribution.contract,error)) return false;
        if (!QRegularExpression(QStringLiteral("^[a-z][a-z0-9.-]{0,63}\\z")).match(contribution.id).hasMatch() || identifiers.contains(contribution.id)
            || !types.contains(contribution.type)) return fail(error, QStringLiteral("A contribution ID or type is not correct or occurs more than once."));
        identifiers.insert(contribution.id);
        const auto parts = contribution.contract.split(QLatin1Char('@'));
        if (parts.size() != 2 || parts[1] != QStringLiteral("1") || !supported.contains(parts[0]) || !contracts.contains(parts[0]))
            return fail(error, QStringLiteral("The contribution needs a declared contract."));
        if (!strings(item.value("features"), &contribution.features,error,"features") || !strings(item.value("mediaKinds"),&contribution.mediaKinds,error,"mediaKinds")
            || !strings(item.value("operations"),&contribution.operations,error,"operations") || !strings(item.value("requires"),&contribution.requires,error,"requires")) return false;
        for (const auto& capability : contribution.requires)
            if (!result.permissionDeclared(capability)) return fail(error, QStringLiteral("The contribution needs a declared permission."));
        if (!validateMetadataCompletionPolicy(item)) return fail(error, QStringLiteral("The metadata completion policy is not correct."));
        if (contribution.type.startsWith(QLatin1String("ui."))) {
            for (const auto& key : {QStringLiteral("parameters"), QStringLiteral("stateSchema"), QStringLiteral("resultSchema")})
                if (item.contains(key) && (!item.value(key).isObject() || !validateUiSchema(item.value(key).toObject())
                    || item.value(key).toObject().value("type").toString() != QLatin1String("object")))
                    return fail(error, QStringLiteral("The UI data schema is not correct."));
            if (item.contains("stateSchema") && !validateUiValue(item.value("stateSchema").toObject(), item.contains("state") ? item.value("state") : QJsonValue(QJsonObject{})))
                return fail(error, QStringLiteral("The initial UI state does not agree with its schema."));
        }
        for (const auto& key : {QStringLiteral("resource"), QStringLiteral("view"), QStringLiteral("theme"), QStringLiteral("scene"), QStringLiteral("icon")})
            if (item.contains(key) && (!item.value(key).isString() || !requiredResource(item.value(key).toString(),result.files,error))) return false;
        if (item.contains("assets")) {
            if (!QStringList{"ui.route", "ui.modal", "ui.section", "ui.replacement"}.contains(contribution.type)
                || !item.value("assets").isObject() || item.value("assets").toObject().size() > 64)
                return fail(error, QStringLiteral("The UI image map is not correct."));
            static const QRegularExpression assetId(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,95}\\z"));
            const auto assets = item.value("assets").toObject();
            for (auto asset = assets.begin(); asset != assets.end(); ++asset) {
                const auto declaration = asset.value().toObject(); const auto path = declaration.value("path").toString();
                if (!assetId.match(asset.key()).hasMatch() || QStringList{"__proto__", "prototype", "constructor"}.contains(asset.key())
                    || !asset.value().isObject() || declaration.size() != 1 || !declaration.value("path").isString() || path.isEmpty()
                    || !requiredResource(path, result.files, error) || !QStringList{"png", "jpg", "jpeg", "webp"}.contains(QFileInfo(path).suffix().toLower()))
                    return fail(error, QStringLiteral("A UI image declaration is not correct."));
            }
        }
        if ((contribution.type == QStringLiteral("metadata.provider") || contribution.type == QStringLiteral("discovery.source") || contribution.type == QStringLiteral("session.controller")) && result.runtimeEntry.isEmpty())
            return fail(error, QStringLiteral("The executable contribution needs a runtime."));
        result.contributions.append(contribution);
    }
    if (root.contains("settings")) {
        if (!root.value("settings").isObject()) return fail(error, QStringLiteral("The settings declaration is not correct."));
        const auto settings = root.value("settings").toObject();
        if (!keys(settings,{"version","schema","ui","migrations","accountFields"},error,"settings") || !positiveInteger(settings.value("version"),&result.settingsVersion)
            || !text(settings,"schema",&result.settingsSchema,error) || !text(settings,"ui",&result.settingsUi,error,false)
            || !requiredResource(result.settingsSchema,result.files,error) || !requiredResource(result.settingsUi,result.files,error)
            || !contracts.contains("settings")) return fail(error, QStringLiteral("The settings resources are not correct."));
        if (settings.contains("accountFields")) {
            const auto fields = settings.value("accountFields");
            static const QRegularExpression fieldName(QStringLiteral("^[A-Za-z][A-Za-z0-9_]{0,63}\\z"));
            if (!fields.isArray() || fields.toArray().size() > 16)
                return fail(error, QStringLiteral("The account field list is too large or is not correct."));
            for (const auto& field : fields.toArray()) {
                if (!field.isString() || !fieldName.match(field.toString()).hasMatch() || result.accountFields.contains(field.toString()))
                    return fail(error, QStringLiteral("An account field name is not correct or occurs more than once."));
                result.accountFields.append(field.toString());
            }
        }
    }
    if (root.contains("locales")) {
        if (!root.value("locales").isObject()) return fail(error, QStringLiteral("The locale resources are not correct."));
        const auto locales = root.value("locales").toObject(); QString fallback;
        if (!keys(locales,{"default","files"},error,"locales") || !text(locales,"default",&fallback,error) || !locales.value("files").isObject()) return false;
        const auto resources = locales.value("files").toObject();
        if (!resources.contains(fallback)) return fail(error, QStringLiteral("The fallback locale is missing."));
        for (auto it = resources.begin(); it != resources.end(); ++it)
            if (!it.value().isString() || !requiredResource(it.value().toString(),result.files,error)) return false;
    }
    if (root.contains("icon") && (!root.value("icon").isString() || !requiredResource(root.value("icon").toString(),result.files,error))) return false;
    for (const auto& contribution : result.contributions)
        if (contribution.type == QStringLiteral("visualizer")
            && (result.permissionDeclared("network.http") || result.permissionDeclared("network.websocket") || result.permissionDeclared("media.stream")
                || result.permissionDeclared("session.background")))
            return fail(error, QStringLiteral("Visualizer packages cannot have network or background session permissions."));
    const auto bindingScope = result.permissionScope(QStringLiteral("credentials.use"));
    for (const auto& binding : bindingScope.value("bindings").toArray()) {
        const auto declared=binding.toObject().value("origin").toString();
        const bool websocket=QUrl(declared,QUrl::StrictMode).scheme()==QLatin1String("wss");
        const auto origin=websocket ? exactWssOrigin(declared) : exactHttpsOrigin(declared);
        const auto network=result.permissionScope(websocket ? QStringLiteral("network.websocket") : QStringLiteral("network.http")).value("origins").toArray();
        bool allowed=false;
        for (const auto& item : network) allowed |= origin==(websocket ? exactWssOrigin(item.toString()) : exactHttpsOrigin(item.toString()));
        if (!allowed || result.settingsSchema.isEmpty()) return fail(error, QStringLiteral("The credential binding needs a settings field and a declared network origin."));
    }
    if (!validateSettingsMigrations(result, {}, error)) return false;
    if ((result.permissionDeclared(QStringLiteral("storage.private")) || root.contains("storage")) && !contracts.contains("storage"))
        return fail(error, QStringLiteral("Private storage needs the storage contract."));
    if (!validatePrivateStorageDeclaration(result, {}, false, error)) return false;
    if (output) *output = result;
    if (error) error->clear();
    return true;
}
bool Manifest::loadDirectory(const QString& directory, Manifest* output, QMap<QString,QByteArray>* contents, QString* error)
{
    const QFileInfo root(directory);
    if (!root.isDir() || root.isSymLink()) return fail(error, QStringLiteral("The package directory is not correct."));
    QFile manifestFile(QDir(directory).filePath(QStringLiteral("addon.json")));
    if (QFileInfo(manifestFile).isSymLink() || !manifestFile.open(QIODevice::ReadOnly) || manifestFile.size() > MaximumManifestBytes)
        return fail(error, QStringLiteral("The app cannot read the manifest."));
    Manifest manifest;
    if (!parse(manifestFile.readAll(),&manifest,error)) return false;
    QMap<QString,QByteArray> data; qint64 total = 0;
    QString rootPath = root.canonicalFilePath();
    if (rootPath.isEmpty()) return fail(error, QStringLiteral("The package directory is not available."));
    // Qt canonical paths use '/', including on Windows.
    if (!rootPath.endsWith(QLatin1Char('/'))) rootPath += QLatin1Char('/');
    for (const auto& path : manifest.files) {
        QString checked = directory;
        for (const auto& component : path.split(QLatin1Char('/'))) {
            checked = QDir(checked).filePath(component);
            if (QFileInfo(checked).isSymLink()) return fail(error,QStringLiteral("A package resource cannot be a symbolic link."));
        }
        QFile file(checked); const QFileInfo info(file);
        if (!info.isFile() || !info.canonicalFilePath().startsWith(rootPath) || info.size() > MaximumFileBytes || !file.open(QIODevice::ReadOnly))
            return fail(error,QStringLiteral("The package resource is missing or too large: ") + path);
        const auto bytes = file.read(MaximumFileBytes + 1);
        total += bytes.size();
        if (bytes.size() > MaximumFileBytes || total > MaximumPackageBytes || bytes.size() != info.size())
            return fail(error,QStringLiteral("A package resource changed or is too large."));
        data.insert(path,bytes);
    }
    if (!validateContents(manifest, data, error)) return false;
    if (output) *output = manifest;
    if (contents) *contents = data;
    if (error) error->clear();
    return true;
}
bool Manifest::validateContents(const Manifest& manifest,
                                const QMap<QString,QByteArray>& data, QString* error)
{
    if (data.size() != manifest.files.size())
        return fail(error, QStringLiteral("The package file list is not complete."));
    qint64 total = 0;
    for (const auto& path : manifest.files) {
        if (!data.contains(path) || data.value(path).size() > MaximumFileBytes
            || (total += data.value(path).size()) > MaximumPackageBytes)
            return fail(error, QStringLiteral("The package files are missing or too large."));
    }
    // Validate secret-binding declarations against the protected input schema.
    for (const auto& contribution : manifest.contributions) {
        qint64 images = 0;
        const auto assets = contribution.document.value(QStringLiteral("assets")).toObject();
        for (const auto& value : assets) {
            const auto bytes = data.value(value.toObject().value(QStringLiteral("path")).toString());
            if (bytes.isEmpty() || bytes.size() > 1024 * 1024 || (images += bytes.size()) > 8 * 1024 * 1024)
                return fail(error, QStringLiteral("The UI images exceed the byte limit or an image is empty."));
        }
    }
    if (!manifest.settingsSchema.isEmpty()) {
        QJsonParseError schemaError;
        const auto schema = QJsonDocument::fromJson(data.value(manifest.settingsSchema),&schemaError);
        if (schemaError.error != QJsonParseError::NoError || !schema.isObject() || schema.object().value("type") != QStringLiteral("object"))
            return fail(error,QStringLiteral("The settings schema is not correct."));
        unsigned annotationNodes = 8192;
        if (!settingsAnnotations(schema.object(), 0, annotationNodes, error)) return false;
        const auto properties = schema.object().value("properties").toObject();
        for (const auto& name : manifest.accountFields) {
            const auto property = properties.value(name).toObject();
            if (property.isEmpty() || property.value("x-hakone-secret").toBool() || property.value("writeOnly").toBool()
                || property.value("format").toString() == QLatin1String("password")
                || !QStringList{QStringLiteral("string"), QStringLiteral("number"), QStringLiteral("integer"), QStringLiteral("boolean")}
                    .contains(property.value("type").toString()))
                return fail(error, QStringLiteral("An account field must be a declared nonsecret scalar setting."));
        }
        QStringList origins;
        for (const auto& value : manifest.permissionScope(QStringLiteral("network.http")).value("origins").toArray()) origins.append(value.toString());
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const auto property = it.value().toObject();
            const bool account = property.value("x-hakone-control").toString() == QLatin1String("account");
            if (account != property.contains("x-hakone-account"))
                return fail(error, QStringLiteral("An account field needs an account recipe, and only an account field can have one."));
            if (!account) continue;
            // The access token is the field's credential.
            if (!property.value("x-hakone-secret").toBool() || property.value("type") != QStringLiteral("string"))
                return fail(error, QStringLiteral("An account field must be a secret string."));
            if (!DeviceRecipe::from(property.value("x-hakone-account").toObject(), origins, error)) return false;
        }
        for (const auto& binding : manifest.permissionScope("credentials.use").value("bindings").toArray()) {
            const auto property = properties.value(binding.toObject().value("field").toString()).toObject();
            if (!property.value("x-hakone-secret").toBool() || property.value("type") != QStringLiteral("string"))
                return fail(error,QStringLiteral("The credential binding does not refer to a secret field."));
        }
    }
    if (!validateSettingsMigrations(manifest, data, error)) return false;
    if (!validatePrivateStorageDeclaration(manifest, data, true, error)) return false;
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
namespace {
bool uiSchema(const QJsonObject& schema, int depth, int& nodes)
{
    if (depth > 10 || --nodes < 0) return false;
    static const QSet<QString> allowed{"type", "properties", "required", "additionalProperties", "items", "enum", "minimum", "maximum", "minLength", "maxLength", "minItems", "maxItems"};
    for (auto it = schema.begin(); it != schema.end(); ++it) if (!allowed.contains(it.key())) return false;
    const auto type = schema.value("type").toString();
    if (!QStringList{"object", "array", "string", "number", "integer", "boolean"}.contains(type)) return false;
    if ((schema.contains("minimum") || schema.contains("maximum")) && type != QLatin1String("number") && type != QLatin1String("integer")) return false;
    if ((schema.contains("minLength") || schema.contains("maxLength")) && type != QLatin1String("string")) return false;
    if (schema.contains("enum")) {
        const auto values = schema.value("enum").toArray();
        if (!schema.value("enum").isArray() || values.isEmpty() || values.size() > 64) return false;
        QJsonArray seen;
        for (const auto& value : values) {
            if (seen.contains(value) || (type == QLatin1String("string") ? !value.isString() || value.toString().size() > 16384
                : type == QLatin1String("boolean") ? !value.isBool()
                : type == QLatin1String("number") || type == QLatin1String("integer") ? !value.isDouble() || !std::isfinite(value.toDouble()) || (type == QLatin1String("integer") && std::floor(value.toDouble()) != value.toDouble()) : true)) return false;
            seen.append(value);
        }
    }
    for (const auto& pair : {qMakePair(QStringLiteral("minimum"), QStringLiteral("maximum")), qMakePair(QStringLiteral("minLength"), QStringLiteral("maxLength")), qMakePair(QStringLiteral("minItems"), QStringLiteral("maxItems"))}) {
        for (const auto& key : {pair.first, pair.second}) if (schema.contains(key)) {
            const auto value = schema.value(key);
            if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
            if (key != QLatin1String("minimum") && key != QLatin1String("maximum")
                && (value.toDouble() < 0 || std::floor(value.toDouble()) != value.toDouble() || value.toDouble() > 16384)) return false;
        }
        if (schema.contains(pair.first) && schema.contains(pair.second) && schema.value(pair.first).toDouble() > schema.value(pair.second).toDouble()) return false;
    }
    if (type == QLatin1String("object")) {
        if (!schema.value("properties").isObject() || schema.value("additionalProperties") != QJsonValue(false)) return false;
        const auto properties = schema.value("properties").toObject(); if (properties.size() > 64) return false;
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            if (it.key() == QLatin1String("__proto__") || it.key() == QLatin1String("constructor") || it.key() == QLatin1String("prototype")
                || it.key().size() > 96 || !it.value().isObject() || !uiSchema(it.value().toObject(), depth + 1, nodes)) return false;
        }
        if (schema.contains("required")) {
            if (!schema.value("required").isArray()) return false;
            QSet<QString> seen;
            for (const auto& field : schema.value("required").toArray()) {
                if (!field.isString() || !properties.contains(field.toString()) || seen.contains(field.toString())) return false;
                seen.insert(field.toString());
            }
        }
    } else if (schema.contains("properties") || schema.contains("required") || schema.contains("additionalProperties")) return false;
    if (type == QLatin1String("array")) {
        if (!schema.value("items").isObject() || !uiSchema(schema.value("items").toObject(), depth + 1, nodes)) return false;
    } else if (schema.contains("items") || schema.contains("minItems") || schema.contains("maxItems")) return false;
    return true;
}
bool uiValue(const QJsonObject& schema, const QJsonValue& value, int depth, int& nodes)
{
    if (depth > 10 || --nodes < 0) return false;
    const auto type = schema.value("type").toString();
    if (schema.contains("enum") && !schema.value("enum").toArray().contains(value)) return false;
    if (type == QLatin1String("object")) {
        if (!value.isObject()) return false;
        const auto object = value.toObject(), properties = schema.value("properties").toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!properties.contains(it.key()) || !uiValue(properties.value(it.key()).toObject(), it.value(), depth + 1, nodes)) return false;
        for (const auto& field : schema.value("required").toArray()) if (!object.contains(field.toString())) return false;
    } else if (type == QLatin1String("array")) {
        if (!value.isArray()) return false;
        const auto array = value.toArray();
        if (array.size() < schema.value("minItems").toInt(0) || array.size() > std::min(1000, schema.value("maxItems").toInt(1000))) return false;
        for (const auto& child : array) if (!uiValue(schema.value("items").toObject(), child, depth + 1, nodes)) return false;
    } else if (type == QLatin1String("string")) {
        if (!value.isString() || value.toString().size() < schema.value("minLength").toInt(0)
            || value.toString().size() > std::min(16384, schema.value("maxLength").toInt(16384))) return false;
    } else if (type == QLatin1String("boolean")) { if (!value.isBool()) return false; }
    else {
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || (type == QLatin1String("integer") && std::floor(value.toDouble()) != value.toDouble())) return false;
        if ((schema.contains("minimum") && value.toDouble() < schema.value("minimum").toDouble())
            || (schema.contains("maximum") && value.toDouble() > schema.value("maximum").toDouble())) return false;
    }
    return true;
}
}
bool validateUiSchema(const QJsonObject& schema)
{ int nodes = 256; return uiSchema(schema, 0, nodes); }
bool validateUiValue(const QJsonObject& schema, const QJsonValue& value)
{ int nodes = 4096; return validateUiSchema(schema) && uiValue(schema, value, 0, nodes); }

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
