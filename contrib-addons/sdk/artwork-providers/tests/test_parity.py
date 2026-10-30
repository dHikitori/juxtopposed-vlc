#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare each core Lua artwork script with its add-on replacement.

Both sides get the same media and the same service answers: the Lua script
through a stand-in `vlc` table, the add-on through the worker's host calls.
Each case states the artwork both must choose. Known differences are
asserted too, so that a change on either side shows up here.

Exit code 77 (skip) when no Lua 5.4 interpreter is available.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT.parent
WORKER = None
LUA = None
R1 = "76df3287-6cda-33eb-8e9a-044b5e15ffdd"
R2 = "0f3a8a51-7e4d-4c4f-9f64-2c1d4a3b5e6f"

LUA_HOST = r'''
local script, meta, pages = ...
requests = {}
local function encode(value)
    return (value:gsub("[^%w%-%._~]", function(c) return string.format("%%%02X", c:byte()) end))
end
vlc = {
    item = { metas = function() return meta end },
    stream = function(url)
        requests[#requests + 1] = url
        for _, page in ipairs(pages) do
            if url:find(page[1], 1, true) then
                return { read = function(_, size) return page[2]:sub(1, size) end }
            end
        end
        return nil
    end,
    strings = { encode_uri_component = encode },
    msg = { dbg = function() end, warn = function() end, err = function() end, info = function() end },
}
dofile(script)
local artwork, outcome = fetch_art()
for _, url in ipairs(requests) do io.write("request\t", url, "\n") end
io.write("artwork\t", artwork or "", "\n", "outcome\t", outcome or "", "\n")
'''


def lua_string(value):
    return '"' + ''.join(chr(b) if b < 128 and (chr(b).isalnum() or b == 32) else f'\\{b:03d}' for b in value.encode()) + '"'


def lua_table(mapping):
    return '{' + ','.join(f'[{lua_string(k)}]={lua_string(v)}' for k, v in mapping.items()) + '}'


def run_lua(script, meta, pages):
    pages = '{' + ','.join('{' + lua_string(p) + ',' + lua_string(b) + '}' for p, b in pages) + '}'
    program = f'local f = load({lua_string(LUA_HOST)}); f({lua_string(str(script))}, {lua_table(meta)}, {pages})'
    done = subprocess.run([str(LUA), '-'], input=program, capture_output=True, text=True, timeout=10)
    if done.returncode != 0:
        raise AssertionError(f'The Lua script failed: {done.stderr}')
    result = {'requests': []}
    for line in done.stdout.splitlines():
        key, _, value = line.partition('\t')
        if key == 'request':
            result['requests'].append(value)
        else:
            result[key] = value
    return result


def identity(meta):
    value = {'kind': 'musicTrack'}
    for field in ('title', 'artist', 'album'):
        if field in meta:
            value[field] = meta[field]
    if meta.get('MB_ALBUMID'):
        value['ids'] = {'musicBrainzAlbumId': meta['MB_ALBUMID']}
    if meta.get('Listing Type'):
        value['release'] = {'listingType': meta['Listing Type']}
    return value


def run_addon(package, meta, pages):
    process = subprocess.Popen([str(WORKER), str(ROOT / 'examples' / package / 'dist/addon.wasm')],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, env={'LANG': 'C'})
    try:
        def send(message):
            process.stdin.write(json.dumps(message) + '\n')
            process.stdin.flush()
            reply = json.loads(process.stdout.readline())
            assert reply.get('type') == 'messages', reply
            return reply['messages']
        if json.loads(process.stdout.readline()).get('type') != 'ready':
            raise AssertionError('The worker did not start.')
        requests = []
        messages = send({'type': 'invoke', 'id': 'lookup', 'contribution': 'artwork', 'operation': 'enrich',
                         'settings': {}, 'credentials': {}, 'hostCapabilities': {},
                         'context': {'identity': identity(meta), 'fields': ['artwork']}})
        while messages and messages[0].get('type') == 'host-call':
            call = messages[0]
            url = call['request']['url']
            requests.append(url)
            body = next((b for p, b in pages if p in url), None)
            response = {'status': 200 if body is not None else 404, 'body': body or '', 'headers': {}, 'truncated': False}
            messages = send({'type': 'host-result', 'id': call['id'], 'invocation': call['invocation'],
                             'status': 'ok', 'response': response})
        reply = messages[0]
        records = reply.get('data', {}).get('records', [])
        artwork = records[0]['fields']['artwork']['url'] if records else ''
        return {'requests': requests, 'artwork': artwork, 'outcome': reply['status']}
    finally:
        process.stdin.close()
        process.wait(timeout=5)
        process.stdout.close()
        process.stderr.close()


