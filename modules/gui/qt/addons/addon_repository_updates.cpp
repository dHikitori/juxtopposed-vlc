// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_repository_updates.hpp"
#include <QRegularExpression>

namespace vlc::addons {
namespace {
bool fail(QString* error, const QString& message)
{ if (error) *error = message; return false; }
bool rangeValid(const QString& range)
{
    if (range.isEmpty() || range.size() > 256) return false;
    const auto clauses = range.split(QStringLiteral("||"));
    if (clauses.size() > 8) return false;
    for (const auto& clause : clauses) {
        const auto terms = clause.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (terms.isEmpty() || terms.size() > 8) return false;
        for (auto term : terms) {
            for (const auto& operation : {QStringLiteral(">="), QStringLiteral("<="), QStringLiteral(">"),
                    QStringLiteral("<"), QStringLiteral("="), QStringLiteral("^")})
                if (term.startsWith(operation)) { term.remove(0, operation.size()); break; }
            if (!SemanticVersion::parse(term, nullptr)) return false;
        }
    }
    return true;
}
}

bool parseRepositoryTags(const QByteArray& advertisement, QList<RepositoryTag>* output, QString* error)
{
    if (advertisement.size() > 1024 * 1024)
        return fail(error, QStringLiteral("The repository tag list is too large."));
    QList<RepositoryTag> result;
    QSet<QString> names;
    int count = 0;
    qsizetype objectWidth = 0;
    static const QRegularExpression objectId(QStringLiteral("^(?:[a-f0-9]{40}|[a-f0-9]{64})$"));
    for (auto line : advertisement.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (line.isEmpty()) continue;
        if (++count > 4096 || line.size() > 1024)
            return fail(error, QStringLiteral("The repository has too many release tags."));
        const auto fields = line.split('\t');
        if (fields.size() != 2 || !objectId.match(QString::fromLatin1(fields[0])).hasMatch()
            || !fields[1].startsWith("refs/tags/"))
            return fail(error, QStringLiteral("The repository tag data is not correct."));
        if (objectWidth && objectWidth != fields[0].size())
            return fail(error, QStringLiteral("The repository advertised more than one Git object format."));
        objectWidth = fields[0].size();
        RepositoryTag tag;
        tag.name = QString::fromUtf8(fields[1].mid(10));
        tag.version = tag.name;
        if (tag.version.startsWith(QLatin1Char('v'))) tag.version.remove(0, 1);
        if (tag.name.size() > 128 || !SemanticVersion::parse(tag.version, &tag.parsedVersion)) continue;
        if (names.contains(tag.name)) return fail(error, QStringLiteral("The repository advertised a release tag more than once."));
        names.insert(tag.name); tag.objectId = QString::fromLatin1(fields[0]);
        result.append(tag);
    }
    if (output) *output = result;
    if (error) error->clear();
    return true;
}

bool selectRepositoryUpdate(const QList<RepositoryTag>& tags, const QString& currentVersion,
                            const QString& range, bool allowPrerelease,
                            RepositoryTag* output, QString* error)
{
    SemanticVersion current;
    if (tags.size() > 4096 || !SemanticVersion::parse(currentVersion, &current) || !rangeValid(range))
        return fail(error, QStringLiteral("The current version or update range is not correct."));
    RepositoryTag selected;
    for (const auto& tag : tags) {
        SemanticVersion parsed;
        if (!SemanticVersion::parse(tag.version, &parsed) || parsed.compare(current) <= 0
            || (!allowPrerelease && !parsed.prerelease.isEmpty())) continue;
        const bool compatible = versionSatisfies(tag.version, range, allowPrerelease);
        if (!compatible) continue;
        const int comparison = selected.name.isEmpty() ? 1 : parsed.compare(selected.parsedVersion);
        if (comparison > 0) { selected = tag; selected.parsedVersion = parsed; }
        else if (comparison == 0) {
            if (tag.version != selected.version || tag.objectId != selected.objectId)
                return fail(error, QStringLiteral("Release tags with equal version priority have different Git objects. Select an exact tag."));
            if (tag.name == QStringLiteral("v") + tag.version) { selected = tag; selected.parsedVersion = parsed; }
        }
    }
    if (output) *output = selected;
    if (error) error->clear();
    return true;
}
}
