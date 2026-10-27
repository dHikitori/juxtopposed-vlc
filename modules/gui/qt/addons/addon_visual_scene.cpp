// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visual_scene.hpp"
#include "addon_visualizer_parameters.hpp"
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace vlc::addons {
namespace {
bool keys(const QJsonObject& object, const QSet<QString>& allowed)
{ for (auto it = object.begin(); it != object.end(); ++it) if (!allowed.contains(it.key())) return false; return true; }
bool number(const QJsonValue& value, double low, double high)
{ return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= low && value.toDouble() <= high; }
bool integer(const QJsonValue& value, int low, int high)
{ return number(value, low, high) && value.toDouble() == value.toInt(); }
bool color(const QJsonValue& value)
{
    if (!value.isString()) return false;
    const auto text = value.toString();
    return text == QLatin1String("theme.background") || text == QLatin1String("theme.foreground") || text == QLatin1String("theme.accent")
        || QRegularExpression(QStringLiteral("^#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?$" )).match(text).hasMatch();
}
QColor resolveColor(const QJsonValue& value, const AddonVisualScene::Colors& colors)
{
    const auto text = value.toString();
    if (text == QLatin1String("theme.background")) return colors.background;
    if (text == QLatin1String("theme.foreground")) return colors.foreground;
    if (text == QLatin1String("theme.accent")) return colors.accent;
    return QColor(text);
}
QRectF rect(const QJsonObject& node, const QSize& size)
{
    const auto value = node.value("rect").toArray();
    return value.isEmpty() ? QRectF(QPointF(0, 0), size)
        : QRectF(value[0].toDouble() * size.width(), value[1].toDouble() * size.height(), value[2].toDouble() * size.width(), value[3].toDouble() * size.height());
}
double noise(unsigned seed) { seed ^= seed >> 16; seed *= 0x7feb352du; seed ^= seed >> 15; seed *= 0x846ca68bu; seed ^= seed >> 16; return double(seed) / 4294967296.0; }
}
bool AddonVisualScene::validate(const QJsonObject& scene, QString* error)
{
    const auto fail = [&] { if (error) *error = QStringLiteral("VLC cannot use this visual scene. Use a scene that follows visualizer.scene@1."); return false; };
    if (!keys(scene, {"contract", "background", "nodes", "assets", "parameters"}) || scene.value("contract").toString() != QLatin1String("visualizer.scene@1")
        || !color(scene.value("background")) || !scene.value("nodes").isArray()) return fail();
    const auto assets = scene.value("assets").toObject();
    if ((scene.contains("assets") && !scene.value("assets").isObject()) || assets.size() > 16) return fail();
    static const QRegularExpression assetId(QStringLiteral("^[a-zA-Z_][a-zA-Z0-9_-]{0,63}$"));
    for (auto it = assets.begin(); it != assets.end(); ++it) {
        const auto path = it.value().toString(); const auto parts = path.split('/');
        if (!assetId.match(it.key()).hasMatch() || QStringList{"__proto__", "prototype", "constructor"}.contains(it.key())
            || !it.value().isString() || path.size() > 240 || path.contains('\\')
            || path.contains(':') || parts.contains(QString()) || parts.contains(QStringLiteral(".")) || parts.contains(QStringLiteral(".."))) return fail();
    }
    int total = 0, particles = 0, feedback = 0;
    QSet<int> buffers;
    std::function<bool(const QJsonArray&, int)> nodes = [&](const QJsonArray& values, int depth) {
        if (depth > 8) return false;
        for (const auto& value : values) {
            if (!value.isObject() || ++total > 256) return false;
            const auto node = value.toObject(); const auto type = node.value("type").toString();
            QSet<QString> allowed{"type", "id", "rect", "opacity"};
            if (type == QLatin1String("spectrum")) allowed.unite({"count", "gain", "color"});
            else if (type == QLatin1String("waveform")) allowed.unite({"points", "gain", "color", "lineWidth"});
            else if (type == QLatin1String("spectrogram")) allowed.unite({"color", "gain"});
            else if (type == QLatin1String("particles")) allowed.unite({"count", "seed", "speed", "size", "color"});
            else if (type == QLatin1String("gradient")) allowed.unite({"stops", "vertical"});
            else if (type == QLatin1String("group")) allowed.unite({"children", "rotation", "scale"});
            else if (type == QLatin1String("feedback")) allowed.unite({"buffer", "decay", "scale"});
            else if (type == QLatin1String("texture")) allowed.insert("asset");
            else return false;
            if (!keys(node, allowed)) return false;
            if (node.contains("rect")) {
                const auto box = node.value("rect").toArray();
                if (!node.value("rect").isArray() || box.size() != 4 || !number(box[0], -2, 2) || !number(box[1], -2, 2)
                    || !number(box[2], 0.001, 4) || !number(box[3], 0.001, 4)) return false;
            }
            if (node.contains("opacity") && !number(node.value("opacity"), 0, 1)) return false;
            if (node.contains("color") && !color(node.value("color"))) return false;
            if (node.contains("gain") && !number(node.value("gain"), 0, 8)) return false;
            if ((type == QLatin1String("spectrum") || type == QLatin1String("waveform") || type == QLatin1String("spectrogram") || type == QLatin1String("particles")) && !color(node.value("color"))) return false;
            if (type == QLatin1String("spectrum") && !integer(node.value("count"), 1, 64)) return false;
            if (type == QLatin1String("waveform") && (!integer(node.value("points"), 2, 256) || (node.contains("lineWidth") && !number(node.value("lineWidth"), 0.1, 8)))) return false;
            if (type == QLatin1String("particles")) {
                if (!integer(node.value("count"), 1, 2048) || !integer(node.value("seed"), 0, 1000000)
                    || !number(node.value("speed"), 0, 2) || !number(node.value("size"), 0.001, 0.1)) return false;
                particles += node.value("count").toInt(); if (particles > 2048) return false;
            }
            if (type == QLatin1String("gradient")) {
                const auto stops = node.value("stops").toArray(); double previous = -1;
                if (!node.value("vertical").isBool() || stops.size() < 2 || stops.size() > 8) return false;
                for (const auto& stop : stops) {
                    const auto pair = stop.toArray();
                    if (pair.size() != 2 || !number(pair[0], 0, 1) || pair[0].toDouble() <= previous || !color(pair[1])) return false;
                    previous = pair[0].toDouble();
                }
            }
            if (type == QLatin1String("group") && (!node.value("children").isArray() || !nodes(node.value("children").toArray(), depth + 1)
                || (node.contains("rotation") && !number(node.value("rotation"), -360, 360)) || (node.contains("scale") && !number(node.value("scale"), 0.1, 4)))) return false;
            if (type == QLatin1String("feedback")) {
                if (!integer(node.value("buffer"), 0, 1) || !number(node.value("decay"), 0, 0.99)
                    || (node.contains("scale") && !number(node.value("scale"), 0.5, 1.5)) || ++feedback > 2) return false;
                const int buffer = node.value("buffer").toInt(); if (buffers.contains(buffer)) return false; buffers.insert(buffer);
            }
            if (type == QLatin1String("texture") && (!node.value("asset").isString()
                || !assets.contains(node.value("asset").toString()))) return false;
        }
        return true;
    };
    return nodes(scene.value("nodes").toArray(), 0) && AddonVisualizerParameters::validate(scene) ? true : fail();
}
void AddonVisualScene::clear()
{ m_feedback = {}; m_spectrogram = {}; m_epoch = m_sequence = 0; }
QImage AddonVisualScene::render(const QJsonObject& scene, const AddonAudioAnalysis::Frame& frame, qint64 mediaTimeUs,
                              QSize requested, const Colors& colors, int quality, bool reducedMotion, const QMap<QString, QImage>& assets)
{
    if (!validate(scene) || mediaTimeUs < 0 || requested.isEmpty()) return {};
    quality = qBound(0, quality, 2); const int maximum = quality == 2 ? 1024 : quality == 1 ? 512 : 256;
    const QSize size = requested.width() > maximum || requested.height() > maximum
        ? requested.scaled(maximum, maximum, Qt::KeepAspectRatio) : requested;
    if (frame.epoch != m_epoch) { clear(); m_epoch = frame.epoch; }
    if (m_spectrogram.isNull()) { m_spectrogram = QImage(128, 64, QImage::Format_RGB32); m_spectrogram.fill(Qt::black); }
    if (frame.sequence != m_sequence && !reducedMotion) {
        std::memmove(m_spectrogram.bits(), m_spectrogram.constBits() + m_spectrogram.bytesPerLine(), std::size_t(m_spectrogram.bytesPerLine()) * 63);
        auto* row = reinterpret_cast<QRgb*>(m_spectrogram.scanLine(63));
        for (int x = 0; x < 128; ++x) { const auto level = int(std::clamp(frame.bands[x / 2], 0.0f, 1.0f) * 255); row[x] = qRgb(level, level, level); }
        m_sequence = frame.sequence;
    }
    QImage image(size, QImage::Format_ARGB32_Premultiplied); image.fill(resolveColor(scene.value("background"), colors));
    QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing, quality > 0);
    QSet<int> feedback;
    std::function<void(const QJsonArray&, const QSize&)> draw = [&](const QJsonArray& nodes, const QSize& viewport) {
        for (const auto& value : nodes) {
            const auto node = value.toObject(); const auto type = node.value("type").toString(); const auto box = rect(node, viewport);
            painter.save(); painter.setOpacity(painter.opacity() * node.value("opacity").toDouble(1)); painter.setClipRect(box, Qt::IntersectClip);
            const auto c = resolveColor(node.value("color"), colors); const double gain = node.value("gain").toDouble(1);
            painter.setBrush(c); painter.setPen(Qt::NoPen);
            if (type == QLatin1String("spectrum")) {
                const int count = node.value("count").toInt(); const double width = box.width() / count;
                for (int i = 0; i < count; ++i) { const double height = box.height() * std::clamp(double(frame.bands[std::size_t(i) * 64 / count]) * gain, 0.0, 1.0); painter.drawRect(QRectF(box.x() + i * width, box.bottom() - height, width * 0.8, height)); }
            } else if (type == QLatin1String("waveform")) {
                const int count = node.value("points").toInt(); QPainterPath path;
                for (int i = 0; i < count; ++i) { const QPointF point(box.x() + box.width() * i / (count - 1), box.center().y() - box.height() * 0.5 * std::clamp(double(frame.waveform[std::size_t(i) * 256 / count]) * gain, -1.0, 1.0)); if (!i) path.moveTo(point); else path.lineTo(point); }
                painter.setBrush(Qt::NoBrush); painter.setPen(QPen(c, node.value("lineWidth").toDouble(1))); painter.drawPath(path);
            } else if (type == QLatin1String("spectrogram")) {
                QImage tinted = m_spectrogram.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                for (int y = 0; y < tinted.height(); ++y) {
                    auto* pixels = reinterpret_cast<QRgb*>(tinted.scanLine(y));
                    for (int x = 0; x < tinted.width(); ++x) {
                        const double level = std::clamp(qRed(pixels[x]) / 255.0 * gain, 0.0, 1.0);
                        pixels[x] = qPremultiply(qRgba(int(c.red() * level), int(c.green() * level), int(c.blue() * level), c.alpha()));
                    }
                }
                painter.drawImage(box, tinted);
            }
            else if (type == QLatin1String("gradient")) {
                QLinearGradient gradient(box.topLeft(), node.value("vertical").toBool() ? box.bottomLeft() : box.topRight());
                for (const auto& stop : node.value("stops").toArray()) { const auto pair = stop.toArray(); gradient.setColorAt(pair[0].toDouble(), resolveColor(pair[1], colors)); }
                painter.fillRect(box, gradient);
            } else if (type == QLatin1String("particles")) {
                const int count = std::max(1, node.value("count").toInt() / (quality == 2 ? 1 : quality == 1 ? 2 : 4));
                const unsigned seed = unsigned(node.value("seed").toInt()); const double time = reducedMotion ? 0 : double(mediaTimeUs) / 1000000 * node.value("speed").toDouble();
                const double radius = std::min(box.width(), box.height()) * node.value("size").toDouble() * (0.5 + frame.rms);
                for (int i = 0; i < count; ++i) { const unsigned n = unsigned(i) + seed * 131u; const double x = std::fmod(noise(n) + time * (0.1 + noise(n + 1)), 1.0); const double y = std::fmod(noise(n + 3) + time * (0.1 + noise(n + 4)), 1.0); painter.drawEllipse(QPointF(box.x() + x * box.width(), box.y() + y * box.height()), radius, radius); }
            } else if (type == QLatin1String("group")) {
                painter.translate(box.center()); painter.rotate(node.value("rotation").toDouble()); const double scale = node.value("scale").toDouble(1); painter.scale(scale, scale); painter.translate(-box.width() / 2, -box.height() / 2);
                draw(node.value("children").toArray(), box.size().toSize());
            } else if (type == QLatin1String("feedback") && !reducedMotion) {
                const int buffer = node.value("buffer").toInt(); feedback.insert(buffer);
                painter.setOpacity(painter.opacity() * node.value("decay").toDouble());
                QRectF destination = box; const double scale = node.value("scale").toDouble(1); destination.setSize(box.size() * scale); destination.moveCenter(box.center()); painter.drawImage(destination, m_feedback[buffer]);
            } else if (type == QLatin1String("texture")) {
                const auto asset = assets.value(node.value("asset").toString());
                if (asset.width() <= 1024 && asset.height() <= 1024) painter.drawImage(box, asset);
            }
            painter.restore();
        }
    };
    draw(scene.value("nodes").toArray(), size); painter.end();
    for (const int buffer : feedback) m_feedback[buffer] = image;
    return image;
}
}
