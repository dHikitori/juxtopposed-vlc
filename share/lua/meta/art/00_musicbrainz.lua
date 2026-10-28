--[[
 Gets an artwork from the Cover Art Archive or Amazon

 Copyright © 2007-2010 the VideoLAN team

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
--]]

function descriptor()
    return { scope="network" }
end

-- Accept only complete known empty responses. Other forms need a validator.
local function empty_response(page, mbid)
    if type(page) ~= "string" or #page == 0 or #page >= 65653 then return false end
    page = page:gsub('^%s*<%?xml version="1.0" encoding="UTF%-8"%?>%s*', '')
    page = page:gsub('<metadata xmlns="http://musicbrainz%.org/ns/mmd%-2%.0#">', '<metadata>', 1)
    page = page:gsub('>%s+<', '><'):match('^%s*(.-)%s*$')
    if not mbid then
        return page == '<metadata><release-list count="0"></release-list></metadata>'
            or page == '<metadata><release-list count="0"/></metadata>'
    end
    if not mbid:match('^%x%x%x%x%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%-%x%x%x%x%x%x%x%x%x%x%x%x$') then return false end
    return page == '<metadata><release id="' .. mbid
        .. '"><cover-art-archive><artwork>false</artwork></cover-art-archive></release></metadata>'
end

local function combined_outcome(before, after)
    if before == "unavailable" or after == "unavailable" then return "unavailable" end
    if before == "no_match" or after == "no_match" then return "no_match" end
    return "not_applicable"
end

function try_query(mbid)
    local relquery = "http://mb.videolan.org/ws/2/release/" .. mbid
    local s = vlc.stream( relquery )
    if not s then return nil, "unavailable" end
    local page = s:read( 65653 )
    if type(page) ~= "string" or #page == 0 then return nil, "unavailable" end

    found, _ = string.find( page, "<artwork>true</artwork>" )
    if found then
        front, _ = string.find( page, "<front>true</front>" )
        if front then
            return "http://coverartarchive.org/release/"..mbid.."/front-500", "match"
        end
    end

    -- FIXME: multiple results may be available
    _, _, asin = string.find( page, "<asin>(%w+)</asin>" )
    if asin then
        return "http://images.amazon.com/images/P/"..asin..".01._SCLZZZZZZZ_.jpg", "match"
    end
    vlc.msg.dbg("Neither coverartarchive.org nor amazon have cover art for this release")
    return nil, empty_response(page, mbid) and "no_match" or "unavailable"
end

-- Return the mbid for the first release returned by the MusicBrainz search server for query
function get_releaseid(query)
    local s = vlc.stream( query )
    if not s then return nil, "unavailable" end
    local page = s:read( 65653 )
    if type(page) ~= "string" or #page == 0 then return nil, "unavailable" end

    -- FIXME: multiple results may be available and the first one is not
    -- guaranteed to have asin, so if it doesnt, we wouldnt get any art
    _, _, releaseid = string.find( page, "<release id=\"([%x%-]-)\"" )
    if releaseid then
        return releaseid, "match"
    end
    return nil, empty_response(page) and "no_match" or "unavailable"
end

-- Return the artwork
function fetch_art()
    if vlc.item == nil then return nil, "not_applicable" end
    local meta = vlc.item:metas()

    if meta["Listing Type"] == "radio"
    or meta["Listing Type"] == "tv"
    then return nil, "not_applicable" end

    -- TagLib normalizes the identifier, while Vorbis/FLAC demuxers can
    -- expose the original comment name during playback.
    local releaseid = meta["MB_ALBUMID"]
    if not releaseid or releaseid == "" then
        releaseid = meta["MUSICBRAINZ_ALBUMID"]
    end
    if releaseid == "" then releaseid = nil end
    local outcome = "not_applicable"

    if not releaseid and meta["artist"] and meta["album"] then
        query = "artist:\"" .. meta["artist"] .. "\" AND release:\"" .. meta["album"] .. "\""
        relquery = "http://mb.videolan.org/ws/2/release/?query=" .. vlc.strings.encode_uri_component( query )
        local result
        releaseid, result = get_releaseid( relquery )
        outcome = combined_outcome(outcome, result)
    end
    if not releaseid and meta["artist"] and meta["title"] then
        query = "artist:\"" .. meta["artist"] .. "\" AND recording:\"" .. meta["title"] .. "\""
        recquery = "http://mb.videolan.org/ws/2/recording/?query=" .. vlc.strings.encode_uri_component( query )
        local result
        releaseid, result = get_releaseid( recquery )
        outcome = combined_outcome(outcome, result)
    end
    if releaseid then
        local artwork, result = try_query( releaseid )
        if artwork then return artwork, "match" end
        return nil, combined_outcome(outcome, result)
    else
        return nil, outcome
    end
end
