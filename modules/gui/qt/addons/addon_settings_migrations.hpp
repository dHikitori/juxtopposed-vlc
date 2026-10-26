// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manifest.hpp"

namespace vlc::addons {
bool validateSettingsMigrations(const Manifest&, const QMap<QString, QByteArray>&, QString* error = nullptr);
bool migrateSettings(const Manifest& next, const QMap<QString, QByteArray>& contents,
                     const Manifest* previous, const QJsonObject& settings,
                     const QJsonObject& grants, QJsonObject* output, QString* error = nullptr);
}
