###############################################################################
# Automake targets and declarations
###############################################################################

AUTOMAKE_OPTIONS = subdir-objects

###############################################################################
# Unit/regression test
###############################################################################
player_programs = \
	test_src_player_abloop \
	test_src_player_attachments \
	test_src_player_capabilities \
	test_src_player_delays \
	test_src_player_discontinuities \
	test_src_player_eof \
	test_src_player_errors \
	test_src_player_es_selection \
	test_src_player_lifecycle \
	test_src_player_loudness \
	test_src_player_media \
	test_src_player_next_prev \
	test_src_player_outputs \
	test_src_player_video_filters \
	test_src_player_pause \
	test_src_player_programs \
	test_src_player_seeks \
	test_src_player_teletext \
	test_src_player_timers \
	test_src_player_timer_seek_length \
	test_src_player_titles \
	test_src_player_tracks \
	test_src_player_tracks_ids

noinst_PROGRAMS =
check_PROGRAMS = \
    test_src_misc_configured_proxy \
    test_src_misc_metadata_backend \
    test_src_misc_metadata_commit \
    test_src_misc_metadata_artwork \
    test_src_misc_metadata_revision \
    test_src_misc_metadata_owner \
    test_src_misc_metadata_requests \
    test_src_misc_addon_medialibrary \
    test_src_input_policy \
    test_src_input_artwork_policy \
    test_src_input_artwork_fetcher \
    test_src_input_source_backend \
    test_src_input_addon_source \
    test_src_input_addon_source_playback \
    test_src_misc_medialibrary_work \
    test_src_player_audio_samples_lifecycle \
    test_src_input_source_owner \
    test_src_misc_media_library_startup \
	test_src_misc_iso_language \
	test_libvlc_core \
	test_libvlc_downloader \
	test_libvlc_equalizer \
	test_libvlc_media \
	test_libvlc_media_callback \
	test_libvlc_media_thumbnail_argb \
	test_libvlc_media_thumbnail_rgba \
	test_libvlc_media_thumbnail_jpeg \
	test_libvlc_media_thumbnail_png \
	test_libvlc_media_thumbnail_webp \
	test_libvlc_media_list \
	test_libvlc_media_list_player \
	test_libvlc_media_player \
	test_libvlc_video_callback \
	test_libvlc_media_player_record \
	test_libvlc_media_discoverer \
	test_libvlc_renderer_discoverer \
	test_libvlc_slaves \
	test_src_audio_output_dec \
	test_src_config_chain \
	test_src_config_startup \
	test_src_config_language \
	test_src_clock_clock \
	test_src_clock_start \
	test_src_misc_ancillary \
	test_src_misc_log_observer \
	test_src_misc_file_logger \
	test_src_misc_variables \
	test_src_misc_actions \
	test_src_input_stream \
	test_src_input_stream_fifo \
	test_src_preparser_serializer \
	test_src_preparser_cmp_internal_external \
	test_src_preparser_subtree_identity \
	test_src_preparser_folder_options \
	test_src_playlist_preparse_depth \
	test_src_preparser_fetcher_shutdown \
	test_src_preparser_artwork_fallback \
	test_src_preparser_thumbnail \
	test_src_preparser_thumbnail_to_files \
	test_src_input_decoder \
	test_modules_demux_mkv_subtitle_cluster \
	test_modules_demux_mkv_subtitle_seek \
	test_src_input_subtitle_style \
	test_src_input_subtitle_duration \
	test_src_input_subtitle_encoding \
	test_src_input_overlay_opacity \
	test_src_input_subtitle_ass_text \
	test_src_input_item_update \
	test_src_playlist_export \
	test_src_interface_dialog \
	test_src_media_source \
	test_src_misc_bits \
	test_src_misc_chroma_probe \
	test_src_misc_epg \
	test_src_misc_keystore \
	test_src_misc_image \
	test_src_misc_addons \
	test_src_misc_instance_policy \
	test_src_misc_instance_forward \
	test_src_misc_viewpoint \
	test_src_video_output \
	test_src_video_output_filter_aspect \
	test_src_video_output_filter_format \
	test_src_video_output_inhibit \
	test_src_video_output_opengl \
	test_src_video_output_osd \
	test_src_video_output_spu \
	test_src_video_output_subtitle_area \
	test_src_video_output_window_backdrop \
	test_src_video_output_subtitle_snapshot \
	test_modules_lua_extension \
	test_modules_misc_medialibrary \
	test_modules_misc_media_tracks \
	test_modules_visualization_resize \
	test_modules_video_filter_puzzle \
	test_modules_video_filter_rotate \
	test_modules_video_filter_sepia \
	test_modules_audio_filter_gain \
	test_modules_packetizer_helpers \
	test_modules_packetizer_hxxx \
	test_modules_packetizer_h264 \
	test_modules_packetizer_copy \
	test_modules_packetizer_h266 \
	test_modules_packetizer_h266_nal \
	test_modules_packetizer_hevc \
	test_modules_packetizer_mpegvideo \
	test_modules_codec_hxxx_helper \
	test_modules_codec_cea708 \
	test_modules_codec_cea708_aspect_ratio \
	test_modules_codec_cea708_integration \
	test_modules_keystore \
	test_modules_demux_json \
	test_modules_demux_libmp4 \
	test_modules_demux_timestamps \
	test_modules_demux_timestamps_filter \
	test_modules_demux_ts_pes \
	test_modules_playlist_m3u \
	test_modules_stream_out_pcr_sync \
	test_modules_stream_out_renderer_quality \
	test_modules_stream_out_sdi \
	test_modules_tls \
	test_modules_stream_out_transcode \
	test_modules_stream_out_audio_tail \
	test_modules_mux_webvtt \
	test_modules_mux_mp4_drain \
	test_modules_video_filter_blend \
	test_modules_stream_out_hls_subtitles_segmenter \
	$(NULL)

check_PROGRAMS += $(player_programs)

if HAVE_FREETYPE
check_PROGRAMS += test_modules_text_renderer_freetype
endif
test_modules_text_renderer_freetype_SOURCES = modules/text_renderer/freetype.c
test_modules_text_renderer_freetype_LDADD = $(LIBVLCCORE) $(LIBVLC)

if HAVE_GL
check_PROGRAMS += \
	test_modules_visualization_gl_resize \
	test_modules_video_output_opengl_offscreen \
	test_modules_video_output_opengl_filters \
	test_modules_video_output_opengl_sub_renderer
endif

if HAVE_GLES2
check_PROGRAMS += \
	test_modules_video_output_opengl_es2_filters \
	test_modules_video_output_opengl_es2_sub_renderer
endif

if HAVE_DARWIN
check_PROGRAMS += test_src_misc_image_cvpx
endif

if BUILD_LUA
check_PROGRAMS += test_modules_lua_playlist_parser test_modules_lua_musicbrainz
endif


