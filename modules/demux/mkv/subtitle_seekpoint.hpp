// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_MKV_SUBTITLE_SEEKPOINT_HPP
#define VLC_MKV_SUBTITLE_SEEKPOINT_HPP

#include <vlc_common.h>
#include <vlc_tick.h>
#include <algorithm>
#include <limits>
#include <map>
#include <tuple>
#include <vector>

namespace mkv {

class SubtitleSeekIndex
{
public:
    struct Seekpoint {
        unsigned int track;
        uint64_t cluster;
        uint64_t relative;
        vlc_tick_t pts;
        vlc_tick_t duration;
    };
    using seekpoints_t = std::vector<Seekpoint>;

    static bool targetBeforeDelay( vlc_tick_t target, vlc_tick_t delay, vlc_tick_t &result )
    {
        if( (delay > 0 && target < std::numeric_limits<vlc_tick_t>::min() + delay)
            || (delay < 0 && target > std::numeric_limits<vlc_tick_t>::max() + delay) )
            return false;
        result = target - delay;
        return true;
    }

    void add( Seekpoint point )
    {
        // -1 marks cue metadata whose packet duration has not been read yet.
        if( point.duration < -1 )
            return;
        const auto key = std::make_tuple( point.track, point.cluster, point.relative, point.pts );
        auto inserted = points.emplace( key, point );
        if( !inserted.second && point.duration >= 0 )
            inserted.first->second.duration = std::max( inserted.first->second.duration, point.duration );
        if( point.relative != UINT64_MAX && point.duration >= 0 )
        {
            auto generic = points.find( std::make_tuple(point.track, point.cluster, UINT64_MAX, point.pts) );
            if( generic != points.end() )
                generic->second.duration = std::max( generic->second.duration, point.duration );
        }
    }

    static bool isActive( const Seekpoint &point, vlc_tick_t target )
    {
        // Unsigned subtraction handles negative cue timestamps without
        // overflowing at the ends of the signed timestamp range.
        return point.pts <= target && (point.duration == -1
            || uint64_t(target) - uint64_t(point.pts) < uint64_t(point.duration));
    }

    seekpoints_t active( unsigned int track, vlc_tick_t target ) const
    {
        seekpoints_t result;
        auto it = points.lower_bound( std::make_tuple( track, 0, 0, std::numeric_limits<vlc_tick_t>::min() ) );
        for( ; it != points.end() && it->second.track == track; ++it )
            if( isActive( it->second, target ) )
                result.push_back( it->second );
        return result;
    }

    // Cues that are active at target or start after it, stored before the
    // cluster at position before. A positive subtitle delay shows cues later,
    // so after a seek the cues starting between the delayed target and the
    // position where demuxing resumes are still to be shown.
    seekpoints_t unfinished( unsigned int track, vlc_tick_t target, uint64_t before ) const
    {
        seekpoints_t result;
        auto it = points.lower_bound( std::make_tuple( track, 0, 0, std::numeric_limits<vlc_tick_t>::min() ) );
        for( ; it != points.end() && it->second.track == track
               && it->second.cluster < before; ++it )
            if( it->second.pts > target || isActive( it->second, target ) )
                result.push_back( it->second );
        return result;
    }

private:
    std::map<std::tuple<unsigned int, uint64_t, uint64_t, vlc_tick_t>, Seekpoint> points;
};

} // namespace mkv
#endif
