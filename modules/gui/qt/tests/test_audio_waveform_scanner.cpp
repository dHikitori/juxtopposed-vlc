// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QUrl>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <limits>
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_modules.h>
#include "../../../../lib/libvlc_internal.h"
#include "../player/audio_waveform_scanner.hpp"

class AudioWaveformScannerTest : public QObject
{
    Q_OBJECT
    libvlc_instance_t* m_vlc = nullptr;
    QTemporaryDir m_directory;
    AudioWaveformSource source() const {
        AudioWaveformSource source;
        source.uri = QUrl::fromLocalFile(m_directory.filePath(QStringLiteral("audio.wav"))).toEncoded();
        source.bins = 16;source.deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
        return source;
    }
    AudioWaveformScan scan(AudioWaveformSource source, bool killed = false) const {
        auto* interrupt = vlc_interrupt_create();
        if (killed) vlc_interrupt_kill(interrupt);
        auto result = scanAudioWaveform(VLC_OBJECT(m_vlc->p_libvlc_int), source, interrupt);
        vlc_interrupt_destroy(interrupt);return result;
    }
    void writeWav(int bits, int channels, bool floating) {
        constexpr int rate = 8000;
        const int bytes = bits / 8;QByteArray data(rate * channels * bytes, '\0');
        for (int frame = 0; frame < rate; ++frame) {
            const double amplitude = frame < rate / 4 ? 0 : frame < rate / 2 ? .25 : frame < 3 * rate / 4 ? .8 : .5;
            const double sample = amplitude * std::sin(2 * M_PI * 100 * frame / rate);
            for (int channel = 0; channel < channels; ++channel) {
                const double value = channels == 2 && channel == 0 ? 0 : sample;
                char* output = data.data() + (frame * channels + channel) * bytes;
                if (floating) {
                    const float value32 = value;quint32 encoded;memcpy(&encoded, &value32, sizeof(encoded));qToLittleEndian(encoded, output);
                } else if (bits == 8) *output = static_cast<quint8>(std::lround(value * 127 + 128));
                else if (bits == 16) qToLittleEndian<qint16>(std::lround(value * 32767), output);
                else if (bits == 24) { const qint32 value24 = std::lround(value * 8388607);for (int i = 0; i < 3; ++i) output[i] = (value24 >> (i * 8)) & 0xff; }
                else qToLittleEndian<qint32>(std::llround(value * 2147483647.), output);
            }
        }
        QByteArray wav(44, '\0');memcpy(wav.data(), "RIFF", 4);qToLittleEndian<quint32>(36 + data.size(), wav.data() + 4);
        memcpy(wav.data() + 8, "WAVEfmt ", 8);qToLittleEndian<quint32>(16, wav.data() + 16);
        qToLittleEndian<quint16>(floating ? 3 : 1, wav.data() + 20);qToLittleEndian<quint16>(channels, wav.data() + 22);
        qToLittleEndian<quint32>(rate, wav.data() + 24);qToLittleEndian<quint32>(rate * channels * bytes, wav.data() + 28);
        qToLittleEndian<quint16>(channels * bytes, wav.data() + 32);qToLittleEndian<quint16>(bits, wav.data() + 34);
        memcpy(wav.data() + 36, "data", 4);qToLittleEndian<quint32>(data.size(), wav.data() + 40);wav.append(data);
        QFile file(m_directory.filePath(QStringLiteral("audio.wav")));QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(wav), wav.size());
    }
