// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#define MODULE_NAME test_native_preparser_ipc
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_artwork_policy.h>
#include <vlc_epg.h>
#include <vlc_interrupt.h>
#include <assert.h>
#include "../../../src/input/item.h"
#include "../../../modules/misc/preparser_serializer/json/tojson.c"
#include "../../../modules/misc/preparser_serializer/json/fromjson.c"
#include "../../../modules/misc/preparser_serializer/json/serializer.c"
#include "../../../src/preparser/ipc.c"

const char vlc_module_name[] = "native-preparser-ipc-test";
struct wire { struct VLC_VECTOR(uint8_t) bytes; size_t offset; unsigned interrupts, calls; bool always_interrupt; };
static ssize_t wire_write(const void *bytes, size_t size, void *opaque)
{
    struct wire *wire = opaque; ++wire->calls;
    if (wire->always_interrupt || wire->interrupts != 0) { if (wire->interrupts) --wire->interrupts; errno = EINTR; return -1; }
    assert(vlc_vector_push_all(&wire->bytes, bytes, size)); return size;
}
static ssize_t wire_read(void *bytes, size_t size, void *opaque)
{
    struct wire *wire = opaque; ++wire->calls;
    if (wire->always_interrupt || wire->interrupts != 0) { if (wire->interrupts) --wire->interrupts; errno = EINTR; return -1; }
    size = __MIN(size, wire->bytes.size - wire->offset); size = __MIN(size, 7u);
    if (size) { memcpy(bytes, wire->bytes.data + wire->offset, size); wire->offset += size; }
    return size;
}
static void codec(struct vlc_preparser_msg_serdes *codec)
{
    static const struct vlc_preparser_msg_serdes_cbs cbs = {.read=wire_read,.write=wire_write};
    *codec = (struct vlc_preparser_msg_serdes){.owner.cbs=&cbs}; assert(serdes_Open(codec, true) == VLC_SUCCESS);
}
static input_item_t *native_item(void)
{
    input_item_t *item = input_item_New("file:///private/Show.Name.S02E03.wav", "User name"); assert(item != NULL);
    input_item_SetTitle(item, "User title");
    vlc_mutex_lock(&item->lock);
    vlc_meta_SetWithPriority(item->p_meta, vlc_meta_Artist, "Native artist", VLC_META_PRIORITY_INBAND);
    vlc_meta_SetExtraWithPriority(item->p_meta, "Native extra", "Exact value", VLC_META_PRIORITY_INBAND);
    item->p_stats = calloc(1, sizeof(*item->p_stats)); assert(item->p_stats != NULL);
    item->p_stats->i_read_bytes = UINT64_MAX; item->p_stats->i_demux_read_packets = UINT64_C(9007199254740993);
    const uint32_t negative_zero = UINT32_C(0x80000000), nan = UINT32_C(0x7fc01234);
    memcpy(&item->p_stats->f_input_bitrate, &negative_zero, 4); memcpy(&item->p_stats->f_demux_bitrate, &nan, 4);
    vlc_mutex_unlock(&item->lock);
    assert(input_item_AddInfo(item, ".Native", "Codec", "%s", "Native codec") == VLC_SUCCESS);
    vlc_epg_t *table = vlc_epg_New(UINT32_MAX, UINT16_MAX); assert(table != NULL); table->b_present = true;
    table->psz_name = strdup("Channel"); assert(table->psz_name != NULL);
    vlc_epg_event_t *event = vlc_epg_event_New(UINT16_MAX, INT64_C(9007199254740993), UINT32_MAX); assert(event != NULL);
    event->psz_name = strdup("Episode"); event->psz_short_description = strdup(""); event->psz_description = strdup("Description"); event->i_rating = UINT8_MAX;
    event->description_items = calloc(2, sizeof(*event->description_items)); assert(event->description_items != NULL); event->i_description_items = 2;
    event->description_items[0].psz_key = strdup("Key"); event->description_items[0].psz_value = strdup("Value");
    event->description_items[1].psz_key = strdup(""); event->description_items[1].psz_value = strdup("");
    assert(vlc_epg_AddEvent(table, event)); vlc_epg_SetCurrent(table, event->i_start);
    input_item_SetEpg(item, table, true); vlc_epg_Delete(table); input_item_SetEpgTime(item, INT64_MIN);
    for (unsigned priority = SLAVE_PRIORITY_USER; priority <= SLAVE_PRIORITY_ML_AUTO; ++priority) {
        char *uri = NULL; assert(asprintf(&uri, "file:///native-priority-%u.srt", priority) >= 0);
        input_item_slave_t *slave = input_item_slave_New(uri, SLAVE_TYPE_SPU, priority); free(uri);
        assert(slave != NULL); slave->b_forced = true; assert(input_item_AddSlave(item, slave) == VLC_SUCCESS);
    }
    return item;
}
static void native_equal(input_item_t *item)
{
    char *title = input_item_GetTitle(item); assert(title != NULL && !strcmp(title, "User title")); free(title);
    char *info = input_item_GetInfo(item, ".Native", "Codec"); assert(info != NULL && !strcmp(info, "Native codec")); free(info);
    assert(item->p_stats != NULL && item->p_stats->i_read_bytes == UINT64_MAX
        && item->p_stats->i_demux_read_packets == UINT64_C(9007199254740993));
    uint32_t input, demux; memcpy(&input, &item->p_stats->f_input_bitrate, 4); memcpy(&demux, &item->p_stats->f_demux_bitrate, 4);
    assert(input == UINT32_C(0x80000000) && demux == UINT32_C(0x7fc01234));
    assert(item->i_epg == 1 && item->p_epg_table == item->pp_epg[0] && item->i_epg_time == INT64_MIN);
    const vlc_epg_t *table = item->pp_epg[0]; assert(table->i_id == UINT32_MAX && table->i_source_id == UINT16_MAX && table->b_present);
    assert(table->i_event == 1 && table->p_current == table->pp_event[0]);
    const vlc_epg_event_t *event = table->p_current;
    assert(event->i_start == INT64_C(9007199254740993) && event->i_duration == UINT32_MAX && event->i_rating == UINT8_MAX);
    assert(event->i_description_items == 2 && !strcmp(event->description_items[0].psz_value, "Value")
        && event->description_items[1].psz_key != NULL && event->description_items[1].psz_key[0] == 0);
    vlc_meta_priority_t priority; assert(!strcmp(vlc_meta_GetWithPriority(item->p_meta, vlc_meta_Artist, &priority), "Native artist")
        && priority == VLC_META_PRIORITY_INBAND);
    assert(!strcmp(vlc_meta_GetExtraWithPriority(item->p_meta, "Native extra", &priority), "Exact value") && priority == VLC_META_PRIORITY_INBAND);
    assert(item->i_slaves == SLAVE_PRIORITY_ML_AUTO - SLAVE_PRIORITY_USER + 1);
    for (int i = 0; i < item->i_slaves; ++i)
        assert(item->pp_slaves[i]->i_priority == (enum slave_priority)(SLAVE_PRIORITY_USER + i) && item->pp_slaves[i]->b_forced);
    vlc_input_policy_t *policy = vlc_input_policy_HoldItem(item); assert(policy == NULL);
}
static void native_baseline_and_complete_output_round_trip(void)
{
    for (unsigned response = 0; response < 2; ++response) {
        struct vlc_preparser_msg_serdes writer, reader; codec(&writer); codec(&reader);
        struct wire wire = {.bytes=VLC_VECTOR_INITIALIZER}; struct vlc_preparser_msg message, decoded;
        vlc_preparser_msg_Init(&message, response ? VLC_PREPARSER_MSG_TYPE_RES : VLC_PREPARSER_MSG_TYPE_REQ, VLC_PREPARSER_MSG_REQ_TYPE_PARSE);
        input_item_t *item = native_item();
        if (response) {
            message.res.item = item; message.res.status = VLC_SUCCESS;
            message.res.subtree = input_item_node_Create(item); assert(message.res.subtree != NULL);
            input_item_node_t *second = input_item_node_Create(item); assert(second != NULL && vlc_vector_push(&message.res.subtrees, second));
            input_attachment_t *attachment = vlc_input_attachment_New("cover", "application/octet-stream", "", "data", 4);
            assert(attachment != NULL && vlc_vector_push(&message.res.attachments, attachment));
        } else { message.req.uri = input_item_GetURI(item); message.req.options = VLC_PREPARSER_TYPE_PARSE; message.req.baseline = item; }
        assert(serdes_Serialize(&writer, &message, &wire) == VLC_SUCCESS);
        assert(serdes_Deserialize(&reader, &decoded, &wire) == VLC_SUCCESS);
        native_equal(response ? decoded.res.item : decoded.req.baseline);
        if (response) assert(decoded.res.subtree != NULL && decoded.res.subtrees.size == 1 && decoded.res.attachments.size == 1
            && decoded.res.attachments.data[0]->i_data == 4);
        vlc_preparser_msg_Clean(&decoded); vlc_preparser_msg_Clean(&message); vlc_vector_clear(&wire.bytes);
        serdes_Close(&reader); serdes_Close(&writer);
    }
}
static void interrupt_retry_and_cancellation_have_distinct_results(void)
{
    for (unsigned cancel = 0; cancel < 2; ++cancel) {
        struct vlc_preparser_msg_serdes writer, reader; codec(&writer); codec(&reader);
        struct wire wire = {.bytes=VLC_VECTOR_INITIALIZER}; struct vlc_preparser_msg message, decoded;
        vlc_preparser_msg_Init(&message, VLC_PREPARSER_MSG_TYPE_RES, VLC_PREPARSER_MSG_REQ_TYPE_PARSE); message.res.status = VLC_SUCCESS;
        wire.interrupts = 1; assert(serdes_Serialize(&writer, &message, &wire) == VLC_SUCCESS);
        vlc_interrupt_t *interrupt = vlc_interrupt_create(); assert(interrupt != NULL); vlc_interrupt_t *previous = vlc_interrupt_set(interrupt);
        wire.calls = 0; wire.interrupts = 1;
        if (cancel) { wire.always_interrupt = true; vlc_interrupt_kill(interrupt); }
        const int status = serdes_Deserialize(&reader, &decoded, &wire);
        if (cancel) assert(status == -EINTR && wire.calls == 1);
        else { assert(status == VLC_SUCCESS && wire.calls > 1); vlc_preparser_msg_Clean(&decoded); }
        vlc_interrupt_set(previous); vlc_interrupt_destroy(interrupt);
        vlc_preparser_msg_Clean(&message); vlc_vector_clear(&wire.bytes); serdes_Close(&reader); serdes_Close(&writer);
    }
}
static void replace_wire(struct wire *wire, const char *needle, const char *replacement)
{
    const size_t old = strlen(needle), length = strlen(replacement);
    const uint8_t *found = memmem(wire->bytes.data, wire->bytes.size, needle, old); assert(found != NULL);
    const size_t before = found - wire->bytes.data, size = wire->bytes.size - old + length;
    uint8_t *next = malloc(size); assert(next != NULL);
    memcpy(next, wire->bytes.data, before); memcpy(next + before, replacement, length);
    memcpy(next + before + length, wire->bytes.data + before + old, wire->bytes.size - before - old);
    free(wire->bytes.data); wire->bytes.data = next; wire->bytes.size = wire->bytes.cap = size;
}
static void bad_native_values_refuse_complete_messages(void)
{
    char priority_before[64], priority_after[64];
    snprintf(priority_before, sizeof(priority_before), "\"i_priority\": %u", SLAVE_PRIORITY_ML_AUTO);
    snprintf(priority_after, sizeof(priority_after), "\"i_priority\": %u", SLAVE_PRIORITY_ML_AUTO + 1);
    const char *before[] = {"\"readBytes\": \"18446744073709551615\"", "\"readBytes\": \"18446744073709551615\"",
        "\"epgTime\": \"-9223372036854775808\"", "\"currentTable\": 0", "\"current\": 0", "\"inputBitrateBits\": 2147483648", priority_before};
    const char *after[] = {"\"readBytes\": \"-1\"", "\"readBytes\": \"18446744073709551616\"",
        "\"epgTime\": \"9223372036854775808\"", "\"currentTable\": 1", "\"current\": 1", "\"inputBitrateBits\": 4294967296", priority_after};
    for (size_t i = 0; i < ARRAY_SIZE(before); ++i) {
        struct vlc_preparser_msg_serdes writer, reader; codec(&writer); codec(&reader);
        struct wire wire = {.bytes=VLC_VECTOR_INITIALIZER}; struct vlc_preparser_msg message, decoded;
        vlc_preparser_msg_Init(&message, VLC_PREPARSER_MSG_TYPE_RES, VLC_PREPARSER_MSG_REQ_TYPE_PARSE);
        message.res.item = native_item(); message.res.status = VLC_SUCCESS;
        assert(serdes_Serialize(&writer, &message, &wire) == VLC_SUCCESS); replace_wire(&wire, before[i], after[i]);
        assert(serdes_Deserialize(&reader, &decoded, &wire) != VLC_SUCCESS);
        vlc_preparser_msg_Clean(&message); vlc_vector_clear(&wire.bytes); serdes_Close(&reader); serdes_Close(&writer);
    }
}
static void baseline_uri_and_message_tags_must_agree(void)
{
    for (unsigned mismatch = 0; mismatch < 2; ++mismatch) {
        struct vlc_preparser_msg_serdes writer, reader; codec(&writer); codec(&reader);
        struct wire wire = {.bytes=VLC_VECTOR_INITIALIZER}; struct vlc_preparser_msg message, decoded;
        vlc_preparser_msg_Init(&message, VLC_PREPARSER_MSG_TYPE_REQ, VLC_PREPARSER_MSG_REQ_TYPE_PARSE);
        message.req.options = VLC_PREPARSER_TYPE_PARSE; message.req.baseline = native_item(); message.req.uri = input_item_GetURI(message.req.baseline);
        assert(serdes_Serialize(&writer, &message, &wire) == VLC_SUCCESS);
        if (mismatch) replace_wire(&wire, "\"req_type\": 0", "\"req_type\": 1");
        else replace_wire(&wire, "\"uri\": \"file:///private/Show.Name.S02E03.wav\"", "\"uri\": \"file:///different.wav\"");
        assert(serdes_Deserialize(&reader, &decoded, &wire) != VLC_SUCCESS);
        vlc_preparser_msg_Clean(&message); vlc_vector_clear(&wire.bytes); serdes_Close(&reader); serdes_Close(&writer);
    }
}
static void declared_attachment_and_tree_limits_refuse_before_publication(void)
{
    for (unsigned tree = 0; tree < 2; ++tree) {
        struct vlc_preparser_msg_serdes writer, reader; codec(&writer); codec(&reader);
        struct wire wire = {.bytes=VLC_VECTOR_INITIALIZER}; struct vlc_preparser_msg message, decoded;
        vlc_preparser_msg_Init(&message, VLC_PREPARSER_MSG_TYPE_RES, VLC_PREPARSER_MSG_REQ_TYPE_PARSE);
        message.res.item = native_item(); message.res.status = VLC_SUCCESS;
        if (tree) {
            message.res.subtree = input_item_node_Create(message.res.item); assert(message.res.subtree != NULL);
            input_item_node_t *node = message.res.subtree;
            for (unsigned i = 1; i < 33; ++i) { node = input_item_node_AppendItem(node, message.res.item); assert(node != NULL); }
        } else {
            input_attachment_t *attachment = vlc_input_attachment_New("x", "", "", "data", 4);
            assert(attachment != NULL && vlc_vector_push(&message.res.attachments, attachment));
        }
        assert(serdes_Serialize(&writer, &message, &wire) == VLC_SUCCESS);
        if (!tree) replace_wire(&wire, "\"i_data\": 4", "\"i_data\": 16777217");
        assert(serdes_Deserialize(&reader, &decoded, &wire) != VLC_SUCCESS);
        vlc_preparser_msg_Clean(&message); vlc_vector_clear(&wire.bytes); serdes_Close(&reader); serdes_Close(&writer);
    }
}
static void native_collection_and_json_depth_limits_are_bounded(void)
{
    for (unsigned depth = 0; depth < 2; ++depth) {
        struct vlc_preparser_msg_serdes writer, reader; codec(&writer); codec(&reader);
        struct wire wire = {.bytes=VLC_VECTOR_INITIALIZER}; struct vlc_preparser_msg message, decoded;
        vlc_preparser_msg_Init(&message, VLC_PREPARSER_MSG_TYPE_RES, VLC_PREPARSER_MSG_REQ_TYPE_PARSE);
        message.res.status = VLC_SUCCESS; message.res.item = input_item_New("file:///test", "Test"); assert(message.res.item != NULL);
        input_item_SetTitle(message.res.item, "Title");
        assert(serdes_Serialize(&writer, &message, &wire) == VLC_SUCCESS);
        struct vlc_memstream replacement; vlc_memstream_open(&replacement);
        if (depth) {
            vlc_memstream_puts(&replacement, "\"infos\":[");
            for (unsigned i = 0; i < 130; ++i) vlc_memstream_putc(&replacement, '[');
            vlc_memstream_putc(&replacement, '0');
            for (unsigned i = 0; i < 130; ++i) vlc_memstream_putc(&replacement, ']');
            vlc_memstream_putc(&replacement, ']');
        } else {
            vlc_memstream_puts(&replacement, "\"infos\":[{\"name\":\"C\",\"entries\":[");
            for (unsigned i = 0; i < 4097; ++i)
                vlc_memstream_puts(&replacement, i ? ",{\"name\":\"K\",\"value\":\"V\"}" : "{\"name\":\"K\",\"value\":\"V\"}");
            vlc_memstream_puts(&replacement, "]}]");
        }
        assert(vlc_memstream_close(&replacement) == 0); replace_wire(&wire, "\"infos\":[]", replacement.ptr); free(replacement.ptr);
        const int status = serdes_Deserialize(&reader, &decoded, &wire);
        assert(status != VLC_SUCCESS); if (depth) assert(status == VLC_EINVAL);
        vlc_preparser_msg_Clean(&message); vlc_vector_clear(&wire.bytes); serdes_Close(&reader); serdes_Close(&writer);
    }
}
int main(void)
{
    bad_native_values_refuse_complete_messages();
    baseline_uri_and_message_tags_must_agree();
    declared_attachment_and_tree_limits_refuse_before_publication();
    native_collection_and_json_depth_limits_are_bounded();
    native_baseline_and_complete_output_round_trip();
    interrupt_retry_and_cancellation_have_distinct_results();
    return 0;
}