def image(url):
    """The image a URL names, without the host differences that are expected."""
    if not url:
        return None
    found = re.search(r'coverartarchive\.org/release/([0-9a-f-]{36})/front-500$', url)
    if found:
        return ('caa', found.group(1))
    found = re.search(r'amazon\.com/images/P/([A-Z0-9]{10})\.01\._SCLZZZZZZZ_\.jpg$', url)
    if found:
        return ('asin', found.group(1))
    return ('url', url)


def query(url):
    """The search text, or the path for a direct lookup."""
    found = re.search(r'[?&]q(?:uery)?=([^&]*)', url)
    return found.group(1) if found else re.sub(r'^https?://[^/]+', '', url).split('?')[0]


# Lua outcomes that an add-on reports as the given status.
SAME_OUTCOME = {'match': 'match', 'no_match': 'no_match', 'not_applicable': 'no_match'}


def mb_release_xml(mbid, front=False, asin=None):
    art = '<artwork>true</artwork><front>true</front>' if front else '<artwork>false</artwork>'
    return (f'<metadata><release id="{mbid}"><cover-art-archive>{art}</cover-art-archive>'
            + (f'<asin>{asin}</asin>' if asin else '') + '</release></metadata>')


def mb_release_json(mbid, title='Album', artist='Artist', front=False, asin=None):
    value = {'id': mbid, 'title': title, 'artist-credit': [{'name': artist}], 'cover-art-archive': {'front': front}}
    if asin:
        value['asin'] = asin
    return json.dumps(value)


