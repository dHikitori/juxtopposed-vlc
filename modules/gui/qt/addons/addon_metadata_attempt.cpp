// SPDX-License-Identifier: GPL-2.0-or-later
// Records one exact provider attempt. Native owner activation is separate.
#include "addon_metadata_attempt.hpp"
#include "addon_result_retention.hpp"
#include "addon_metadata_identity.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QThread>
#include <QSet>
#include <QDate>
#include <algorithm>
#include <cmath>
#include <utility>
#include <mutex>

namespace vlc::addons {
struct MetadataAttemptBudgetLease {
    struct Global { std::mutex mutex; unsigned count = 0; qsizetype bytes = 0; };
    std::shared_ptr<Global> global;
    qsizetype bytes = 0;
    bool admitted = false;
    static std::shared_ptr<MetadataAttemptBudgetLease> acquire() {
        static const auto all = std::make_shared<Global>();
        auto value = std::make_shared<MetadataAttemptBudgetLease>(); value->global = all;
        std::lock_guard<std::mutex> lock(all->mutex);
        if (all->count >= 1024) return {};
        ++all->count; value->admitted = true; return value;
    }
    bool charge(qsizetype amount) {
        std::lock_guard<std::mutex> lock(global->mutex);
        if (amount < 0 || amount > 16 * 1024 * 1024 - global->bytes) return false;
        global->bytes += amount; bytes += amount; return true;
    }
    ~MetadataAttemptBudgetLease() {
        if (!admitted) return;
        std::lock_guard<std::mutex> lock(global->mutex);
        global->bytes -= bytes; --global->count;
    }
};
namespace {
constexpr qsizetype MaxResult = 1024 * 1024;
bool atom(const QString& value, qsizetype limit = 128)
{
    return !value.isEmpty() && value.size() <= limit
        && std::none_of(value.cbegin(), value.cend(), [](QChar c) { return c.category() == QChar::Other_Control; });
}
bool live(const std::function<bool()>& check) noexcept
{ try { return check && check(); } catch (...) { return false; } }
QByteArray digest(const QJsonObject& value)
{ return QCryptographicHash::hash(QJsonDocument(value).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256); }
bool bounded(const QJsonValue& value, unsigned& nodes, qsizetype& bytes, unsigned depth = 0)
{
    if (++nodes > 8192 || depth > 16) return false;
    if (value.isString()) { if (value.toString().size() > 65536) return false; bytes += value.toString().size() * 3; }
    else if (value.isDouble() && !std::isfinite(value.toDouble())) return false;
    else if (value.isArray()) {
        const auto values = value.toArray(); if (values.size() > 1024) return false;
        for (const auto& item : values) if (!bounded(item, nodes, bytes, depth + 1)) return false;
    } else if (value.isObject()) {
        const auto values = value.toObject(); if (values.size() > 256) return false;
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (it.key().size() > 128) return false;
            bytes += it.key().size() * 3;
            if (!bounded(it.value(), nodes, bytes, depth + 1)) return false;
        }
    }
    return bytes <= MaxResult;
}
bool only(const QJsonObject& value, const QSet<QString>& keys)
{
    for (auto it = value.begin(); it != value.end(); ++it) if (!keys.contains(it.key())) return false;
    return true;
}
bool number(const QJsonValue& value, double minimum, double maximum)
{ return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= minimum
    && value.toDouble() <= maximum && std::trunc(value.toDouble()) == value.toDouble(); }
bool textMap(const QJsonValue& value, qsizetype count, qsizetype length)
{
    if (!value.isObject() || value.toObject().size() > count) return false;
    const auto map = value.toObject();
    for (auto it = map.begin(); it != map.end(); ++it)
        if (!atom(it.key(), 64) || !it.value().isString() || !atom(it.value().toString(), length)) return false;
    return true;
}
bool identity(const QJsonObject& value)
{
    static const QSet<QString> keys{QStringLiteral("kind"), QStringLiteral("type"), QStringLiteral("title"),
        QStringLiteral("titles"), QStringLiteral("aliases"), QStringLiteral("year"), QStringLiteral("season"),
        QStringLiteral("episodes"), QStringLiteral("episode"), QStringLiteral("episodeNumbering"), QStringLiteral("absoluteEpisode"), QStringLiteral("airDate"),
        QStringLiteral("ids"), QStringLiteral("externalIds"), QStringLiteral("release"), QStringLiteral("provenance"),
        QStringLiteral("conflicts"), QStringLiteral("artist"), QStringLiteral("albumArtist"), QStringLiteral("album"),
        QStringLiteral("series"), QStringLiteral("guid"), QStringLiteral("feedUrl"), QStringLiteral("track"), QStringLiteral("disc")};
    static const QSet<QString> kinds{QStringLiteral("unknown"), QStringLiteral("movie"), QStringLiteral("series"),
        QStringLiteral("episode"), QStringLiteral("musicTrack"), QStringLiteral("album"), QStringLiteral("artist"),
        QStringLiteral("podcast"), QStringLiteral("podcastEpisode"), QStringLiteral("audiobook")};
    if (!only(value, keys)) return false;
    for (const auto& key : {QStringLiteral("kind"), QStringLiteral("type")})
        if (value.contains(key) && (!value[key].isString() || !kinds.contains(value[key].toString()))) return false;
    if (value.contains(QStringLiteral("kind")) && value.contains(QStringLiteral("type"))
        && value[QStringLiteral("kind")] != value[QStringLiteral("type")]) return false;
    for (auto it = value.begin(); it != value.end(); ++it) {
        const auto key = it.key();
        if (key == QLatin1String("ids") || key == QLatin1String("provenance")) {
            if (!textMap(it.value(), 64, 1024)) return false;
        } else if (key == QLatin1String("release")) {
            if (!it.value().isObject() || it.value().toObject().size() > 64) return false;
            const auto fields = it.value().toObject();
            for (auto field = fields.begin(); field != fields.end(); ++field)
                if (!atom(field.key(), 64) || !field.value().isString() || field.value().toString().size() > 1024) return false;
        } else if (key == QLatin1String("externalIds")) {
            if (!it.value().isArray() || it.value().toArray().size() > 32) return false;
            for (const auto& item : it.value().toArray()) {
                if (!item.isObject()) return false;
                const auto id = item.toObject();
                if (!only(id, {QStringLiteral("namespace"), QStringLiteral("scope"), QStringLiteral("value")})
                    || id.size() != 3 || !id[QStringLiteral("namespace")].isString() || !id[QStringLiteral("scope")].isString()
                    || !id[QStringLiteral("value")].isString() || !atom(id[QStringLiteral("namespace")].toString(), 32)
                    || !atom(id[QStringLiteral("scope")].toString(), 32) || !atom(id[QStringLiteral("value")].toString(), 256)) return false;
                static const QRegularExpression idAtom(QStringLiteral("^[a-z][a-z0-9._-]{0,31}\\z"));
                if (!idAtom.match(id[QStringLiteral("namespace")].toString()).hasMatch()
                    || !idAtom.match(id[QStringLiteral("scope")].toString()).hasMatch()) return false;
            }
        } else if (key == QLatin1String("episodes")) {
            if (!it.value().isArray() || it.value().toArray().size() > 128) return false;
            for (const auto& episode : it.value().toArray()) if (!number(episode, 0, 9999)) return false;
        } else if (key == QLatin1String("episode") || key == QLatin1String("episodeNumbering")) {
            if (key == QLatin1String("episode") && it.value().isDouble()) {
                if (!number(it.value(), 0, 9999)) return false;
                continue;
            }
            if (!it.value().isObject()) return false;
            const auto episode = it.value().toObject();
            if (key == QLatin1String("episodeNumbering") && value.contains(QStringLiteral("episode"))
                && value.value(QStringLiteral("episode")) != it.value()) return false;
            if (!only(episode, {QStringLiteral("scheme"), QStringLiteral("season"), QStringLiteral("episodes"),
                    QStringLiteral("number"), QStringLiteral("date")})) return false;
            const auto scheme = episode[QStringLiteral("scheme")].toString();
            if (scheme == QLatin1String("seasonal")) {
                if (episode.size() != 3 || !number(episode[QStringLiteral("season")], 0, 999) || !episode[QStringLiteral("episodes")].isArray()
                    || episode[QStringLiteral("episodes")].toArray().isEmpty()) return false;
                for (const auto& numberValue : episode[QStringLiteral("episodes")].toArray()) if (!number(numberValue, 0, 9999)) return false;
                if ((value.contains(QStringLiteral("season")) && value.value(QStringLiteral("season")) != episode.value(QStringLiteral("season")))
                    || (value.contains(QStringLiteral("episodes")) && value.value(QStringLiteral("episodes")) != episode.value(QStringLiteral("episodes")))) return false;
            } else if (scheme == QLatin1String("absolute")) {
                const auto absolute = episode[QStringLiteral("number")];
                if (episode.size() != 2 || !absolute.isString() || !QRegularExpression(QStringLiteral("^[0-9]{1,6}(?:\\.[0-9]{1,3})?\\z")).match(absolute.toString()).hasMatch()) return false;
                if (value.contains(QStringLiteral("absoluteEpisode")) && !value.value(QStringLiteral("absoluteEpisode")).toString().isEmpty()
                    && value.value(QStringLiteral("absoluteEpisode")) != absolute) return false;
            } else if (scheme == QLatin1String("air-date")) {
                const auto date = episode[QStringLiteral("date")];
                if (episode.size() != 2 || !date.isString() || !QDate::fromString(date.toString(), Qt::ISODate).isValid()) return false;
                if (value.contains(QStringLiteral("airDate")) && !value.value(QStringLiteral("airDate")).toString().isEmpty()
                    && value.value(QStringLiteral("airDate")) != date) return false;
            } else return false;
        } else if (key == QLatin1String("year") || key == QLatin1String("season")
            || key == QLatin1String("track") || key == QLatin1String("disc")) {
            if (!number(it.value(), -1, key == QLatin1String("year") ? 2099 : key == QLatin1String("season") ? 999 : 9999)) return false;
            if (key == QLatin1String("year") && it.value().toDouble() != -1 && it.value().toDouble() < 1880) return false;
        } else if (key == QLatin1String("titles") || key == QLatin1String("aliases") || key == QLatin1String("conflicts")) {
            if (!it.value().isArray() || it.value().toArray().size() > 64) return false;
            for (const auto& text : it.value().toArray()) if (!text.isString() || !atom(text.toString(), 1024)) return false;
        } else {
            if (!it.value().isString() || it.value().toString().size() > 1024) return false;
            if (key == QLatin1String("airDate") && !it.value().toString().isEmpty()
                && !QDate::fromString(it.value().toString(), Qt::ISODate).isValid()) return false;
            if (key == QLatin1String("absoluteEpisode") && !it.value().toString().isEmpty()
                && !QRegularExpression(QStringLiteral("^[0-9]{1,6}(?:\\.[0-9]{1,3})?\\z")).match(it.value().toString()).hasMatch()) return false;
        }
    }
    return true;
}
std::optional<QJsonObject> canonicalIdentity(const QJsonObject& value)
{
    const auto titles = value.value(QStringLiteral("titles")).toArray();
    const auto originalTitle = value.value(QStringLiteral("title")).toString().isEmpty() && !titles.isEmpty()
        ? titles.first().toString() : value.value(QStringLiteral("title")).toString();
    // Positive normalization may bound display text. A negative cannot discard it.
    if (originalTitle.trimmed().size() > 512) return {};
    const auto normalized = normalizeMetadataIdentity(value.toVariantMap());
    if (!normalized) return {};
    auto result = QJsonObject::fromVariantMap(*normalized);
    QStringList aliases;
    for (const auto& alias : result.value(QStringLiteral("aliases")).toArray()) aliases.append(alias.toString());
    for (const auto& title : titles)
        if (title.toString() != result.value(QStringLiteral("title")).toString()) aliases.append(title.toString());
    aliases.removeDuplicates(); aliases.sort(); result.insert(QStringLiteral("aliases"), QJsonArray::fromStringList(aliases));
    return result;
}
// This is stricter than ordinary positive acceptance. A malformed, ambiguous,
// nonempty, paged or failed result cannot be filtered into a negative receipt.
bool completeNoMatch(const QJsonObject& data, const QJsonObject& expectedIdentity)
{
    unsigned nodes = 0; qsizetype bytes = 0;
    if (!bounded(data, nodes, bytes) || QJsonDocument(data).toJson(QJsonDocument::Compact).size() > MaxResult
        || !only(data, {QStringLiteral("identity"), QStringLiteral("records"), QStringLiteral("candidates"), QStringLiteral("fields"),
            QStringLiteral("retention")})) return false;
    // A retention record describes reuse, not the answer; it must be correct.
    if (data.contains(QStringLiteral("retention"))
        && !vlc::addons::ResultRetention::from(QStringLiteral("no_match"), data.toVariantMap()).valid) return false;
    if (data.contains(QStringLiteral("identity"))) {
        const auto observed = data[QStringLiteral("identity")];
        if (!observed.isObject() || !identity(observed.toObject())
            || !observed.toObject().value(QStringLiteral("conflicts")).toArray().isEmpty()) return false;
        const auto normalized = canonicalIdentity(observed.toObject());
        const auto expected = canonicalIdentity(expectedIdentity);
        if (!normalized || !expected || *normalized != *expected) return false;
    }
    for (const auto& key : {QStringLiteral("records"), QStringLiteral("candidates")})
        if (data.contains(key) && (!data[key].isArray() || !data[key].toArray().isEmpty())) return false;
    return !data.contains(QStringLiteral("fields")) || (data[QStringLiteral("fields")].isObject()
        && data[QStringLiteral("fields")].toObject().isEmpty());
}
}