if UPDATE_CHECK
check_PROGRAMS += test_src_crypto_update
endif
if HAVE_TAGLIB
check_PROGRAMS += test_libvlc_meta
endif

check_SCRIPTS = \
	modules/lua/telnet.sh \
	check_POTFILES.sh

# Disabled tests:
EXTRA_PROGRAMS = \
	test_src_input_stream_net \
	$(NULL)

EXTRA_DIST = \
	bin/cachegen.py \
	modules/lua/extensions/extensions.lua \
	modules/lua/playlist/playlist.lua \
	modules/lua/musicbrainz.lua \
	samples/certs/certkey.pem \
	samples/empty.voc \
	samples/image.jpg \
	samples/meta.mp3 \
	samples/subitems \
	samples/slaves \
	$(check_SCRIPTS)

check_HEADERS = libvlc/test.h libvlc/media_utils.h libvlc/media_player.h

TESTS = $(check_PROGRAMS) check_POTFILES.sh

AM_CPPFLAGS = \
	-DSRCDIR=\"$(srcdir)\" \
	-DTOP_BUILDDIR=\"$(abs_top_builddir)\" \
	-DTOP_SRCDIR=\"$(abs_top_srcdir)\"

if HAVE_EMSCRIPTEN
AM_CFLAGS = -sMAIN_MODULE=1
AM_CXXFLAGS = -sMAIN_MODULE=1
endif

AM_LDFLAGS = -no-install $(LDFLAGS_vlc)
LIBVLCCORE = -L../src/ -lvlccore
LIBVLC = -L../lib -lvlc

if HAVE_LINUX
check_PROGRAMS += test_modules_services_discovery_sap
test_modules_services_discovery_sap_SOURCES = modules/services_discovery/sap.c
test_modules_services_discovery_sap_LDADD = $(LIBVLC) $(LIBVLCCORE)
endif

test_src_misc_configured_proxy_SOURCES = src/misc/configured_proxy.c
test_src_misc_configured_proxy_LDADD = $(LIBVLC) $(LIBVLCCORE)

check_PROGRAMS += test_modules_lua_meta_selector
test_modules_lua_meta_selector_SOURCES = modules/lua/meta_selector.c
test_modules_lua_meta_selector_LDADD = $(LIBVLCCORE)
if BUILD_LUA
check_PROGRAMS += test_modules_lua_meta_selector_runtime
test_modules_lua_meta_selector_runtime_SOURCES = modules/lua/meta_selector_runtime.c
test_modules_lua_meta_selector_runtime_LDADD = $(LIBVLC) $(LIBVLCCORE)
endif

test_src_misc_metadata_backend_SOURCES = src/misc/metadata_backend.c
test_src_misc_metadata_backend_LDADD = $(LIBVLCCORE)

test_src_misc_metadata_commit_SOURCES = src/misc/metadata_commit.c
test_src_misc_metadata_commit_LDADD = $(LIBVLCCORE)

test_src_misc_metadata_artwork_SOURCES = src/misc/metadata_artwork.c
test_src_misc_metadata_artwork_LDADD = $(LIBVLCCORE)

if HAVE_LINUX
check_PROGRAMS += test_src_misc_metadata_commit_limit
test_src_misc_metadata_commit_limit_SOURCES = src/misc/metadata_commit_limit.c
test_src_misc_metadata_commit_limit_CFLAGS = $(AM_CFLAGS) -ffunction-sections -fdata-sections
test_src_misc_metadata_commit_limit_LDFLAGS = $(AM_LDFLAGS) -Wl,--gc-sections
test_src_misc_metadata_commit_limit_LDADD = $(LIBVLCCORE)
endif

test_src_misc_metadata_revision_SOURCES = src/misc/metadata_revision.c
test_src_misc_metadata_revision_LDADD = $(LIBVLCCORE)

test_src_misc_metadata_owner_SOURCES = src/misc/metadata_owner.c
test_src_misc_metadata_owner_LDADD = $(LIBVLCCORE)

test_src_misc_metadata_requests_SOURCES = src/misc/metadata_requests.c
test_src_misc_metadata_requests_LDADD = $(LIBVLCCORE)

test_src_misc_addon_medialibrary_SOURCES = src/misc/addon_medialibrary.c

test_src_input_policy_SOURCES = src/input/input_policy.c
test_src_input_policy_LDADD = $(LIBVLCCORE)

if HAVE_LINUX
check_PROGRAMS += test_src_input_ytdl_policy
test_src_input_ytdl_policy_SOURCES = src/input/ytdl_policy.c
test_src_input_ytdl_policy_LDADD = $(LIBVLC) $(LIBVLCCORE) ../modules/libvlc_json.la -lm
endif

test_src_input_artwork_policy_SOURCES = src/input/artwork_policy.c
test_src_input_artwork_policy_LDADD = $(LIBVLCCORE)

test_src_input_artwork_fetcher_SOURCES = src/input/artwork_fetcher.c
test_src_input_artwork_fetcher_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src
test_src_input_artwork_fetcher_LDADD = $(LIBVLCCORE)

test_src_input_source_backend_SOURCES = src/input/source_backend.c
test_src_input_source_backend_LDADD = $(LIBVLCCORE)

test_src_input_addon_source_SOURCES = src/input/addon_source.c
test_src_input_addon_source_LDADD = $(LIBVLCCORE)

test_src_input_addon_source_playback_SOURCES = src/input/addon_source_playback.c
test_src_input_addon_source_playback_LDADD = $(LIBVLC) $(LIBVLCCORE)

test_src_misc_medialibrary_work_SOURCES = src/misc/medialibrary_work.c
test_src_misc_medialibrary_work_LDADD = $(LIBVLC) $(LIBVLCCORE)

test_src_player_audio_samples_lifecycle_SOURCES = src/player/audio_samples_lifecycle.c
test_src_player_audio_samples_lifecycle_LDADD = $(LIBVLC) $(LIBVLCCORE)

test_src_input_source_owner_SOURCES = src/input/source_owner.c
test_src_input_source_owner_LDADD = $(LIBVLCCORE)

if HAVE_WIN32
check_PROGRAMS += test_src_config_dirs_win32
test_src_config_dirs_win32_SOURCES = src/config/dirs_win32.c
test_src_config_dirs_win32_LDADD = $(LIBVLCCORE) -lshell32 -lole32 -ladvapi32 -luuid
check_PROGRAMS += test_src_misc_win32_pipe
test_src_misc_win32_pipe_SOURCES = src/misc/win32_pipe.c
test_src_misc_win32_pipe_LDADD = $(LIBVLCCORE)
if !HAVE_WINSTORE
check_PROGRAMS += test_src_misc_win32_proxy
test_src_misc_win32_proxy_SOURCES = src/misc/win32_proxy.c
test_src_misc_win32_proxy_LDADD = $(LIBVLCCORE) ../compat/libcompat.la -lwinhttp -lws2_32
endif
endif

