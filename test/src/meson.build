vlc_tests += {
    'name': 'test_src_misc_configured_proxy',
    'sources': files('misc/configured_proxy.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlc, libvlccore],
}

vlc_tests += {
    'name': 'test_src_misc_metadata_backend',
    'sources': files('misc/metadata_backend.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_misc_metadata_commit',
    'sources': files('misc/metadata_commit.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_misc_metadata_artwork',
    'sources': files('misc/metadata_artwork.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_misc_metadata_commit_limit',
    'sources': files('misc/metadata_commit_limit.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
    'c_args': ['-ffunction-sections', '-fdata-sections'],
    'link_args': ['-Wl,--gc-sections'],
    'enabled': host_system == 'linux',
}

vlc_tests += {
    'name': 'test_src_misc_metadata_revision',
    'sources': files('misc/metadata_revision.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_misc_addon_medialibrary',
    'sources': files('misc/addon_medialibrary.c'),
    'suite': ['src', 'test_src', 'addons'],
}

vlc_tests += {
    'name': 'test_src_misc_metadata_owner',
    'sources': files('misc/metadata_owner.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_misc_metadata_requests',
    'sources': files('misc/metadata_requests.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_input_policy',
    'sources': files('input/input_policy.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_input_ytdl_policy',
    'sources': files('input/ytdl_policy.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlc, libvlccore, vlc_json_lib],
    'enabled': host_system == 'linux',
}

vlc_tests += {
    'name': 'test_src_input_artwork_policy',
    'sources': files('input/artwork_policy.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_input_artwork_fetcher',
    'sources': files('input/artwork_fetcher.c'),
    'include_directories': include_directories('../../src'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_input_source_backend',
    'sources': files('input/source_backend.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_input_source_owner',
    'sources': files('input/source_owner.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_input_addon_source',
    'sources': files('input/addon_source.c'),
    'include_directories': include_directories('../../src'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_input_addon_source_playback',
    'sources': files('input/addon_source_playback.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlc, libvlccore],
}

vlc_tests += {
    'name': 'test_src_misc_medialibrary_work',
    'sources': files('misc/medialibrary_work.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlc, libvlccore],
}

vlc_tests += {
    'name': 'test_src_player_audio_samples_lifecycle',
    'sources': files('player/audio_samples_lifecycle.c'),
    'suite': ['src', 'test_src', 'addons'],
    'link_with': [libvlc, libvlccore],
}

vlc_tests += {
    'name': 'test_src_misc_file_logger',
    'sources': files('misc/file_logger.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['file_logger'],
}

vlc_tests += {
    'name': 'test_src_misc_log_observer',
    'sources': files('misc/log_observer.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_misc_iso_language',
    'sources' : files('misc/iso_language.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlccore],
}

if host_system == 'windows'
    if not have_win_store
        vlc_tests += {
            'name': 'test_src_misc_win32_proxy',
            'sources': files('misc/win32_proxy.c'),
            'suite': ['src', 'test_src'],
            'link_with': [libvlccore],
            'dependencies': [cc.find_library('winhttp'), cc.find_library('ws2_32')],
        }
    endif
    vlc_tests += {
        'name': 'test_src_config_dirs_win32',
        'sources': files('config/dirs_win32.c'),
        'suite': ['src', 'test_src'],
        'link_with': [libvlccore],
        'dependencies': [cc.find_library('shell32'), cc.find_library('ole32'),
                         cc.find_library('advapi32'), cc.find_library('uuid')],
    }
    vlc_tests += {
        'name' : 'test_src_misc_win32_pipe',
        'sources' : files('misc/win32_pipe.c'),
        'suite' : ['src', 'test_src'],
        'link_with' : [libvlccore],
    }
endif

vlc_tests += {
    'name': 'test_src_config_language',
    'sources': files('config/language.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlc, libvlccore],
}

vlc_tests += {
    'name': 'test_src_config_startup',
    'sources': files('config/startup.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_config_chain',
    'sources' : files('config/chain.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlccore],
}

vlc_tests += {
    'name' : 'test_src_misc_ancillary',
    'sources' : files('misc/ancillary.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_misc_bits',
    'sources' : files('misc/bits.c'),
    'suite' : ['src', 'test_src'],
}

vlc_tests += {
    'name' : 'test_src_misc_viewpoint',
    'sources' : files('misc/viewpoint.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlccore],
}

vlc_tests += {
    'name' : 'test_src_clock_clock',
    'sources' : files(
        'clock/clock.c',
        '../../src/clock/clock.c',
        '../../src/clock/clock_internal.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_clock_start',
    'sources' : files(
        'clock/clock_start.c',
        '../../src/clock/clock.c',
        '../../src/clock/clock_internal.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_misc_actions',
    'sources' : files('misc/actions.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_misc_instance_policy',
    'sources' : files('misc/instance_policy.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_misc_instance_forward',
    'sources' : files('misc/instance_forward.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : dbus_dep.found() ? ['dbus'] : [],
}

addons_test_depends = []
addons_test_args = []
addons_test_env = environment()
if host_system == 'windows'
    addons_test_plugin = shared_module('test_addons_plugin',
        files('misc/addons_plugin.c'),
        name_prefix: 'lib',
        c_args: ['-DVLC_DYNAMIC_PLUGIN', '-DMODULE_STRING="test_misc_addons"'],
        include_directories: [vlc_include_dirs],
        dependencies: [libvlccore_dep],
        build_by_default: false,
        install: false)
    addons_test_depends += addons_test_plugin
    addons_test_args += '-DADDONS_TEST_DYNAMIC_FIXTURE'
    addons_test_env.set('VLC_TEST_PLUGIN_PATH', meson.current_build_dir())
endif

vlc_tests += {
    'name' : 'test_src_misc_addons',
    'sources' : files('misc/addons.c'),
    'depends' : addons_test_depends,
    'c_args' : addons_test_args,
    'env' : addons_test_env,
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_audio_output_dec',
    'sources' : files(
        'audio_output/dec.c',
        '../../src/audio_output/dec.c',
        '../../src/audio_output/output.c',
        '../../src/audio_output/common.c',
        '../../src/audio_output/filters.c',
        '../../src/audio_output/volume.c',
        '../../src/audio_output/meter.c',
        '../../src/input/replay_gain.c',
        '../../src/misc/objects.c',
        '../../src/misc/variables.c',
        '../../src/clock/clock.c',
        '../../src/clock/clock_internal.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'include_directories' : include_directories('../../src'),
}

vlc_tests += {
    'name' : 'test_src_misc_variables',
    'sources' : files('misc/variables.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

if gcrypt_dep.found() and get_option('update-check').allowed()
    vlc_tests += {
        'name' : 'test_src_crypto_update',
        'sources' : files('crypto/update.c'),
        'suite' : ['src', 'test_src'],
        'link_with' : [libvlccore],
        'dependencies' : [gcrypt_dep],
        'module_depends' : vlc_plugins_targets.keys()
    }
endif

if not(host_system == 'windows')
vlc_tests += {
    'name' : 'test_src_input_stream',
    'sources' : files('input/stream.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_input_stream_net',
    'sources' : files('input/stream.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'c_args' : ['-DTEST_NET'],
    'module_depends' : vlc_plugins_targets.keys()
}
endif

vlc_tests += {
    'name' : 'test_src_input_stream_fifo',
    'sources' : files('input/stream_fifo.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_preparser_serializer',
    'sources' : files('preparser/serializer.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['preparserserializer_json'],
}

vlc_tests += {
    'name' : 'test_src_preparser_folder_options',
    'sources' : files('preparser/folder_options.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['filesystem', 'preparserserializer_json'],
}

vlc_tests += {
    'name' : 'test_src_preparser_subtree_identity',
    'sources' : files('preparser/subtree_identity.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['demux_mock'],
}

vlc_tests += {
    'name' : 'test_src_preparser_fetcher_shutdown',
    'sources' : files('preparser/fetcher_shutdown.c',
        '../../src/preparser/art.c', '../../src/misc/interrupt.c',
        '../../src/misc/objects.c', '../../src/misc/variables.c',
        '../../src/input/item.c', '../../src/input/policy.c', '../../src/text/strings.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'include_directories' : include_directories('../../src'),
}

vlc_tests += {
    'name' : 'test_src_preparser_artwork_fallback',
    'sources' : files('preparser/artwork_fallback.c'),
    'c_args' : ['-DHAVE_DYNAMIC_PLUGINS'],
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_preparser_thumbnail',
    'sources' : files('preparser/thumbnail.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['demux_mock', 'rawvideo']
}

vlc_tests += {
    'name' : 'test_src_preparser_thumbnail_to_files',
    'sources' : files('preparser/thumbnail_to_files.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['demux_mock', 'rawvideo']
}

vlc_tests += {
    'name' : 'test_src_player_abloop',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/timers.h',
        'player/abloop.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_attachments',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/attachments.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_capabilities',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/capabilities.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_discontinuities',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/discontinuities.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_eof',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/eof.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_errors',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/errors.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_es_selection',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/es_selection.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_delays',
    'sources' : files('player/common.h', 'player/modules.c', 'player/delays.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_playlist_preparse_depth',
    'sources' : files('playlist/preparse_depth.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['demux_mock'],
}

vlc_tests += {
    'name' : 'test_src_playlist_export',
    'sources' : files('playlist/export.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlccore, libvlc],
    'module_depends' : ['export'],
}

vlc_tests += {
    'name' : 'test_src_input_item_update',
    'sources' : files('input/item_update.c'),
    'suite' : ['test_src'],
    'link_with' : [libvlccore, libvlc],
}

vlc_tests += {
    'name' : 'test_src_input_subtitle_ass_text',
    'sources' : files('input/subtitle_ass_text.c'),
    'suite' : ['src', 'test_src'],
}

vlc_tests += {
    'name' : 'test_src_input_overlay_opacity',
    'sources' : files('input/overlay_opacity.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['marq']
}

vlc_tests += {
    'name' : 'test_src_input_subtitle_encoding',
    'sources' : files('input/subtitle_encoding.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['subsdec']
}

vlc_tests += {
    'name' : 'test_src_input_subtitle_duration',
    'sources' : files('input/subtitle_duration.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['subsdelay']
}

vlc_tests += {
    'name' : 'test_src_input_subtitle_style',
    'sources' : files('input/subtitle_style.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['freetype']
}

vlc_tests += {
    'name' : 'test_src_misc_media_library_startup',
    'sources' : files('misc/media_library_startup.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['medialibrary'],
}

vlc_tests += {
    'name' : 'test_src_player_lifecycle',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/lifecycle.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_loudness',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/loudness.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_media',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/media.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_next_prev',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/next_prev.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_video_filters',
    'sources' : files('player/common.h', 'player/modules.c', 'player/video_filters.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'c_args' : ['-DVIDEO_FILTER_TEST'],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_outputs',
    'c_args' : ['-DOUTPUT_TEARDOWN_TEST'],
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/outputs.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_pause',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/pause.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_programs',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/programs.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_seeks',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/seeks.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_teletext',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/teletext.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'c_args' : use_zvbi ? ['-DZVBI_COMPILED=1'] : [],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_timers',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/timers.h',
        'player/timers.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_titles',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/titles.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_tracks',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/tracks.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_player_tracks_ids',
    'sources' : files(
        'player/common.h',
        'player/modules.c',
        'player/tracks_ids.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_video_output',
    'sources' : files(
        'video_output/video_output.c',
        'video_output/video_output.h',
        'video_output/video_output_scenarios.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name': 'test_src_video_output_subtitle_snapshot',
    'sources': files('video_output/subtitle_snapshot.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['demux_mock', 'rawvideo', 'blend', 'tdummy', 'swscale', 'yuvp'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}

vlc_tests += {
    'name': 'test_src_video_output_subtitle_area',
    'sources': files('video_output/subtitle_area.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_video_output_filter_aspect',
    'sources': files('video_output/filter_aspect.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['demux_mock', 'rawvideo', 'transform', 'orient', 'chain', 'swscale'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}

vlc_tests += {
    'name': 'test_src_video_output_filter_format',
    'sources': files('video_output/filter_format.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['demux_mock', 'rawvideo'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}

vlc_tests += {
    'name' : 'test_src_video_output_inhibit',
    'sources' : files('video_output/inhibit.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['demux_mock']
}

vlc_tests += {
    'name': 'test_src_video_output_window_backdrop',
    'sources': files('video_output/window_backdrop.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_video_output_opengl',
    'sources' : files('video_output/opengl.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_input_decoder',
    'sources' : files(
        'input/decoder/input_decoder.c',
        'input/decoder/input_decoder.h',
        'input/decoder/input_decoder_scenarios.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_src_misc_image',
    'sources' : files('misc/image.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_misc_epg',
    'sources' : files('misc/epg.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_misc_keystore',
    'sources' : files('misc/keystore.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['memory_keystore', 'file_keystore'],
}

if host_system == 'darwin'
    vlc_tests += {
        'name' : 'test_src_misc_image_cvpx',
        'sources' : files('misc/image_cvpx.c'),
        'suite' : ['src', 'test_src'],
        'link_with' : [libvlc, libvlccore, libvlc_vtutils],
    }
endif

vlc_tests += {
    'name' : 'test_src_interface_dialog',
    'sources' : files('interface/dialog.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_media_source',
    'sources' : files('media_source/media_source.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_video_output_spu',
    'sources' : files('video_output/spu.c'),
    'suite' : ['src', 'test_src'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_src_video_output_osd',
    'sources' : files('video_output/osd.c',
                      '../../src/video_output/video_text.c',
                      '../../src/video_output/video_widgets.c'),
    'suite' : ['src', 'test_src'],
    'c_args' : ['-Dvout_PutSubpicture=CaptureSubpicture'],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name': 'test_src_playlist',
    'sources': files(
        '../../src/playlist/test.c',
        '../../src/playlist/content.c',
        '../../src/playlist/control.c',
        '../../src/playlist/item.c',
        '../../src/playlist/notify.c',
        '../../src/playlist/player.c',
        '../../src/playlist/playlist.c',
        '../../src/playlist/preparse.c',
        '../../src/playlist/randomizer.c',
        '../../src/playlist/request.c',
        '../../src/playlist/shuffle.c',
        '../../src/playlist/sort.c'),
    'c_args': ['-DTEST_PLAYLIST'],
    'include_directories': include_directories('../../src'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_player_timer_seek_length',
    'sources': files('player/timer_seek_length.c', '../../src/player/timer.c'),
    'include_directories': include_directories('../../src'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_network_http_rate',
    'sources': files('network/http_rate.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_src_network_http_auth',
    'sources': files('network/http_auth.c'),
    'suite': ['src', 'test_src'],
    'link_with': [libvlc, libvlccore],
}
