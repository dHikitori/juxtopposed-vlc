// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_PREPARSER_NATIVE_FACTS_FROMJSON_H
#define VLC_PREPARSER_NATIVE_FACTS_FROMJSON_H 1
#include <vlc_epg.h>

#define NATIVE_MAX_ENTRIES 4096u
#define NATIVE_MAX_BYTES (16u * 1024u * 1024u)
struct native_decode_limit { size_t bytes, events, infos; };
static void *native_allocate(struct native_decode_limit *limit, size_t count, size_t size, bool *error)
{
    if (*error || size == 0 || count > limit->bytes / size) { *error = true; return NULL; }
    if (count == 0) return NULL;
    void *value = calloc(count, size);
    if (value == NULL) { *error = true; return NULL; }
    limit->bytes -= count * size; return value;
}
static const struct json_array *native_array(const struct json_object *obj, const char *name, bool *error)
{
    if (*error) return NULL;
    const struct json_array *array = json_get_array(obj, name);
    if (array == NULL || array->size > NATIVE_MAX_ENTRIES) { *error = true; return NULL; }
    return array;
}
static bool native_string(struct native_decode_limit *limit, const struct json_object *obj,
    const char *name, char **value, bool *error)
{
    if (*error) return false;
    const struct json_value *raw = json_get(obj, name);
    if (raw == NULL || (raw->type != JSON_STRING && raw->type != JSON_NULL)) { *error = true; return false; }
    if (raw->type == JSON_STRING) {
        const size_t length = strnlen(raw->string, limit->bytes);
        if (length == limit->bytes) { *error = true; return false; }
        limit->bytes -= length + 1;
    }
    json_object_to_string(obj, name, value, error); return !*error;
}
static bool native_integer(const struct json_object *obj, const char *name, bool sign, void *output, bool *error)
{
    if (*error) return false;
    const struct json_value *value = json_get(obj, name);
    if (value == NULL) { *error = true; return false; }
    if (value->type == JSON_NUMBER) {
        const double number = value->number;
        if (!isfinite(number) || floor(number) != number || number > 9007199254740991.0
            || number < (sign ? -9007199254740991.0 : 0.0)) { *error = true; return false; }
        if (sign) *(int64_t *)output = (int64_t)number; else *(uint64_t *)output = (uint64_t)number;
        return true;
    }
    if (value->type != JSON_STRING) { *error = true; return false; }
    const char *text = value->string;
    size_t length = strnlen(text, 21);
    if (length == 0 || length > 20) { *error = true; return false; }
    for (size_t i = 0; i < length; ++i)
        if (!(i == 0 && sign && text[i] == '-') && (text[i] < '0' || text[i] > '9')) { *error = true; return false; }
    char *end = NULL; errno = 0;
    if (sign) *(int64_t *)output = strtoll(text, &end, 10); else *(uint64_t *)output = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') { *error = true; return false; }
    return true;
}
static vlc_epg_event_t *native_read_epg_event(struct native_decode_limit *limit,
    const struct json_object *obj, bool *error)
{
    uint16_t id = 0; uint32_t duration = 0; int64_t start = 0;
    json_object_to_uint16(obj, "id", &id, error); json_object_to_uint32(obj, "duration", &duration, error);
    native_integer(obj, "start", true, &start, error);
    if (*error || limit->bytes < sizeof(vlc_epg_event_t)) { *error = true; return NULL; }
    limit->bytes -= sizeof(vlc_epg_event_t);
    vlc_epg_event_t *event = vlc_epg_event_New(id, start, duration);
    if (event == NULL) { *error = true; return NULL; }
    native_string(limit, obj, "name", &event->psz_name, error);
    native_string(limit, obj, "shortDescription", &event->psz_short_description, error);
    native_string(limit, obj, "description", &event->psz_description, error);
    json_object_to_uint8(obj, "rating", &event->i_rating, error);
    const struct json_array *items = native_array(obj, "descriptionItems", error);
    if (!*error) {
        event->description_items = native_allocate(limit, items->size, sizeof(*event->description_items), error);
        if (!*error) event->i_description_items = items->size;
        for (size_t i = 0; !*error && i < items->size; ++i) {
            const struct json_value *item = &items->entries[i];
            if (item->type != JSON_OBJECT) { *error = true; break; }
            native_string(limit, &item->object, "key", &event->description_items[i].psz_key, error);
            native_string(limit, &item->object, "value", &event->description_items[i].psz_value, error);
            if (event->description_items[i].psz_key == NULL || event->description_items[i].psz_value == NULL) *error = true;
        }
    }
    if (*error) { vlc_epg_event_Delete(event); return NULL; }
    return event;
}
static vlc_epg_t *native_read_epg(struct native_decode_limit *limit, const struct json_object *obj, bool *error)
{
    uint32_t id = 0; uint16_t source = 0;
    json_object_to_uint32(obj, "id", &id, error); json_object_to_uint16(obj, "sourceId", &source, error);
    if (*error || limit->bytes < sizeof(vlc_epg_t)) { *error = true; return NULL; }
    limit->bytes -= sizeof(vlc_epg_t);
    vlc_epg_t *table = vlc_epg_New(id, source); if (table == NULL) { *error = true; return NULL; }
    native_string(limit, obj, "name", &table->psz_name, error);
    json_object_to_boolean(obj, "present", &table->b_present, error);
    const struct json_array *events = native_array(obj, "events", error);
    if (!*error && events->size > NATIVE_MAX_ENTRIES - limit->events) *error = true;
    if (!*error) {
        limit->events += events->size;
        table->pp_event = native_allocate(limit, events->size, sizeof(*table->pp_event), error);
        for (size_t i = 0; !*error && i < events->size; ++i) {
            const struct json_value *value = &events->entries[i];
            if (value->type != JSON_OBJECT) { *error = true; break; }
            vlc_epg_event_t *event = native_read_epg_event(limit, &value->object, error);
            if (event != NULL) table->pp_event[table->i_event++] = event;
        }
    }
    int current = -1; json_object_to_int32(obj, "current", &current, error);
    if (!*error && (current < -1 || (current >= 0 && (size_t)current >= table->i_event))) *error = true;
    if (!*error && current >= 0) table->p_current = table->pp_event[current];
    if (*error) { vlc_epg_Delete(table); return NULL; }
    return table;
}
static void native_read_infos(struct native_decode_limit *limit, const struct json_object *obj,
    input_item_t *item, bool *error)
{
    const struct json_array *categories = native_array(obj, "infos", error);
    for (size_t i = 0; !*error && i < categories->size; ++i) {
        const struct json_value *value = &categories->entries[i];
        if (value->type != JSON_OBJECT) { *error = true; break; }
        info_category_t *category = native_allocate(limit, 1, sizeof(*category), error);
        if (category == NULL) break;
        vlc_list_init(&category->infos);
        native_string(limit, &value->object, "name", &category->psz_name, error);
        if (*error || category->psz_name == NULL) { *error = true; free(category->psz_name); free(category); break; }
        vlc_list_append(&category->node, &item->categories);
        const struct json_array *entries = native_array(&value->object, "entries", error);
        if (!*error && entries->size > NATIVE_MAX_ENTRIES - limit->infos) *error = true;
        if (!*error) limit->infos += entries->size;
        for (size_t j = 0; !*error && j < entries->size; ++j) {
            const struct json_value *entry = &entries->entries[j];
            if (entry->type != JSON_OBJECT) { *error = true; break; }
            info_t *info = native_allocate(limit, 1, sizeof(*info), error); if (info == NULL) break;
            vlc_list_append(&info->node, &category->infos);
            native_string(limit, &entry->object, "name", &info->psz_name, error);
            native_string(limit, &entry->object, "value", &info->psz_value, error);
            if (info->psz_name == NULL || info->psz_value == NULL) *error = true;
        }
    }
}
static void native_read_stats(struct native_decode_limit *limit, const struct json_object *obj,
    input_item_t *item, bool *error)
{
    const struct json_value *value = json_get(obj, "stats");
    if (*error) return;
    if (value == NULL || (value->type != JSON_NULL && value->type != JSON_OBJECT)) { *error = true; return; }
    if (value->type == JSON_NULL) return;
    struct input_stats_t *stats = native_allocate(limit, 1, sizeof(*stats), error); if (stats == NULL) return;
    item->p_stats = stats; const struct json_object *fields = &value->object;
#define READ_STAT(name, field) native_integer(fields, name, false, &stats->field, error)
    READ_STAT("readPackets", i_read_packets); READ_STAT("readBytes", i_read_bytes);
    READ_STAT("demuxPackets", i_demux_read_packets); READ_STAT("demuxBytes", i_demux_read_bytes);
    READ_STAT("demuxCorrupted", i_demux_corrupted); READ_STAT("demuxDiscontinuity", i_demux_discontinuity);
    READ_STAT("decodedAudio", i_decoded_audio); READ_STAT("decodedVideo", i_decoded_video);
    READ_STAT("displayedPictures", i_displayed_pictures); READ_STAT("latePictures", i_late_pictures);
    READ_STAT("lostPictures", i_lost_pictures); READ_STAT("playedAudioBuffers", i_played_abuffers);
    READ_STAT("lostAudioBuffers", i_lost_abuffers);
#undef READ_STAT
    uint32_t input = 0, demux = 0;
    json_object_to_uint32(fields, "inputBitrateBits", &input, error);
    json_object_to_uint32(fields, "demuxBitrateBits", &demux, error);
    _Static_assert(sizeof(float) == sizeof(uint32_t), "Native float statistics need 32 bits.");
    memcpy(&stats->f_input_bitrate, &input, sizeof(input)); memcpy(&stats->f_demux_bitrate, &demux, sizeof(demux));
}
static void native_read_facts(const struct json_object *obj, input_item_t *item, bool *error)
{
    const struct json_value *value = json_get(obj, "native");
    if (*error || value == NULL) return;
    if (value->type != JSON_OBJECT) { *error = true; return; }
    obj = &value->object; struct native_decode_limit limit = {.bytes=NATIVE_MAX_BYTES};
    native_read_infos(&limit, obj, item, error); native_read_stats(&limit, obj, item, error);
    const struct json_array *tables = native_array(obj, "epg", error);
    if (!*error) {
        item->pp_epg = native_allocate(&limit, tables->size, sizeof(*item->pp_epg), error);
        for (size_t i = 0; !*error && i < tables->size; ++i) {
            const struct json_value *table = &tables->entries[i];
            if (table->type != JSON_OBJECT) { *error = true; break; }
            vlc_epg_t *parsed = native_read_epg(&limit, &table->object, error);
            if (parsed != NULL) item->pp_epg[item->i_epg++] = parsed;
        }
    }
    int current = -1; json_object_to_int32(obj, "currentTable", &current, error);
    if (!*error && (current < -1 || current >= item->i_epg)) *error = true;
    if (!*error && current >= 0) item->p_epg_table = item->pp_epg[current];
    native_integer(obj, "epgTime", true, &item->i_epg_time, error);
}
#endif
