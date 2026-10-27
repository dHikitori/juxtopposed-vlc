// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_PREPARSER_NATIVE_FACTS_TOJSON_H
#define VLC_PREPARSER_NATIVE_FACTS_TOJSON_H 1
#include <vlc_epg.h>

static void native_write_info(struct serdes_sys *sys, const info_t *info)
{
    json_stringify_first(string, sys, "name", info->psz_name);
    json_stringify_last(string, sys, "value", info->psz_value);
}
static void native_write_categories(struct serdes_sys *sys, const input_item_t *item)
{
    if (serdes_buf_puts(sys, "\"infos\":[") < 0) return;
    bool first_category = true;
    const info_category_t *category;
    vlc_list_foreach_const(category, &item->categories, node) {
        if (!first_category && serdes_buf_putc(sys, ',') < 0) return;
        first_category = false;
        json_stringify_first(string, sys, "name", category->psz_name);
        if (serdes_buf_puts(sys, "\"entries\":[") < 0) return;
        bool first_info = true;
        const info_t *info;
        vlc_list_foreach_const(info, &category->infos, node) {
            if (!first_info && serdes_buf_putc(sys, ',') < 0) return;
            first_info = false; native_write_info(sys, info);
        }
        if (serdes_buf_puts(sys, "]}") < 0) return;
    }
    serdes_buf_putc(sys, ']');
}
static void native_write_epg_event(struct serdes_sys *sys, vlc_epg_event_t *const *value, const char *unused)
{
    (void)unused; const vlc_epg_event_t *event = *value;
    json_stringify_first(number, sys, "id", event->i_id);
    json_stringify(number, sys, "start", event->i_start);
    json_stringify(number, sys, "duration", event->i_duration);
    json_stringify(string, sys, "name", event->psz_name);
    json_stringify(string, sys, "shortDescription", event->psz_short_description);
    json_stringify(string, sys, "description", event->psz_description);
    json_stringify(number, sys, "rating", event->i_rating);
    if (serdes_buf_puts(sys, "\"descriptionItems\":[") < 0) return;
    for (int i = 0; i < event->i_description_items; ++i) {
        if (i != 0 && serdes_buf_putc(sys, ',') < 0) return;
        json_stringify_first(string, sys, "key", event->description_items[i].psz_key);
        json_stringify_last(string, sys, "value", event->description_items[i].psz_value);
    }
    serdes_buf_puts(sys, "]}");
}
static void native_write_epg(struct serdes_sys *sys, vlc_epg_t *const *value, const char *unused)
{
    (void)unused; const vlc_epg_t *table = *value; int current = -1;
    for (size_t i = 0; i < table->i_event; ++i) if (table->p_current == table->pp_event[i]) current = i;
    json_stringify_first(number, sys, "id", table->i_id);
    json_stringify(number, sys, "sourceId", table->i_source_id);
    json_stringify(string, sys, "name", table->psz_name);
    json_stringify(boolean, sys, "present", table->b_present);
    json_stringify_array(native_write_epg_event, table->i_event, sys, "events", table->pp_event);
    if (serdes_buf_putc(sys, ',') < 0) return;
    json_stringify_last(number, sys, "current", current);
}
static void native_write_stats(struct serdes_sys *sys, const struct input_stats_t *stats)
{
    if (stats == NULL) { serdes_buf_puts(sys, "null"); return; }
    uint32_t input, demux;
    _Static_assert(sizeof(float) == sizeof(uint32_t), "Native float statistics need 32 bits.");
    memcpy(&input, &stats->f_input_bitrate, sizeof(input)); memcpy(&demux, &stats->f_demux_bitrate, sizeof(demux));
    json_stringify_first(number, sys, "readPackets", stats->i_read_packets);
    json_stringify(number, sys, "readBytes", stats->i_read_bytes);
    json_stringify(number, sys, "inputBitrateBits", input);
    json_stringify(number, sys, "demuxPackets", stats->i_demux_read_packets);
    json_stringify(number, sys, "demuxBytes", stats->i_demux_read_bytes);
    json_stringify(number, sys, "demuxBitrateBits", demux);
    json_stringify(number, sys, "demuxCorrupted", stats->i_demux_corrupted);
    json_stringify(number, sys, "demuxDiscontinuity", stats->i_demux_discontinuity);
    json_stringify(number, sys, "decodedAudio", stats->i_decoded_audio);
    json_stringify(number, sys, "decodedVideo", stats->i_decoded_video);
    json_stringify(number, sys, "displayedPictures", stats->i_displayed_pictures);
    json_stringify(number, sys, "latePictures", stats->i_late_pictures);
    json_stringify(number, sys, "lostPictures", stats->i_lost_pictures);
    json_stringify(number, sys, "playedAudioBuffers", stats->i_played_abuffers);
    json_stringify_last(number, sys, "lostAudioBuffers", stats->i_lost_abuffers);
}
static void native_write_facts(struct serdes_sys *sys, const input_item_t *item)
{
    if (serdes_buf_puts(sys, "\"native\":{") < 0) return;
    native_write_categories(sys, item);
    if (serdes_buf_puts(sys, ",\"stats\":") < 0) return;
    native_write_stats(sys, item->p_stats);
    if (serdes_buf_putc(sys, ',') < 0) return;
    json_stringify_array(native_write_epg, (size_t)item->i_epg, sys, "epg", item->pp_epg);
    if (serdes_buf_putc(sys, ',') < 0) return;
    int current = -1;
    for (int i = 0; i < item->i_epg; ++i) if (item->p_epg_table == item->pp_epg[i]) current = i;
    json_stringify(number, sys, "currentTable", current);
    json_stringify_last(number, sys, "epgTime", item->i_epg_time);
}
#endif
