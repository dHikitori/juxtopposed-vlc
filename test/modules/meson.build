if host_system == 'linux'
    vlc_tests += {
        'name': 'test_modules_services_discovery_sap',
        'sources': files('services_discovery/sap.c'),
        'suite': ['modules', 'test_modules'],
        'link_with': [libvlc, libvlccore],
        'module_depends': ['sap'],
        'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
    }
endif

vlc_tests += {
    'name': 'test_modules_playlist_m3u',
    'sources': files('demux/playlist/m3u.c'),
    'suite': ['modules', 'test_modules'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['playlist'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}

vlc_tests += {
    'name': 'test_modules_demux_mkv_subtitle_cluster',
    'sources': files('demux/mkv_subtitle_cluster.cpp', '../src/input/demux-run.c',
                     '../src/input/common.c', '../../src/input/var.c'),
    'suite': ['modules', 'test_modules'],
    'module_depends': ['mkv'],
    'link_with': [libvlc, libvlccore],
}

vlc_tests += {
    'name': 'test_modules_demux_mkv_subtitle_seek',
    'sources': files('demux/mkv_subtitle_seek.cpp'),
    'suite': ['modules', 'test_modules'],
}

vlc_tests += {
    'name': 'test_modules_stream_out_renderer_quality',
    'sources': files('stream_out/renderer_quality.cpp'),
    'suite': ['modules', 'test_modules'],
    'link_with': [libvlc, libvlccore],
}

vlc_tests += {
    'name': 'test_modules_text_renderer_freetype',
    'sources': files('text_renderer/freetype.c'),
    'suite': ['modules', 'test_modules'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['freetype'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}

vlc_tests += {
    'name': 'test_modules_video_filter_puzzle',
    'sources': files('video_filter/puzzle.c', '../../modules/video_filter/puzzle/puzzle_lib.c'),
    'suite': ['modules', 'test_modules'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_modules_visualization_resize',
    'sources': files('visualization/resize.c'),
    'suite': ['modules', 'test_modules'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['visual', 'demux_mock', 'araw', 'adummy'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}

if opengl_dep.found() and (host_system == 'windows' or egl_dep.found())
    vlc_tests += {
        'name': 'test_modules_visualization_gl_resize',
        'sources': files('visualization/resize.c'),
        'suite': ['modules', 'test_modules'],
        'link_with': [libvlc, libvlccore],
        'c_args': ['-DVISUAL_RESIZE_TEST_GL'],
        'module_depends': vlc_plugins_targets.keys(),
        'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
    }
    vlc_tests += {
        'name': 'test_modules_visualization_projectm_resize',
        'sources': files('visualization/resize.c'),
        'suite': ['modules', 'test_modules'],
        'link_with': [libvlc, libvlccore],
        'c_args': ['-DVISUAL_RESIZE_TEST_GL', '-DVISUAL_RESIZE_TEST_PROJECTM'],
        'module_depends': vlc_plugins_targets.keys(),
        'enabled': get_option('projectm').allowed(),
        'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
    }
endif


vlc_tests += {
    'name' : 'test_modules_lua_extension',
    'sources' : files('lua/extension.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_lua_playlist_parser',
    'sources' : files('lua/playlist_parser.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['lua'],
}

vlc_tests += {
    'name' : 'test_modules_lua_musicbrainz',
    'sources' : files('lua/musicbrainz.c'),
    'suite' : ['modules', 'test_modules'],
    'dependencies' : [lua_dep],
    'enabled' : lua_dep.found(),
}

vlc_tests += {
    'name': 'test_modules_misc_media_tracks',
    'sources': files('misc/media_tracks.c'),
    'suite': ['modules', 'test_modules'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['medialibrary', 'wav', 'araw', 'adummy', 'subtitle', 'subsdec'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
    'enabled': medialibrary_dep.found(),
}

if not (host_system == 'windows') # missing mkdtemp()
vlc_tests += {
    'name' : 'test_modules_misc_medialibrary',
    'sources' : files('misc/medialibrary.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys(),
    'enabled' : medialibrary_dep.found(),
    #'module_depends' : ['medialibrary', 'demux_mock', 'jpeg', 'png', 'rawvid'],
}
endif

vlc_tests += {
    'name' : 'test_modules_packetizer_helpers',
    'sources' : files('packetizer/helpers.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_packetizer_hxxx',
    'sources' : files('packetizer/hxxx.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_packetizer_copy',
    'sources' : files('packetizer/copy.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys(),
}

vlc_tests += {
    'name' : 'test_modules_packetizer_h264',
    'sources' : files('packetizer/h264.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_packetizer_hevc',
    'sources' : files('packetizer/hevc.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_packetizer_h266',
    'sources' : files('packetizer/h266.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_packetizer_h266_nal',
    'sources' : files(
        'packetizer/h266_nal.c',
        '../../modules/packetizer/h266_nal.c'
      ),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_packetizer_mpegvideo',
    'sources' : files('packetizer/mpegvideo.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_keystore',
    'sources' : files('keystore/test.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

if not(host_system == 'windows')
vlc_tests += {
    'name' : 'test_modules_tls',
    'sources' : files('misc/tls.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}
endif

vlc_tests += {
    'name' : 'test_modules_demux_json',
    'sources' : files('demux/json.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [vlc_json_lib, libvlccore],
}

vlc_tests += {
    'name' : 'test_modules_demux_libmp4',
    'sources' : files(
        'demux/libmp4.c',
        '../../modules/demux/mp4/libmp4.c'),
    'suite' : ['modules', 'test_modules'],
    'dependencies' : [z_dep],
    'link_with' : [libvlc, libvlccore],
}

vlc_tests += {
    'name' : 'test_modules_demux_timestamps',
    'sources' : files('demux/timestamps.c'),
    'suite' : ['modules', 'test_modules']
}

vlc_tests += {
    'name' : 'test_modules_demux_timestamps_filter',
    'sources' : files('demux/timestamps_filter.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_ts_pes',
    'sources' : files(
        'demux/ts_pes.c',
        '../../modules/demux/mpeg/ts_pes.c',
        '../../modules/demux/mpeg/ts_pes.h'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_codec_hxxx_helper',
    'sources' : files('codec/hxxx_helper.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore, hxxxhelper_lib],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_codec_cea708_aspect_ratio',
    'sources' : files('codec/cea708_aspect_ratio.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_codec_cea708_integration',
    'sources' : files('codec/cea708_integration.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_codec_cea708',
    'sources' : files('codec/cea708.c',
                      '../../modules/codec/cea708.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

if host_system == 'linux' and opengl_dep.found()
    vlc_tests += {
        'name': 'test_modules_video_output_opengl_display',
        'sources': files('video_output/opengl/display.c'),
        'suite': ['modules', 'test_modules'],
        'link_with': [libvlccore],
        'c_args': ['-ffunction-sections', '-fdata-sections', '-Werror=incompatible-pointer-types'],
        'link_args': ['-Wl,--gc-sections'],
    }
endif

if opengl_dep.found() and (host_system == 'windows' or egl_dep.found())
    foreach api, api_flag : {'opengl': 'VLC_OPENGL', 'opengl_es2': 'VLC_OPENGL_ES2'}
        if api == 'opengl' or host_system != 'windows'
            vlc_tests += {
                'name': 'test_modules_video_output_' + api + '_offscreen',
                'sources': files('video_output/opengl/offscreen.c'),
                'suite': ['modules', 'test_modules'],
                'link_with': [libvlc, libvlccore, libvlc_opengl],
                'dependencies': host_system == 'windows' ? [] : [egl_dep],
                'c_args': ['-DVLC_TEST_OPENGL_API=' + api_flag] + (host_system == 'windows' ? [] : ['-DVLC_TEST_EXTERNAL_EGL']),
                'module_depends': host_system == 'windows' ? ['opengl_win_offscreen'] : ['egl_pbuffer_filter', 'egl_display_generic'],
            }
        endif
    endforeach
endif

if host_system == 'linux' and opengl_dep.found() and egl_dep.found()
    foreach api, api_flag : {'opengl': 'VLC_OPENGL', 'opengl_es2': 'VLC_OPENGL_ES2'}
        vlc_tests += {
            'name': 'test_modules_video_output_' + api + '_backdrop',
            'enabled': api == 'opengl' or cc.has_header('GLES2/gl2.h'),
            'sources': files('video_output/opengl/backdrop.c', '../../modules/video_output/opengl/backdrop.c'),
            'suite': ['modules', 'test_modules'],
            'link_with': [libvlc, libvlccore, libvlc_opengl],
            'dependencies': [egl_dep, m_lib],
            'c_args': ['-DVLC_TEST_OPENGL_API=' + api_flag] + (api == 'opengl_es2' ? ['-DUSE_OPENGL_ES2'] : []),
        }
    endforeach
endif

vlc_tests += {
    'name' : 'test_modules_video_output_opengl_filters',
    'sources' : files(
        'video_output/opengl/filters.c',
        '../../modules/video_output/opengl/filters.c',
        '../../modules/video_output/opengl/filters.h',
        '../../modules/video_output/opengl/filter.c',
        '../../modules/video_output/opengl/gl_api.c',
        '../../modules/video_output/opengl/gl_api.h',
        '../../modules/video_output/opengl/interop.c',
        '../../modules/video_output/opengl/interop.h',
        '../../modules/video_output/opengl/importer.c',
        '../../modules/video_output/opengl/importer.h'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore, libvlc_opengl],
    'c_args' : ['-DVLC_TEST_OPENGL_API=VLC_OPENGL'],
    'module_depends' : vlc_plugins_targets.keys(),
    'enabled' : opengl_dep.found(),
}

vlc_tests += {
    'name' : 'test_modules_video_output_opengl_es2_filters',
    'sources' : files(
        'video_output/opengl/filters.c',
        '../../modules/video_output/opengl/filters.c',
        '../../modules/video_output/opengl/filters.h',
        '../../modules/video_output/opengl/filter.c',
        '../../modules/video_output/opengl/gl_api.c',
        '../../modules/video_output/opengl/gl_api.h',
        '../../modules/video_output/opengl/interop.c',
        '../../modules/video_output/opengl/interop.h',
        '../../modules/video_output/opengl/importer.c',
        '../../modules/video_output/opengl/importer.h'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore, libvlc_opengles],
    'c_args' : ['-DVLC_TEST_OPENGL_API=VLC_OPENGL_ES2'],
    'module_depends' : vlc_plugins_targets.keys(),
    'enabled' : opengles2_dep.found(),
}

vlc_tests += {
    'name' : 'test_modules_stream_out_transcode',
    'sources' : files(
        'stream_out/transcode.c',
        'stream_out/transcode.h',
        'stream_out/transcode_scenarios.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_stream_out_audio_tail',
    'sources' : files('stream_out/audio_tail.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys(),
}

vlc_tests += {
    'name' : 'test_modules_stream_out_sdi',
    'sources' : files(
        'stream_out/sdi.cpp',
        '../../modules/stream_out/sdi/SDIStream.cpp',
        '../../modules/stream_out/sdi/SDIStream.hpp'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore]
}

vlc_tests += {
    'name' : 'test_modules_stream_out_pcr_sync',
    'sources' : files(
        'stream_out/pcr_sync.c',
        '../../modules/stream_out/transcode/pcr_sync.c',
        '../../modules/stream_out/transcode/pcr_sync.h',
        '../../modules/stream_out/transcode/pcr_helper.c',
        '../../modules/stream_out/transcode/pcr_helper.h'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

vlc_tests += {
    'name' : 'test_modules_mux_webvtt',
    'sources' : files('mux/webvtt.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : vlc_plugins_targets.keys()
}

if pipewire_dep.found()
    vlc_tests += {
        'name': 'test_modules_audio_output_pipewire_devices',
        'sources': files('audio_output/pipewire_devices.c'),
        'suite': ['modules', 'test_modules'],
        'dependencies': [pipewire_dep, m_lib],
        'link_with': [libvlccore, libvlc_pipewire],
        'c_args': ['-ffunction-sections', '-fdata-sections'],
        'link_args': ['-Wl,--gc-sections'],
    }
endif

if have_wayland and egl_dep.found()
    vlc_tests += {
        'name': 'test_modules_video_output_egl_runtime',
        'sources': files('video_output/egl_runtime.c'),
        'suite': ['modules', 'test_modules'],
        'dependencies': [egl_dep, wayland_deps],
        'link_with': [libvlccore],
        'c_args': ['-ffunction-sections', '-fdata-sections', '-Werror=incompatible-pointer-types'],
        'link_args': ['-Wl,--gc-sections'],
    }
    foreach variant, variant_args : {
        'modern': [],
        'ext': ['-DTEST_EGL14'],
        'no_reference_headers': ['-DTEST_NO_REFERENCE_HEADERS'],
    }
        vlc_tests += {
            'name': 'test_modules_video_output_egl_display_' + variant,
            'sources': files('video_output/egl_display.c'),
            'suite': ['modules', 'test_modules'],
            'dependencies': [egl_dep, wayland_deps],
            'link_with': [libvlccore],
            'c_args': variant_args + ['-ffunction-sections', '-fdata-sections', '-Werror=incompatible-pointer-types'],
            'link_args': ['-Wl,--gc-sections'],
        }
    endforeach
endif

vlc_tests += {
    'name': 'test_modules_video_filter_rotate',
    'sources': files('video_filter/rotate.c'),
    'suite': ['modules', 'test_modules'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['rotate'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}

if have_wayland
    vlc_tests += {
        'name': 'test_modules_video_output_wayland_shm_backdrop',
        'sources': files('video_output/wayland/shm_backdrop.c', '../../modules/video_output/wayland/shm_backdrop.c'),
        'suite': ['modules', 'test_modules'],
        'link_with': [libvlccore],
        'dependencies': [m_lib],
    }
endif

if libplacebo_dep.found()
    vlc_tests += {
        'name': 'test_modules_video_output_libplacebo_plane',
        'sources': files('video_output/libplacebo/plane.c'),
        'suite': ['modules', 'test_modules'],
        'link_with': [libvlccore, libplacebo_utils],
        'dependencies': [libplacebo_dep, m_lib],
    }
endif

if libplacebo_dep.found() and vulkan_dep.found()
    vlc_tests += {
        'name': 'test_modules_video_output_libplacebo_backdrop',
        'sources': files('video_output/libplacebo/backdrop.c', '../../modules/video_output/libplacebo/backdrop.c'),
        'suite': ['modules', 'test_modules'],
        'dependencies': [libplacebo_dep, m_lib],
    }
endif

vlc_tests += {
    'name': 'test_modules_audio_filter_gain',
    'sources': files('audio_filter/gain.c'),
    'suite': ['modules', 'test_modules'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['gain', 'float_mixer', 'integer_mixer'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}

vlc_tests += {
    'name': 'test_modules_mux_mp4_drain',
    'sources': files('mux/mp4_drain.c'),
    'suite': ['modules', 'test_modules'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['mux_mp4'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}

vlc_tests += {
    'name': 'test_modules_video_filter_sepia',
    'sources': files('video_filter/sepia.c'),
    'suite': ['modules', 'test_modules'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['sepia'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}

vlc_tests += {
    'name' : 'test_modules_video_filter_blend',
    'sources' : files('video_filter/blend.c'),
    'suite' : ['modules', 'test_modules'],
    'link_with' : [libvlc, libvlccore],
    'module_depends' : ['blend']
}

vlc_tests += {
    'name': 'test_modules_lua_meta_selector',
    'sources': files('lua/meta_selector.c'),
    'suite': ['modules', 'test_modules', 'addons'],
    'link_with': [libvlccore],
}

vlc_tests += {
    'name': 'test_modules_lua_meta_selector_runtime',
    'sources': files('lua/meta_selector_runtime.c'),
    'suite': ['modules', 'test_modules', 'addons'],
    'link_with': [libvlc, libvlccore],
    'module_depends': ['lua'],
    'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
}
