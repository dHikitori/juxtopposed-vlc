#
# Preparser serializer modules
#

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
