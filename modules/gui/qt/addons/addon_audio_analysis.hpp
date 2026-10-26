// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace vlc::addons {
// The audio callback has one producer. It does not allocate or take a lock.
class AddonAudioAnalysis
{
public:
    enum class Format { Float32, Signed16, Signed32 };
    static constexpr std::size_t SampleCount = 1024, BandCount = 64, WaveCount = 256;
    struct Frame {
        std::uint64_t epoch = 0, sequence = 0;
        std::int64_t timeUs = 0;
        unsigned sampleRate = 0, channels = 0;
        float rms = 0, peak = 0;
        std::array<float, BandCount> bands{};
        std::array<float, WaveCount> waveform{};
    };
    AddonAudioAnalysis();
    ~AddonAudioAnalysis();
    AddonAudioAnalysis(const AddonAudioAnalysis&) = delete;
    AddonAudioAnalysis& operator=(const AddonAudioAnalysis&) = delete;
    void setActive(bool active);
    void reset(std::uint64_t epoch);
    // data is interleaved. frames and bytes must describe the same block.
    bool append(std::uint64_t epoch, Format, const void* data, std::size_t bytes, std::size_t frames,
                unsigned channels, unsigned sampleRate, std::int64_t timeUs, double rate);
    bool frameAt(std::uint64_t epoch, std::int64_t mediaTimeUs, Frame*) const;
    std::uint64_t dropped() const { return m_dropped.load(std::memory_order_relaxed); }
private:
    struct Input {
        std::uint64_t epoch = 0;
        std::int64_t timeUs = 0;
        unsigned sampleRate = 0, channels = 0;
        std::size_t count = 0;
        std::array<float, SampleCount> samples{};
    };
    static constexpr std::size_t InputSlots = 8, OutputSlots = 128;
    void run();
    static Frame analyze(const Input&);
    std::array<Input, InputSlots> m_inputs{};
    std::atomic<std::uint64_t> m_read{0}, m_write{0}, m_epoch{0}, m_dropped{0};
    std::atomic<bool> m_active{false}, m_stop{false};
    mutable std::mutex m_outputMutex;
    std::array<Frame, OutputSlots> m_outputs{};
    std::uint64_t m_sequence = 0;
    std::size_t m_outputCount = 0, m_outputNext = 0;
    std::thread m_thread;
};
}
