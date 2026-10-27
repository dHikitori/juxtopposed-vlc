// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <vlc_common.h>
#include <vlc_player.h>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>

namespace vlc::addons {
class AddonVisualizerClock
{
public:
    struct Snapshot { std::uint64_t epoch = 0; std::int64_t timeUs = -1; double rate = 1; bool playing = false; };
    static constexpr unsigned SnapshotAttempts = 3;
    // Core timer.lock serializes update, pause, and seek. These methods do not
    // wait for a reader. Media, state, and format changes can run concurrently.
    bool update(const vlc_player_timer_point& point) { return update(point, captureEpoch()); }
    bool update(const vlc_player_timer_point& point, std::uint64_t captured)
    {
        if (!isLockFree() || !captured || captured != m_epoch.load() || m_controls.load() || !m_running.load()
            || !std::isfinite(point.rate) || point.rate <= 0 || !std::isfinite(point.position)
            || point.ts < VLC_TICK_0 || point.system_date <= 0
            || (point.length != VLC_TICK_INVALID && point.length < VLC_TICK_0)) return false;
        syncMedia(); if (m_seeking) return false;
        if (m_rateKnown && point.rate != m_rate) {
            auto expected = captured;
            if (!m_epoch.compare_exchange_strong(expected, captured + 1)) return false;
            ++captured;
        }
        if (captured != m_epoch.load() || m_controls.load() || !m_running.load()) return false;
        m_writerPoint = point; m_writerEpoch = captured; m_haveWriterPoint = true;
        m_rate = point.rate; m_rateKnown = true; publish(point, captured, true); return true;
    }
    std::uint64_t captureEpoch() const
    {
        if (!isLockFree() || m_controls.load() || !m_running.load()) return 0;
        const auto value = m_epoch.load();
        return !m_controls.load() && m_running.load() && value == m_epoch.load() ? value : 0;
    }
    void pause(vlc_tick_t date)
    {
        if (!isLockFree()) return;
        syncMedia(); if (m_seeking) return;
        if (date == VLC_TICK_INVALID) {
            m_haveWriterPoint = false; const auto value = m_epoch.fetch_add(1) + 1; publish({}, value, false); return;
        }
        const auto captured = captureEpoch();
        if (!captured || !m_haveWriterPoint || m_writerEpoch != captured) return;
        auto point = m_writerPoint; vlc_tick_t time;
        if (!interpolate(point, date, &time)) { m_haveWriterPoint = false; publish({}, captured, false); return; }
        point.ts = time; point.system_date = VLC_TICK_MAX;
        m_writerPoint = point; publish(point, captured, true);
    }
    void seek(const vlc_player_timer_point* point)
    {
        if (!isLockFree()) return;
        syncMedia(); m_seeking = point != nullptr; m_haveWriterPoint = false;
        const auto value = m_epoch.fetch_add(1) + 1; publish({}, value, false);
    }
    void mediaChanged()
    {
        if (!isLockFree()) return;
        m_controls.fetch_add(1); m_epoch.fetch_add(1); m_media.fetch_add(1);
        m_epoch.fetch_add(1); m_controls.fetch_sub(1);
    }
    void setRunning(bool value)
    {
        if (!isLockFree() || m_running.load() == value) return;
        m_controls.fetch_add(1); m_epoch.fetch_add(1); m_running.store(value);
        if (!value) m_media.fetch_add(1);
        m_epoch.fetch_add(1); m_controls.fetch_sub(1);
    }
    void formatChanged() { if (isLockFree()) m_epoch.fetch_add(1); }
    bool isLockFree() const
    {
        return m_epoch.is_lock_free() && m_controls.is_lock_free() && m_media.is_lock_free() && m_running.is_lock_free()
            && m_sequence.is_lock_free() && m_pointEpoch.is_lock_free() && m_valid.is_lock_free() && m_live.is_lock_free()
            && m_time.is_lock_free() && m_date.is_lock_free() && m_length.is_lock_free()
            && m_position.is_lock_free() && m_pointRate.is_lock_free();
    }
    std::uint64_t epoch() const { return isLockFree() ? m_epoch.load() : 0; }
    Snapshot snapshot(vlc_tick_t now) const
    {
        if (now <= 0 || !isLockFree()) return {};
        for (unsigned attempt = 0; attempt < SnapshotAttempts; ++attempt) {
            if (m_controls.load() || !m_running.load()) return {};
            const auto epoch = m_epoch.load(), sequence = m_sequence.load(); if (sequence & 1) continue;
            vlc_player_timer_point point{}; point.ts = m_time.load(); point.system_date = m_date.load(); point.length = m_length.load();
            point.position = number(m_position.load()); point.rate = number(m_pointRate.load()); point.live = m_live.load();
            const auto pointEpoch = m_pointEpoch.load(); const bool valid = m_valid.load();
            if (sequence != m_sequence.load() || epoch != m_epoch.load() || m_controls.load() || !m_running.load()) continue;
            if (!valid || pointEpoch != epoch) return {};
            vlc_tick_t time;
            if (!interpolate(point, now, &time)) return {};
            return {epoch, time - VLC_TICK_0, point.rate, point.system_date != VLC_TICK_MAX};
        }
        return {};
    }
private:
    static std::uint64_t bits(double value)
    { std::uint64_t result; static_assert(sizeof(result) == sizeof(value)); std::memcpy(&result, &value, sizeof(result)); return result; }
    static double number(std::uint64_t value)
    { double result; std::memcpy(&result, &value, sizeof(result)); return result; }
    static bool interpolate(const vlc_player_timer_point& point, vlc_tick_t now, vlc_tick_t* time)
    {
        if (now <= 0 || point.ts < VLC_TICK_0 || point.system_date <= 0 || !std::isfinite(point.rate) || point.rate <= 0) return false;
        const double drift = point.system_date == VLC_TICK_MAX ? 0 : (now - point.system_date) * point.rate;
        const auto low = std::numeric_limits<vlc_tick_t>::min(), high = std::numeric_limits<vlc_tick_t>::max();
        if (!std::isfinite(drift) || static_cast<long double>(drift) <= static_cast<long double>(low)
            || static_cast<long double>(drift) >= static_cast<long double>(high)) return false;
        const auto delta = static_cast<vlc_tick_t>(drift);
        if ((delta > 0 && point.ts > high - delta) || (delta < 0 && point.ts < low - delta)) return false;
        return vlc_player_timer_point_Interpolate(&point, now, time, nullptr) == VLC_SUCCESS;
    }
    void syncMedia()
    {
        const auto media = m_media.load();
        if (media != m_writerMedia) { m_writerMedia = media; m_rateKnown = false; m_seeking = false; m_haveWriterPoint = false; }
    }
    void publish(const vlc_player_timer_point& point, std::uint64_t epoch, bool valid)
    {
        // Every field is atomic. Sequential consistency also orders each field
        // between the odd and even sequence values in the C++ memory model.
        m_sequence.fetch_add(1); m_time.store(point.ts); m_date.store(point.system_date); m_length.store(point.length);
        m_position.store(bits(point.position)); m_pointRate.store(bits(point.rate)); m_live.store(point.live);
        m_pointEpoch.store(epoch); m_valid.store(valid); m_sequence.fetch_add(1);
    }
    std::atomic<std::uint64_t> m_epoch{1}, m_controls{0}, m_media{1}, m_sequence{0}, m_pointEpoch{0};
    std::atomic<vlc_tick_t> m_time{VLC_TICK_INVALID}, m_date{VLC_TICK_INVALID}, m_length{VLC_TICK_INVALID};
    std::atomic<std::uint64_t> m_position{0}, m_pointRate{0};
    std::atomic_bool m_running{false}, m_valid{false}, m_live{false};
    // Only serialized Core timer callbacks access these writer fields.
    vlc_player_timer_point m_writerPoint{};
    std::uint64_t m_writerEpoch = 0, m_writerMedia = 0;
    double m_rate = 1;
    bool m_haveWriterPoint = false, m_rateKnown = false, m_seeking = false;
};
}