test_libvlc_core_SOURCES = libvlc/core.c
test_libvlc_core_LDADD = $(LIBVLC)
test_libvlc_downloader_SOURCES = libvlc/downloader.c
test_libvlc_downloader_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_equalizer_SOURCES = libvlc/equalizer.c
test_libvlc_equalizer_LDADD = $(LIBVLC)
test_libvlc_media_SOURCES = libvlc/media.c
test_libvlc_media_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_media_callback_SOURCES = libvlc/media_callback.c
test_libvlc_media_callback_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_media_thumbnail_argb_SOURCES = libvlc/thumbnailer.c
test_libvlc_media_thumbnail_argb_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_media_thumbnail_argb_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_THUMB_TYPE=libvlc_picture_Argb -DTEST_REQUIRED_MODULES=\"avcodec\"
test_libvlc_media_thumbnail_rgba_SOURCES = libvlc/thumbnailer.c
test_libvlc_media_thumbnail_rgba_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_media_thumbnail_rgba_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_THUMB_TYPE=libvlc_picture_Rgba -DTEST_REQUIRED_MODULES=\"avcodec\"
test_libvlc_media_thumbnail_png_SOURCES = libvlc/thumbnailer.c
test_libvlc_media_thumbnail_png_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_media_thumbnail_png_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_THUMB_TYPE=libvlc_picture_Png -DTEST_REQUIRED_MODULES=\"png\"
test_libvlc_media_thumbnail_jpeg_SOURCES = libvlc/thumbnailer.c
test_libvlc_media_thumbnail_jpeg_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_media_thumbnail_jpeg_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_THUMB_TYPE=libvlc_picture_Jpg -DTEST_REQUIRED_MODULES=\"jpeg\"
test_libvlc_media_thumbnail_webp_SOURCES = libvlc/thumbnailer.c
test_libvlc_media_thumbnail_webp_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_media_thumbnail_webp_CPPFLAGS = $(AM_CPPFLAGS) -DTEST_THUMB_TYPE=libvlc_picture_WebP -DTEST_REQUIRED_MODULES=\"vpx\"
test_libvlc_media_list_player_SOURCES = libvlc/media_list_player.c
test_libvlc_media_list_player_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_media_list_SOURCES = libvlc/media_list.c
test_libvlc_media_list_LDADD = $(LIBVLC)
test_libvlc_media_player_SOURCES = libvlc/media_player.c
test_libvlc_media_player_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_video_callback_SOURCES = libvlc/video_callback.c
test_libvlc_video_callback_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_media_player_record_SOURCES = libvlc/media_player_record.c
test_libvlc_media_player_record_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_media_discoverer_SOURCES = libvlc/media_discoverer.c
test_libvlc_media_discoverer_LDADD = $(LIBVLC)
test_libvlc_renderer_discoverer_SOURCES = libvlc/renderer_discoverer.c
test_libvlc_renderer_discoverer_LDADD = $(LIBVLC)
test_libvlc_slaves_SOURCES = libvlc/slaves.c
test_libvlc_slaves_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_libvlc_meta_SOURCES = libvlc/meta.c
test_libvlc_meta_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_src_audio_output_dec_SOURCES = src/audio_output/dec.c \
	../src/audio_output/dec.c \
	../src/audio_output/output.c \
	../src/audio_output/common.c \
	../src/audio_output/filters.c \
	../src/audio_output/volume.c \
	../src/audio_output/meter.c \
	../src/input/replay_gain.c \
	../src/misc/objects.c \
	../src/misc/variables.c \
	../src/clock/clock.c \
	../src/clock/clock_internal.c
test_src_audio_output_dec_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src
test_src_audio_output_dec_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)

test_src_clock_clock_SOURCES = src/clock/clock.c \
	../src/clock/clock.c \
	../src/clock/clock_internal.c
test_src_clock_clock_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_clock_start_SOURCES = src/clock/clock_start.c \
	../src/clock/clock.c \
	../src/clock/clock_internal.c
test_src_clock_start_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_src_misc_ancillary_SOURCES = src/misc/ancillary.c
test_src_misc_ancillary_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_misc_iso_language_SOURCES = src/misc/iso_language.c
test_src_misc_iso_language_LDADD = $(LIBVLCCORE)
test_src_misc_variables_SOURCES = src/misc/variables.c
test_src_misc_variables_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_misc_actions_SOURCES = src/misc/actions.c
test_src_misc_actions_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_config_startup_SOURCES = src/config/startup.c
test_src_config_startup_LDADD = $(LIBVLC) $(LIBVLCCORE)

test_src_config_chain_SOURCES = src/config/chain.c
test_src_config_chain_LDADD = $(LIBVLCCORE)
test_src_crypto_update_SOURCES = src/crypto/update.c
test_src_crypto_update_LDADD = $(LIBVLCCORE) $(GCRYPT_LIBS)
test_src_input_stream_SOURCES = src/input/stream.c
test_src_input_stream_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_input_stream_net_SOURCES = src/input/stream.c
test_src_input_stream_net_CFLAGS = $(AM_CFLAGS) -DTEST_NET
test_src_input_stream_net_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_input_stream_fifo_SOURCES = src/input/stream_fifo.c
test_src_input_stream_fifo_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_preparser_serializer_SOURCES = src/preparser/serializer.c
test_src_preparser_serializer_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_preparser_cmp_internal_external_SOURCES = src/preparser/cmp_internal_external.c
test_src_preparser_cmp_internal_external_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_preparser_folder_options_SOURCES = src/preparser/folder_options.c
test_src_preparser_folder_options_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_preparser_subtree_identity_SOURCES = src/preparser/subtree_identity.c
test_src_preparser_subtree_identity_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_playlist_preparse_depth_SOURCES = src/playlist/preparse_depth.c
test_src_playlist_preparse_depth_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_preparser_fetcher_shutdown_SOURCES = src/preparser/fetcher_shutdown.c \
    ../src/preparser/art.c ../src/misc/interrupt.c ../src/misc/objects.c \
    ../src/misc/variables.c ../src/input/item.c ../src/input/policy.c ../src/text/strings.c
test_src_preparser_fetcher_shutdown_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src
test_src_preparser_fetcher_shutdown_LDADD = $(LIBVLCCORE) $(LIBVLC) ../compat/libcompat.la $(SOCKET_LIBS)
test_src_preparser_artwork_fallback_SOURCES = src/preparser/artwork_fallback.c
test_src_preparser_artwork_fallback_CPPFLAGS = $(AM_CPPFLAGS)
if HAVE_DYNAMIC_PLUGINS
test_src_preparser_artwork_fallback_CPPFLAGS += -DHAVE_DYNAMIC_PLUGINS
endif
test_src_preparser_artwork_fallback_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_preparser_thumbnail_SOURCES = src/preparser/thumbnail.c
test_src_preparser_thumbnail_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_preparser_thumbnail_to_files_SOURCES = src/preparser/thumbnail_to_files.c
test_src_preparser_thumbnail_to_files_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_player_abloop_SOURCES = src/player/common.h src/player/modules.c \
	src/player/abloop.c src/player/timers.h