struct AddonMetadataAttemptAdmission::BoundScope {
    const ManagerBinding manager;
    const QString worker, invocation;
};
struct AddonMetadataCompletionReceipt::Data {
    // Keep the admission charge through the final capture destructor.
    const std::shared_ptr<MetadataAttemptBudgetLease> budget;
    const AddonMetadataAttemptAdmission::Capture capture;
    const QString worker, invocation;
    const QByteArray contextDigest, resultDigest;
    const Outcome outcome;
};
AddonMetadataCompletionReceipt::AddonMetadataCompletionReceipt(std::unique_ptr<Data> data) : m_data(std::move(data)) {}
AddonMetadataCompletionReceipt::AddonMetadataCompletionReceipt(AddonMetadataCompletionReceipt&&) noexcept = default;
AddonMetadataCompletionReceipt& AddonMetadataCompletionReceipt::operator=(AddonMetadataCompletionReceipt&&) noexcept = default;
AddonMetadataCompletionReceipt::~AddonMetadataCompletionReceipt() = default;
AddonMetadataCompletionReceipt::Outcome AddonMetadataCompletionReceipt::outcome() const noexcept
{ return m_data ? m_data->outcome : Outcome::Invalid; }
bool AddonMetadataCompletionReceipt::current() const noexcept
{ return m_data && m_data->capture.lifetime.current() && m_data->capture.admission
    && m_data->capture.admission->current() && live(m_data->capture.live); }
