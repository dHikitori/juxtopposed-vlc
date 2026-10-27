// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_stream_manifest.hpp"
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>
#include <limits>

namespace vlc::addons {
namespace {
using Resource = AddonStreamManifest::Resource;
using Kind = AddonStreamManifest::Kind;
using Range = AddonStreamManifest::Range;
struct Attribute { QString value; qsizetype start, length; bool quoted; };
struct Replacement { qsizetype line, start, length, resource; };
struct PendingRange { quint64 length; std::optional<quint64> offset; qsizetype line; };
bool https(const QUrl& url)
{
    if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host().isEmpty()
        || !url.userInfo().isEmpty() || url.hasFragment() || url.toEncoded().size() > 8192
        || url.port(443) <= 0 || url.port(443) > 65535) return false;
    const auto bytes = url.toEncoded();
    for (const unsigned char byte : bytes) if (byte <= 32 || byte == 127 || byte == '\\') return false;
    return true;
}
bool local(const QString& value, const QUrl& prefix)
{
    const auto root = prefix.toString(QUrl::FullyEncoded);
    if (!value.startsWith(root)) return false;
    const auto path = value.mid(root.size());
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+){0,2}$"));
    return path.size() <= 512 && pattern.match(path).hasMatch();
}
bool proxy(const QUrl& value)
{
    static const QRegularExpression path(QStringLiteral("^/[A-Za-z0-9_-]{1,128}/$"));
    return value.isValid() && value.scheme() == QLatin1String("http") && value.host() == QLatin1String("127.0.0.1")
        && value.port() > 0 && value.port() <= 65535 && value.userInfo().isEmpty()
        && !value.hasQuery() && !value.hasFragment() && path.match(value.path(QUrl::FullyEncoded)).hasMatch();
}
bool unsignedNumber(const QString& value, quint64* result)
{
    if (value.isEmpty() || value.size() > 19) return false;
    for (const auto character : value) if (character < QLatin1Char('0') || character > QLatin1Char('9')) return false;
    bool ok; *result = value.toULongLong(&ok); return ok && *result <= quint64(std::numeric_limits<qint64>::max());
}
bool range(const QString& value, qsizetype line, PendingRange* result)
{
    const auto parts = value.split(QLatin1Char('@'));
    quint64 length, offset = 0;
    if (parts.size() > 2 || !unsignedNumber(parts[0], &length) || !length
        || (parts.size() == 2 && !unsignedNumber(parts[1], &offset))) return false;
    *result = {length, parts.size() == 2 ? std::optional<quint64>(offset) : std::nullopt, line}; return true;
}
bool attributes(const QString& line, qsizetype start, QMap<QString, Attribute>* output)
{
    qsizetype at = start;
    while (at < line.size()) {
        const auto equals = line.indexOf(QLatin1Char('='), at);
        if (equals <= at || output->size() >= 64) return false;
        const auto name = line.mid(at, equals - at);
        for (const auto c : name) if (!(c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
            && !(c >= QLatin1Char('0') && c <= QLatin1Char('9')) && c != QLatin1Char('-')) return false;
        if (output->contains(name)) return false;
        at = equals + 1; const bool quoted = at < line.size() && line[at] == QLatin1Char('"');
        if (quoted) ++at;
        const auto first = at;
        const auto end = line.indexOf(quoted ? QLatin1Char('"') : QLatin1Char(','), at);
        if (quoted && end < 0) return false;
        at = end < 0 ? line.size() : end;
        if (at == first) return false;
        output->insert(name, {line.mid(first, at - first), first, at - first, quoted});
        if (quoted) ++at;
        if (at < line.size() && line[at++] != QLatin1Char(',')) return false;
        if (at == line.size() && line.endsWith(QLatin1Char(','))) return false;
    }
    return !output->isEmpty();
}
bool resolved(const QString& text, const QUrl& base, QUrl* result)
{
    if (text.isEmpty() || text.size() > 8192 || text.contains(QStringLiteral("{$"))) return false;
    for (const auto c : text) if (c.unicode() <= 32 || c.unicode() == 127 || c == QLatin1Char('\\')) return false;
    const QUrl relative(text, QUrl::StrictMode);
    if (!relative.isValid()) return false;
    *result = base.resolved(relative); return https(*result);
}
}
AddonStreamManifest::Result AddonStreamManifest::hls(const QByteArray& bytes, const QUrl& upstream,
    const QUrl& proxyPrefix, const Register& install)
{
    const auto invalid = [] { return Result{{}, QStringLiteral("The stream playlist is not valid.")}; };
    const auto unsupported = [] { return Result{{}, QStringLiteral("This stream playlist uses a feature that is not supported.")}; };
    if (bytes.isEmpty() || bytes.size() > 1024 * 1024 || !https(upstream) || !proxy(proxyPrefix) || !install) return invalid();
    QStringDecoder decoder(QStringDecoder::Utf8); const QString text = decoder.decode(bytes);
    if (decoder.hasError() || text.contains(QChar(0)) || text.startsWith(QChar(0xfeff))) return invalid();
    auto lines = text.split(QLatin1Char('\n'));
    if (lines.size() > 8192) return invalid();
    for (auto& line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (line.size() > 8192) return invalid();
        for (const auto character : line)
            if (character.unicode() < 32 || (character.unicode() >= 127 && character.unicode() <= 159)) return invalid();
    }
    if (lines.isEmpty() || lines[0] != QLatin1String("#EXTM3U")) return invalid();
    QList<Resource> resources; QList<Replacement> replacements;
    std::optional<PendingRange> pendingRange;
    std::optional<Resource> previousMedia, previousMap;
    enum class Form { Unknown, Master, Media };
    Form form = Form::Unknown;
    bool variant = false, iframesOnly = false, expectingSegment = false, target = false, version = false;
    qsizetype expandedBytes = 0;
    const auto append = [&](qsizetype line, const Attribute& value, Kind kind, std::optional<Range> limits) {
        QUrl url;
        if (resources.size() >= 512 || !resolved(value.value, upstream, &url)
            || (expandedBytes += url.toEncoded().size()) > 4 * 1024 * 1024) return false;
        replacements.append({line, value.start, value.length, resources.size()});
        resources.append({url, kind, limits}); return true;
    };
    const auto limits = [](const PendingRange& pending, const QUrl& url, const std::optional<Resource>& previous,
                           std::optional<Range>* output) {
        quint64 offset = 0;
        if (pending.offset) offset = *pending.offset;
        else if (previous && previous->url == url && previous->range) offset = previous->range->offset + previous->range->length;
        else return false;
        if (offset > quint64(std::numeric_limits<qint64>::max()) - pending.length) return false;
        *output = Range{offset, pending.length}; return true;
    };
    for (qsizetype index = 1; index < lines.size(); ++index) {
        const auto line = lines[index];
        if (line.isEmpty()) continue;
        if (!line.startsWith(QLatin1Char('#'))) {
            if (iframesOnly && !previousMap) return unsupported();
            if (!variant && (!expectingSegment || form == Form::Master)) return invalid();
            std::optional<Range> segment; QUrl url;
            if (!resolved(line, upstream, &url)) return invalid();
            if (pendingRange) {
                if (variant || !limits(*pendingRange, url, previousMedia, &segment)) return invalid();
                lines[pendingRange->line] = QStringLiteral("#EXT-X-BYTERANGE:") + QString::number(segment->length)
                    + QLatin1Char('@') + QString::number(segment->offset);
            }
            if (!append(index, {line, 0, line.size(), false}, variant ? Kind::Playlist : Kind::Media, segment)) return invalid();
            if (!variant) previousMedia = resources.back();
            variant = false; expectingSegment = false; pendingRange.reset(); continue;
        }
        if (line == QLatin1String("#EXTM3U")) return invalid();
        const auto colon = line.indexOf(QLatin1Char(':'));
        const auto tag = colon < 0 ? line : line.left(colon);
        if (tag == QLatin1String("#EXT-X-I-FRAMES-ONLY")) {
            if (form == Form::Master) return invalid();
            form = Form::Media; iframesOnly = true; continue;
        }
        if (tag == QLatin1String("#EXT-X-TARGETDURATION")) {
            quint64 duration;
            if (target || form == Form::Master || colon < 0 || !unsignedNumber(line.mid(colon + 1), &duration) || !duration) return invalid();
            form = Form::Media; target = true; continue;
        }
        if (tag == QLatin1String("#EXT-X-VERSION")) {
            quint64 number;
            if (version || colon < 0 || !unsignedNumber(line.mid(colon + 1), &number) || !number) return invalid();
            if (number > 7) return unsupported();
            version = true; continue;
        }
        if (tag == QLatin1String("#EXTINF")) {
            if (form == Form::Master || expectingSegment || colon < 0) return invalid();
            form = Form::Media; expectingSegment = true; continue;
        }
        if (tag == QLatin1String("#EXT-X-DEFINE") || tag == QLatin1String("#EXT-X-PART")
            || tag == QLatin1String("#EXT-X-PRELOAD-HINT") || tag == QLatin1String("#EXT-X-RENDITION-REPORT")
            || tag == QLatin1String("#EXT-X-SERVER-CONTROL") || tag == QLatin1String("#EXT-X-SKIP")) return unsupported();
        if (tag == QLatin1String("#EXT-X-BYTERANGE")) {
            PendingRange parsed;
            if (pendingRange || colon < 0 || !range(line.mid(colon + 1), index, &parsed)) return invalid();
            pendingRange = parsed; continue;
        }
        if (tag == QLatin1String("#EXT-X-STREAM-INF")) {
            QMap<QString, Attribute> values; quint64 bandwidth;
            if (form == Form::Media || variant || pendingRange || colon < 0 || !attributes(line, colon + 1, &values)
                || !unsignedNumber(values.value(QStringLiteral("BANDWIDTH")).value, &bandwidth) || !bandwidth) return invalid();
            for (auto it = values.cbegin(); it != values.cend(); ++it) if (it.key().contains(QLatin1String("URI"))) return unsupported();
            form = Form::Master; variant = true; continue;
        }
        const bool media = tag == QLatin1String("#EXT-X-MEDIA"), iframe = tag == QLatin1String("#EXT-X-I-FRAME-STREAM-INF");
        const bool map = tag == QLatin1String("#EXT-X-MAP"), session = tag == QLatin1String("#EXT-X-SESSION-DATA");
        const bool key = tag == QLatin1String("#EXT-X-KEY"), sessionKey = tag == QLatin1String("#EXT-X-SESSION-KEY");
        if (!media && !iframe && !map && !session && !key && !sessionKey) {
            static const QSet<QString> plainTags{QStringLiteral("#EXTINF"), QStringLiteral("#EXT-X-VERSION"),
                QStringLiteral("#EXT-X-TARGETDURATION"), QStringLiteral("#EXT-X-MEDIA-SEQUENCE"),
                QStringLiteral("#EXT-X-DISCONTINUITY-SEQUENCE"), QStringLiteral("#EXT-X-ENDLIST"),
                QStringLiteral("#EXT-X-PLAYLIST-TYPE"), QStringLiteral("#EXT-X-INDEPENDENT-SEGMENTS"),
                QStringLiteral("#EXT-X-START"), QStringLiteral("#EXT-X-DISCONTINUITY"),
                QStringLiteral("#EXT-X-PROGRAM-DATE-TIME"), QStringLiteral("#EXT-X-DATERANGE"),
                QStringLiteral("#EXT-X-ALLOW-CACHE"), QStringLiteral("#EXT-X-GAP"), QStringLiteral("#EXT-X-BITRATE")};
            if (line.startsWith(QLatin1String("#EXT")) && !plainTags.contains(tag)) return unsupported();
            static const QRegularExpression uriAttribute(QStringLiteral("[A-Za-z0-9-]*URI\\s*="), QRegularExpression::CaseInsensitiveOption);
            if (line.startsWith(QLatin1String("#EXT")) && uriAttribute.match(line).hasMatch()) return unsupported();
            continue;
        }
        QMap<QString, Attribute> values;
        if (colon < 0 || !attributes(line, colon + 1, &values)) return invalid();
        if (media || iframe || session || sessionKey) {
            if (form == Form::Media) return invalid();
            form = Form::Master;
        } else {
            if (form == Form::Master) return invalid();
            form = Form::Media;
        }
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (it.key().contains(QLatin1String("URI")) && it.key() != QLatin1String("URI")) return unsupported();
        if (key || sessionKey) {
            const auto method = values.value(QStringLiteral("METHOD")).value;
            if (method == QLatin1String("NONE") && !sessionKey) {
                if (values.size() != 1) return invalid();
                continue;
            }
            if (method != QLatin1String("AES-128") || (values.contains(QStringLiteral("KEYFORMAT"))
                && values.value(QStringLiteral("KEYFORMAT")).value != QLatin1String("identity"))
                || (values.contains(QStringLiteral("KEYFORMATVERSIONS"))
                    && values.value(QStringLiteral("KEYFORMATVERSIONS")).value != QLatin1String("1"))) return unsupported();
        }
        if (!values.contains(QStringLiteral("URI"))) {
            if (media || (session && values.contains(QStringLiteral("VALUE")))) continue;
            return invalid();
        }
        const auto uri = values.value(QStringLiteral("URI"));
        if (!uri.quoted) return invalid();
        if (session && values.contains(QStringLiteral("VALUE"))) return invalid();
        std::optional<Range> extent;
        if (map && values.contains(QStringLiteral("BYTERANGE"))) {
            PendingRange pending; QUrl url;
            if (!range(values.value(QStringLiteral("BYTERANGE")).value, index, &pending)
                || !resolved(uri.value, upstream, &url) || !limits(pending, url, previousMap, &extent)) return invalid();
            if (!pending.offset) return unsupported();
            const auto entry = values.value(QStringLiteral("BYTERANGE"));
            if (!entry.quoted) return invalid();
            // Preserve absolute offsets. The transport must enforce this range.
            const QString explicitRange = QString::number(extent->length) + QLatin1Char('@') + QString::number(extent->offset);
            lines[index].replace(entry.start, entry.length, explicitRange);
            if (entry.start < uri.start) {
                auto adjusted = uri; adjusted.start += explicitRange.size() - entry.length;
                if (!append(index, adjusted, Kind::Initialization, extent)) return invalid();
                previousMap = resources.back(); continue;
            }
        }
        if (!append(index, uri, key || sessionKey ? Kind::Key : map ? Kind::Initialization
            : session ? Kind::SessionData : Kind::Playlist, extent)) return invalid();
        if (map) previousMap = resources.back();
    }
    if (variant || pendingRange || expectingSegment || form == Form::Unknown
        || (form == Form::Media && !target) || (form == Form::Master && resources.isEmpty())) return invalid();
    const auto urls = install(resources);
    if (urls.size() != resources.size()) return invalid();
    for (const auto& url : urls) if (!local(url, proxyPrefix)) return invalid();
    for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
        lines[it->line].replace(it->start, it->length, urls[it->resource]);
    auto result = lines.join(QLatin1Char('\n')).toUtf8();
    if (result.size() > 2 * 1024 * 1024) return invalid();
    return {std::move(result), {}};
}
}
