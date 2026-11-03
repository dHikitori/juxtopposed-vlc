// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonObject>
#include <QVariantMap>

namespace vlc::addons::themeDocument {
bool isColor(const QString&);
bool validate(const QJsonObject&,QString* error=nullptr);
QVariantMap resolve(const QJsonObject&,bool light,const QString& accent=QString(),
                    bool highContrast=false,bool reducedTransparency=false);
}
