// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

namespace vlc::addons {
// The retention record of a metadata result (data.retention). The provider
// states what it permits; the host clamps each value to its own limits.
//
//   "retention": {
//     "reuseSeconds": 0..86400,     how long the host may reuse the answer
//     "keep": "memory" | "library", whether accepted fields may be saved
//     "offlineSeconds": 0..604800   how long an expired answer may be shown
//                                   while the provider is offline
//   }
//
// Every key is optional. Without a record the host uses short defaults,
// keeps fields in memory only and shows no expired answer. A record that is
// not correct permits nothing: no reuse, no saving and no offline use.
struct ResultRetention
{
    static constexpr qint64 MaxReuseSeconds = 86400;
    static constexpr qint64 MaxOfflineSeconds = 604800;

    bool valid = true;
    bool declared = false;
    qint64 reuseMs = 0;
    qint64 offlineMs = 0;
    bool library = false;

    // Host defaults and limits for each result status, in seconds. Errors,
    // timeouts and sign-in requests are never kept as answers.
    static qint64 defaultReuse(const QString& status)
    {
        if (status == QLatin1String("match") || status == QLatin1String("partial")) return 600;
        if (status == QLatin1String("no_match") || status == QLatin1String("ambiguous")) return 60;
        return 0;
    }
    static qint64 maximumReuse(const QString& status)
    {
        if (status == QLatin1String("match") || status == QLatin1String("partial")) return MaxReuseSeconds;
        if (status == QLatin1String("no_match")) return 3600;
        if (status == QLatin1String("ambiguous")) return 600;
        return 0;
    }

    static ResultRetention from(const QString& status, const QVariantMap& data)
    {
        ResultRetention result;
        const auto positive = status == QLatin1String("match") || status == QLatin1String("partial");
        result.reuseMs = defaultReuse(status) * 1000;
        if (!data.contains(QStringLiteral("retention"))) return result;
        result.declared = true;
        const auto value = data.value(QStringLiteral("retention"));
        if (value.metaType().id() != QMetaType::QVariantMap) return refused();
        const auto record = value.toMap();
        static const QStringList keys{QStringLiteral("reuseSeconds"), QStringLiteral("keep"), QStringLiteral("offlineSeconds")};
        for (auto it = record.cbegin(); it != record.cend(); ++it) if (!keys.contains(it.key())) return refused();
        const auto seconds = [&record](const QString& name, qint64 limit, qint64* out) {
            if (!record.contains(name)) return true;
            const auto item = record.value(name);
            bool number = false;
            const double amount = item.toDouble(&number);
            const auto type = item.metaType().id();
            if (!number || (type != QMetaType::Double && type != QMetaType::Int && type != QMetaType::LongLong)
                || !std::isfinite(amount) || amount < 0 || amount > limit || std::floor(amount) != amount) return false;
            *out = qint64(amount);
            return true;
        };
        qint64 reuse = defaultReuse(status), offline = 0;
        if (!seconds(QStringLiteral("reuseSeconds"), MaxReuseSeconds, &reuse)
            || !seconds(QStringLiteral("offlineSeconds"), MaxOfflineSeconds, &offline)) return refused();
        if (record.contains(QStringLiteral("keep"))) {
            const auto keep = record.value(QStringLiteral("keep"));
            if (keep.metaType().id() != QMetaType::QString) return refused();
            if (keep.toString() == QLatin1String("library")) result.library = true;
            else if (keep.toString() != QLatin1String("memory")) return refused();
        }
        result.reuseMs = std::min(reuse, maximumReuse(status)) * 1000;
        // Only an answer with fields can be shown while offline.
        result.offlineMs = positive ? offline * 1000 : 0;
        result.library = result.library && positive;
        return result;
    }
    static ResultRetention refused()
    {
        ResultRetention result;
        result.valid = false;
        result.declared = true;
        return result;
    }
    // The record the host attaches to field provenance.
    QVariantMap provenance() const
    {
        return {{QStringLiteral("keep"), library ? QStringLiteral("library") : QStringLiteral("memory")}};
    }
    // True when provenance permits saving the field outside memory.
    static bool mayKeep(const QVariantMap& provenance)
    {
        return provenance.value(QStringLiteral("retention")).toMap().value(QStringLiteral("keep")).toString()
            == QLatin1String("library");
    }
};
}
