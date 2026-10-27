// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_input.h>
#include <vlc_epg.h>
#include <vlc_meta.h>
#include <vlc_input_policy.h>
#include "item_parse.h"
#include "metadata_revision.h"
#include "artwork_policy.h"
#include "info.h"

static void *parse_alloc(size_t count, size_t width, size_t *remaining)
{
    if (width == 0 || count > *remaining / width) return NULL;
    if (count == 0) return NULL;
    void *value = calloc(count, width);
    if (value != NULL) *remaining -= count * width;
    return value;
}
static bool parse_string(char **target, const char *source, size_t *remaining)
{
    *target = NULL;
    if (source == NULL) return true;
    const size_t length = strnlen(source, *remaining);
    if (length == *remaining) return false;
    *target = parse_alloc(length + 1, 1, remaining);
    if (*target == NULL) return false;
    memcpy(*target, source, length + 1); return true;
}
static bool parse_es(es_format_t *target, const es_format_t *source, size_t *remaining)
{
    *target = *source;
    target->psz_language = target->psz_description = NULL;
    target->p_extra = NULL; target->i_extra = 0;
    target->p_extra_languages = NULL; target->i_extra_languages = 0;
    if (source->i_cat == VIDEO_ES) target->video.p_palette = NULL;
    if (source->i_cat == SPU_ES) target->subs.psz_encoding = NULL;
    if (!parse_string(&target->psz_language, source->psz_language, remaining)
        || !parse_string(&target->psz_description, source->psz_description, remaining)) return false;
    if (source->i_extra != 0) {
        if (source->p_extra == NULL) return false;
        target->p_extra = parse_alloc(source->i_extra, 1, remaining);
        if (target->p_extra == NULL) return false;
        memcpy(target->p_extra, source->p_extra, source->i_extra); target->i_extra = source->i_extra;
    }
    if (source->i_extra_languages > VLC_ITEM_PARSE_MAX_ENTRIES) return false;
    if (source->i_extra_languages != 0) {
        if (source->p_extra_languages == NULL) return false;
        target->p_extra_languages = parse_alloc(source->i_extra_languages, sizeof(*target->p_extra_languages), remaining);
        if (target->p_extra_languages == NULL) return false;
        target->i_extra_languages = source->i_extra_languages;
        for (unsigned i = 0; i < source->i_extra_languages; ++i)
            if (!parse_string(&target->p_extra_languages[i].psz_language, source->p_extra_languages[i].psz_language, remaining)
                || !parse_string(&target->p_extra_languages[i].psz_description, source->p_extra_languages[i].psz_description, remaining)) return false;
    }
    if (source->i_cat == SPU_ES
        && !parse_string(&target->subs.psz_encoding, source->subs.psz_encoding, remaining)) return false;
    if (source->i_cat == VIDEO_ES && source->video.p_palette != NULL) {
        target->video.p_palette = parse_alloc(1, sizeof(*target->video.p_palette), remaining);
        if (target->video.p_palette == NULL) return false;
        *target->video.p_palette = *source->video.p_palette;
    }
    return true;
}
static vlc_epg_t *parse_epg(const vlc_epg_t *source, size_t *remaining, size_t *events)
{
    if (source->i_event > VLC_ITEM_PARSE_MAX_ENTRIES - *events) return NULL;
    *events += source->i_event;
    if (*remaining < sizeof(vlc_epg_t)) return NULL;
    *remaining -= sizeof(vlc_epg_t);
    vlc_epg_t *copy = vlc_epg_New(source->i_id, source->i_source_id);
    if (copy == NULL) return NULL;
    copy->b_present = source->b_present;
    if (!parse_string(&copy->psz_name, source->psz_name, remaining)) goto error;
    if (source->i_event != 0) {
        copy->pp_event = parse_alloc(source->i_event, sizeof(*copy->pp_event), remaining);
        if (copy->pp_event == NULL) goto error;
    }
    for (size_t i = 0; i < source->i_event; ++i) {
        const vlc_epg_event_t *original = source->pp_event[i];
        if (original->i_description_items < 0 || (unsigned)original->i_description_items > VLC_ITEM_PARSE_MAX_ENTRIES) goto error;
        if (original->i_description_items != 0 && original->description_items == NULL) goto error;
        if (*remaining < sizeof(vlc_epg_event_t)) goto error;
        *remaining -= sizeof(vlc_epg_event_t);
        vlc_epg_event_t *event = vlc_epg_event_New(original->i_id, original->i_start, original->i_duration);
        if (event == NULL) goto error;
        copy->pp_event[copy->i_event++] = event; event->i_rating = original->i_rating;
        if (!parse_string(&event->psz_name, original->psz_name, remaining)
            || !parse_string(&event->psz_description, original->psz_description, remaining)
            || !parse_string(&event->psz_short_description, original->psz_short_description, remaining)) goto error;
        if (original->i_description_items != 0) {
            event->description_items = parse_alloc(original->i_description_items, sizeof(*event->description_items), remaining);
            if (event->description_items == NULL) goto error;
            event->i_description_items = original->i_description_items;
            for (int j = 0; j < original->i_description_items; ++j)
                if (!parse_string(&event->description_items[j].psz_key, original->description_items[j].psz_key, remaining)
                    || !parse_string(&event->description_items[j].psz_value, original->description_items[j].psz_value, remaining)) goto error;
        }
        if (source->p_current == original) copy->p_current = event;
    }
    return copy;
error:
    vlc_epg_Delete(copy); return NULL;
}
input_item_t *vlc_item_parse_CopyLocked(input_item_t *source)
{
    size_t remaining = VLC_ITEM_PARSE_MAX_BYTES;
    return vlc_item_parse_CopyBudgetLocked(source, &remaining);
}
input_item_t *vlc_item_parse_CopyBudgetLocked(input_item_t *source, size_t *budget)
{
    vlc_mutex_assert(&source->lock);
    size_t remaining = *budget;
    if (source->es_vec.size > VLC_ITEM_PARSE_MAX_ENTRIES || source->i_options < 0
        || (unsigned)source->i_options > VLC_ITEM_PARSE_MAX_ENTRIES || source->i_slaves < 0
        || (unsigned)source->i_slaves > VLC_ITEM_PARSE_MAX_ENTRIES || source->i_epg < 0
        || (unsigned)source->i_epg > VLC_ITEM_PARSE_MAX_ENTRIES || remaining < sizeof(input_item_owner_t)) return NULL;
    remaining -= sizeof(input_item_owner_t);
    input_item_t *copy = input_item_NewExt(NULL, NULL, source->i_duration, source->i_type,
                                         source->b_net ? ITEM_NET : ITEM_LOCAL);
    if (copy == NULL) return NULL;
    if (!parse_string(&copy->psz_uri, source->psz_uri, &remaining)
        || !parse_string(&copy->psz_name, source->psz_name, &remaining)) goto error;
    vlc_meta_t *meta = vlc_meta_CopyNative(source->p_meta, &remaining);
    if (meta == NULL) goto error;
    vlc_meta_Delete(copy->p_meta); copy->p_meta = meta;
    if (source->i_options != 0) {
        copy->ppsz_options = parse_alloc(source->i_options, sizeof(*copy->ppsz_options), &remaining);
        copy->optflagv = parse_alloc(source->i_options, sizeof(*copy->optflagv), &remaining);
        if (copy->ppsz_options == NULL || copy->optflagv == NULL || source->optflagc != (unsigned)source->i_options) goto error;
        for (int i = 0; i < source->i_options; ++i) {
            if (!parse_string(&copy->ppsz_options[i], source->ppsz_options[i], &remaining)) goto error;
            ++copy->i_options;
        }
        memcpy(copy->optflagv, source->optflagv, source->optflagc); copy->optflagc = source->optflagc;
    }
    if (source->i_slaves != 0) {
        copy->pp_slaves = parse_alloc(source->i_slaves, sizeof(*copy->pp_slaves), &remaining);
        if (copy->pp_slaves == NULL) goto error;
        for (int i = 0; i < source->i_slaves; ++i) {
            input_item_slave_t *original = source->pp_slaves[i];
            const size_t length = strnlen(original->psz_uri, remaining);
            if (length == remaining) goto error;
            input_item_slave_t *slave = parse_alloc(1, sizeof(*slave) + length + 1, &remaining);
            if (slave == NULL) goto error;
            *slave = *original; memcpy(slave->psz_uri, original->psz_uri, length + 1);
            copy->pp_slaves[copy->i_slaves++] = slave;
        }
    }
    if (source->es_vec.size != 0) {
        copy->es_vec.data = parse_alloc(source->es_vec.size, sizeof(*copy->es_vec.data), &remaining);
        if (copy->es_vec.data == NULL) goto error;
        copy->es_vec.cap = source->es_vec.size;
        for (size_t i = 0; i < source->es_vec.size; ++i) {
            struct input_item_es *next = &copy->es_vec.data[copy->es_vec.size++];
            if (source->es_vec.data[i].id == NULL) goto error;
            next->id_stable = source->es_vec.data[i].id_stable;
            if (!parse_es(&next->es, &source->es_vec.data[i].es, &remaining)
                || !parse_string(&next->id, source->es_vec.data[i].id, &remaining)) goto error;
        }
    }
    size_t categories = 0, infos = 0;
    info_category_t *category;
    vlc_list_foreach(category, &source->categories, node) {
        if (++categories > VLC_ITEM_PARSE_MAX_ENTRIES) goto error;
        if (category->psz_name == NULL) goto error;
        info_category_t *next = parse_alloc(1, sizeof(*next), &remaining);
        if (next == NULL) goto error;
        vlc_list_init(&next->infos); vlc_list_append(&next->node, &copy->categories);
        if (!parse_string(&next->psz_name, category->psz_name, &remaining)) goto error;
        info_t *info;
        info_foreach(info, &category->infos) {
            if (++infos > VLC_ITEM_PARSE_MAX_ENTRIES) goto error;
            if (info->psz_name == NULL) goto error;
            info_t *next_info = parse_alloc(1, sizeof(*next_info), &remaining);
            if (next_info == NULL) goto error;
            vlc_list_append(&next_info->node, &next->infos);
            if (!parse_string(&next_info->psz_name, info->psz_name, &remaining)
                || !parse_string(&next_info->psz_value, info->psz_value, &remaining)) goto error;
        }
    }
    if (source->p_stats != NULL) {
        copy->p_stats = parse_alloc(1, sizeof(*copy->p_stats), &remaining);
        if (copy->p_stats == NULL) goto error;
        *copy->p_stats = *source->p_stats;
    }
    if (source->i_epg != 0) {
        copy->pp_epg = parse_alloc(source->i_epg, sizeof(*copy->pp_epg), &remaining);
        if (copy->pp_epg == NULL) goto error;
        size_t events = 0;
        for (int i = 0; i < source->i_epg; ++i) {
            vlc_epg_t *epg = parse_epg(source->pp_epg[i], &remaining, &events);
            if (epg == NULL) goto error;
            copy->pp_epg[copy->i_epg++] = epg;
            if (source->p_epg_table == source->pp_epg[i]) copy->p_epg_table = epg;
        }
    }
    copy->i_epg_time = source->i_epg_time;
    item_owner(copy)->input_policy = vlc_input_policy_Hold(item_owner(source)->input_policy);
    item_owner(copy)->parse_revisions = item_owner(source)->parse_revisions;
    const char *proof_strings[] = {item_owner(source)->artwork_uri, item_owner(source)->artwork_source_uri};
    for (size_t i = 0; i < ARRAY_SIZE(proof_strings); ++i) {
        if (proof_strings[i] == NULL) continue;
        const size_t length = strnlen(proof_strings[i], remaining);
        if (length == remaining) goto error;
        remaining -= length + 1;
    }
    if (!vlc_artwork_item_CopyApprovalLocked(copy, source)) goto error;
    *budget = remaining;
    return copy;
error:
    input_item_Release(copy); return NULL;
}

