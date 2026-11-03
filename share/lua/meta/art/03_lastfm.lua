--[[
 Gets an artwork from last.fm

 Copyright © 2010 the VideoLAN team

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

-- Return the artwork
function fetch_art()
    if vlc.item == nil then return nil, "not_applicable" end
    local meta = vlc.item:metas()

    if meta["Listing Type"] == "radio"
    or meta["Listing Type"] == "tv"
    then return nil, "not_applicable" end

    if meta["artist"] and meta["album"] then
        title = vlc.strings.encode_uri_component(meta["artist"]).."/"..vlc.strings.encode_uri_component(meta["album"])
    else
        return nil, "not_applicable"
    end
    -- remove -.* from string
    title = string.gsub( title, " ?%-.*", "" )
    -- remove (info..) from string
    title = string.gsub( title, "%(.*%)", "" )
    -- remove CD2 etc from string
    title = string.gsub( title, "CD%d+", "" )
    -- remove Disc \w+ from string
    title = string.gsub( title, "Disc %w+", "" )
    fd = vlc.stream( "http://www.last.fm/music/" .. title )
    if not fd then return nil, "unavailable" end
    page = fd:read( 65653 )
    fd = nil
    if type(page) ~= "string" or #page == 0 then return nil, "unavailable" end
    _, _, arturl = string.find( page, "<meta property=\"og:image\" content=\"([^\"]+)\" />" )
    -- Don't use default album-art (not found one)
    if not arturl then return nil, "unavailable" end
    if string.find( arturl, "default_album_mega.png") then
        local complete = #page < 65653 and page:gsub('>%s+<', '><'):match('^%s*(.-)%s*$')
        if not arturl:find('[<>&%c]') and complete == '<html><meta property="og:image" content="' .. arturl .. '" /></html>' then
            return nil, "no_match"
        end
        return nil, "unavailable"
    end
    return arturl, "match"
end
