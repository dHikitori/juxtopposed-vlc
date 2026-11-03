/*****************************************************************************
 * vlc_meta_fetcher.h
 *****************************************************************************
 * Copyright (C) 2009 Rémi Denis-Courmont
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

#ifndef VLC_META_FETCHER_H
#define VLC_META_FETCHER_H 1

typedef enum meta_fetcher_scope_t
{
    FETCHER_SCOPE_LOCAL   = 0x01,
    FETCHER_SCOPE_NETWORK = 0x02,
    FETCHER_SCOPE_ANY     = 0x03
} meta_fetcher_scope_t;

/** A native selector. ANY permits the four app-shipped art providers. NONE
 * permits no Lua provider file. The named values select one shipped provider
 * before Lua state creation or file execution. They grant no network access.
 */
typedef enum meta_fetcher_provider_t
{
    META_FETCHER_PROVIDER_ANY = 0,
    META_FETCHER_PROVIDER_NONE,
    META_FETCHER_PROVIDER_MUSICBRAINZ,
    META_FETCHER_PROVIDER_GOOGLE_IMAGES,
    META_FETCHER_PROVIDER_FRENCH_TV,
    META_FETCHER_PROVIDER_LASTFM,
} meta_fetcher_provider_t;

/* A trusted native lookup result. This value is not item metadata or a guest
 * result field. UNKNOWN, transport failure, and execution failure are not a
 * completed empty lookup. Only an exact named native probe can seal a receipt.
 */
typedef enum meta_fetcher_outcome_t
{
    META_FETCHER_OUTCOME_UNKNOWN = 0,
    META_FETCHER_OUTCOME_MATCH,
    META_FETCHER_OUTCOME_NO_MATCH,
    META_FETCHER_OUTCOME_NOT_APPLICABLE,
    META_FETCHER_OUTCOME_UNAVAILABLE,
    META_FETCHER_OUTCOME_DENIED,
    META_FETCHER_OUTCOME_ERROR,
} meta_fetcher_outcome_t;

typedef struct meta_fetcher_t
{
    struct vlc_object_t obj;
    input_item_t *p_item;
    meta_fetcher_scope_t e_scope;
    meta_fetcher_provider_t e_provider;
    meta_fetcher_outcome_t e_outcome;
} meta_fetcher_t;

#endif