int vlc_item_parse_PrepareLinksLocked(struct vlc_item_parse_delta *delta, input_item_t *target)
{
    vlc_mutex_assert(&target->lock);
    input_item_t *source = delta->item;
    if (!(delta->fields & VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_SLAVES))) return VLC_SUCCESS;
    if (target->i_slaves < 0 || (unsigned)target->i_slaves > VLC_ITEM_PARSE_MAX_ENTRIES
        || (unsigned)source->i_slaves > VLC_ITEM_PARSE_MAX_ENTRIES - target->i_slaves) return VLC_EINVAL;
    const size_t count = target->i_slaves + source->i_slaves;
    delta->links = count != 0 ? calloc(count, sizeof(*delta->links)) : NULL;
    if (count != 0 && delta->links == NULL) return VLC_ENOMEM;
    delta->link_count = delta->preserved_links = target->i_slaves;
    if (target->i_slaves != 0) memcpy(delta->links, target->pp_slaves, target->i_slaves * sizeof(*delta->links));
    for (int i = 0; i < source->i_slaves; ++i) {
        input_item_slave_t *slave = source->pp_slaves[i];
        bool exists = false;
        for (int j = 0; j < delta->link_count; ++j)
            if (!strcmp(delta->links[j]->psz_uri, slave->psz_uri)) { exists = true; break; }
        if (!exists) {
            delta->links[delta->link_count++] = slave; source->pp_slaves[i] = NULL;
        }
    }
    /* Compact the duplicate links. The prepared item retains their ownership. */
    int retained = 0;
    for (int i = 0; i < source->i_slaves; ++i)
        if (source->pp_slaves[i] != NULL) source->pp_slaves[retained++] = source->pp_slaves[i];
    source->i_slaves = retained;
    return VLC_SUCCESS;
}
void vlc_item_parse_Clear(struct vlc_item_parse_delta *delta)
{
    for (int i = delta->preserved_links; i < delta->link_count; ++i) input_item_slave_Delete(delta->links[i]);
    free(delta->links);
    if (delta->item != NULL) input_item_Release(delta->item);
    memset(delta, 0, sizeof(*delta));
}
static void swap_categories(struct vlc_list *left, struct vlc_list *right)
{
    struct vlc_list temporary; vlc_list_init(&temporary);
    while (!vlc_list_is_empty(left)) { struct vlc_list *node = left->next; vlc_list_remove(node); vlc_list_append(node, &temporary); }
    while (!vlc_list_is_empty(right)) { struct vlc_list *node = right->next; vlc_list_remove(node); vlc_list_append(node, left); }
    while (!vlc_list_is_empty(&temporary)) { struct vlc_list *node = temporary.next; vlc_list_remove(node); vlc_list_append(node, right); }
}
void vlc_item_parse_SwapLocked(struct vlc_item_parse_delta *delta, input_item_t *target, uint32_t fields)
{
    vlc_mutex_assert(&target->lock);
    input_item_t *prepared = delta->item;
#define SWAP(member) do { typeof(target->member) old = target->member; target->member = prepared->member; prepared->member = old; } while (0)
    if (fields & VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_NAME)) { SWAP(psz_name); }
    if (fields & VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_DURATION)) { SWAP(i_duration); }
    if (fields & VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_TRACKS)) { SWAP(es_vec); }
    if (fields & VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_INFOS)) swap_categories(&target->categories, &prepared->categories);
    if (fields & VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_TYPE)) { SWAP(i_type); }
    if (fields & VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_EPG)) { SWAP(i_epg); SWAP(pp_epg); SWAP(p_epg_table); }
    if (fields & VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_SLAVES)) {
        input_item_slave_t **old = target->pp_slaves;
        target->pp_slaves = delta->links; target->i_slaves = delta->link_count;
        delta->links = old; delta->link_count = delta->preserved_links;
    }
    /* Live counters and clocks have precedence. Fill an absent value only. */
    if (target->p_stats == NULL) { SWAP(p_stats); }
    if (target->i_epg_time == 0) target->i_epg_time = prepared->i_epg_time;
    for (unsigned i = 0; i < VLC_ITEM_PARSE_FIELD_COUNT; ++i)
        if (fields & VLC_ITEM_PARSE_BIT(i)) vlc_item_parse_TouchLocked(target, i);
#undef SWAP
}