bool AddonMetadataCompletionReceipt::matches(const QByteArray& subject, unsigned ordinal, const QString& contribution,
    const QByteArray& context, const QString& partition) const
{ return current() && m_data->capture.nativeSubject == subject && m_data->capture.ordinal == ordinal
    && m_data->capture.contribution == contribution && m_data->contextDigest == context && m_data->capture.partition == partition; }
QByteArray AddonMetadataCompletionReceipt::resultDigest() const { return m_data ? m_data->resultDigest : QByteArray{}; }
bool AddonMetadataCompletionReceipt::matchesResult(const QJsonObject& data) const
{
    unsigned nodes = 0; qsizetype bytes = 0;
    return current() && bounded(data, nodes, bytes)
        && QJsonDocument(data).toJson(QJsonDocument::Compact).size() <= MaxResult
        && m_data->resultDigest == digest(data);
}
std::shared_ptr<AddonMetadataNativeAdmission> AddonMetadataCompletionReceipt::nativeAdmission() const
{ return current() ? m_data->capture.admission : nullptr; }

AddonMetadataAttemptAdmission::AddonMetadataAttemptAdmission(Capture capture, std::shared_ptr<MetadataAttemptBudgetLease> budget)
    : m_budget(std::move(budget)), m_capture(std::move(capture)), m_thread(QThread::currentThread()) {}