TRACK = {'artist': 'Artist', 'album': 'Album', 'title': 'Song'}
CASES = {
    'musicbrainz': ('00_musicbrainz.lua', [
        ('release id, Cover Art Archive', {**TRACK, 'MB_ALBUMID': R1},
         [(f'release/{R1}', mb_release_xml(R1, front=True))],
         [(f'release/{R1}', mb_release_json(R1, front=True))], ('caa', R1)),
        ('release id, Amazon only', {**TRACK, 'MB_ALBUMID': R1},
         [(f'release/{R1}', mb_release_xml(R1, asin='B000001FI7'))],
         [(f'release/{R1}', mb_release_json(R1, asin='B000001FI7'))], ('asin', 'B000001FI7')),
        ('artist and album search', TRACK,
         [('release/?query=', f'<metadata><release-list count="1"><release id="{R2}"></release></release-list></metadata>'),
          (f'release/{R2}', mb_release_xml(R2, front=True))],
         [('release/?query=', json.dumps({'releases': [{'id': R2, 'title': 'Album', 'artist-credit': [{'name': 'Artist'}]}]})),
          (f'release/{R2}', mb_release_json(R2, front=True))], ('caa', R2)),
        ('artist and title search', {'artist': 'Artist', 'title': 'Song'},
         [('recording/?query=', f'<metadata><recording-list count="1"><recording id="r"><release-list><release id="{R2}"></release></release-list></recording></recording-list></metadata>'),
          (f'release/{R2}', mb_release_xml(R2, front=True))],
         [('recording/?query=', json.dumps({'recordings': [{'title': 'Song', 'artist-credit': [{'name': 'Artist'}], 'releases': [{'id': R2}]}]})),
          (f'release/{R2}', mb_release_json(R2, title='Other', front=True))], ('caa', R2)),
        ('search without results', {'artist': 'Artist', 'album': 'Album'},
         [('release/?query=', '<metadata><release-list count="0"/></metadata>')],
         [('release/?query=', json.dumps({'releases': []}))], None),
        ('radio listing', {**TRACK, 'Listing Type': 'radio'}, [], [], None),
    ]),
    'lastfm': ('03_lastfm.lua', [
        ('album page', TRACK,
         [('last.fm/music/Artist/Album', '<html><meta property="og:image" content="https://lastfm.freetls.fastly.net/i/u/300x300/cover.jpg" /></html>')],
         [('last.fm/music/Artist/Album', '<html><meta property="og:image" content="https://lastfm.freetls.fastly.net/i/u/300x300/cover.jpg" /></html>')],
         ('url', 'https://lastfm.freetls.fastly.net/i/u/300x300/cover.jpg')),
        ('default image only', TRACK,
         [('last.fm/music/', '<html><meta property="og:image" content="https://lastfm.freetls.fastly.net/i/u/300x300/default_album_mega.png" /></html>')],
         [('last.fm/music/', '<html><meta property="og:image" content="https://lastfm.freetls.fastly.net/i/u/300x300/default_album_mega.png" /></html>')], None),
        ('no album', {'artist': 'Artist', 'title': 'Song'}, [], [], None),
        ('tv listing', {**TRACK, 'Listing Type': 'tv'}, [], [], None),
    ]),
    'google-images': ('01_googleimage.lua', [
        ('album cover', TRACK,
         [('images?q=', '<img height="100" src="https://encrypted-tbn0.gstatic.com/images?q=tbn:one"><img height="100" src="https://encrypted-tbn1.gstatic.com/images?q=tbn:two">')],
         [('images?q=', '<img height="100" src="https://encrypted-tbn0.gstatic.com/images?q=tbn:one"><img height="100" src="https://encrypted-tbn1.gstatic.com/images?q=tbn:two">')],
         ('url', 'https://encrypted-tbn0.gstatic.com/images?q=tbn:one')),
        ('tv logo', {'title': 'Channel', 'Listing Type': 'tv'},
         [('images?q=', '<img height="80" src="https://encrypted-tbn2.gstatic.com/images?q=tbn:logo">')],
         [('images?q=', '<img height="80" src="https://encrypted-tbn2.gstatic.com/images?q=tbn:logo">')],
         ('url', 'https://encrypted-tbn2.gstatic.com/images?q=tbn:logo')),
        ('artist and title', {'artist': 'Artist', 'title': 'Song'},
         [('images?q=', '<html><head><title>Google Images</title></head><body>Your search did not match any image results.</body></html>')],
         [('images?q=', '<html><head><title>Google Images</title></head><body>Your search did not match any image results.</body></html>')], None),
        ('nothing to search', {'album': 'Album'}, [], [], None),
    ]),
}

# Each known difference: (family, case, meta, pages for both, Lua result,
# add-on result). A result is (artwork, outcome, end of the last request).
DIFFERENCES = [
    # The old script cannot tell an empty page from a changed page, so it
    # reports the service as unavailable. The add-on reports no match.
    ('google-images', 'script-only page', TRACK, [('images?q=', '<html><script>enablejs()</script></html>')],
     ('', 'unavailable', None), ('', 'no_match', None)),
    ('lastfm', 'page without an image', TRACK, [('last.fm/music/', '<html><title>Album</title></html>')],
     ('', 'unavailable', None), ('', 'no_match', None)),
    # The old script cleans the album name after it encodes it, so its
    # bracket rule never matches. The add-on cleans first.
    ('lastfm', 'album with edition and disc', {'artist': 'Artist', 'album': 'Album (Deluxe) CD2'}, [],
     ('', 'unavailable', '/Artist/Album%20%28Deluxe%29%20'), ('', 'no_match', '/Artist/Album')),
]


