// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonObject>
#include <QString>

namespace vlc::addons {
class AddonVisualizerParameters
{
public:
    // The scene parser validates the graph. These functions validate the
    // node IDs and the closed numeric parameter declarations and updates.
    static bool validate(const QJsonObject& scene, QString* error = nullptr);
    static bool apply(const QJsonObject& scene, const QJsonObject& values,
                      QJsonObject* output, QString* error = nullptr);
};
}
