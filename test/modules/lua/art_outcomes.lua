-- SPDX-License-Identifier: LGPL-2.1-or-later
-- Run the shipped scripts with fixed metadata and stream responses.
-- This test has no network access.
local directory = assert(provider_source:match("^(.*[/\\])"))
local count = 0
local release = "76df3287-6cda-33eb-8e9a-044b5e15ffdd"
local cover = "http://coverartarchive.org/release/" .. release .. "/front-500"
local empty_release = '<metadata><release id="' .. release
    .. '"><cover-art-archive><artwork>false</artwork></cover-art-archive></release></metadata>'
local empty_search = '<metadata><release-list count="0"></release-list></metadata>'

local function check(file, metadata, responses, expected, outcome, expected_requests)
    local requests = 0
    local env = setmetatable({}, {__index = _G})
    env.vlc = {
        item = {metas = function() return metadata end},
        strings = {encode_uri_component = function(value) return value end},
        msg = {dbg = function() end},
        stream = function()
            requests = requests + 1
            local response = responses[requests]
            assert(response, "The script made an extra request.")
            if response.open_error then return nil end
            return {read = function()
                if response.read_error then return nil end
                return response.page
            end}
        end
    }
    local chunk
    if setfenv then
        chunk = assert(loadfile(directory .. file))
        setfenv(chunk, env)
    else
        chunk = assert(loadfile(directory .. file, "t", env))
    end
    chunk()
    assert(env.descriptor().scope == "network")
    local value, actual_outcome = env.fetch_art()
    assert(value == expected, file .. ": The first return changed.")
    assert(actual_outcome == outcome, file .. ": The native outcome is not correct.")
    assert(requests == expected_requests, file .. ": The request count changed.")
    count = count + 1
end

local mb = "00_musicbrainz.lua"
check(mb, {}, {}, nil, "not_applicable", 0)
check(mb, {MUSICBRAINZ_ALBUMID = ""}, {}, nil, "not_applicable", 0)
check(mb, {["Listing Type"] = "radio", MB_ALBUMID = release}, {}, nil, "not_applicable", 0)
check(mb, {MB_ALBUMID = release}, {{page = "<artwork>true</artwork><front>true</front>"}}, cover, "match", 1)
check(mb, {MB_ALBUMID = release}, {{page = empty_release}}, nil, "no_match", 1)
check(mb, {MB_ALBUMID = release}, {{open_error = true}}, nil, "unavailable", 1)
check(mb, {MB_ALBUMID = release}, {{read_error = true}}, nil, "unavailable", 1)
check(mb, {MB_ALBUMID = release}, {{page = "<html>Service error</html>"}}, nil, "unavailable", 1)
check(mb, {MB_ALBUMID = release}, {{page = "<metadata><release>"}}, nil, "unavailable", 1)
check(mb, {artist = "Artist", album = "Album", title = "Title"},
    {{page = empty_search}, {page = empty_search}}, nil, "no_match", 2)
check(mb, {artist = "Artist", album = "Album", title = "Title"},
    {{open_error = true}, {page = empty_search}}, nil, "unavailable", 2)
check(mb, {artist = "Artist", album = "Album", title = "Title"},
    {{open_error = true}, {page = '<metadata><release-list><release id="' .. release
        .. '"></release></release-list></metadata>'},
     {page = "<artwork>true</artwork><front>true</front>"}}, cover, "match", 3)

local google = "01_googleimage.lua"
local image = "https://encrypted-tbn0.gstatic.com/example.jpg"
local music = {artist = "Artist", album = "Album"}
check(google, {}, {}, nil, "not_applicable", 0)
check(google, music, {{page = '<img height="120" src="' .. image .. '"'}}, image, "match", 1)
check(google, music, {{open_error = true}}, nil, "unavailable", 1)
check(google, music, {{read_error = true}}, nil, "unavailable", 1)
check(google, music, {{page = '<html><head><title>Google Images</title></head><body>'
    .. 'Your search did not match any image results.</body></html>'}}, nil, "no_match", 1)
check(google, music, {{page = '<html><head><title>Google Images</title></head><body>'
    .. 'Unusual traffic</body></html>'}}, nil, "unavailable", 1)
check(google, music, {{page = '<html>Service error</html>'}}, nil, "unavailable", 1)
check(google, music, {{page = '<html><head><title>Google Images</title></head><body>'
    .. 'Your search did not match any image results.'}}, nil, "unavailable", 1)

local tv = "02_frenchtv.lua"
check(tv, {}, {}, nil, "not_applicable", 0)
check(tv, {title = "No such channel"}, {}, nil, "no_match", 0)
check(tv, {title = "France 2 HD"}, {},
    "https://upload.wikimedia.org/wikipedia/fr/thumb/e/e8/France_2_logo_antenne_(2008).png/270px-France_2_logo_antenne_(2008).png",
    "match", 0)

local last = "03_lastfm.lua"
check(last, {}, {}, nil, "not_applicable", 0)
check(last, {["Listing Type"] = "radio", artist = "Artist", album = "Album"}, {}, nil, "not_applicable", 0)
check(last, music, {{page = '<meta property="og:image" content="https://last.example/album.png" />'}},
    "https://last.example/album.png", "match", 1)
check(last, music, {{page = '<html><meta property="og:image" content="https://last.example/default_album_mega.png" /></html>'}},
    nil, "no_match", 1)
check(last, music, {{page = '<html>Service error</html>'}}, nil, "unavailable", 1)
check(last, music, {{open_error = true}}, nil, "unavailable", 1)
check(last, music, {{read_error = true}}, nil, "unavailable", 1)
print("Native artwork outcome cases passed: " .. count)