test_src_player_abloop_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_attachments_SOURCES = src/player/common.h src/player/modules.c \
	src/player/attachments.c
test_src_player_attachments_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_capabilities_SOURCES = src/player/common.h src/player/modules.c \
	src/player/capabilities.c
test_src_player_capabilities_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_delays_SOURCES = src/player/common.h src/player/modules.c \
	src/player/delays.c
test_src_player_delays_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_modules_demux_mkv_subtitle_cluster_SOURCES = modules/demux/mkv_subtitle_cluster.cpp
test_modules_demux_mkv_subtitle_cluster_LDADD = libvlc_demux_run.la $(LIBVLCCORE) $(LIBVLC)

test_modules_demux_mkv_subtitle_seek_SOURCES = modules/demux/mkv_subtitle_seek.cpp

test_src_input_subtitle_style_SOURCES = src/input/subtitle_style.c
test_src_input_subtitle_style_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_misc_media_library_startup_SOURCES = src/misc/media_library_startup.c
test_src_misc_media_library_startup_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_input_overlay_opacity_SOURCES = src/input/overlay_opacity.c
test_src_input_overlay_opacity_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_input_subtitle_duration_SOURCES = src/input/subtitle_duration.c
test_src_input_subtitle_duration_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_input_subtitle_encoding_SOURCES = src/input/subtitle_encoding.c
test_src_input_subtitle_encoding_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_input_subtitle_ass_text_SOURCES = src/input/subtitle_ass_text.c
test_src_input_item_update_SOURCES = src/input/item_update.c
test_src_input_item_update_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_playlist_export_SOURCES = src/playlist/export.c
test_src_playlist_export_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_player_discontinuities_SOURCES = src/player/common.h src/player/modules.c \
	src/player/discontinuities.c
test_src_player_discontinuities_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_eof_SOURCES = src/player/common.h src/player/modules.c \
	src/player/eof.c
test_src_player_eof_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_errors_SOURCES = src/player/common.h src/player/modules.c \
	src/player/errors.c
test_src_player_errors_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_es_selection_SOURCES = src/player/common.h src/player/modules.c \
	src/player/es_selection.c
test_src_player_es_selection_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_lifecycle_SOURCES = src/player/common.h src/player/modules.c \
	src/player/lifecycle.c
test_src_player_lifecycle_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_loudness_SOURCES = src/player/common.h src/player/modules.c \
	src/player/loudness.c
test_src_player_loudness_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_media_SOURCES = src/player/common.h src/player/modules.c \
	src/player/media.c
test_src_player_media_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_next_prev_SOURCES = src/player/common.h src/player/modules.c \
	src/player/next_prev.c
test_src_player_next_prev_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_outputs_SOURCES = src/player/common.h src/player/modules.c \
	src/player/outputs.c
test_src_player_outputs_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_outputs_CPPFLAGS = $(AM_CPPFLAGS) -DOUTPUT_TEARDOWN_TEST

test_src_player_video_filters_SOURCES = src/player/common.h src/player/modules.c \
	src/player/video_filters.c
test_src_player_video_filters_CPPFLAGS = $(AM_CPPFLAGS) -DVIDEO_FILTER_TEST
test_src_player_video_filters_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_pause_SOURCES = src/player/common.h src/player/modules.c \
	src/player/pause.c
test_src_player_pause_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_programs_SOURCES = src/player/common.h src/player/modules.c \
	src/player/programs.c
test_src_player_programs_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_seeks_SOURCES = src/player/common.h src/player/modules.c \
	src/player/seeks.c
test_src_player_seeks_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_teletext_SOURCES = src/player/common.h src/player/modules.c \
	src/player/teletext.c
test_src_player_teletext_CPPFLAGS = $(AM_CPPFLAGS)
if HAVE_ZVBI
test_src_player_teletext_CPPFLAGS += -DZVBI_COMPILED=1
endif
test_src_player_teletext_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_timers_SOURCES = src/player/common.h src/player/modules.c \
	src/player/timers.c src/player/timers.h
test_src_player_timers_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_titles_SOURCES = src/player/common.h src/player/modules.c \
	src/player/titles.c
test_src_player_titles_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_tracks_SOURCES = src/player/common.h src/player/modules.c \
	src/player/tracks.c
test_src_player_tracks_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_player_tracks_ids_SOURCES = src/player/common.h src/player/modules.c \
	src/player/tracks_ids.c
test_src_player_tracks_ids_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM)
test_src_misc_bits_SOURCES = src/misc/bits.c
test_src_misc_bits_LDADD = $(LIBVLC)
test_src_misc_epg_SOURCES = src/misc/epg.c
test_src_misc_epg_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_misc_keystore_SOURCES = src/misc/keystore.c
test_src_misc_keystore_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_misc_image_cvpx_SOURCES = src/misc/image_cvpx.c
test_src_misc_image_cvpx_LDADD = $(LIBVLCCORE) $(LIBVLC) ../modules/libvlc_vtutils.la
test_src_misc_image_cvpx_LDFLAGS = $(AM_LDFLAGS) -Wl,-framework,CoreVideo
test_src_misc_viewpoint_SOURCES = src/misc/viewpoint.c
test_src_misc_viewpoint_LDADD = $(LIBVLCCORE) $(LIBM)
test_src_interface_dialog_SOURCES = src/interface/dialog.c
test_src_interface_dialog_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_media_source_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_media_source_SOURCES = src/media_source/media_source.c

test_src_video_output_SOURCES = \
	src/video_output/video_output.c \
	src/video_output/video_output.h \
	src/video_output/video_output_scenarios.c
test_src_video_output_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_video_output_subtitle_snapshot_SOURCES = src/video_output/subtitle_snapshot.c
test_src_video_output_subtitle_snapshot_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_video_output_subtitle_area_SOURCES = src/video_output/subtitle_area.c
test_src_video_output_subtitle_area_LDADD = $(LIBVLCCORE)
test_src_video_output_filter_aspect_SOURCES = src/video_output/filter_aspect.c
test_src_video_output_filter_aspect_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_video_output_filter_format_SOURCES = src/video_output/filter_format.c
test_src_video_output_filter_format_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_video_output_inhibit_SOURCES = src/video_output/inhibit.c
test_src_video_output_inhibit_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_video_output_opengl_SOURCES = src/video_output/opengl.c
test_src_video_output_opengl_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_video_output_spu_SOURCES = src/video_output/spu.c
test_src_video_output_spu_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_video_output_osd_SOURCES = src/video_output/osd.c \
	../src/video_output/video_text.c ../src/video_output/video_widgets.c
