// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_player.h>
#include <vlc_aout.h>
#include <vlc_block.h>
#include <vlc_cxx_helpers.hpp>
#include "qt.hpp"
#include "addon_native_player_analysis.hpp"
#include "addon_visualizer_clock.hpp"
#include <atomic>
#include <mutex>

namespace vlc::addons {
using vlc_player_locker = vlc_locker<vlc_player_t, vlc_player_Lock, vlc_player_Unlock>;
struct AddonNativePlayerAnalysis::State {
    vlc_player_t* player;
    vlc_player_metadata_listener_id* samples = nullptr;
    vlc_player_timer_id* timer = nullptr;
    vlc_player_listener_id* listener = nullptr;
    vlc_player_aout_listener_id* outputListener = nullptr;
    audio_output_t* samplesOutput = nullptr;
    AddonAudioAnalysis* analysis = nullptr;
    std::uint64_t generation = 0;
    std::atomic<std::uint64_t> blocks{0}, accepted{0}, formatChanges{0}, outputChanges{0}, maximumCopyUs{0};
    AddonVisualizerClock clock;
    unsigned formatRate = 0, formatChannels = 0;
    vlc_fourcc_t formatCodec = 0;

    explicit State(vlc_player_t* value) : player(value)
    {
        if (!player) return;
        static const struct vlc_player_timer_cbs timerCallbacks{
            [](const vlc_player_timer_point* point, void* data) { static_cast<State*>(data)->clock.update(*point); },
            [](vlc_tick_t date, void* data) { static_cast<State*>(data)->clock.pause(date); },
            [](const vlc_player_timer_point* point, void* data) { static_cast<State*>(data)->clock.seek(point); }
        };
        timer = vlc_player_AddTimer(player, VLC_TICK_FROM_MS(10), &timerCallbacks, this);
        static const struct vlc_player_aout_cbs outputCallbacks = [] {
            struct vlc_player_aout_cbs cbs{};
            cbs.on_device_changed = [](audio_output_t*, const char*, void* data) {
                auto& state = *static_cast<State*>(data);
                state.clock.formatChanged(); state.outputChanges.fetch_add(1, std::memory_order_relaxed);
            };
            return cbs;
        }();
        outputListener = vlc_player_aout_AddListener(player, &outputCallbacks, this);
        static const struct vlc_player_cbs playerCallbacks = [] {
            struct vlc_player_cbs cbs{};
            cbs.on_current_media_changed = [](vlc_player_t*, input_item_t*, void* data) { static_cast<State*>(data)->clock.mediaChanged(); };
            cbs.on_state_changed = [](vlc_player_t*, enum vlc_player_state state, void* data) {
                static_cast<State*>(data)->clock.setRunning(state == VLC_PLAYER_STATE_PLAYING || state == VLC_PLAYER_STATE_PAUSED);
            };
            return cbs;
        }();
        vlc_player_locker lock{player};
        const auto state = vlc_player_GetState(player);
        clock.setRunning(state == VLC_PLAYER_STATE_PLAYING || state == VLC_PLAYER_STATE_PAUSED);
        listener = vlc_player_AddListener(player, &playerCallbacks, this);
    }
    void stop()
    {
        if (samples && player) {
            vlc_player_locker lock{player}; vlc_player_RemoveMetadataListener(player, samples); samples = nullptr;
        }
        if (samplesOutput) aout_Release(samplesOutput);
        samplesOutput = nullptr;
        analysis = nullptr;
    }
    void close()
    {
        stop();
        if (listener && player) {
            vlc_player_locker lock{player}; vlc_player_RemoveListener(player, listener); listener = nullptr;
        }
        if (timer && player) vlc_player_RemoveTimer(player, timer);
        if (outputListener && player) vlc_player_aout_RemoveListener(player, outputListener);
        outputListener = nullptr;
        timer = nullptr; player = nullptr;
        clock.setRunning(false);
    }
    bool start(AddonAudioAnalysis* target, std::uint64_t value)
    {
        stop(); if (!player || !timer || !listener || !outputListener || !target || !clock.isLockFree() || !blocks.is_lock_free()
            || !accepted.is_lock_free() || !formatChanges.is_lock_free() || !outputChanges.is_lock_free() || !maximumCopyUs.is_lock_free()) return false;
        analysis = target; generation = value; formatRate = formatChannels = 0; formatCodec = 0;
        static const union vlc_player_metadata_cbs callbacks = [] {
            union vlc_player_metadata_cbs callbacks{};
            callbacks.on_audio_samples = [](vlc_tick_t time, double rate, const audio_sample_format_t* format, const block_t* block, void* data) {
                auto& self = *static_cast<State*>(data); AddonAudioAnalysis::Format kind;
                switch (format->i_format) {
                case VLC_CODEC_FL32: kind = AddonAudioAnalysis::Format::Float32; break;
                case VLC_CODEC_S16N: kind = AddonAudioAnalysis::Format::Signed16; break;
                case VLC_CODEC_S32N: kind = AddonAudioAnalysis::Format::Signed32; break;
                default: return;
                }
                if (self.formatRate && (self.formatRate != format->i_rate || self.formatChannels != format->i_channels || self.formatCodec != format->i_format)) {
                    self.clock.formatChanged(); self.formatChanges.fetch_add(1, std::memory_order_relaxed);
                }
                self.formatRate = format->i_rate; self.formatChannels = format->i_channels; self.formatCodec = format->i_format;
                const auto start = vlc_tick_now();
                const bool accepted = self.analysis->append(self.generation, kind, block->p_buffer, block->i_buffer, block->i_nb_samples,
                    format->i_channels, format->i_rate, time, rate);
                const auto elapsed = std::uint64_t(vlc_tick_now() - start);
                if (elapsed > self.maximumCopyUs.load(std::memory_order_relaxed)) self.maximumCopyUs.store(elapsed, std::memory_order_relaxed);
                self.blocks.fetch_add(1, std::memory_order_relaxed);
                if (accepted) self.accepted.fetch_add(1, std::memory_order_relaxed);
            };
            return callbacks;
        }();
        vlc_player_locker lock{player}; samplesOutput = vlc_player_aout_Hold(player);
        if (samplesOutput) samples = vlc_player_AddMetadataListener(player, VLC_PLAYER_METADATA_AUDIO_SAMPLES, &callbacks, this);
        if (!samples) { analysis = nullptr; if (samplesOutput) aout_Release(samplesOutput); samplesOutput = nullptr; }
        return samples != nullptr;
    }
    AddonVisualizers::Snapshot snapshot()
    {
        if (player && samplesOutput) {
            auto* output = vlc_player_aout_Hold(player); const bool changed = output != samplesOutput;
            if (output) aout_Release(output);
            if (changed) {
                stop(); clock.formatChanged(); outputChanges.fetch_add(1, std::memory_order_relaxed);
            }
        }
        const auto point = clock.snapshot(vlc_tick_now());
        return {point.epoch, point.timeUs, point.rate, point.playing};
    }

};
AddonNativePlayerAnalysis::AddonNativePlayerAnalysis(vlc_player_t* player) : m_state(std::make_unique<State>(player)) {}
std::shared_ptr<AddonNativePlayerAnalysis> AddonNativePlayerAnalysis::create(vlc_player_t* player)
{ return std::shared_ptr<AddonNativePlayerAnalysis>(new AddonNativePlayerAnalysis(player)); }
AddonNativePlayerAnalysis::~AddonNativePlayerAnalysis() { close(); }
void AddonNativePlayerAnalysis::close() { m_state->close(); }
AddonVisualizers::Backend AddonNativePlayerAnalysis::backend()
{
    const auto self = shared_from_this();
    return {[self] { return self->m_state->snapshot(); },
        [self](AddonAudioAnalysis* analysis, std::uint64_t epoch) { return self->m_state->start(analysis, epoch); },
        [self] { self->m_state->stop(); }};
}
QVariantMap AddonNativePlayerAnalysis::statistics() const
{
    return {{"epoch", QVariant::fromValue<qulonglong>(m_state->clock.epoch())},
        {"blocks", QVariant::fromValue<qulonglong>(m_state->blocks.load(std::memory_order_relaxed))},
        {"accepted", QVariant::fromValue<qulonglong>(m_state->accepted.load(std::memory_order_relaxed))},
        {"formatChanges", QVariant::fromValue<qulonglong>(m_state->formatChanges.load(std::memory_order_relaxed))},
        {"outputChanges", QVariant::fromValue<qulonglong>(m_state->outputChanges.load(std::memory_order_relaxed))},
        {"maximumCopyUs", QVariant::fromValue<qulonglong>(m_state->maximumCopyUs.load(std::memory_order_relaxed))},
        {"subscribed", m_state->samples != nullptr}};
}
}
