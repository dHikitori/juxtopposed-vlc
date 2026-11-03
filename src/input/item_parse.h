// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_ITEM_PARSE_INTERNAL_H
#define VLC_ITEM_PARSE_INTERNAL_H 1
#include "item.h"

#define VLC_ITEM_PARSE_MAX_BYTES (16u * 1024u * 1024u)
#define VLC_ITEM_PARSE_MAX_ENTRIES 4096u
#define VLC_ITEM_PARSE_BIT(field) (UINT32_C(1) << (field))

/* The source lock is held. Copy every native parse fact and execution option.
 * Do not copy opaque owner data. Refuse a limit or incomplete allocation. */
input_item_t *vlc_item_parse_CopyLocked(input_item_t *source);
input_item_t *vlc_item_parse_CopyBudgetLocked(input_item_t *source, size_t *remaining_bytes);

struct vlc_item_parse_delta {
    input_item_t *item;
    uint32_t fields;
    bool extras, status;
    input_item_slave_t **links;
    int link_count, preserved_links;
};
/* The target lock is held. Allocate a table that retains its current links.
 * Only new URI links come from the prepared native item. */
int vlc_item_parse_PrepareLinksLocked(struct vlc_item_parse_delta *, input_item_t *target);
void vlc_item_parse_Clear(struct vlc_item_parse_delta *);
/* No allocation or callback. The target lock is held. Extra tags and status
 * use the separate reserved metadata tokens in the backend. */
void vlc_item_parse_SwapLocked(struct vlc_item_parse_delta *, input_item_t *, uint32_t fields);
#endif
