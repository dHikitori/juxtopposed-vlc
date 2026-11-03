// SPDX-License-Identifier: GPL-2.0-or-later
// Host validation for installed metadata completion rules.
#pragma once
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <optional>
namespace vlc::addons {
// An absent policy returns an empty set. Invalid/unknown policy returns nullopt.
// The contribution must be the installed, integrity-checked metadata@1 row.
std::optional<QSet<int>> metadataCompletionAbsentStatuses(const QJsonObject& contribution,
    const QString& feature, const QString& operation);
bool validateMetadataCompletionPolicy(const QJsonObject& contribution);
}
