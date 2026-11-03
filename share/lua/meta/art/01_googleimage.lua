--[[
 Gets an artwork from images.google.com

 Copyright © 2007 the VideoLAN team

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

-- Radio Entries
    if meta["Listing Type"] == "radio"
    then
        if not meta["title"] then return nil, "not_applicable" end
        title = meta["title"] .. " radio logo"
-- TV Entries
    elseif meta["Listing Type"] == "tv"
    then
        if not meta["title"] then return nil, "not_applicable" end
        title = meta["title"] .. " tv logo"
-- Album entries
    elseif meta["artist"] and meta["album"] then
        title = meta["artist"].." "..meta["album"].." cover"
    elseif meta["artist"] and meta["title"] then
        title = meta["artist"].." "..meta["title"].." cover"
    else
        return nil, "not_applicable"
    end
    fd = vlc.stream( "http://images.google.com/images?q="..vlc.strings.encode_uri_component( title ) )
    if not fd then return nil, "unavailable" end

    page = fd:read( 65653 )
    fd = nil
    if type(page) ~= "string" or #page == 0 then return nil, "unavailable" end
    _, _, _, arturl = string.find( page, "<img height=\"([0-9]+)\" src=\"([^\"]+gstatic.com[^\"]+)\"" )
    if arturl then return arturl, "match" end
    local complete = #page < 65653 and page:gsub('>%s+<', '><'):match('^%s*(.-)%s*$')
    if complete == '<html><head><title>Google Images</title></head><body>Your search did not match any image results.</body></html>' then
        return nil, "no_match"
    end
    return nil, "unavailable"
end