test_src_video_output_osd_CPPFLAGS = $(AM_CPPFLAGS) -Dvout_PutSubpicture=CaptureSubpicture
test_src_video_output_osd_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_src_input_decoder_SOURCES = \
	src/input/decoder/input_decoder.c \
	src/input/decoder/input_decoder.h \
	src/input/decoder/input_decoder_scenarios.c
test_src_input_decoder_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_src_misc_chroma_probe_SOURCES = src/misc/chroma_probe.c
test_src_misc_chroma_probe_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_src_misc_image_SOURCES = src/misc/image.c
test_src_misc_image_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_misc_addons_SOURCES = src/misc/addons.c
test_src_misc_addons_LDADD = $(LIBVLCCORE) $(LIBVLC)
if HAVE_WIN32
if HAVE_DYNAMIC_PLUGINS
check_LTLIBRARIES = libtest_addons_plugin.la
libtest_addons_plugin_la_SOURCES = src/misc/addons_plugin.c
libtest_addons_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) -DVLC_DYNAMIC_PLUGIN -DMODULE_STRING=\"test_misc_addons\"
libtest_addons_plugin_la_LDFLAGS = -module -avoid-version -no-undefined -rpath /not-installed
libtest_addons_plugin_la_LIBADD = $(LIBVLCCORE)
test_src_misc_addons_CPPFLAGS = $(AM_CPPFLAGS) -DADDONS_TEST_DYNAMIC_FIXTURE
test_src_misc_addons_DEPENDENCIES = libtest_addons_plugin.la
endif
endif
test_src_misc_instance_policy_SOURCES = src/misc/instance_policy.c
test_src_misc_instance_policy_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_misc_instance_policy_CPPFLAGS = $(AM_CPPFLAGS)
test_src_misc_instance_forward_SOURCES = src/misc/instance_forward.c
test_src_misc_instance_forward_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_src_misc_instance_forward_CPPFLAGS = $(AM_CPPFLAGS)
if HAVE_DBUS
test_src_misc_instance_policy_CPPFLAGS += -DHAVE_DBUS
test_src_misc_instance_forward_CPPFLAGS += -DHAVE_DBUS
endif

test_modules_lua_extension_SOURCES = modules/lua/extension.c
test_modules_lua_extension_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_lua_extension_CPPFLAGS = $(AM_CPPFLAGS)

test_modules_lua_playlist_parser_SOURCES = modules/lua/playlist_parser.c
test_modules_lua_playlist_parser_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_lua_playlist_parser_CPPFLAGS = $(AM_CPPFLAGS)
test_modules_lua_musicbrainz_SOURCES = modules/lua/musicbrainz.c
test_modules_lua_musicbrainz_CPPFLAGS = $(AM_CPPFLAGS) $(LUA_CFLAGS)
test_modules_lua_musicbrainz_LDADD = $(LUA_LIBS)

test_modules_misc_medialibrary_SOURCES = modules/misc/medialibrary.c
test_modules_misc_medialibrary_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_misc_media_tracks_SOURCES = modules/misc/media_tracks.c
test_modules_misc_media_tracks_LDADD = $(LIBVLCCORE) $(LIBVLC) ../compat/libcompat.la
test_modules_visualization_resize_SOURCES = modules/visualization/resize.c
test_modules_visualization_resize_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_video_filter_puzzle_SOURCES = modules/video_filter/puzzle.c \
	../modules/video_filter/puzzle/puzzle_lib.c \
	../modules/video_filter/puzzle/puzzle_lib.h
test_modules_video_filter_puzzle_LDADD = $(LIBVLCCORE)
test_modules_packetizer_helpers_SOURCES = modules/packetizer/helpers.c
test_modules_packetizer_helpers_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_packetizer_hxxx_SOURCES = modules/packetizer/hxxx.c
test_modules_packetizer_hxxx_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_packetizer_copy_SOURCES = modules/packetizer/copy.c
test_modules_packetizer_copy_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_packetizer_h264_SOURCES = modules/packetizer/h264.c \
				modules/packetizer/packetizer.h
test_modules_packetizer_h264_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_packetizer_h266_SOURCES = modules/packetizer/h266.c \
				modules/packetizer/packetizer.h
test_modules_packetizer_h266_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_packetizer_h266_nal_SOURCES = modules/packetizer/h266_nal.c \
				           ../modules/packetizer/h266_nal.c \
				           ../modules/packetizer/h266_nal.h
test_modules_packetizer_h266_nal_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_packetizer_hevc_SOURCES = modules/packetizer/hevc.c \
				modules/packetizer/packetizer.h
test_modules_packetizer_hevc_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_packetizer_mpegvideo_SOURCES = modules/packetizer/mpegvideo.c \
				modules/packetizer/packetizer.h
test_modules_packetizer_mpegvideo_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_keystore_SOURCES = modules/keystore/test.c
test_modules_keystore_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_tls_SOURCES = modules/misc/tls.c
test_modules_tls_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_demux_json_SOURCES = modules/demux/json.c \
				../modules/demux/json/json.h
test_modules_demux_json_LDADD = $(LIBVLCCORE) ../modules/libvlc_json.la
test_modules_demux_libmp4_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBM) $(LIBZ)
test_modules_demux_libmp4_SOURCES = modules/demux/libmp4.c \
				../modules/demux/mp4/libmp4.c \
				../modules/demux/mp4/libmp4.h
test_modules_demux_timestamps_SOURCES = modules/demux/timestamps.c
test_modules_demux_timestamps_filter_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_demux_timestamps_filter_SOURCES = modules/demux/timestamps_filter.c
test_modules_demux_ts_pes_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_demux_ts_pes_SOURCES = modules/demux/ts_pes.c \
				../modules/demux/mpeg/ts_pes.c \
				../modules/demux/mpeg/ts_pes.h
test_modules_playlist_m3u_SOURCES = modules/demux/playlist/m3u.c
test_modules_playlist_m3u_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_codec_hxxx_helper_SOURCES = modules/codec/hxxx_helper.c \
                                      ../modules/codec/hxxx_helper.c \
                                      ../modules/packetizer/hxxx_nal.c \
                                      ../modules/packetizer/h264_slice.c \
                                      ../modules/packetizer/h264_nal.c \
                                      ../modules/packetizer/hevc_nal.c
test_modules_codec_hxxx_helper_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_codec_cea708_SOURCES = modules/codec/cea708.c \
				../modules/codec/cea708.c
