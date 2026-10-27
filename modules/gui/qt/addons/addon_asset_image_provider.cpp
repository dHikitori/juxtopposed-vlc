// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_asset_image_provider.hpp"

namespace vlc::addons {
AddonAssetImageProvider::AddonAssetImageProvider(AddonUiAssets* assets)
    : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading),
      m_images(assets ? assets->m_images : nullptr) {}
QImage AddonAssetImageProvider::requestImage(const QString& id, QSize* size, const QSize&)
{ return AddonUiAssets::image(m_images, id, size); }
}
