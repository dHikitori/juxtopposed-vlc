// SPDX-License-Identifier: GPL-2.0-or-later
// Validate installed metadata completion rules before provider dispatch.
#include "addon_metadata_completion_policy.hpp"
#include <QJsonArray>
#include <algorithm>
namespace vlc::addons {
namespace {
std::optional<QSet<int>> parse(const QJsonObject& contribution, const QString& feature, const QString& operation)
{
    const auto value = contribution.value(QStringLiteral("completionPolicy"));
    if (value.isUndefined()) return QSet<int>{};
    if (contribution.value(QStringLiteral("type")) != QLatin1String("metadata.provider")
        || contribution.value(QStringLiteral("contract")) != QLatin1String("metadata@1") || !value.isObject()) return {};
    const auto policy = value.toObject();
    if (policy.size() != 2 || !policy.value(QStringLiteral("version")).isDouble()
        || policy.value(QStringLiteral("version")).toDouble() != 1
        || !policy.value(QStringLiteral("httpAbsent")).isArray()) return {};
    const auto rules = policy.value(QStringLiteral("httpAbsent")).toArray();
    if (rules.isEmpty() || rules.size() > 16) return {};
    QSet<QString> seen; QSet<int> result;
    for (const auto& item : rules) {
        if (!item.isObject()) return {};
        const auto rule = item.toObject();
        const auto f = rule.value(QStringLiteral("feature")), op = rule.value(QStringLiteral("operation"));
        if (rule.size() != 3 || !f.isString() || !op.isString() || f.toString().isEmpty() || op.toString().isEmpty()
            || f.toString().size() > 128 || op.toString().size() > 128 || !rule.value(QStringLiteral("statuses")).isArray()
            || !contribution.value(QStringLiteral("features")).toArray().contains(f)
            || !contribution.value(QStringLiteral("operations")).toArray().contains(op)) return {};
        const auto hasControl = [](const QString& text) { return std::any_of(text.cbegin(), text.cend(),
            [](QChar c) { return c.category() == QChar::Other_Control; }); };
        if (hasControl(f.toString()) || hasControl(op.toString())) return {};
        const auto key = f.toString() + QLatin1Char('\n') + op.toString(); if (seen.contains(key)) return {};
        seen.insert(key); const auto codes = rule.value(QStringLiteral("statuses")).toArray();
        if (codes.isEmpty() || codes.size() > 2) return {};
        QSet<int> statuses;
        for (const auto& code : codes) {
            if (!code.isDouble() || (code.toDouble() != 404 && code.toDouble() != 410)) return {};
            const int status = code.toInt(); if (statuses.contains(status)) return {};
            statuses.insert(status);
        }
        if (f.toString() == feature && op.toString() == operation) result = statuses;
    }
    return result;
}
}
std::optional<QSet<int>> metadataCompletionAbsentStatuses(const QJsonObject& contribution,
    const QString& feature, const QString& operation) { return parse(contribution, feature, operation); }
bool validateMetadataCompletionPolicy(const QJsonObject& contribution) { return parse(contribution, {}, {}).has_value(); }
}