test_modules_codec_cea708_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_codec_cea708_aspect_ratio_SOURCES = modules/codec/cea708_aspect_ratio.c
test_modules_codec_cea708_aspect_ratio_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_codec_cea708_integration_SOURCES = modules/codec/cea708_integration.c
test_modules_codec_cea708_integration_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_video_output_opengl_filters_SOURCES = \
	modules/video_output/opengl/filters.c \
	../modules/video_output/opengl/filters.c \
	../modules/video_output/opengl/filters.h \
	../modules/video_output/opengl/filter.c \
	../modules/video_output/opengl/gl_api.c \
	../modules/video_output/opengl/gl_api.h \
	../modules/video_output/opengl/interop.c \
	../modules/video_output/opengl/interop.h \
	../modules/video_output/opengl/importer.c \
	../modules/video_output/opengl/importer.h
test_modules_visualization_gl_resize_SOURCES = modules/visualization/resize.c
test_modules_visualization_gl_resize_CPPFLAGS = $(AM_CPPFLAGS) -DVISUAL_RESIZE_TEST_GL
test_modules_visualization_gl_resize_LDADD = $(LIBVLCCORE) $(LIBVLC)
if HAVE_GL
check_PROGRAMS += test_modules_visualization_projectm_resize
endif
test_modules_visualization_projectm_resize_SOURCES = modules/visualization/resize.c
test_modules_visualization_projectm_resize_CPPFLAGS = $(AM_CPPFLAGS) -DVISUAL_RESIZE_TEST_GL -DVISUAL_RESIZE_TEST_PROJECTM
test_modules_visualization_projectm_resize_LDADD = $(LIBVLCCORE) $(LIBVLC)
EXTRA_DIST += modules/visualization/projectm-presets/canvas-check.milk

test_modules_video_output_opengl_offscreen_SOURCES = modules/video_output/opengl/offscreen.c
test_modules_video_output_opengl_offscreen_LDADD = $(LIBVLCCORE) $(LIBVLC) ../modules/libvlc_opengl.la
test_modules_video_output_opengl_offscreen_CPPFLAGS = $(AM_CPPFLAGS)
if HAVE_GL
if HAVE_EGL
check_PROGRAMS += test_modules_video_output_opengl_es2_offscreen
test_modules_video_output_opengl_offscreen_CPPFLAGS += -DVLC_TEST_EXTERNAL_EGL $(EGL_FLAGS)
test_modules_video_output_opengl_offscreen_LDADD += $(EGL_LIBS)
test_modules_video_output_opengl_es2_offscreen_SOURCES = $(test_modules_video_output_opengl_offscreen_SOURCES)
test_modules_video_output_opengl_es2_offscreen_CPPFLAGS = $(AM_CPPFLAGS) -DVLC_TEST_OPENGL_API=VLC_OPENGL_ES2 -DVLC_TEST_EXTERNAL_EGL $(EGL_FLAGS)
test_modules_video_output_opengl_es2_offscreen_LDADD = $(LIBVLCCORE) $(LIBVLC) ../modules/libvlc_opengl.la $(EGL_LIBS)
endif
endif
test_modules_video_output_opengl_filters_LDADD = $(LIBVLCCORE) $(LIBVLC) ../modules/libvlc_opengl.la
test_modules_video_output_opengl_filters_CPPFLAGS = $(AM_CPPFLAGS) -DVLC_TEST_OPENGL_API=VLC_OPENGL
test_modules_video_output_opengl_es2_filters_SOURCES = $(test_modules_video_output_opengl_filters_SOURCES)
test_modules_video_output_opengl_es2_filters_LDADD = $(LIBVLCCORE) $(LIBVLC) ../modules/libvlc_opengles.la
test_modules_video_output_opengl_es2_filters_CPPFLAGS = $(AM_CPPFLAGS) -DVLC_TEST_OPENGL_API=VLC_OPENGL_ES2

test_modules_video_output_opengl_sub_renderer_SOURCES = \
	modules/video_output/opengl/sub_renderer.c \
	../modules/video_output/opengl/sub_renderer.c \
	../modules/video_output/opengl/sub_renderer.h \
	../modules/video_output/opengl/filter.c \
	../modules/video_output/opengl/gl_api.c \
	../modules/video_output/opengl/gl_api.h \
	../modules/video_output/opengl/gl_util.c \
	../modules/video_output/opengl/gl_util.h \
	../modules/video_output/opengl/interop.c \
	../modules/video_output/opengl/interop.h
test_modules_video_output_opengl_sub_renderer_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBPLACEBO_LIBS) ../modules/libvlc_opengl.la
test_modules_video_output_opengl_sub_renderer_CPPFLAGS = $(AM_CPPFLAGS) -DVLC_TEST_OPENGL_API=VLC_OPENGL
test_modules_video_output_opengl_es2_sub_renderer_SOURCES = $(test_modules_video_output_opengl_sub_renderer_SOURCES)
test_modules_video_output_opengl_es2_sub_renderer_LDADD = $(LIBVLCCORE) $(LIBVLC) $(LIBPLACEBO_LIBS) ../modules/libvlc_opengles.la
test_modules_video_output_opengl_es2_sub_renderer_CPPFLAGS = $(AM_CPPFLAGS) -DVLC_TEST_OPENGL_API=VLC_OPENGL_ES2

test_modules_stream_out_transcode_SOURCES = \
	modules/stream_out/transcode.c \
	modules/stream_out/transcode.h \
	modules/stream_out/transcode_scenarios.c
test_modules_stream_out_transcode_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_stream_out_audio_tail_SOURCES = modules/stream_out/audio_tail.c
test_modules_stream_out_audio_tail_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_stream_out_pcr_sync_SOURCES = modules/stream_out/pcr_sync.c \
	../modules/stream_out/transcode/pcr_sync.c \
	../modules/stream_out/transcode/pcr_sync.h \
	../modules/stream_out/transcode/pcr_helper.h \
	../modules/stream_out/transcode/pcr_helper.c
test_modules_stream_out_pcr_sync_LDADD = $(LIBVLCCORE)

test_modules_stream_out_renderer_quality_SOURCES = modules/stream_out/renderer_quality.cpp \
	modules/stream_out/renderer_quality_profiles.h
test_modules_stream_out_renderer_quality_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_stream_out_sdi_SOURCES = modules/stream_out/sdi.cpp \
	../modules/stream_out/sdi/SDIStream.cpp \
	../modules/stream_out/sdi/SDIStream.hpp
test_modules_stream_out_sdi_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_mux_webvtt_SOURCES = modules/mux/webvtt.c
test_modules_mux_webvtt_LDADD = $(LIBVLCCORE) $(LIBVLC)
test_modules_video_filter_blend_SOURCES = modules/video_filter/blend.c
test_modules_video_filter_blend_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_stream_out_hls_subtitles_segmenter_SOURCES = \
	modules/stream_out/hls/subtitles_segmenter.c \
	../modules/stream_out/hls/hls.h \
	../modules/stream_out/hls/subtitles_segmenter.c
