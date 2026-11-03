/*****************************************************************************
 * item.h
 *****************************************************************************
 * Copyright (C) 2008 Laurent Aimar
 *
 * Authors: Laurent Aimar <fenrir _AT_ videolan _DOT_ org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef LIBVLC_INPUT_ITEM_H
#define LIBVLC_INPUT_ITEM_H 1

#include "input_interface.h"
#include <vlc_atomic.h>

enum vlc_item_parse_field {
    VLC_ITEM_PARSE_URI, VLC_ITEM_PARSE_NAME, VLC_ITEM_PARSE_DURATION,
    VLC_ITEM_PARSE_TRACKS, VLC_ITEM_PARSE_INFOS, VLC_ITEM_PARSE_TYPE,
    VLC_ITEM_PARSE_SLAVES, VLC_ITEM_PARSE_OPTIONS, VLC_ITEM_PARSE_EPG,
    VLC_ITEM_PARSE_FIELD_COUNT,
};
struct vlc_item_parse_revisions { uint64_t fields[VLC_ITEM_PARSE_FIELD_COUNT]; };

void input_item_UpdateTracksInfo( input_item_t *item, const es_format_t *fmt,
                                  const char *es_id, bool stable );
/**
 * Set the libvlc owner (libvlc_media_t *) with a release callback
 *
 * Can only be set after the item is just created.
 */
VLC_API void input_item_SetLibvlcOwner(input_item_t *, void *,
                                       void (*release)(void *));
/**
 * Get the libvlc owner (libvlc_media_t *)
 */
VLC_API void *input_item_GetLibvlcOwner(input_item_t *);

typedef struct input_item_owner
{
    input_item_t item;
    void *libvlc_owner;
    void (*libvlc_owner_release)(void *libvlc_owner);
    vlc_atomic_rc_t rc;
    struct vlc_input_policy *input_policy;
    char *artwork_uri;
    char *artwork_source_uri;
    uint64_t artwork_revision;
    struct vlc_input_policy *artwork_policy;
    struct vlc_item_parse_revisions parse_revisions;
} input_item_owner_t;

# define item_owner(item) ((struct input_item_owner *)(item))

/* The item lock is held. A saturated token never admits a local delta. */
static inline void vlc_item_parse_TouchLocked(input_item_t *item, enum vlc_item_parse_field field)
{
    uint64_t *token = &item_owner(item)->parse_revisions.fields[field];
    if (*token != UINT64_MAX) ++*token;
}

#endif
