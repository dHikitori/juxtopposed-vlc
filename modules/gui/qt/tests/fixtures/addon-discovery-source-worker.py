#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
import json
import pathlib
import sys

def output(value):
    print(json.dumps(value), flush=True)

output({'type': 'ready', 'protocol': 'wasm-message-v1'})
for line in sys.stdin:
    request = json.loads(line)
    if request.get('type') != 'invoke':
        continue
    if request['operation'] == 'resolve':
        with pathlib.Path(sys.argv[0] + '.resolves').open('a') as log:
            log.write(request['context'].get('itemRef', '') + '\n')
        data = {'url': 'https://media.example.test/audio.wav', 'format': 'progressive'}
    else:
        data = {'items': [{'id': 'station', 'kind': 'media', 'title': 'Station', 'mediaKind': 'musicTrack'}]}
    output({'type': 'messages', 'messages': [{'type': 'result', 'id': request['id'], 'status': 'match', 'data': data}]})