test_modules_stream_out_hls_subtitles_segmenter_LDADD = $(LIBVLCCORE) $(LIBVLC)

checkall:
	$(MAKE) check_PROGRAMS="$(check_PROGRAMS) $(EXTRA_PROGRAMS)" check

checkplayer:
	$(MAKE) check_PROGRAMS="$(player_programs)" check

FORCE:
	@echo "Generated source cannot be phony. Go away." >&2
	@exit 1

.PHONY: FORCE

libvlc_demux_run_la_SOURCES = src/input/demux-run.c src/input/demux-run.h \
	src/input/common.c src/input/common.h $(top_srcdir)/src/input/var.c
libvlc_demux_run_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src
libvlc_demux_run_la_LDFLAGS = -no-install -static
libvlc_demux_run_la_LIBADD = \
	../lib/libvlc.la ../src/libvlccore.la ../compat/libcompat.la
if !HAVE_DYNAMIC_PLUGINS
libvlc_demux_run_la_CPPFLAGS += -DHAVE_STATIC_MODULES
libvlc_demux_run_la_LIBADD += \
	../modules/libxml_plugin.la \
	../modules/libconsole_logger_plugin.la \
	../modules/libaiff_plugin.la \
	../modules/libasf_plugin.la \
	../modules/libau_plugin.la \
	../modules/libavi_plugin.la \
	../modules/libcaf_plugin.la \
	../modules/libdemux_cdg_plugin.la \
	../modules/libes_plugin.la \
	../modules/libflacsys_plugin.la \
	../modules/libh26x_plugin.la \
	../modules/libmjpeg_plugin.la \
	../modules/libmp4_plugin.la \
	../modules/libnsv_plugin.la \
	../modules/libnuv_plugin.la \
	../modules/libps_plugin.la \
	../modules/libpva_plugin.la \
	../modules/libsap_plugin.la \
	../modules/libsmf_plugin.la \
	../modules/libsubtitle_plugin.la \
	../modules/libtta_plugin.la \
	../modules/libttml_plugin.la \
	../modules/libty_plugin.la \
	../modules/libvoc_plugin.la \
	../modules/libwav_plugin.la \
	../modules/libwebvtt_plugin.la \
	../modules/libxa_plugin.la \
	../modules/libpacketizer_a52_plugin.la \
	../modules/libpacketizer_copy_plugin.la \
	../modules/libpacketizer_dts_plugin.la \
	../modules/libpacketizer_flac_plugin.la \
	../modules/libpacketizer_h264_plugin.la \
	../modules/libpacketizer_hevc_plugin.la \
	../modules/libpacketizer_mlp_plugin.la \
	../modules/libpacketizer_mpeg4audio_plugin.la \
	../modules/libpacketizer_mpeg4video_plugin.la \
	../modules/libpacketizer_mpegaudio_plugin.la \
	../modules/libpacketizer_mpegvideo_plugin.la \
	../modules/libpacketizer_vc1_plugin.la \
	../modules/librawaud_plugin.la \
	../modules/librawvid_plugin.la \
	../modules/libfilesystem_plugin.la \
	../modules/libxml_plugin.la \
	../modules/libogg_plugin.la \
	-lstdc++
if HAVE_DVBPSI
libvlc_demux_run_la_CPPFLAGS += -DHAVE_DVBPSI
libvlc_demux_run_la_LIBADD += ../modules/libts_plugin.la
endif
if HAVE_MATROSKA
libvlc_demux_run_la_CPPFLAGS += -DHAVE_MATROSKA
libvlc_demux_run_la_LIBADD += ../modules/libmkv_plugin.la
endif
endif
EXTRA_LTLIBRARIES = libvlc_demux_run.la

libvlc_demux_dec_run_la_SOURCES = $(libvlc_demux_run_la_SOURCES) \
	src/input/decoder.c src/input/decoder.h
libvlc_demux_dec_run_la_CPPFLAGS = $(libvlc_demux_run_la_CPPFLAGS) -DHAVE_DECODERS
libvlc_demux_dec_run_la_LDFLAGS = $(libvlc_demux_run_la_LDFLAGS)
libvlc_demux_dec_run_la_LIBADD = $(libvlc_demux_run_la_LIBADD)
if !HAVE_DYNAMIC_PLUGINS
libvlc_demux_dec_run_la_LIBADD += \
	../modules/libadpcm_plugin.la \
	../modules/libaes3_plugin.la \
	../modules/libaraw_plugin.la \
	../modules/libg711_plugin.la \
	../modules/liblpcm_plugin.la \
	../modules/libuleaddvaudio_plugin.la \
	../modules/librawvideo_plugin.la \
	../modules/libcc_plugin.la \
	../modules/libcvdsub_plugin.la \
	../modules/libdvbsub_plugin.la \
	../modules/libscte18_plugin.la \
	../modules/libscte27_plugin.la \
	../modules/libspudec_plugin.la \
	../modules/libstl_plugin.la \
	../modules/libsubsdec_plugin.la \
	../modules/libsubsusf_plugin.la \
	../modules/libsvcdsub_plugin.la \
	../modules/libtextst_plugin.la \
	../modules/libsubstx3g_plugin.la
if HAVE_ZVBI
libvlc_demux_dec_run_la_LIBADD += ../modules/libzvbi_plugin.la
endif
if HAVE_TELX
libvlc_demux_dec_run_la_LIBADD += ../modules/libtelx_plugin.la
endif
endif
EXTRA_LTLIBRARIES += libvlc_demux_dec_run.la

#
# Fuzzers
#
vlc_demux_run_LDFLAGS = -no-install -static
vlc_demux_run_LDADD = libvlc_demux_run.la
vlc_demux_dec_run_SOURCES = vlc-demux-run.c
vlc_demux_dec_run_LDFLAGS = -no-install -static
if HAVE_OSX
vlc_demux_dec_run_LDFLAGS += -Xlinker -rpath -Xlinker "@executable_path/../lib/.libs/"
vlc_demux_dec_run_LDFLAGS += -Xlinker -rpath -Xlinker "@executable_path/../src/.libs/"
vlc_demux_dec_run_LDFLAGS += -Xlinker -rpath -Xlinker "@executable_path/../Frameworks/"
endif
vlc_demux_dec_run_LDADD = libvlc_demux_dec_run.la
EXTRA_PROGRAMS += vlc-demux-run vlc-demux-dec-run

vlc_demux_libfuzzer_LDADD = libvlc_demux_run.la
vlc_demux_dec_libfuzzer_SOURCES = vlc-demux-libfuzzer.c
vlc_demux_dec_libfuzzer_LDADD = libvlc_demux_dec_run.la
if HAVE_LIBFUZZER
noinst_PROGRAMS += vlc-demux-libfuzzer vlc-demux-dec-libfuzzer vlc-demux-run vlc-demux-dec-run
endif