private slots:
    void initTestCase() {
        const char* args[] = {"--ignore-config", "--no-media-library", "--no-plugins-cache", "--quiet"};
        m_vlc = libvlc_new(4, args);QVERIFY(m_vlc);QVERIFY(m_directory.isValid());
    }
    void cleanupTestCase() { libvlc_release(m_vlc); }
    void acceptsFiniteFilesIncludingShares_data() {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<qint64>("duration");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("local") << QStringLiteral("file:///C:/Music/song.mp3") << qint64(VLC_TICK_FROM_SEC(350)) << true;
        QTest::newRow("mapped drive") << QStringLiteral("file:///Z:/Music/song.mp3") << qint64(VLC_TICK_FROM_SEC(350)) << true;
        QTest::newRow("UNC share") << QStringLiteral("file://server/share/Music/song.mp3") << qint64(VLC_TICK_FROM_SEC(350)) << true;
        QTest::newRow("UNC escaped spaces") << QStringLiteral("file://server/share/My%20Music/song.mp3") << qint64(VLC_TICK_FROM_SEC(350)) << true;
        QTest::newRow("localhost") << QStringLiteral("file://localhost/Music/song.mp3") << qint64(VLC_TICK_FROM_SEC(350)) << true;
        QTest::newRow("live file") << QStringLiteral("file://server/share/Music/song.mp3") << qint64(0) << false;
        QTest::newRow("HTTP stream") << QStringLiteral("https://radio.example/song.mp3") << qint64(VLC_TICK_FROM_SEC(350)) << false;
        QTest::newRow("capture device") << QStringLiteral("dshow://") << qint64(VLC_TICK_FROM_SEC(350)) << false;
    }
    void acceptsFiniteFilesIncludingShares() {
        QFETCH(QString, uri);QFETCH(qint64, duration);QFETCH(bool, accepted);
        QCOMPARE(canScanAudioWaveform(QUrl(uri), duration), accepted);
    }
    void readsOwnedUncFixture() {
        const auto uri = qEnvironmentVariable("VLC_WAVEFORM_UNC_FIXTURE");
        if (uri.isEmpty()) QSKIP("No owned UNC fixture was provided.");
        const QUrl url(uri);
        QVERIFY(url.isLocalFile());QVERIFY(!url.host().isEmpty());
        QVERIFY(canScanAudioWaveform(url, VLC_TICK_FROM_SEC(8)));
        auto request = source();request.uri = url.toEncoded();request.duration = VLC_TICK_FROM_SEC(8);
        request.deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(15);
        const auto result = scan(request);
        QCOMPARE(result.status, AudioWaveformScan::Complete);QCOMPARE(result.peaks.size(), 16);
        for (int i = 0; i < result.peaks.size(); ++i) {
            const float expected = i < 4 ? 0 : i < 8 ? .25 : i < 12 ? .8 : .5;
            QVERIFY(std::abs(result.peaks[i] - expected) < .05);
        }
    }
    void readsAmplitudeOverTheWholeTrack_data() {
        QTest::addColumn<int>("bits");QTest::addColumn<int>("channels");QTest::addColumn<bool>("floating");
        QTest::newRow("unsigned 8-bit mono") << 8 << 1 << false;
        QTest::newRow("signed 16-bit mono") << 16 << 1 << false;
        QTest::newRow("signed 24-bit stereo") << 24 << 2 << false;
        QTest::newRow("signed 32-bit stereo") << 32 << 2 << false;
        QTest::newRow("float stereo") << 32 << 2 << true;
    }
    void readsAmplitudeOverTheWholeTrack() {
        QFETCH(int, bits);QFETCH(int, channels);QFETCH(bool, floating);writeWav(bits, channels, floating);
        const auto result = scan(source());QCOMPARE(result.status, AudioWaveformScan::Complete);QCOMPARE(result.peaks.size(), 16);
        for (int i = 0; i < result.peaks.size(); ++i) {
            const float expected = i < 4 ? 0 : i < 8 ? .25 : i < 12 ? .8 : .5;
            QVERIFY2(std::abs(result.peaks[i] - expected) < .025, qPrintable(QStringLiteral("bin %1: actual %2, expected %3").arg(i).arg(result.peaks[i]).arg(expected)));
        }
    }
    // FLAC sizes its output buffers from the decoder's own audio format,
    // which the scanner must prepare as the core's decoder owner does.
    void readsFlacWhoseDecoderSizesItsOwnBuffers() {
        if (!module_exists("flac") || !module_exists("flacsys")) QSKIP("This build has no FLAC decoder.");
        const auto path = QFINDTESTDATA("fixtures/waveform-steps.flac");
        QVERIFY(!path.isEmpty());
        auto request = source();request.uri = QUrl::fromLocalFile(path).toEncoded();
        const auto result = scan(request);QCOMPARE(result.status, AudioWaveformScan::Complete);QCOMPARE(result.peaks.size(), 16);
        for (int i = 0; i < result.peaks.size(); ++i) {
            const float expected = i < 4 ? 0 : i < 8 ? .25 : i < 12 ? .8 : .5;
            QVERIFY2(std::abs(result.peaks[i] - expected) < .025, qPrintable(QStringLiteral("bin %1: actual %2, expected %3").arg(i).arg(result.peaks[i]).arg(expected)));
        }
    }
    void progressiveCaptureKeepsGapsAndUsesMediaTime() {
        ProgressiveAudioWaveform waveform(VLC_TICK_FROM_SEC(10), 10);
        audio_sample_format_t format{};
        format.i_format = VLC_CODEC_FL32; format.i_channels = 2; format.i_rate = 4;
        const float samples[] = {0.f, .25f, -.8f, .1f, .5f, .2f, .1f, .4f};
        block_t block{}; block.p_buffer = (uint8_t*)samples;
        block.i_buffer = sizeof(samples); block.i_nb_samples = 4;
        waveform.append(VLC_TICK_FROM_SEC(2), 1., format, block);
        auto peaks = waveform.snapshot();
        QCOMPARE(peaks.size(), 10); QCOMPARE(peaks[0], -1.f); QCOMPARE(peaks[1], -1.f);
        QCOMPARE(peaks[2], .8f); QCOMPARE(peaks[3], -1.f);
        waveform.append(VLC_TICK_FROM_SEC(8), 1., format, block);
        peaks = waveform.snapshot();
        QCOMPARE(peaks[8], .8f); QCOMPARE(peaks[7], -1.f); QCOMPARE(peaks[9], -1.f);
        ProgressiveAudioWaveform cached(VLC_TICK_FROM_SEC(10), 10);
        QVERIFY(cached.restore(peaks)); QCOMPARE(cached.snapshot(), peaks);
        peaks[1] = std::numeric_limits<float>::quiet_NaN(); QVERIFY(!cached.restore(peaks));
        QCOMPARE(cached.snapshot()[1], -1.f);
    }
    void progressiveCaptureHandlesRateSilenceAndUnsupportedAudio() {
        ProgressiveAudioWaveform waveform(VLC_TICK_FROM_SEC(4), 4);
        audio_sample_format_t format{};
        format.i_format = VLC_CODEC_S16N; format.i_channels = 1; format.i_rate = 2;
        const int16_t samples[] = {0, 16384, -32768, 0};
        block_t block{}; block.p_buffer = (uint8_t*)samples;
        block.i_buffer = sizeof(samples); block.i_nb_samples = 4;
        waveform.append(0, 2., format, block);
        QCOMPARE(waveform.snapshot(), QVector<float>({0.f, .5f, 1.f, 0.f}));
        format.i_format = VLC_CODEC_A52;
        waveform.append(0, 1., format, block);
        QCOMPARE(waveform.snapshot(), QVector<float>({0.f, .5f, 1.f, 0.f}));
    }
    void missingTrackDoesNotInventPeaks() {
        writeWav(16, 1, false);auto request = source();request.audioIndex = 1;
        auto result = scan(request);QCOMPARE(result.status, AudioWaveformScan::TrackMissing);QVERIFY(result.peaks.isEmpty());
    }
    void cancelledRequestsHaveNoPeaks() {
        auto result = scan(source(), true);QCOMPARE(result.status, AudioWaveformScan::Cancelled);QVERIFY(result.peaks.isEmpty());
    }
    void expiredRequestsHaveNoPeaks() {
        auto request = source();request.deadline = vlc_tick_now() - 1;auto result = scan(request);
        QCOMPARE(result.status, AudioWaveformScan::LimitReached);QVERIFY(result.peaks.isEmpty());
    }
    void refusesUnboundedBinAllocations() {
        auto request = source();request.bins = INT_MAX;auto result = scan(request);
        QCOMPARE(result.status, AudioWaveformScan::LimitReached);QVERIFY(result.peaks.isEmpty());
    }
    void missingSourceFailsCleanly() {
        auto request = source();request.uri += ".missing";auto result = scan(request);
        QCOMPARE(result.status, AudioWaveformScan::Failed);QVERIFY(result.peaks.isEmpty());
    }
};
QTEST_GUILESS_MAIN(AudioWaveformScannerTest)
#include "test_audio_waveform_scanner.moc"
