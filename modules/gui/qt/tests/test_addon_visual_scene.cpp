// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QJsonArray>
#include <QCryptographicHash>
#include "addons/addon_visual_scene.hpp"
using namespace vlc::addons;

class TestAddonVisualScene : public QObject
{
    Q_OBJECT
    static QJsonObject scene(const QJsonObject& node) {
        return {{"contract", "visualizer.scene@1"}, {"background", "theme.background"}, {"nodes", QJsonArray{node}}};
    }
    static QByteArray digest(const QImage& image) {
        return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()), QCryptographicHash::Sha256);
    }
    static AddonVisualScene::Colors colors() { return {QColor(Qt::black), QColor(Qt::white), QColor(Qt::cyan)}; }
private slots:
    void rejectsCodeUnknownFieldsAndUnboundedScenes() {
        QVERIFY(!AddonVisualScene::validate(scene({{"type", "shader"}, {"source", "void main(){}"}})));
        QVERIFY(!AddonVisualScene::validate(scene({{"type", "texture"}, {"asset", "https://example.org/image.png"}})));
        QVERIFY(!AddonVisualScene::validate(scene({{"type", "spectrum"}, {"count", 65}, {"color", "#123456"}})));
        QVERIFY(!AddonVisualScene::validate(scene({{"type", "waveform"}, {"points", 257}, {"color", "#123456"}})));
        const QJsonObject particle{{"type", "particles"}, {"count", 2048}, {"seed", 1}, {"speed", 0.1}, {"size", 0.01}, {"color", "theme.accent"}};
        auto many = scene(particle); many.insert("nodes", QJsonArray{particle, particle}); QVERIFY(!AddonVisualScene::validate(many));
        QJsonObject deep{{"type", "spectrum"}, {"count", 32}, {"color", "theme.accent"}};
        for (int i = 0; i < 10; ++i) deep = {{"type", "group"}, {"children", QJsonArray{deep}}};
        QVERIFY(!AddonVisualScene::validate(scene(deep)));
        QJsonArray excessive; for (int i = 0; i < 257; ++i) excessive.append(QJsonObject{{"type", "spectrum"}, {"count", 2}, {"color", "theme.accent"}});
        auto document = scene({}); document.insert("nodes", excessive); QVERIFY(!AddonVisualScene::validate(document));
        QVERIFY(!AddonVisualScene::validate(scene({{"type", "feedback"}, {"buffer", 0}, {"decay", 1.0}})));
    }
    void fourStylesProduceDifferentBoundedImages() {
        const QList<QJsonObject> styles{
            {{"type", "spectrum"}, {"count", 32}, {"color", "theme.accent"}},
            {{"type", "waveform"}, {"points", 128}, {"color", "theme.accent"}},
            {{"type", "spectrogram"}, {"color", "theme.accent"}},
            {{"type", "particles"}, {"count", 512}, {"seed", 7}, {"speed", 0.1}, {"size", 0.01}, {"color", "theme.accent"}}
        };
        AddonAudioAnalysis::Frame frame; frame.epoch = 1; frame.sequence = 1; frame.rms = 0.4;
        for (std::size_t i = 0; i < frame.bands.size(); ++i) frame.bands[i] = float(i + 1) / 64;
        for (std::size_t i = 0; i < frame.waveform.size(); ++i) frame.waveform[i] = i % 2 ? 0.4 : -0.4;
        QSet<QByteArray> images;
        for (const auto& style : styles) {
            QVERIFY(AddonVisualScene::validate(scene(style))); AddonVisualScene renderer;
            const auto image = renderer.render(scene(style), frame, 1000000, {4096, 2160}, colors(), 1, false);
            QVERIFY(!image.isNull()); QCOMPARE(image.width(), 512); QVERIFY(image.height() <= 512);
            images.insert(digest(image));
        }
        QCOMPARE(images.size(), 4);
    }
    void mediaClockAndReducedMotionControlParticles() {
        const auto document = scene({{"type", "particles"}, {"count", 128}, {"seed", 7}, {"speed", 0.3}, {"size", 0.02}, {"color", "theme.accent"}});
        AddonAudioAnalysis::Frame frame; frame.epoch = 1; frame.rms = 0.5; AddonVisualScene renderer;
        const auto first = renderer.render(document, frame, 1000000, {320, 240}, colors(), 1, false);
        QCOMPARE(digest(first), digest(renderer.render(document, frame, 1000000, {320, 240}, colors(), 1, false)));
        QVERIFY(digest(first) != digest(renderer.render(document, frame, 2000000, {320, 240}, colors(), 1, false)));
        const auto reduced = renderer.render(document, frame, 1000000, {320, 240}, colors(), 1, true);
        QCOMPARE(digest(reduced), digest(renderer.render(document, frame, 2000000, {320, 240}, colors(), 1, true)));
    }
    void feedbackIsBoundedAndClearedOnEpochChange() {
        AddonVisualScene renderer; AddonAudioAnalysis::Frame frame; frame.epoch = 1; frame.bands.fill(0.5);
        const QJsonObject feedback{{"type", "feedback"}, {"buffer", 0}, {"decay", 0.8}, {"scale", 1.1}};
        auto document = scene(feedback); auto nodes = document.value("nodes").toArray();
        nodes.append(QJsonObject{{"type", "spectrum"}, {"count", 8}, {"color", "#ff0000"}}); document.insert("nodes", nodes);
        QVERIFY(!renderer.render(document, frame, 0, {10000, 10000}, colors(), 2, false).isNull());
        frame.epoch = 2; frame.bands.fill(0);
        const auto blank = renderer.render(scene(feedback), frame, 0, {320, 240}, colors(), 1, false);
        QCOMPARE(blank.pixelColor(160, 200), QColor(Qt::black));
        QVERIFY(renderer.render(document, frame, 0, {0, 240}, colors(), 1, false).isNull());
    }
    void onlyHostSuppliedTextureCanAppear() {
        auto document = scene({{"type", "texture"}, {"asset", "tile"}});
        document.insert("assets", QJsonObject{{"tile", "images/tile.png"}});
        AddonVisualScene renderer; AddonAudioAnalysis::Frame frame;
        QImage tile(16, 16, QImage::Format_ARGB32_Premultiplied); tile.fill(Qt::red);
        const auto blank = renderer.render(document, frame, 0, {32, 32}, colors(), 1, false);
        QCOMPARE(blank.pixelColor(16, 16), QColor(Qt::black));
        const auto image = renderer.render(document, frame, 0, {32, 32}, colors(), 1, false, {{QStringLiteral("tile"), tile}});
        QCOMPARE(image.pixelColor(16, 16), QColor(Qt::red));
    }
};
QTEST_GUILESS_MAIN(TestAddonVisualScene)
#include "test_addon_visual_scene.moc"
