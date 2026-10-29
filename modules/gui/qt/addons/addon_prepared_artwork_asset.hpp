// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <QVariantMap>
#include <functional>
#include <memory>
namespace vlc::addons {
class AddonManager;
// Host only. Create this value on joined work. It retains copied permissions
// and bounded secret bytes. current() reads memory only.
class PreparedArtworkAsset final {
public:
    ~PreparedArtworkAsset();
    PreparedArtworkAsset(const PreparedArtworkAsset&) = delete;
    PreparedArtworkAsset& operator=(const PreparedArtworkAsset&) = delete;
    bool current() const noexcept;
    QVariantMap registration() const;
private:
    friend class AddonManager;
    struct Data;
    explicit PreparedArtworkAsset(std::shared_ptr<Data>);
    const std::shared_ptr<Data> m_data;
};
using PreparedArtwork = std::shared_ptr<const PreparedArtworkAsset>;
using ArtworkPreparationComplete = std::function<void(PreparedArtwork,const QString&)>;
}
