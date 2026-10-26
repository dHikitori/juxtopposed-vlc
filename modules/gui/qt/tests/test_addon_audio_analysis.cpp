// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "addons/addon_audio_analysis.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace vlc::addons;

class TestAddonAudioAnalysis : public QObject
{
    Q_OBJECT
    using Analysis = AddonAudioAnalysis;
private slots:
    void spectrumAndEnvelopeUseDecodedSamples() {
        Analysis analysis; analysis.reset(7); analysis.setActive(true);
        std::array<float, 2048> samples{};
        for (std::size_t i = 0; i < 1024; ++i) samples[i * 2] = samples[i * 2 + 1] = float(0.5 * std::sin(2 * 3.141592653589793 * 64 * i / 1024));
        QVERIFY(analysis.append(7, Analysis::Format::Float32, samples.data(), sizeof samples, 1024, 2, 48000, 1000000, 1.0));
        Analysis::Frame frame; QTRY_VERIFY(analysis.frameAt(7, 1000000, &frame));
        QCOMPARE(frame.channels, 2u); QCOMPARE(frame.sampleRate, 48000u);
        QVERIFY(std::abs(frame.rms - 0.353553f) < 0.001f); QVERIFY(std::abs(frame.peak - 0.5f) < 0.001f);
        const auto loudest = std::max_element(frame.bands.begin(), frame.bands.end());
        QVERIFY(*loudest > 0.45f); QVERIFY(*loudest < 0.55f);
        const auto band = std::size_t(loudest - frame.bands.begin());
        QVERIFY(band >= 40 && band <= 44);
        QVERIFY(!analysis.frameAt(7, 999999, &frame));
        QVERIFY(!analysis.frameAt(7, 1200001, &frame));
    }
    void formatsAndShortBlocksStayFinite() {
        Analysis analysis; analysis.reset(1); analysis.setActive(true);
        std::array<std::int16_t, 17> samples{}; samples.fill(16384);
        QVERIFY(analysis.append(1, Analysis::Format::Signed16, samples.data(), sizeof samples, samples.size(), 1, 44100, 10, 1.0));
        Analysis::Frame frame; QTRY_VERIFY(analysis.frameAt(1, 10, &frame));
        QCOMPARE(frame.rms, 0.5f); QCOMPARE(frame.waveform.back(), 0.5f);
        QVERIFY(!analysis.append(1, Analysis::Format::Signed16, samples.data(), 1, 17, 1, 44100, 10, 1.0));
        QVERIFY(!analysis.append(1, Analysis::Format::Signed16, samples.data(), sizeof samples, 0, 1, 44100, 10, 1.0));
        QVERIFY(!analysis.append(1, Analysis::Format::Signed16, samples.data(), sizeof samples, 17, 33, 44100, 10, 1.0));
        QVERIFY(!analysis.append(1, Analysis::Format::Signed16, samples.data(), sizeof samples, 17, 1, 0, 10, 1.0));
        std::array<float, 1024> invalid{}; invalid.fill(std::numeric_limits<float>::quiet_NaN());
        QVERIFY(analysis.append(1, Analysis::Format::Float32, invalid.data(), sizeof invalid, invalid.size(), 1, 44100, 20, 1.0));
        QTRY_VERIFY(analysis.frameAt(1, 20, &frame) && frame.timeUs == 20);
        QCOMPARE(frame.rms, 0.0f); QCOMPARE(frame.peak, 0.0f);
        for (const auto band : frame.bands) QVERIFY(std::isfinite(band));
        std::array<std::int32_t, 1024> integers{}; integers.fill(1073741824);
        QVERIFY(analysis.append(1, Analysis::Format::Signed32, integers.data(), sizeof integers, integers.size(), 1, 48000, 30, 1.0));
        QTRY_VERIFY(analysis.frameAt(1, 30, &frame) && frame.timeUs == 30); QCOMPARE(frame.rms, 0.5f);
    }
    void oldEpochAndInactiveObserversHaveNoFrames() {
        Analysis analysis; analysis.reset(1); analysis.setActive(true); std::array<float, 1024> samples{};
        QVERIFY(analysis.append(1, Analysis::Format::Float32, samples.data(), sizeof samples, samples.size(), 1, 48000, 0, 1));
        Analysis::Frame frame; QTRY_VERIFY(analysis.frameAt(1, 0, &frame));
        analysis.reset(2); QVERIFY(!analysis.frameAt(1, 0, &frame));
        QVERIFY(!analysis.append(1, Analysis::Format::Float32, samples.data(), sizeof samples, samples.size(), 1, 48000, 0, 1));
        QVERIFY(analysis.append(2, Analysis::Format::Float32, samples.data(), sizeof samples, samples.size(), 1, 48000, 0, 1));
        QTRY_VERIFY(analysis.frameAt(2, 0, &frame));
        analysis.setActive(false); QVERIFY(!analysis.frameAt(2, 0, &frame));
        QVERIFY(!analysis.append(2, Analysis::Format::Float32, samples.data(), sizeof samples, samples.size(), 1, 48000, 0, 1));
    }
    void overloadDropsInputAndKeepsTheNewestFrame() {
        Analysis analysis; analysis.reset(1); analysis.setActive(true); std::array<float, 1024> samples{};
        for (int i = 0; i < 10000; ++i)
            analysis.append(1, Analysis::Format::Float32, samples.data(), sizeof samples, samples.size(), 1, 48000, i, 1);
        QVERIFY(analysis.dropped() > 0);
        Analysis::Frame frame;
        QTRY_VERIFY(analysis.append(1, Analysis::Format::Float32, samples.data(), sizeof samples, samples.size(), 1, 48000, 20000, 1));
        QTRY_VERIFY(analysis.frameAt(1, 20000, &frame) && frame.timeUs == 20000);
    }
};
QTEST_GUILESS_MAIN(TestAddonAudioAnalysis)
#include "test_addon_audio_analysis.moc"
