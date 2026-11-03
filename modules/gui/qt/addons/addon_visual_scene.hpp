// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_audio_analysis.hpp"
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QMap>

namespace vlc::addons {
class AddonVisualScene
{
public:
    struct Colors { QColor background, foreground, accent; };
    static bool validate(const QJsonObject&, QString* error = nullptr);
    // The host supplies decoded assets. A scene cannot open a file or a URL.
    QImage render(const QJsonObject&, const AddonAudioAnalysis::Frame&, qint64 mediaTimeUs,
                  QSize, const Colors&, int quality, bool reducedMotion,
                  const QMap<QString, QImage>& assets = {});
    void clear();
private:
    std::array<QImage, 2> m_feedback;
    QImage m_spectrogram;
    std::uint64_t m_epoch = 0, m_sequence = 0;
};
}
