// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_player.h>
#include <vlc_aout.h>
#include <vlc_block.h>
#include "addon_player_visualizers.hpp"
#include "addon_visualizers.hpp"
#include "player/player_controller.hpp"
#include <atomic>
#include <memory>
#include <mutex>

namespace vlc::addons {
namespace {
struct PlayerAnalysis {
    vlc_player_t* player;
    vlc_player_metadata_listener_id* samples = nullptr;
    vlc_player_timer_id* timer = nullptr;
    AddonAudioAnalysis* analysis = nullptr;
    std::uint64_t generation = 0;
    std::atomic<std::uint64_t> epoch{1};
    std::mutex clockMutex;
    vlc_player_timer_point point{};
    bool hasPoint = false, paused = true;
    unsigned formatRate = 0, formatChannels = 0;
    vlc_fourcc_t formatCodec = 0;
    explicit PlayerAnalysis(vlc_player_t* value) : player(value) {
        static const vlc_player_timer_cbs callbacks{
            [](const vlc_player_timer_point* point, void* data) {
                auto& self = *static_cast<PlayerAnalysis*>(data); std::lock_guard<std::mutex> lock(self.clockMutex);
                self.point = *point; self.hasPoint = true; self.paused = point->system_date == INT64_MAX;
            },
            [](vlc_tick_t date, void* data) {
                auto& self = *static_cast<PlayerAnalysis*>(data); std::lock_guard<std::mutex> lock(self.clockMutex);
                if (self.hasPoint && date != VLC_TICK_INVALID) {
                    vlc_tick_t time;
                    if (vlc_player_timer_point_Interpolate(&self.point, date, &time, nullptr) == VLC_SUCCESS) self.point.ts = time;
                }
                self.paused = true;
            },
            [](const vlc_player_timer_point* point, void* data) {
                if (point) static_cast<PlayerAnalysis*>(data)->epoch.fetch_add(1, std::memory_order_relaxed);
            }
        };
        timer = vlc_player_AddTimer(player, VLC_TICK_FROM_MS(10), &callbacks, this);
    }
    ~PlayerAnalysis() { shutdown(); }
    void stop() {
        if (samples && player) {
            vlc_player_locker lock{player}; vlc_player_RemoveMetadataListener(player, samples); samples = nullptr;
        }
        analysis = nullptr;
    }
    void shutdown() {
        stop(); if (timer && player) vlc_player_RemoveTimer(player, timer); timer = nullptr; player = nullptr;
    }
    bool start(AddonAudioAnalysis* target, std::uint64_t value) {
        stop(); if (!player || !timer) return false;
        analysis = target; generation = value; formatRate = formatChannels = 0; formatCodec = 0;
        static const union vlc_player_metadata_cbs callbacks = [] {
            union vlc_player_metadata_cbs callbacks{};
            callbacks.on_audio_samples = [](vlc_tick_t time, double rate, const audio_sample_format_t* format, const block_t* block, void* data) {
                auto& self = *static_cast<PlayerAnalysis*>(data); AddonAudioAnalysis::Format kind;
                switch (format->i_format) {
                case VLC_CODEC_FL32: kind = AddonAudioAnalysis::Format::Float32; break;
                case VLC_CODEC_S16N: kind = AddonAudioAnalysis::Format::Signed16; break;
                case VLC_CODEC_S32N: kind = AddonAudioAnalysis::Format::Signed32; break;
                default: return;
                }
                if (self.formatRate && (self.formatRate != format->i_rate || self.formatChannels != format->i_channels || self.formatCodec != format->i_format)) self.epoch.fetch_add(1, std::memory_order_relaxed);
                self.formatRate = format->i_rate; self.formatChannels = format->i_channels; self.formatCodec = format->i_format;
                self.analysis->append(self.generation, kind, block->p_buffer, block->i_buffer, block->i_nb_samples,
                                     format->i_channels, format->i_rate, time, rate);
            };
            return callbacks;
        }();
        vlc_player_locker lock{player}; samples = vlc_player_AddMetadataListener(player, VLC_PLAYER_METADATA_AUDIO_SAMPLES, &callbacks, this);
        return samples != nullptr;
    }
    AddonVisualizers::Snapshot snapshot() {
        std::lock_guard<std::mutex> lock(clockMutex); qint64 time = -1;
        if (hasPoint && point.ts != VLC_TICK_INVALID) {
            if (paused) time = point.ts - VLC_TICK_0;
            else { vlc_tick_t value; if (vlc_player_timer_point_Interpolate(&point, vlc_tick_now(), &value, nullptr) == VLC_SUCCESS) time = value - VLC_TICK_0; }
        }
        return {player && hasPoint ? epoch.load(std::memory_order_relaxed) : 0, time, point.rate, !paused};
    }
};
}
AddonVisualizers* createPlayerVisualizers(AddonManager* manager, PlayerController* player, QObject* parent)
{
    if (!player) return new AddonVisualizers(manager, {}, parent);
    const auto state = std::make_shared<PlayerAnalysis>(player->getPlayer());
    auto* result = new AddonVisualizers(manager, {[state] { return state->snapshot(); },
        [state](AddonAudioAnalysis* analysis, std::uint64_t epoch) { return state->start(analysis, epoch); }, [state] { state->stop(); }}, parent);
    QObject::connect(player, &PlayerController::inputChanged, result, [state, result] {
        state->epoch.fetch_add(1, std::memory_order_relaxed);
        { std::lock_guard<std::mutex> lock(state->clockMutex); state->hasPoint = false; }
        result->invalidateTimeline();
    });
    QObject::connect(player, &QObject::destroyed, result, [state, result] { result->close(); state->shutdown(); });
    return result;
}
}