class ArtworkParity(unittest.TestCase):
    def compare(self, family, script, name, meta, lua_pages, addon_pages, expected):
        lua = run_lua(SOURCE / 'share/lua/meta/art' / script, meta, lua_pages)
        addon = run_addon(family, meta, addon_pages)
        context = f'{family}: {name}: Lua {lua}, add-on {addon}'
        self.assertEqual(image(lua['artwork']), expected, context)
        self.assertEqual(image(addon['artwork']), expected, context)
        self.assertEqual(SAME_OUTCOME.get(lua['outcome']), addon['outcome'], context)
        # The same searches, in the same order. Hosts and formats differ:
        # the add-ons use HTTPS and the current service addresses.
        self.assertEqual([query(u) for u in lua['requests']], [query(u) for u in addon['requests']], context)
        for url in addon['requests']:
            self.assertTrue(url.startswith('https://'), context)

    def test_cases(self):
        for family, (script, cases) in CASES.items():
            for name, meta, lua_pages, addon_pages, expected in cases:
                with self.subTest(family=family, case=name):
                    self.compare(family, script, name, meta, lua_pages, addon_pages, expected)

    def test_french_tv_channels(self):
        channels = json.loads((ROOT / 'sdk/artwork-providers/src/french-tv.json').read_text())
        script = SOURCE / 'share/lua/meta/art/02_frenchtv.lua'
        repaired = []
        for channel, url in channels.items():
            if run_lua(script, {'title': channel}, [])['artwork'] != url:
                repaired.append(channel)
            for title in (channel, channel + ' HD', channel + ' (bas débit)'):
                with self.subTest(channel=title):
                    lua = run_lua(script, {'title': title}, [])
                    addon = run_addon('french-tv', {'title': title}, [])
                    self.assertEqual((lua['outcome'], addon['outcome']), ('match', 'match'))
                    self.assertEqual(addon['artwork'], url)
                    self.assertEqual(lua['requests'] + addon['requests'], [])
        # The add-on replaces image addresses that no longer answer.
        self.assertEqual(repaired, ['France Info'])
        for title in ('Nope', ''):
            with self.subTest(channel=title):
                lua = run_lua(script, {'title': title} if title else {}, [])
                addon = run_addon('french-tv', {'title': title} if title else {}, [])
                self.assertEqual(lua['artwork'] + addon['artwork'], '')
                self.assertEqual(SAME_OUTCOME.get(lua['outcome']), addon['outcome'])

    def test_known_differences(self):
        scripts = {family: script for family, (script, _) in CASES.items()}
        for family, name, meta, pages, lua_expected, addon_expected in DIFFERENCES:
            with self.subTest(family=family, case=name):
                lua = run_lua(SOURCE / 'share/lua/meta/art' / scripts[family], meta, pages)
                addon = run_addon(family, meta, pages)
                for result, (artwork, outcome, request) in ((lua, lua_expected), (addon, addon_expected)):
                    self.assertEqual((result['artwork'], result['outcome']), (artwork, outcome), result)
                    if request:
                        self.assertTrue(result['requests'][-1].endswith(request), result)


def lua_interpreter(path):
    for candidate in ([str(path)] if path else []) + ['lua5.4', 'lua']:
        found = shutil.which(candidate)
        if found and '5.4' in subprocess.run([found, '-v'], capture_output=True, text=True).stdout:
            return Path(found)
    return None


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--worker', required=True, type=Path)
    parser.add_argument('--lua', type=Path, help='a Lua 5.4 interpreter')
    args, remaining = parser.parse_known_args()
    WORKER = args.worker.resolve()
    LUA = lua_interpreter(args.lua)
    if LUA is None:
        print('No Lua 5.4 interpreter: skipped.')
        sys.exit(77)
    unittest.main(argv=[__file__, *remaining])