AddonMetadataAttemptAdmission::~AddonMetadataAttemptAdmission() = default;
std::shared_ptr<AddonMetadataAttemptAdmission> AddonMetadataAttemptAdmission::create(Capture capture)
{
    if (capture.nativeSubject.size() != 32 || !capture.nativeChildHold || capture.ordinal >= 16
        || !atom(capture.contribution, 256) || !atom(capture.feature) || !atom(capture.operation)
        || capture.releaseDigest.size() != 64 || !capture.releaseGeneration || !atom(capture.accountReference)
        || !atom(capture.partition, 512) || capture.requestedFields.size() > 64
        || !capture.lifetime.current() || !capture.admission || !capture.admission->current() || !live(capture.live)) return {};
    const auto budget = MetadataAttemptBudgetLease::acquire(); if (!budget) return {};
    const auto cost = QJsonDocument(capture.approvedGrants).toJson(QJsonDocument::Compact).size()
        + 3 * (capture.contribution.size() + capture.feature.size() + capture.operation.size()
            + capture.accountReference.size() + capture.partition.size() + capture.requestedFields.join(QLatin1Char('\n')).size());
    if (cost > MaxResult || !budget->charge(cost)) return {};
    return std::shared_ptr<AddonMetadataAttemptAdmission>(new AddonMetadataAttemptAdmission(std::move(capture), budget));
}
bool AddonMetadataAttemptAdmission::ownerThread() const noexcept { return m_thread == QThread::currentThread(); }
bool AddonMetadataAttemptAdmission::current() const noexcept
{ return m_capture.lifetime.current() && m_capture.admission && m_capture.admission->current() && live(m_capture.live); }
void AddonMetadataAttemptAdmission::revoke() noexcept { if (m_capture.admission) m_capture.admission->revoke(); }
void AddonMetadataAttemptAdmission::blocked() noexcept { m_receipt.reset(); m_calls.clear(); m_phase = Phase::Blocked; }
bool AddonMetadataAttemptAdmission::bindManager(ManagerBinding binding)
{
    if (!ownerThread() || m_phase != Phase::Captured || !current()
        || binding.contract != QLatin1String("metadata@1") || binding.contribution != m_capture.contribution
        || binding.feature != m_capture.feature || binding.operation != m_capture.operation
        || binding.releaseDigest != m_capture.releaseDigest || binding.releaseGeneration != m_capture.releaseGeneration
        || binding.settingsRevision != m_capture.settingsRevision || binding.accountReference != m_capture.accountReference
        || binding.partition != m_capture.partition || binding.approvedGrants != m_capture.approvedGrants
        || binding.requestedFields != m_capture.requestedFields
        || binding.context.value(QStringLiteral("feature")).toString() != binding.feature
        || !binding.context.value(QStringLiteral("identity")).isObject()
        || !identity(binding.context.value(QStringLiteral("identity")).toObject())
        || !canonicalIdentity(binding.context.value(QStringLiteral("identity")).toObject())) { blocked(); return false; }
    unsigned nodes = 0; qsizetype bytes = 0;
    const auto contextBytes = QJsonDocument(binding.context).toJson(QJsonDocument::Compact).size();
    if (!bounded(binding.context, nodes, bytes) || contextBytes > MaxResult || !m_budget->charge(contextBytes)
        || (!binding.absentHttpStatuses.isEmpty() && binding.absentHttpStatuses != QSet<int>{404}
            && binding.absentHttpStatuses != QSet<int>{410} && binding.absentHttpStatuses != QSet<int>{404, 410})) { blocked(); return false; }
    m_scope.reset(new BoundScope{std::move(binding), {}, {}}); m_phase = Phase::ManagerBound; return true;
}
bool AddonMetadataAttemptAdmission::bindWorker(const QString& instance, const QString& invocation,
    const QString& localContribution, const QString& operation, const QJsonObject& context)
{
    if (!ownerThread() || m_phase != Phase::ManagerBound || !current() || !atom(instance) || !atom(invocation)
        || m_capture.contribution.section('/', 1) != localContribution || operation != m_capture.operation
        || context != m_scope->manager.context) { blocked(); return false; }
    m_scope.reset(new BoundScope{m_scope->manager, instance, invocation}); m_phase = Phase::WorkerBound; return true;
}
bool AddonMetadataAttemptAdmission::dispatched(const QString& invocation)
{
    if (!ownerThread() || m_phase != Phase::WorkerBound || !current() || m_scope->invocation != invocation) { blocked(); return false; }
    m_phase = Phase::Dispatched; return true;
}
bool AddonMetadataAttemptAdmission::hostStarted(const QString& invocation, const QString& id, const QString& operation)
{
    if (!ownerThread() || m_phase != Phase::Dispatched || !current() || m_scope->invocation != invocation
        || !atom(id) || !atom(operation) || m_calls.contains(id) || m_calls.size() >= 4096
        || !m_budget->charge(128 + 2 * (id.size() + operation.size()))) { blocked(); return false; }
    m_calls.insert(id, false); return true;
}
void AddonMetadataAttemptAdmission::hostFinished(const QString& invocation, const QString& id, AddonMetadataHostObservation value)
{
    if (!ownerThread() || m_phase == Phase::Blocked) return;
    const auto found = m_calls.find(id);
    if (m_phase != Phase::Dispatched || !m_scope || m_scope->invocation != invocation || found == m_calls.end() || *found
        || value.terminal != AddonMetadataHostTerminal::Complete
        || (value.network && !(value.httpStatus >= 200 && value.httpStatus < 300)
            && !m_scope->manager.absentHttpStatuses.contains(value.httpStatus))) { blocked(); return; }
    *found = true;
}
void AddonMetadataAttemptAdmission::terminal(const QString& invocation, const QString& status,
    const QJsonObject& data, const QString& error)
{
    if (!ownerThread() || m_phase != Phase::Dispatched || !m_scope || m_scope->invocation != invocation
        || !current() || !error.isEmpty()
        || std::any_of(m_calls.cbegin(), m_calls.cend(), [](bool done) { return !done; })) { blocked(); return; }
    using Outcome = AddonMetadataCompletionReceipt::Outcome;
    Outcome outcome = Outcome::Invalid;
    if (status == QLatin1String("no_match")) {
        if (!completeNoMatch(data, m_scope->manager.context.value(QStringLiteral("identity")).toObject())) { blocked(); return; }
        outcome = Outcome::NoMatch;
    } else if (status == QLatin1String("match") || status == QLatin1String("partial")) {
        // This certifies execution and exact bytes only. Shared consumer
        // acceptance still checks identity and fields before image resolution.
        unsigned nodes = 0; qsizetype bytes = 0;
        if (data.isEmpty() || !bounded(data, nodes, bytes)
            || QJsonDocument(data).toJson(QJsonDocument::Compact).size() > MaxResult) { blocked(); return; }
        outcome = status == QLatin1String("match") ? Outcome::Match : Outcome::Partial;
    } else { blocked(); return; }
    m_receipt.reset(new AddonMetadataCompletionReceipt::Data{m_budget, m_capture, m_scope->worker, invocation,
        digest(m_scope->manager.context), digest(data), outcome});
    m_calls.clear();
    m_phase = Phase::Terminal;
}
std::optional<AddonMetadataCompletionReceipt> AddonMetadataAttemptAdmission::take()
{
    if (!ownerThread() || m_phase != Phase::Terminal || !current() || !m_receipt) return {};
    m_phase = Phase::Taken; return AddonMetadataCompletionReceipt(std::move(m_receipt));
}
}
