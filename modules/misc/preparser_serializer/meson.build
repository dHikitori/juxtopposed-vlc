#
# Preparser serializer modules
#

# Without this module the external preparser cannot exchange requests with
# its child process, and nothing is preparsed. Its JSON parser needs Flex and
# Bison 3.0 or later: fail now rather than build a player that cannot parse.
if cdata.has('HAVE_VLC_EXTERNAL_PREPARSER') and not (flex.found() and bison.found())
    error('The external preparser needs Flex and Bison 3.0 or later for its JSON serializer. ' +
          'On macOS, put Homebrew\'s bison and flex first on PATH.')
endif

vlc_modules += {
    'name' : 'preparserserializer_json',
    'sources' : files(
        'json/serializer.c',
        'json/serializer.h',
        'json/native_facts_tojson.h',
        'json/native_facts_fromjson.h',
        'json/fromjson.c',
        'json/tojson.c',
    ),
    'link_with' : [vlc_json_lib],
    'plugin_dir' : 'misc',
}
