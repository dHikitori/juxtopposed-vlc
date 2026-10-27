// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_ui_assets.hpp"
#include <QQuickImageProvider>

namespace vlc::addons {
class AddonAssetImageProvider final : public QQuickImageProvider
{
public:
    explicit AddonAssetImageProvider(AddonUiAssets*);
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
private:
    std::shared_ptr<AddonUiAssets::SharedImages> m_images;
};
}
