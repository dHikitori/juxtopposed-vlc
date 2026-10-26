// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_audio_analysis.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <limits>

namespace vlc::addons {
namespace {
constexpr double pi = 3.14159265358979323846;
template<typename T> T load(const unsigned char* data) { T value; std::memcpy(&value, data, sizeof value); return value; }
}
AddonAudioAnalysis::AddonAudioAnalysis() : m_thread([this] { run(); }) {}
AddonAudioAnalysis::~AddonAudioAnalysis()
{ m_active.store(false, std::memory_order_release); m_stop.store(true, std::memory_order_release); m_thread.join(); }
void AddonAudioAnalysis::setActive(bool active) { m_active.store(active, std::memory_order_release); }
void AddonAudioAnalysis::reset(std::uint64_t epoch)
{
    m_epoch.store(epoch, std::memory_order_release);
    std::lock_guard<std::mutex> lock(m_outputMutex); m_outputCount = 0; m_outputNext = 0;
}
bool AddonAudioAnalysis::append(std::uint64_t epoch, Format format, const void* data, std::size_t bytes, std::size_t frames,
                               unsigned channels, unsigned sampleRate, std::int64_t timeUs, double rate)
{
    if (!m_active.load(std::memory_order_acquire) || epoch != m_epoch.load(std::memory_order_acquire)) return false;
    const std::size_t sampleBytes = format == Format::Signed16 ? 2 : 4;
    if (!data || !frames || !channels || channels > 32 || sampleRate < 8000 || sampleRate > 384000
        || timeUs < 0 || !std::isfinite(rate) || rate <= 0 || rate > 16
        || frames > bytes / sampleBytes / channels) return false;
    const auto write = m_write.load(std::memory_order_relaxed);
    if (write - m_read.load(std::memory_order_acquire) >= InputSlots) { m_dropped.fetch_add(1, std::memory_order_relaxed); return false; }
    auto& input = m_inputs[write % InputSlots];
    input.epoch = epoch; input.timeUs = timeUs;
    input.sampleRate = sampleRate; input.channels = channels; input.count = std::min(frames, SampleCount);
    const auto* source = static_cast<const unsigned char*>(data);
    // Copy at most 2,048 source samples. Surround channels do not increase this bound.
    const unsigned observedChannels = std::min(channels, 2u);
    for (std::size_t frame = 0; frame < input.count; ++frame) {
        double sum = 0;
        for (unsigned channel = 0; channel < observedChannels; ++channel) {
            const auto* sample = source + (frame * channels + channel) * sampleBytes;
            double value = 0;
            switch (format) {
            case Format::Float32: value = load<float>(sample); break;
            case Format::Signed16: value = load<std::int16_t>(sample) / 32768.0; break;
            case Format::Signed32: value = load<std::int32_t>(sample) / 2147483648.0; break;
            }
            sum += std::isfinite(value) ? std::clamp(value, -1.0, 1.0) : 0;
        }
        input.samples[frame] = float(sum / observedChannels);
    }
    m_write.store(write + 1, std::memory_order_release); return true;
}
AddonAudioAnalysis::Frame AddonAudioAnalysis::analyze(const Input& input)
{
    Frame frame; frame.epoch = input.epoch; frame.timeUs = input.timeUs;
    frame.sampleRate = input.sampleRate; frame.channels = input.channels;
    std::array<std::complex<double>, SampleCount> fft{}; double sum = 0;
    for (std::size_t i = 0; i < input.count; ++i) {
        const float value = input.samples[i]; sum += double(value) * value; frame.peak = std::max(frame.peak, std::abs(value));
        fft[i] = value * (0.5 - 0.5 * std::cos(2 * pi * i / (SampleCount - 1)));
    }
    frame.rms = float(std::sqrt(sum / input.count));
    for (std::size_t i = 0; i < WaveCount; ++i) frame.waveform[i] = input.samples[std::min(input.count - 1, i * input.count / WaveCount)];
    for (std::size_t i = 1, j = 0; i < SampleCount; ++i) {
        std::size_t bit = SampleCount >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit; if (i < j) std::swap(fft[i], fft[j]);
    }
    for (std::size_t size = 2; size <= SampleCount; size <<= 1) {
        const auto step = std::polar(1.0, -2 * pi / size);
        for (std::size_t base = 0; base < SampleCount; base += size) {
            std::complex<double> phase = 1;
            for (std::size_t j = 0; j < size / 2; ++j) {
                const auto even = fft[base + j], odd = phase * fft[base + j + size / 2];
                fft[base + j] = even + odd; fft[base + j + size / 2] = even - odd; phase *= step;
            }
        }
    }
    for (std::size_t band = 0; band < BandCount; ++band) {
        const auto first = std::size_t(std::pow(512.0, double(band) / BandCount));
        const auto last = std::max(first + 1, std::size_t(std::pow(512.0, double(band + 1) / BandCount)));
        double magnitude = 0;
        for (auto bin = first; bin < std::min(last, SampleCount / 2); ++bin) magnitude = std::max(magnitude, std::abs(fft[bin]) * 4 / SampleCount);
        frame.bands[band] = float(std::clamp(magnitude, 0.0, 1.0));
    }
    return frame;
}
void AddonAudioAnalysis::run()
{
    while (!m_stop.load(std::memory_order_acquire)) {
        auto read = m_read.load(std::memory_order_relaxed); const auto write = m_write.load(std::memory_order_acquire);
        if (read == write) { std::this_thread::sleep_for(std::chrono::milliseconds(4)); continue; }
        // Keep only the newest waiting block. Audio playback does not wait for analysis.
        if (write - read > 1) { m_dropped.fetch_add(write - read - 1, std::memory_order_relaxed); read = write - 1; }
        const auto input = m_inputs[read % InputSlots];
        m_read.store(read + 1, std::memory_order_release);
        if (!m_active.load(std::memory_order_acquire) || input.epoch != m_epoch.load(std::memory_order_acquire)) continue;
        auto output = analyze(input);
        std::lock_guard<std::mutex> lock(m_outputMutex);
        if (output.epoch != m_epoch.load(std::memory_order_acquire)) continue;
        output.sequence = ++m_sequence; m_outputs[m_outputNext] = output;
        m_outputNext = (m_outputNext + 1) % OutputSlots; m_outputCount = std::min(OutputSlots, m_outputCount + 1);
    }
}
bool AddonAudioAnalysis::frameAt(std::uint64_t epoch, std::int64_t mediaTimeUs, Frame* result) const
{
    if (!result || !m_active.load(std::memory_order_acquire) || mediaTimeUs < 0) return false;
    std::lock_guard<std::mutex> lock(m_outputMutex); const Frame* best = nullptr;
    for (std::size_t i = 0; i < m_outputCount; ++i) {
        const auto& frame = m_outputs[i];
        if (frame.epoch == epoch && frame.timeUs <= mediaTimeUs && mediaTimeUs - frame.timeUs <= 200000
            && (!best || frame.timeUs > best->timeUs || (frame.timeUs == best->timeUs && frame.sequence > best->sequence))) best = &frame;
    }
    if (!best) return false;
    *result = *best; return true;
}
}
