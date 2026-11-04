// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <cassert>
#include <limits>
#include "../../../modules/demux/mkv/subtitle_seekpoint.hpp"

int main()
{
    using mkv::SubtitleSeekIndex;
    SubtitleSeekIndex index;
    index.add( { 3, 100, 5, 0, VLC_TICK_FROM_SEC(30) } );
    index.add( { 3, 200, 9, VLC_TICK_FROM_SEC(5), VLC_TICK_FROM_SEC(10) } );
    index.add( { 4, 100, 7, 0, VLC_TICK_FROM_SEC(30) } );
    auto active = index.active( 3, VLC_TICK_FROM_SEC(3) );
    assert( active.size() == 1 && active.front().cluster == 100 );
    active = index.active( 3, VLC_TICK_FROM_SEC(7) );
    assert( active.size() == 2 ); // Preserve the older overlapping cue too.
    assert( index.active( 3, VLC_TICK_FROM_SEC(15) ).size() == 1 );
    assert( index.active( 3, VLC_TICK_FROM_SEC(30) ).empty() );
    assert( index.active( 4, VLC_TICK_FROM_SEC(7) ).size() == 1 );
    assert( index.active( 5, VLC_TICK_FROM_SEC(7) ).empty() );
    assert( index.active( 3, -1 ).empty() );

    // Equal timestamps can represent separate simultaneous subtitle blocks.
    index.add( { 3, 100, 15, 0, VLC_TICK_FROM_SEC(30) } );
    assert( index.active( 3, 0 ).size() == 2 );
    // Re-reading a block or a shorter metadata entry must not duplicate it
    // or truncate a cue that is already known to last longer.
    index.add( { 3, 100, 5, 0, VLC_TICK_FROM_SEC(1) } );
    assert( index.active( 3, VLC_TICK_FROM_SEC(3) ).size() == 2 );
    index.add( { 3, 300, 5, 0, 0 } );
    index.add( { 3, 300, 6, 0, -2 } );
    assert( index.active( 3, 0 ).size() == 2 );

    SubtitleSeekIndex metadata;
    metadata.add( { 1, 100, 5, 0, -1 } );
    assert( metadata.active( 1, VLC_TICK_FROM_SEC(60) ).size() == 1 );
    metadata.add( { 1, 100, 5, 0, VLC_TICK_FROM_SEC(5) } );
    assert( metadata.active( 1, VLC_TICK_FROM_SEC(60) ).empty() );
    metadata.add( { 2, 100, UINT64_MAX, 0, -1 } );
    metadata.add( { 2, 100, 15, 0, VLC_TICK_FROM_SEC(5) } );
    assert( metadata.active( 2, VLC_TICK_FROM_SEC(60) ).empty() );
    metadata.add( { 3, 100, 5, 0, -1 } );
    metadata.add( { 3, 100, 5, 0, 0 } );
    assert( metadata.active( 3, 0 ).empty() );

    SubtitleSeekIndex limits;
    const auto low = std::numeric_limits<vlc_tick_t>::min();
    const auto high = std::numeric_limits<vlc_tick_t>::max();
    limits.add( { 1, 1, 1, low, 20 } );
    limits.add( { 2, 1, 1, high - 10, 20 } );
    assert( limits.active( 1, low + 10 ).size() == 1 );
    assert( limits.active( 1, low + 20 ).empty() );
    assert( limits.active( 1, high ).empty() );
    assert( limits.active( 2, high ).size() == 1 );
    assert( limits.active( 2, low ).empty() );
    // A positive delay shows cues later: the cues starting after the delayed
    // target are needed too, up to where demuxing resumes.
    SubtitleSeekIndex later;
    later.add( { 1, 100, 5, VLC_TICK_FROM_SEC(1), VLC_TICK_FROM_SEC(1) } );
    later.add( { 1, 200, 5, VLC_TICK_FROM_SEC(3), VLC_TICK_FROM_SEC(1) } );
    later.add( { 1, 300, 5, VLC_TICK_FROM_SEC(5), VLC_TICK_FROM_SEC(1) } );
    later.add( { 1, 400, 5, VLC_TICK_FROM_SEC(7), -1 } );
    later.add( { 2, 200, 5, VLC_TICK_FROM_SEC(3), VLC_TICK_FROM_SEC(1) } );
    auto unfinished = later.unfinished( 1, VLC_TICK_FROM_SEC(2), 400 );
    assert( unfinished.size() == 2 && unfinished[0].cluster == 200
            && unfinished[1].cluster == 300 ); // the cue at 1s is over
    assert( later.unfinished( 1, VLC_TICK_FROM_SEC(1), 200 ).size() == 1 );
    assert( later.unfinished( 1, VLC_TICK_FROM_SEC(1), 100 ).empty() );
    assert( later.unfinished( 1, VLC_TICK_FROM_SEC(8), UINT64_MAX ).size() == 1 );
    assert( later.unfinished( 2, 0, UINT64_MAX ).size() == 1 );
    assert( later.unfinished( 3, 0, UINT64_MAX ).empty() );

    vlc_tick_t target;
    assert( SubtitleSeekIndex::targetBeforeDelay(10, 5, target) && target == 5 );
    assert( SubtitleSeekIndex::targetBeforeDelay(10, -5, target) && target == 15 );
    assert( !SubtitleSeekIndex::targetBeforeDelay(low, 1, target) );
    assert( !SubtitleSeekIndex::targetBeforeDelay(high, -1, target) );
    assert( SubtitleSeekIndex::targetBeforeDelay(low, -1, target) && target == low + 1 );
    assert( SubtitleSeekIndex::targetBeforeDelay(high, 1, target) && target == high - 1 );
    assert( !SubtitleSeekIndex::targetBeforeDelay(0, low, target) );
    assert( SubtitleSeekIndex::targetBeforeDelay(-1, low, target) && target == high );
    assert( SubtitleSeekIndex::targetBeforeDelay(low, low, target) && target == 0 );
}