vlccoreios_SOURCES = iosvlc.m
vlccoreios_LDFLAGS = $(LDFLAGS_vlc) -Wl,-framework,Foundation,-framework,UIKit
vlccoreios_LDFLAGS += -Xlinker -rpath -Xlinker "@executable_path/Frameworks/"
vlccoreios_OBJCFLAGS = -fobjc-arc
vlccoreios_LDADD = ../lib/libvlc.la ../src/libvlccore.la
if HAVE_IOS
noinst_PROGRAMS += vlccoreios
endif

if HAVE_TVOS
noinst_PROGRAMS += vlccoreios
endif

if HAVE_XROS
noinst_PROGRAMS += vlccoreios
endif

vlccorewatchos_SOURCES = watchosvlc.m
vlccorewatchos_LDFLAGS = $(LDFLAGS_vlc) -Wl,-framework,WatchKit
vlccorewatchos_LDFLAGS += -Xlinker -rpath -Xlinker "@executable_path/Frameworks/"
vlccorewatchos_OBJCFLAGS = -fobjc-arc
vlccorewatchos_LDADD = ../lib/libvlc.la ../src/libvlccore.la

if HAVE_WATCHOS
noinst_PROGRAMS += vlccorewatchos
endif

vlc_window_SOURCES = vlc-window.c
vlc_window_CPPFLAGS = $(AM_CPPFLAGS) -I../include/
vlc_window_LDADD = ../lib/libvlc.la ../src/libvlccore.la ../compat/libcompat.la
# Opt-out for building the test driver if there is no dynamic plugins, since we
# cannot link static plugins currently anyway.
if HAVE_DYNAMIC_PLUGINS
noinst_PROGRAMS += vlc-window
endif

test_src_config_language_SOURCES = src/config/language.c
test_src_config_language_LDADD = $(LIBVLC) $(LIBVLCCORE)

test_src_misc_log_observer_SOURCES = src/misc/log_observer.c
test_src_misc_log_observer_LDADD = $(LIBVLC) $(LIBVLCCORE)

test_src_misc_file_logger_SOURCES = src/misc/file_logger.c
test_src_misc_file_logger_LDADD = $(LIBVLC) $(LIBVLCCORE)

test_src_player_timer_seek_length_SOURCES = src/player/timer_seek_length.c ../src/player/timer.c
test_src_player_timer_seek_length_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src
test_src_player_timer_seek_length_LDADD = $(LIBVLCCORE) $(LIBM)

check_PROGRAMS += test_src_network_http_rate
test_src_network_http_rate_SOURCES = src/network/http_rate.c
test_src_network_http_rate_LDADD = $(LIBVLCCORE)

check_PROGRAMS += test_src_network_http_auth
test_src_network_http_auth_SOURCES = src/network/http_auth.c
test_src_network_http_auth_LDADD = $(LIBVLC) $(LIBVLCCORE)

test_modules_video_filter_rotate_SOURCES = modules/video_filter/rotate.c
test_modules_video_filter_rotate_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_src_video_output_window_backdrop_SOURCES = src/video_output/window_backdrop.c
test_src_video_output_window_backdrop_LDADD = $(LIBVLCCORE) $(LIBVLC)

if HAVE_LINUX
if HAVE_GL
if HAVE_EGL
check_PROGRAMS += test_modules_video_output_opengl_backdrop test_modules_video_output_opengl_es2_backdrop
test_modules_video_output_opengl_backdrop_SOURCES = modules/video_output/opengl/backdrop.c \
    ../modules/video_output/opengl/backdrop.c ../modules/video_output/opengl/backdrop.h
test_modules_video_output_opengl_backdrop_CPPFLAGS = $(AM_CPPFLAGS) $(EGL_CFLAGS)
test_modules_video_output_opengl_backdrop_LDADD = $(LIBVLCCORE) $(LIBVLC) ../modules/libvlc_opengl.la $(EGL_LIBS) $(LIBM)
test_modules_video_output_opengl_es2_backdrop_SOURCES = $(test_modules_video_output_opengl_backdrop_SOURCES)
test_modules_video_output_opengl_es2_backdrop_CPPFLAGS = $(AM_CPPFLAGS) $(EGL_CFLAGS) -DVLC_TEST_OPENGL_API=VLC_OPENGL_ES2
if HAVE_GLES2
test_modules_video_output_opengl_es2_backdrop_CPPFLAGS += -DUSE_OPENGL_ES2
endif
test_modules_video_output_opengl_es2_backdrop_LDADD = $(test_modules_video_output_opengl_backdrop_LDADD)
endif
endif
endif

if HAVE_WAYLAND
check_PROGRAMS += test_modules_video_output_wayland_shm_backdrop
test_modules_video_output_wayland_shm_backdrop_SOURCES = modules/video_output/wayland/shm_backdrop.c \
    ../modules/video_output/wayland/shm_backdrop.c ../modules/video_output/wayland/shm_backdrop.h
test_modules_video_output_wayland_shm_backdrop_LDADD = $(LIBVLCCORE) $(LIBM)
endif

if HAVE_LIBPLACEBO
check_PROGRAMS += test_modules_video_output_libplacebo_plane
test_modules_video_output_libplacebo_plane_SOURCES = modules/video_output/libplacebo/plane.c \
    ../modules/video_output/libplacebo/utils.c ../modules/video_output/libplacebo/utils.h
test_modules_video_output_libplacebo_plane_CPPFLAGS = $(AM_CPPFLAGS) $(LIBPLACEBO_CFLAGS)
test_modules_video_output_libplacebo_plane_LDADD = $(LIBPLACEBO_LIBS) $(LIBVLCCORE) $(LIBM)
if HAVE_VULKAN
check_PROGRAMS += test_modules_video_output_libplacebo_backdrop
test_modules_video_output_libplacebo_backdrop_SOURCES = modules/video_output/libplacebo/backdrop.c \
    ../modules/video_output/libplacebo/backdrop.c ../modules/video_output/libplacebo/backdrop.h
test_modules_video_output_libplacebo_backdrop_CPPFLAGS = $(AM_CPPFLAGS) $(LIBPLACEBO_CFLAGS)
test_modules_video_output_libplacebo_backdrop_LDADD = $(LIBPLACEBO_LIBS) $(LIBM)
endif
endif

test_modules_audio_filter_gain_SOURCES = modules/audio_filter/gain.c
test_modules_audio_filter_gain_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_mux_mp4_drain_SOURCES = modules/mux/mp4_drain.c
test_modules_mux_mp4_drain_LDADD = $(LIBVLCCORE) $(LIBVLC)

test_modules_video_filter_sepia_SOURCES = modules/video_filter/sepia.c
test_modules_video_filter_sepia_LDADD = $(LIBVLCCORE) $(LIBVLC)
