guidir = $(pluginsdir)/gui
gui_LTLIBRARIES =
gui_PLUGINS =
gui_RPATH =

noinst_LTLIBRARIES =
if ENABLE_QT
noinst_LTLIBRARIES += libqt_addon_host.la
endif
if HAVE_PARTIAL_LINKING
noinst_LTLIBRARIES += $(gui_PLUGINS)
else
gui_LTLIBRARIES += $(gui_PLUGINS)
gui_RPATH += -rpath '$(guidir)'
endif


## Howto
# For each Q_OBJECT:
#   - Add *.moc.cpp to nodist_..._SOURCES
#   - Add *.cpp to ..._SOURCES
#   - Add *.hpp to ..._SOURCES
# For each UI
#   - Add *.ui to ..._UI
#   - Add *.h to nodist_..._SOURCES
# For each resource (icon, png, ...)
#   - Add it to DEPS_res
# For each qml and js resource
#   - Add it to ..._QML

CLEANFILES =

include ../../common.am

# Generate vlc_modules_list for Qt plugins
include ../../modulelist.mk

SUFFIXES += .ui .h .hpp .moc.cpp .qml .js .mjs .moc
TEST_EXTENSIONS = .qml
EXTRA_DIST = qt6.pro qtest.pro quicktest.pro scripts
vlc_qt_libs = $(QT_LIBS) $(LIBZ)
RCC_EXTRA_PARAMS =

all-local: $(VLC_QT_CHECK_DEP) $(QML_MODULE_STAMPS)

BUILT_SOURCES += subtitle_consumer_key.hpp addon_distribution_paths.hpp
CLEANFILES += subtitle_consumer_key.hpp addon_distribution_paths.hpp
.PHONY: subtitle-consumer-key-force
subtitle_consumer_key.hpp: subtitle-consumer-key-force $(srcdir)/scripts/subtitle-consumer-key.py
	$(AM_V_GEN)$(PYTHON3) $(srcdir)/scripts/subtitle-consumer-key.py --output $@

addon_distribution_paths.hpp: $(srcdir)/scripts/addon-distribution-paths.py
	$(AM_V_GEN)$(PYTHON3) $(srcdir)/scripts/addon-distribution-paths.py \
		--root "$(abs_top_srcdir)/contrib-addons/examples" --output $@

if !HAVE_QT_ZSTD
RCC_EXTRA_PARAMS += --no-zstd
endif

vlc_qt_check_SOURCES = vlc-qt-check.cpp plugins.hpp
vlc_qt_check_CXXFLAGS = $(AM_CXXFLAGS) $(QT_CFLAGS) -fPIC $(CXXFLAGS_qt)
vlc_qt_check_CPPFLAGS = $(AM_CPPFLAGS) $(CPPFLAGS_qt)
vlc_qt_check_LDADD = $(vlc_qt_libs)
vlc_qt_check_LDFLAGS = $(AM_LDFLAGS) $(QT_LDFLAGS)

libqt_plugin_la_CPPFLAGS = $(AM_CPPFLAGS) $(CPPFLAGS_qt) \
	-DQT_STRICT_ITERATORS \
	-DQT_USE_QSTRINGBUILDER \
	-DQT_NO_CAST_TO_ASCII \
	-DQT_NO_CAST_FROM_BYTEARRAY \
	-I$(srcdir) -I$(builddir) \
	-I$(builddir)/dialogs/extended \
	-I$(builddir)/dialogs/fingerprint \
	-I$(builddir)/dialogs/firstrun \
	-I$(builddir)/dialogs/help \
	-I$(builddir)/dialogs/messages \
	-I$(builddir)/dialogs/open \
	-I$(builddir)/dialogs/podcast \
	-I$(builddir)/dialogs/preferences \
	-I$(builddir)/dialogs/sout \
	-I$(builddir)/dialogs/vlm

libqt_plugin_la_CXXFLAGS = $(AM_CXXFLAGS) $(QT_CFLAGS) $(CXXFLAGS_qt) -fPIC
libqt_plugin_la_LIBADD = $(vlc_qt_libs) $(ADDON_PLATFORM_LIBS) libqt_addon_host.la
if HAVE_LIBATOMIC
libqt_plugin_la_CXXFLAGS += -DQT_HAS_LIBATOMIC
libqt_plugin_la_LIBADD += -latomic
endif
libqt_plugin_la_LDFLAGS = $(AM_LDFLAGS) $(QT_LDFLAGS) $(GC_SECTIONS_LDFLAGS) $(ADDON_PLATFORM_LDFLAGS) $(QT_LOW_MEMORY_LDFLAGS)
if HAVE_OSX
libqt_plugin_la_LDFLAGS += -Wl,-framework,Cocoa
endif

if HAVE_X
if HAVE_XCB
libqt_plugin_la_CXXFLAGS += $(XCB_CFLAGS) -DQT_HAS_X11
libqt_plugin_la_LIBADD += $(XCB_LIBS) $(X_LIBS) $(X_PRE_LIB) -lX11
vlc_qt_check_CXXFLAGS += $(XCB_CFLAGS) -DQT_HAS_X11
endif
endif

if HAVE_WIN32
libqt_plugin_la_CXXFLAGS += $(LIBCOMCXXFLAGS)
libqt_plugin_la_LIBADD += $(LIBCOM) -lcomctl32 -luuid -ldwmapi
endif
if QT_UPDATE_CHECK
libqt_plugin_la_CPPFLAGS += -DUPDATE_CHECK
endif
if QT_QML_DEBUG
libqt_plugin_la_CPPFLAGS += -DQT_QML_DEBUG
endif
if HAVE_QT_GTK
libqt_plugin_la_CPPFLAGS += -DQT_HAS_GTK
endif

QT_ADDON_HOST_SOURCES = \
	addons/addon_manifest.cpp \
	addons/addon_manifest.hpp \
	addons/addon_package_store.cpp \
	addons/addon_package_store.hpp \
	addons/addon_store_authority.cpp \
	addons/addon_store_authority.hpp \
	addons/addon_credentials.cpp \
	addons/addon_credentials.hpp \
	addons/addon_manager.cpp \
	addons/addon_manager.hpp \
	addons/addon_package_reader.hpp \
	addons/addon_package_summaries.cpp \
	addons/addon_package_summaries.hpp \
	addons/addon_native_authority.hpp \
	addons/addon_network.cpp \
	addons/addon_network.hpp \
	addons/addon_public_network_policy.cpp \
	addons/addon_public_network_policy.hpp \
	addons/addon_worker.cpp \
	addons/addon_worker.hpp \
	addons/addon_metadata_coordinator.cpp \
	addons/addon_metadata_coordinator.hpp \
	addons/addon_metadata_family_plan.cpp \
	addons/addon_metadata_family_plan.hpp \
	addons/addon_metadata_choices.cpp \
	addons/addon_metadata_choices.hpp \
	addons/addon_native_provider_context.cpp \
	addons/addon_native_provider_context.hpp \
	addons/addon_native_provider_artwork_assets.cpp \
	addons/addon_native_provider_artwork_assets.hpp \
	addons/addon_native_metadata_owner.cpp \
	addons/addon_native_metadata_owner.hpp \
	addons/addon_ui_registry.cpp \
	addons/addon_ui_registry.hpp \
	addons/addon_ui_assets.cpp \
	addons/addon_ui_assets.hpp \
	addons/addon_metadata_service.cpp \
	addons/addon_metadata_service.hpp \
	addons/addon_discovery_coordinator.cpp \
	addons/addon_discovery_coordinator.hpp \
	addons/addon_queue_transactions.cpp \
	addons/addon_queue_transactions.hpp \
	addons/addon_discovery_service.cpp \
	addons/addon_discovery_service.hpp \
	addons/addon_player_discovery.cpp \
	addons/addon_player_discovery.hpp \
	addons/addon_source_admissions.cpp \
	addons/addon_source_admissions.hpp \
	addons/addon_stream_manifest.cpp \
	addons/addon_stream_manifest.hpp \
	addons/addon_stream_http.cpp \
	addons/addon_stream_http.hpp \
	addons/addon_stream_policy.cpp \
	addons/addon_stream_policy.hpp \
	addons/addon_stream_sockets.cpp \
	addons/addon_stream_sockets.hpp \
	addons/addon_stream_lifetime.cpp \
	addons/addon_stream_lifetime.hpp \
	addons/addon_stream_shutdown.cpp \
	addons/addon_stream_shutdown.hpp \
	addons/addon_app_shutdown.cpp \
	addons/addon_app_shutdown.hpp \
	addons/addon_stream_dns.cpp \
	addons/addon_stream_dns.hpp \
	addons/addon_stream_proxy.cpp \
	addons/addon_stream_proxy.hpp \
	addons/addon_stream_proxy_native.cpp \
	addons/addon_stream_proxy_native.hpp \
	addons/addon_websocket_codec.cpp \
	addons/addon_websocket_codec.hpp \
	addons/addon_source_consent.cpp \
	addons/addon_source_consent.hpp \
	addons/addon_visualizer_catalog.cpp \
	addons/addon_visualizer_catalog.hpp \
	addons/addon_visualizer_resources.cpp \
	addons/addon_visualizer_resources.hpp \
	addons/addon_visualizer_worker.cpp \
	addons/addon_visualizer_worker.hpp \
	addons/addon_visualizer_render_queue.cpp \
	addons/addon_visualizer_render_queue.hpp \
	addons/addon_native_source_resolver.cpp \
	addons/addon_native_source_resolver.hpp \
	addons/addon_discovery_source_owner.cpp \
	addons/addon_discovery_source_owner.hpp \
	addons/addon_source_consent_ui_owner.cpp \
	addons/addon_native_player_analysis.cpp \
	addons/addon_native_player_analysis.hpp \
	addons/addon_visualizer_worker_runtime.cpp \
	addons/addon_visualizer_activation.hpp \
	addons/addon_visualizer_clock.hpp \
	addons/addon_native_queue_store.cpp \
	addons/addon_native_queue_store.hpp \
	addons/addon_native_library_store.cpp \
	addons/addon_native_library_store.hpp \
	addons/addon_native_library_targets.cpp \
	addons/addon_native_library_targets.hpp \
	addons/addon_theme_service.cpp \
	addons/addon_theme_service.hpp \
	addons/addon_sessions.cpp \
	addons/addon_sessions.hpp \
	addons/addon_session_player.hpp \
	addons/addon_session_websocket.cpp \
	addons/addon_session_websocket.hpp \
	addons/addon_session_host.cpp \
	addons/addon_session_host.hpp \
	addons/addon_session_controller.cpp \
	addons/addon_session_controller.hpp \
	addons/addon_session_ui.cpp \
	addons/addon_session_ui.hpp \
	addons/addon_session_ui_manager.cpp \
	addons/addon_source_consent_ui.cpp \
	addons/addon_source_consent_ui.hpp \
	addons/addon_native_session_player.cpp \
	addons/addon_native_session_player.hpp \
	addons/addon_player_sessions.cpp \
	addons/addon_player_sessions.hpp \
	addons/addon_audio_analysis.cpp \
	addons/addon_audio_analysis.hpp \
	addons/addon_visual_scene.cpp \
	addons/addon_visual_scene.hpp \
	addons/addon_visualizer_parameters.cpp \
	addons/addon_visualizer_parameters.hpp \
	addons/addon_visualizers.cpp \
	addons/addon_visualizers.hpp \
	addons/addon_settings_migrations.cpp \
	addons/addon_settings_migrations.hpp \
	addons/addon_storage.cpp \
	addons/addon_storage.hpp \
	addons/addon_storage_data.cpp \
	addons/addon_storage_data.hpp \
	addons/addon_host_services.cpp \
	addons/addon_host_services.hpp \
	addons/addon_repository_updates.cpp \
	addons/addon_repository_updates.hpp \
	addons/addon_repository_jobs.cpp \
	addons/addon_repository_jobs.hpp \
	addons/addon_repository_cancellation.hpp \
	addons/addon_playable_gate.cpp \
	addons/addon_playable_gate.hpp \
	addons/addon_artwork_assets.cpp \
	addons/addon_artwork_assets.hpp \
	addons/addon_artwork_image.cpp \
	addons/addon_artwork_image.hpp \
	addons/addon_native_image_formats.hpp \
	addons/addon_native_artwork_assets.cpp \
	addons/addon_native_artwork_assets.hpp
QT_ADDON_HOST_MOC = \
	addons/addon_manager.moc.cpp \
	addons/addon_package_summaries.moc.cpp \
	addons/addon_network.moc.cpp \
	addons/addon_worker.moc.cpp \
	addons/addon_metadata_coordinator.moc.cpp \
	addons/addon_ui_registry.moc.cpp \
	addons/addon_ui_assets.moc.cpp \
	addons/addon_metadata_service.moc.cpp \
	addons/addon_discovery_coordinator.moc.cpp \
	addons/addon_queue_transactions.moc.cpp \
	addons/addon_discovery_service.moc.cpp \
	addons/addon_native_queue_store.moc.cpp \
	addons/addon_discovery_source_owner.moc.cpp \
	addons/addon_native_library_store.moc.cpp \
	addons/addon_native_library_targets.moc.cpp \
	addons/addon_theme_service.moc.cpp \
	addons/addon_sessions.moc.cpp \
	addons/addon_session_host.moc.cpp \
	addons/addon_session_ui.moc.cpp \
	addons/addon_source_consent_ui.moc.cpp \
	addons/addon_native_metadata_owner.moc.cpp \
	addons/addon_player_sessions.moc.cpp \
	addons/addon_visualizers.moc.cpp \
	addons/addon_visualizer_catalog.moc.cpp \
	addons/addon_visualizer_resources.moc.cpp \
	addons/addon_visualizer_worker.moc.cpp \
	addons/addon_visualizer_render_queue.moc.cpp \
	addons/addon_host_services.moc.cpp \
	addons/addon_playable_gate.moc.cpp \
	addons/addon_artwork_assets.moc.cpp

libqt_addon_host_la_SOURCES = $(QT_ADDON_HOST_SOURCES)
nodist_libqt_addon_host_la_SOURCES = $(QT_ADDON_HOST_MOC)
if ENABLE_QT
BUILT_SOURCES += $(QT_ADDON_HOST_MOC)
CLEANFILES += $(QT_ADDON_HOST_MOC)
endif
libqt_addon_host_la_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqt_addon_host_la_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqt_addon_host_la_LDFLAGS = $(ADDON_PLATFORM_LDFLAGS)

ADDON_PLATFORM_LIBS =
ADDON_PLATFORM_LDFLAGS =
if HAVE_WIN32
ADDON_PLATFORM_LIBS += -ladvapi32 -lws2_32
endif
if HAVE_OSX
ADDON_PLATFORM_LDFLAGS += -framework Security
endif

EXTRA_DIST += addons/worker/Cargo.toml addons/worker/Cargo.lock addons/worker/src/main.rs \
	tests/fixtures/addon-worker-fixture.py
bin_SCRIPTS =
if BUILD_ADDON_RUNTIME
bin_SCRIPTS += vlc-addon-worker$(EXEEXT)
CLEANFILES += vlc-addon-worker$(EXEEXT)
vlc-addon-worker$(EXEEXT): $(srcdir)/addons/worker/Cargo.toml $(srcdir)/addons/worker/Cargo.lock $(srcdir)/addons/worker/src/main.rs $(srcdir)/scripts/build-addon-worker.py
	$(AM_V_GEN)$(PYTHON3) $(srcdir)/scripts/build-addon-worker.py \
		--cargo "$(ADDON_CARGO)" --manifest "$(abs_srcdir)/addons/worker/Cargo.toml" \
		--output "$@" --target "$(ADDON_RUST_TARGET)" --linker "$(ADDON_RUST_LINKER)" \
		--system "$(ADDON_RUNTIME_HOST_OS)" --cpu "$(ADDON_RUNTIME_HOST_CPU)" $(ADDON_RUNTIME_CROSS)
libqt_plugin_la_CPPFLAGS += -DVLC_ADDON_WORKER_BUILD_PATH='"$(abs_builddir)/vlc-addon-worker$(EXEEXT)"'
endif

libqt_plugin_la_SOURCES = \
	addons/addon_visualizer_surface.cpp \
	addons/addon_visualizer_surface.hpp \
	addons/addon_player_visualizers.cpp \
	addons/addon_player_visualizers.hpp \
	qt.cpp qt.hpp plugins.hpp \
	dialogs/bookmarks/bookmarks.cpp dialogs/bookmarks/bookmarks.hpp \
	dialogs/dialogs/dialogmodel.cpp \
	dialogs/dialogs/dialogmodel.hpp \
	dialogs/dialogs_provider.cpp dialogs/dialogs_provider.hpp \
	dialogs/epg/EPGChannels.cpp \
	dialogs/epg/EPGChannels.hpp \
	dialogs/epg/EPGItem.cpp \
	dialogs/epg/EPGItem.hpp \
	dialogs/epg/EPGProgram.cpp \
	dialogs/epg/EPGProgram.hpp \
	dialogs/epg/EPGRuler.cpp dialogs/epg/EPGRuler.hpp \
	dialogs/epg/EPGView.cpp dialogs/epg/EPGView.hpp \
	dialogs/epg/EPGWidget.cpp \
	dialogs/epg/EPGWidget.hpp \
	dialogs/epg/epg.cpp dialogs/epg/epg.hpp \
	dialogs/extended/extended.cpp dialogs/extended/extended.hpp \
	dialogs/extended/extended_panels.cpp \
	dialogs/extended/extended_panels.hpp \
	dialogs/extensions/extensions.cpp dialogs/extensions/extensions.hpp \
	dialogs/extensions/extensions_manager.cpp dialogs/extensions/extensions_manager.hpp \
	dialogs/fingerprint/chromaprint.cpp \
	dialogs/fingerprint/chromaprint.hpp \
	dialogs/fingerprint/fingerprintdialog.cpp \
	dialogs/fingerprint/fingerprintdialog.hpp \
	dialogs/firstrun/firstrunwizard.cpp dialogs/firstrun/firstrunwizard.hpp \
	dialogs/gototime/gototime.cpp dialogs/gototime/gototime.hpp \
	dialogs/help/aboutmodel.hpp \
	dialogs/help/help.cpp dialogs/help/help.hpp \
	dialogs/mediainfo/info_panels.cpp dialogs/mediainfo/info_panels.hpp \
	dialogs/mediainfo/info_widgets.cpp dialogs/mediainfo/info_widgets.hpp \
	dialogs/mediainfo/mediainfo.cpp dialogs/mediainfo/mediainfo.hpp \
	dialogs/messages/messages.cpp dialogs/messages/messages.hpp \
	dialogs/open/open.cpp dialogs/open/open.hpp \
	dialogs/open/open_panels.cpp dialogs/open/open_panels.hpp \
	dialogs/open/openurl.cpp dialogs/open/openurl.hpp \
	dialogs/plugins/plugins.cpp dialogs/plugins/plugins.hpp \
	dialogs/podcast/podcast_configuration.cpp \
	dialogs/podcast/podcast_configuration.hpp \
	dialogs/preferences/complete_preferences.cpp \
	dialogs/preferences/complete_preferences.hpp \
	dialogs/preferences/expert_model.cpp \
	dialogs/preferences/expert_model.hpp \
	dialogs/preferences/expert_view.cpp \
	dialogs/preferences/expert_view.hpp \
	dialogs/preferences/redesign_preferences.cpp \
	dialogs/preferences/redesign_preferences.hpp \
	player/stereo_test_player.cpp \
	player/stereo_test_player.hpp \
	player/stereo_test_signal.hpp \
	dialogs/preferences/preferences.cpp \
	dialogs/preferences/preferences.hpp \
	dialogs/preferences/preferences_widgets.cpp \
	dialogs/preferences/preferences_widgets.hpp \
	dialogs/preferences/simple_preferences.cpp \
	dialogs/preferences/simple_preferences.hpp \
	dialogs/sout/convert.cpp dialogs/sout/convert.hpp \
	dialogs/sout/profile_selector.cpp \
	dialogs/sout/profile_selector.hpp \
	dialogs/sout/profiles.hpp \
	dialogs/sout/sout.cpp dialogs/sout/sout.hpp \
	dialogs/sout/sout_widgets.cpp \
	dialogs/sout/sout_widgets.hpp \
	dialogs/systray/systray.cpp \
	dialogs/systray/notification_policy.hpp \
	dialogs/systray/systray.hpp \
	dialogs/toolbar/controlbar_profile.hpp \
	dialogs/toolbar/controlbar_profile.cpp \
	dialogs/toolbar/controlbar_profile_model.cpp \
	dialogs/toolbar/controlbar_profile_model.hpp \
	dialogs/vlm/vlm.cpp dialogs/vlm/vlm.hpp \
	dialogs/playlists/playlists.cpp dialogs/playlists/playlists.hpp \
	maininterface/compositor.hpp \
	maininterface/compositor.cpp \
	maininterface/compositor_common.hpp \
	maininterface/compositor_common.cpp \
	maininterface/compositor_accessibility.cpp \
	maininterface/compositor_accessibility.hpp \
	maininterface/compositor_dummy.hpp \
	maininterface/compositor_dummy.cpp \
	maininterface/compositor_platform.hpp \
	maininterface/compositor_platform.cpp \
	maininterface/interface_window_handler.cpp \
	maininterface/interface_window_handler.hpp \
	maininterface/mainctx.cpp \
	maininterface/mainctx.hpp \
	maininterface/window_fullscreen.hpp \
	maininterface/window_csd_geometry.hpp \
	maininterface/mainctx_submodels.cpp maininterface/mainctx_submodels.hpp \
	maininterface/mainui.cpp \
	addons/addon_asset_image_provider.cpp \
	addons/addon_asset_image_provider.hpp \
	maininterface/mainui.hpp \
	maininterface/navigationmodel.cpp maininterface/navigationmodel.hpp \
	maininterface/videobackdrop.cpp maininterface/videobackdrop.hpp \
	maininterface/videosurface.cpp \
	maininterface/videosurface.hpp \
	maininterface/video_window_handler.cpp \
	maininterface/video_window_handler.hpp \
	maininterface/windoweffects_module.hpp \
	medialibrary/medialib.cpp \
	medialibrary/medialib.hpp \
	medialibrary/mlmediastore.cpp \
	medialibrary/mlmediastore.hpp \
	medialibrary/mlfileartwork.cpp medialibrary/mlfileartwork.hpp \
	medialibrary/mlalbum.cpp \
	medialibrary/mlalbum.hpp \
	medialibrary/mlalbummodel.cpp \
	medialibrary/mlalbummodel.hpp \
	medialibrary/mlaudio.cpp \
	medialibrary/mlaudio.hpp \
	medialibrary/mlaudiomodel.cpp \
	medialibrary/mlaudiomodel.hpp \
	medialibrary/mlartist.cpp \
	medialibrary/mlartist.hpp \
	medialibrary/mlartistmodel.cpp \
	medialibrary/mlartistmodel.hpp \
	medialibrary/mlbasemodel.cpp \
	medialibrary/mlbasemodel.hpp \
	medialibrary/mlbookmarkmodel.cpp \
	medialibrary/mlbookmarkmodel.hpp \
	medialibrary/mlevent.hpp \
	medialibrary/mlfolder.cpp \
	medialibrary/mlfolder.hpp \
	medialibrary/mlfoldersmodel.cpp \
	medialibrary/mlfoldersmodel.hpp \
	medialibrary/mlgenre.cpp \
	medialibrary/mlgenre.hpp \
	medialibrary/mlgenremodel.cpp \
	medialibrary/mlgenremodel.hpp \
	medialibrary/mlgroup.cpp \
	medialibrary/mlgroup.hpp \
	medialibrary/mlhelper.cpp \
	medialibrary/mlhelper.hpp \
	medialibrary/mlthreadpool.cpp \
	medialibrary/mljoinedthreadrunner.cpp \
	medialibrary/mljoinedthreadrunner.hpp \
	medialibrary/mltaskid.hpp \
	medialibrary/mlthreadpool.hpp \
	medialibrary/mlqmltypes.hpp \
	medialibrary/mlmedia.hpp \
	medialibrary/mlqueryparams.cpp \
	medialibrary/mlqueryparams.hpp \
	medialibrary/mlrecentmediamodel.cpp \
	medialibrary/mlrecentmediamodel.hpp \
	medialibrary/mlrecentaudiomodel.cpp \
	medialibrary/mlrecentaudiomodel.hpp \
	medialibrary/mlplayedartistmodel.cpp \
	medialibrary/mlplayedartistmodel.hpp \
	medialibrary/mlrecentvideomodel.cpp \
	medialibrary/mlrecentvideomodel.hpp \
	medialibrary/mlurlmodel.cpp \
	medialibrary/mlurlmodel.hpp \
	medialibrary/mlvideo.cpp \
	medialibrary/mlvideo.hpp \
	medialibrary/mlvideofoldersmodel.cpp \
	medialibrary/mlvideofoldersmodel.hpp \
	medialibrary/mlvideogroupsmodel.cpp \
	medialibrary/mlvideogroupsmodel.hpp \
	medialibrary/mlmediamodel.cpp \
	medialibrary/mlmediamodel.hpp \
	medialibrary/mlvideomodel.cpp \
	medialibrary/mlvideomodel.hpp \
	medialibrary/mlplaylist.cpp \
	medialibrary/mlplaylist.hpp \
	medialibrary/mlplaylistlistmodel.cpp \
	medialibrary/mlplaylistlistmodel.hpp \
	medialibrary/mlplaylistmedia.cpp \
	medialibrary/mlplaylistmedia.hpp \
	medialibrary/mlplaylistmodel.cpp \
	medialibrary/mlplaylistmodel.hpp \
	medialibrary/thumbnailcollector.hpp \
	medialibrary/thumbnailcollector.cpp \
	medialibrary/mlcustomcover.hpp \
	medialibrary/mlcustomcover.cpp \
	menus/custom_menus.cpp \
	menus/custom_menus.hpp \
	menus/qml_menu_wrapper.cpp \
	menus/qml_menu_wrapper.hpp \
	menus/menus.cpp menus/menus.hpp \
	menus/redesign_menu_style.cpp menus/redesign_menu_style.hpp \
	network/addonsmodel.cpp \
	network/addonsmodel.hpp \
	network/mediatreelistener.cpp \
	network/mediatreelistener.hpp \
	network/devicesourceprovider.cpp \
	network/devicesourceprovider.hpp \
	network/networkbasemodel.cpp \
	network/networkbasemodel.hpp \
	network/networkdevicemodel.cpp \
	network/networkdevicemodel.hpp \
	network/networksourcesmodel.cpp \
	network/networkplaylistmodel.cpp network/networkplaylistmodel.hpp \
	network/networksourcesmodel.hpp \
	network/networkmediamodel.cpp \
	network/networkmediamodel.hpp \
	network/standardpathmodel.cpp \
	network/standardpathmodel.hpp \
	network/vlcmediasourcewrapper.hpp \
	style/qtthemeprovider.hpp \
	style/colorcontext.cpp \
	style/colorcontext.hpp \
	style/systempalette.cpp \
	style/systempalette.hpp \
	style/vlcicons.cpp 	style/vlcicons.hpp \
	style/defaultthemeproviders.hpp \
	style/systempalettethemeprovider.cpp \
	player/input_models.cpp player/input_models.hpp \
	player/delay_estimator.cpp \
	player/delay_estimator.hpp \
	player/player_controller.cpp player/player_controller.hpp player/player_controller_p.hpp \
	player/series_model.cpp player/series_model.hpp \
	player/episode_metadata.cpp player/episode_metadata.hpp \
	util/seek_preview_cache.hpp util/seek_preview_image_provider.hpp \
	util/subtitle_source_service.hpp util/subtitle_source_service.cpp \
	util/github_release_service.hpp util/github_release_service.cpp \
	util/release_info.hpp util/release_info.cpp \
	player/subtitle_timeline_scanner.cpp player/subtitle_timeline_scanner.hpp \
	player/audio_waveform_scanner.cpp player/audio_waveform_scanner.hpp \
	util/podcast_metadata.hpp \
	player/player_controlbar_model.cpp player/player_controlbar_model.hpp \
	player/control_list_model.cpp player/control_list_model.hpp \
	player/control_list_filter.cpp \
	player/control_list_filter.hpp \
	playlist/media.hpp \
	playlist/playlist_common.hpp \
	playlist/playlist_controller.cpp \
	playlist/playlist_controller.hpp \
	playlist/playlist_controller_p.hpp \
	playlist/playlist_item.cpp \
	playlist/playlist_item.hpp \
	playlist/playlist_model.cpp \
	playlist/playlist_model.hpp \
	playlist/playlist_tree_model.cpp \
	playlist/playlist_tree_model.hpp \
	playlist/playlist_model_p.hpp \
	util/asynctask.hpp \
	util/accessible_compat_attached.cpp util/accessible_compat_attached.hpp \
	util/audio_device_model.cpp  \
	util/audio_device_model.hpp \
	util/redesign_folder_model.cpp util/redesign_folder_model.hpp \
	util/series_discovery.hpp \
	util/workerthreadset.hpp util/workerthreadset.cpp \
	util/base_model.hpp util/base_model_p.hpp util/base_model.cpp \
	util/color_scheme_model.cpp util/color_scheme_model.hpp \
	util/redesign_widget_theme.cpp util/redesign_widget_theme.hpp \
	util/scroll_focus_tracker.cpp util/scroll_focus_tracker.hpp \
	util/playlist_cover_store.hpp \
	util/artwork_selection.cpp util/artwork_selection.hpp \
	util/filter_chain_edit.hpp util/track_matching.hpp util/media_identity.hpp util/media_fingerprint.hpp util/media_identity_vlc.hpp util/episode_title_cache.hpp util/media_identity_ml.hpp util/artwork_discovery.hpp util/artwork_store.hpp util/subtitle_matching.hpp \
	util/local_file_url.hpp \
	util/gettext_translator.cpp util/gettext_translator.hpp \
	util/interface_translator.cpp util/interface_translator.hpp \
	util/ui_notifier.cpp util/ui_notifier.hpp \
	util/color_svg_image_provider.cpp util/color_svg_image_provider.hpp \
	util/vlcaccess_image_provider.cpp util/vlcaccess_image_provider.hpp \
	util/covergenerator.cpp \
	util/covergenerator.hpp \
	util/imageluminanceextractor.cpp util/imageluminanceextractor.hpp \
	util/csdbuttonmodel.cpp util/csdbuttonmodel.hpp \
	util/csdmenu.cpp util/csdmenu.hpp \
	util/imagehelper.cpp util/imagehelper.hpp \
	util/keyhelper.cpp util/keyhelper.hpp \
	util/listcache.hxx util/listcache.hpp \
	util/listcacheloader.hpp \
	util/locallistcacheloader.hpp \
	util/locallistbasemodel.hpp \
	util/navigation_history.cpp util/navigation_history.hpp \
	util/item_key_event_filter.cpp \
	util/item_key_event_filter.hpp \
	util/flickable_scroll_handler.cpp \
	util/flickable_scroll_handler.hpp \
	util/qt_dirs.cpp util/qt_dirs.hpp \
	util/proxycolumnmodel.hpp \
	util/registry.cpp util/registry.hpp \
	util/renderer_manager.cpp util/renderer_manager.hpp \
	util/singleton.hpp \
	util/soutchain.cpp util/soutchain.hpp \
	util/validators.cpp util/validators.hpp \
	util/varcommon_p.hpp \
	util/varchoicemodel.cpp  util/varchoicemodel.hpp \
	util/variables.cpp util/variables.hpp \
	util/vlchotkeyconverter.cpp util/vlchotkeyconverter.hpp \
	util/postprocessing_preferences.hpp \
	util/file_log_preferences.hpp \
	util/preference_values.hpp \
	util/message_log.hpp \
	util/message_log_buffer.hpp \
	util/video_window_geometry.hpp \
	util/vlctick.cpp \
	util/vlctick.hpp \
	util/shared_input_item.hpp \
	util/fast_gaussian_blur_template.h \
	util/effects_image_provider.cpp \
	util/effects_image_provider.hpp \
	util/qsgtextureview.cpp \
	util/qsgtextureview.hpp \
	util/dismiss_popup_event_filter.cpp \
	util/dismiss_popup_event_filter.hpp \
	util/list_selection_model.cpp \
	util/list_selection_model.hpp \
	util/model_recovery_agent.hpp \
	util/vlcqtmessagehandler.cpp \
	util/vlcqtmessagehandler.hpp \
	util/colorizedsvgicon.cpp \
	util/colorizedsvgicon.hpp \
	util/textureproviderobserver.cpp \
	util/textureproviderobserver.hpp \
	util/kirigamiwheelhandler.cpp \
	util/kirigamiwheelhandler.hpp \
	util/textureproviderindirection.cpp \
	util/textureproviderindirection.hpp \
	util/sgmanipulator.cpp \
	util/sgmanipulator.hpp \
	util/pointingtooltipattached.hpp \
	util/video_backdrop_region.hpp \
	util/video_backdrop_reporter.hpp \
	widgets/native/animators.cpp \
	widgets/native/animators.hpp \
	widgets/native/customwidgets.cpp widgets/native/customwidgets.hpp \
	widgets/native/embedded_file_browser.cpp widgets/native/embedded_file_browser.hpp \
	widgets/native/csdthemeimage.cpp \
	widgets/native/csdthemeimage.hpp \
	widgets/native/interface_widgets.cpp \
	widgets/native/interface_widgets.hpp \
	widgets/native/navigation_attached.cpp \
	widgets/native/navigation_attached.hpp \
	widgets/native/mlfolderseditor.cpp \
	widgets/native/mlfolderseditor.hpp \
	widgets/native/qvlcframe.cpp \
	widgets/native/qvlcframe.hpp \
	widgets/native/searchlineedit.cpp widgets/native/searchlineedit.hpp \
	widgets/native/viewblockingrectangle.cpp widgets/native/viewblockingrectangle.hpp \
	widgets/native/softwarebackdropblur.cpp widgets/native/softwarebackdropblur.hpp \
	widgets/native/doubleclickignoringitem.hpp

# Meta-object compilation

moc_verbose = $(moc_verbose_$(V))
moc_verbose_ = $(moc_verbose__$(AM_DEFAULT_VERBOSITY))
moc_verbose_0 = @echo "  MOC     " $@;
moc_verbose__0 = $(moc_verbose_0)

.hpp.moc.cpp:
	$(moc_verbose)$(MOC) $(MOC_CPPFLAGS) -o $@ $<

.cpp.moc:
	$(moc_verbose)$(MOC) $(MOC_CPPFLAGS) -o $@ $<

# These MOC files depend on the configure settings:
dialogs/dialogs_provider.moc.cpp dialogs/vlm/vlm.moc.cpp: $(top_builddir)/config.h

nodist_libqt_plugin_la_SOURCES = \
	addons/addon_visualizer_surface.moc.cpp \
	dialogs/bookmarks/bookmarks.moc.cpp \
	dialogs/dialogs/dialogmodel.moc.cpp \
	dialogs/dialogs_provider.moc.cpp \
	dialogs/epg/EPGChannels.moc.cpp \
	dialogs/epg/EPGProgram.moc.cpp \
	dialogs/epg/EPGRuler.moc.cpp \
	dialogs/epg/EPGView.moc.cpp \
	dialogs/epg/EPGWidget.moc.cpp \
	dialogs/epg/epg.moc.cpp \
	dialogs/extended/extended.moc.cpp \
	dialogs/extended/extended_panels.moc.cpp \
	dialogs/extensions/extensions.moc.cpp \
	dialogs/extensions/extensions_manager.moc.cpp \
	dialogs/fingerprint/chromaprint.moc.cpp \
	dialogs/fingerprint/fingerprintdialog.moc.cpp \
	dialogs/firstrun/firstrunwizard.moc.cpp \
	dialogs/gototime/gototime.moc.cpp \
	dialogs/help/aboutmodel.moc.cpp \
	dialogs/help/help.moc.cpp \
	dialogs/mediainfo/info_panels.moc.cpp \
	dialogs/mediainfo/info_widgets.moc.cpp \
	dialogs/mediainfo/mediainfo.moc.cpp \
	dialogs/messages/messages.moc.cpp \
	dialogs/open/open.moc.cpp \
	dialogs/open/open_panels.moc.cpp \
	dialogs/open/openurl.moc.cpp \
	dialogs/plugins/plugins.moc.cpp \
	dialogs/podcast/podcast_configuration.moc.cpp \
	dialogs/preferences/complete_preferences.moc.cpp \
	dialogs/preferences/expert_model.moc.cpp \
	dialogs/preferences/expert_view.moc.cpp \
	dialogs/preferences/redesign_preferences.moc.cpp \
	dialogs/preferences/preferences.moc.cpp \
	dialogs/preferences/preferences_widgets.moc.cpp \
	dialogs/preferences/simple_preferences.moc.cpp \
	dialogs/sout/convert.moc.cpp \
	dialogs/sout/profile_selector.moc.cpp \
	dialogs/sout/sout.moc.cpp \
	dialogs/sout/sout_widgets.moc.cpp \
	dialogs/systray/systray.moc.cpp \
	dialogs/toolbar/controlbar_profile.moc.cpp \
	dialogs/toolbar/controlbar_profile_model.moc.cpp \
	dialogs/playlists/playlists.moc.cpp \
	maininterface/compositor.moc.cpp \
	maininterface/compositor_common.moc.cpp \
	maininterface/compositor_dummy.moc.cpp \
	maininterface/compositor_platform.moc.cpp \
	maininterface/interface_window_handler.moc.cpp \
	maininterface/mainctx.moc.cpp \
	maininterface/mainctx_submodels.moc.cpp \
	maininterface/mainui.moc.cpp \
	maininterface/navigationmodel.moc.cpp \
	maininterface/videobackdrop.moc.cpp \
	maininterface/videosurface.moc.cpp \
	maininterface/video_window_handler.moc.cpp \
	medialibrary/medialib.moc.cpp \
	medialibrary/mlmediastore.moc.cpp \
	medialibrary/mlfileartwork.moc.cpp \
	medialibrary/mlalbummodel.moc.cpp \
	medialibrary/mlaudiomodel.moc.cpp \
	medialibrary/mlartistmodel.moc.cpp \
	medialibrary/mlbookmarkmodel.moc.cpp \
	medialibrary/mlbasemodel.moc.cpp \
	medialibrary/mlfoldersmodel.moc.cpp \
	medialibrary/mlgenremodel.moc.cpp \
	medialibrary/mlthreadpool.moc.cpp \
	medialibrary/mlqmltypes.moc.cpp \
	medialibrary/mlrecentmediamodel.moc.cpp \
	medialibrary/mlrecentaudiomodel.moc.cpp \
	medialibrary/mlplayedartistmodel.moc.cpp \
	medialibrary/mlrecentvideomodel.moc.cpp \
	medialibrary/mlurlmodel.moc.cpp \
	medialibrary/mlvideo.moc.cpp \
	medialibrary/mlmediamodel.moc.cpp \
	medialibrary/mlvideomodel.moc.cpp \
	medialibrary/mlplaylistlistmodel.moc.cpp \
	medialibrary/mlplaylistmodel.moc.cpp \
	medialibrary/mlvideofoldersmodel.moc.cpp \
	medialibrary/mlvideogroupsmodel.moc.cpp \
	medialibrary/thumbnailcollector.moc.cpp \
	menus/custom_menus.moc.cpp \
	menus/qml_menu_wrapper.moc.cpp \
	menus/menus.moc.cpp \
	network/addonsmodel.moc.cpp \
	network/devicesourceprovider.moc.cpp \
	network/networkdevicemodel.moc.cpp \
	network/networkbasemodel.moc.cpp \
	network/networksourcesmodel.moc.cpp \
	network/networkplaylistmodel.moc.cpp \
	network/networkmediamodel.moc.cpp \
	network/standardpathmodel.moc.cpp \
	style/colorcontext.moc.cpp \
	style/systempalette.moc.cpp \
	style/vlcicons.moc.cpp \
	player/delay_estimator.moc.cpp \
	player/input_models.moc.cpp \
	util/subtitle_source_service.moc.cpp \
	util/github_release_service.moc.cpp \
	player/player_controller.moc.cpp \
	player/series_model.moc.cpp \
	player/episode_metadata.moc.cpp \
	player/player_controlbar_model.moc.cpp \
	player/control_list_model.moc.cpp \
	player/control_list_filter.moc.cpp \
	playlist/playlist_controller.moc.cpp \
	playlist/playlist_item.moc.cpp \
	playlist/playlist_model.moc.cpp \
	playlist/playlist_tree_model.moc.cpp \
	util/asynctask.moc.cpp \
	util/accessible_compat_attached.moc.cpp \
	util/audio_device_model.moc.cpp \
	util/redesign_folder_model.moc.cpp \
	util/workerthreadset.moc.cpp \
	util/base_model.moc.cpp \
	util/color_scheme_model.moc.cpp \
	util/ui_notifier.moc.cpp \
	util/color_svg_image_provider.moc.cpp \
	util/vlcaccess_image_provider.moc.cpp \
	util/imageluminanceextractor.moc.cpp \
	util/csdbuttonmodel.moc.cpp \
	util/csdmenu.moc.cpp \
	util/keyhelper.moc.cpp \
	util/listcache.moc.cpp \
	util/locallistcacheloader.moc.cpp \
	util/navigation_history.moc.cpp \
	util/item_key_event_filter.moc.cpp \
	util/effects_image_provider.moc.cpp \
	util/flickable_scroll_handler.moc.cpp \
	util/renderer_manager.moc.cpp \
	util/validators.moc.cpp \
	util/varchoicemodel.moc.cpp \
	util/variables.moc.cpp \
	util/vlctick.moc.cpp \
	util/dismiss_popup_event_filter.moc.cpp \
	util/list_selection_model.moc.cpp \
	util/vlchotkeyconverter.moc.cpp \
	util/qsgtextureview.moc.cpp \
	util/textureproviderobserver.moc.cpp \
	util/kirigamiwheelhandler.moc.cpp \
	util/textureproviderindirection.moc.cpp \
	util/sgmanipulator.moc.cpp \
	util/pointingtooltipattached.moc.cpp \
	widgets/native/animators.moc.cpp \
	widgets/native/csdthemeimage.moc.cpp \
	widgets/native/customwidgets.moc.cpp \
	widgets/native/embedded_file_browser.moc.cpp \
	util/interface_translator.moc.cpp \
	widgets/native/interface_widgets.moc.cpp \
	widgets/native/navigation_attached.moc.cpp \
	widgets/native/mlfolderseditor.moc.cpp \
	widgets/native/searchlineedit.moc.cpp \
	widgets/native/viewblockingrectangle.moc.cpp \
	widgets/native/softwarebackdropblur.moc.cpp

nodist_libqt_plugin_la_SOURCES += \
	dialogs/extended/ui_video_effects.h \
	dialogs/fingerprint/ui_fingerprintdialog.h \
	dialogs/help/ui_about.h \
	dialogs/help/ui_update.h \
	dialogs/messages/ui_messages_panel.h \
	dialogs/open/ui_open.h \
	dialogs/open/ui_open_capture.h \
	dialogs/open/ui_open_disk.h \
	dialogs/open/ui_open_file.h \
	dialogs/open/ui_open_net.h \
	dialogs/podcast/ui_podcast_configuration.h \
	dialogs/preferences/ui_sprefs_audio.h \
	dialogs/preferences/ui_sprefs_input.h \
	dialogs/preferences/ui_sprefs_interface.h \
	dialogs/preferences/ui_sprefs_medialibrary.h \
	dialogs/preferences/ui_sprefs_subtitles.h \
	dialogs/preferences/ui_sprefs_video.h \
	dialogs/sout/ui_profiles.h \
	dialogs/sout/ui_sout.h \
	dialogs/vlm/ui_vlm.h


# windows compositor

if HAVE_WIN32

libqt_plugin_la_SOURCES += \
	maininterface/mainctx_win32.cpp \
	maininterface/mainctx_win32.hpp \
	maininterface/compositor_win7.cpp \
	maininterface/compositor_win7.hpp \
	style/windowsthemeprovider.cpp \
	maininterface/win32windoweffects_module.cpp \
	maininterface/win32windoweffects_module.hpp \
	util/csdmenu_win32.c \
	util/csdmenu_win32.h \
	util/win32_emutls.cpp

nodist_libqt_plugin_la_SOURCES += maininterface/mainctx_win32.moc.cpp \
	maininterface/compositor_win7.moc.cpp

libqt_plugin_la_SOURCES += \
	maininterface/compositor_dcomp.cpp \
	maininterface/compositor_dcomp.hpp \
	maininterface/compositor_dcomp_background.hpp \
	maininterface/compositor_dcomp_backdrop.cpp maininterface/compositor_dcomp_backdrop.hpp \
	maininterface/compositor_dcomp_error.hpp \
	maininterface/compositor_dcomp_acrylicsurface.hpp \
	maininterface/compositor_dcomp_acrylicsurface.cpp

nodist_libqt_plugin_la_SOURCES += \
	maininterface/compositor_dcomp_acrylicsurface.moc.cpp \
	maininterface/compositor_dcomp.moc.cpp

endif

# X11 compositor

if HAVE_X
if HAVE_QT_XCB
if HAVE_XCB
if HAVE_XCB_DAMAGE
if HAVE_XCB_XFIXES

libqt_plugin_la_CXXFLAGS += -DQT_HAS_X11_COMPOSITOR \
	$(QT_X11_CFLAGS) $(XCB_RENDER_CFLAGS) $(XCB_COMPOSITE_CFLAGS) $(XCB_DAMAGE_CFLAGS) $(XCB_XFIXES_CFLAGS)

libqt_plugin_la_LIBADD += $(QT_X11_LIBS) $(XCB_RENDER_LIBS) $(XCB_COMPOSITE_LIBS) $(XCB_DAMAGE_LIBS) $(XCB_XFIXES_LIBS)

libqt_plugin_la_SOURCES += \
	maininterface/compositor_x11.cpp \
	maininterface/compositor_x11.hpp \
	maininterface/compositor_x11_backdrop.cpp \
	maininterface/compositor_x11_backdrop.hpp \
	maininterface/compositor_x11_renderclient.cpp \
	maininterface/compositor_x11_renderclient.hpp \
	maininterface/compositor_x11_renderwindow.cpp \
	maininterface/compositor_x11_renderwindow.hpp \
	maininterface/compositor_x11_uisurface.cpp \
	maininterface/compositor_x11_uisurface.hpp \
	maininterface/compositor_x11_utils.cpp \
	maininterface/compositor_x11_utils.hpp

nodist_libqt_plugin_la_SOURCES += \
	maininterface/compositor_x11.moc.cpp \
	maininterface/compositor_x11_renderclient.moc.cpp \
	maininterface/compositor_x11_renderwindow.moc.cpp \
	maininterface/compositor_x11_uisurface.moc.cpp

#extra X11 features

libqt_x11_plugin_la_CXXFLAGS = $(AM_CXXFLAGS) $(XCB_CFLAGS) $(XCB_RENDER_CFLAGS) $(XCB_COMPOSITE_CFLAGS) $(XCB_DAMAGE_CFLAGS) $(XCB_XFIXES_CFLAGS)
libqt_x11_plugin_la_LIBADD = $(XCB_LIBS) $(XCB_RENDER_LIBS) $(XCB_COMPOSITE_LIBS) $(XCB_DAMAGE_LIBS) $(XCB_XFIXES_LIBS)
libqt_x11_plugin_la_SOURCES = \
	maininterface/qt_x11_plugin.c \
	maininterface/compositor_x11_utils.hpp \
	maininterface/compositor_x11_utils.cpp \
	util/csdmenu_module.h \
	util/csdmenu_x11.cpp \
	util/csdmenu_x11.h

if ENABLE_QT
gui_PLUGINS += libqt_x11_plugin.la
endif

endif
endif
endif
endif
endif

# wayland compositor

if HAVE_WAYLAND
if HAVE_QT_GUI_PRIVATE

libqt_plugin_la_CPPFLAGS += -DQT_HAS_WAYLAND_COMPOSITOR

libqt_plugin_la_SOURCES += \
	maininterface/compositor_wayland.cpp \
	maininterface/compositor_wayland.hpp


nodist_libqt_plugin_la_SOURCES += \
	maininterface/compositor_wayland.moc.cpp

libqt_wayland_plugin_la_CFLAGS = $(AM_CFLAGS) $(WAYLAND_CLIENT_CFLAGS)
libqt_wayland_plugin_la_LIBADD =  $(WAYLAND_CLIENT_LIBS) $(LIBM)
libqt_wayland_plugin_la_SOURCES = \
	maininterface/qt_wayland_plugin.c \
	maininterface/compositor_wayland_module.c \
	maininterface/compositor_wayland_module.h

#if WAYLAND_PROTOCOLS
#if WAYLAND_SCANNER
libqt_wayland_plugin_la_CFLAGS += -DQT_HAS_WAYLAND_PROTOCOLS -I$(builddir)/maininterface

libqt_wayland_plugin_la_SOURCES += \
	util/csdmenu_wayland.c \
	util/csdmenu_wayland.h \
	util/csdmenu_module.h

nodist_libqt_wayland_plugin_la_SOURCES = \
	maininterface/xdg-shell-client-protocol.h \
	maininterface/xdg-shell-protocol.c

if HAVE_QT65
libqt_wayland_plugin_la_CFLAGS += -DQT_HAS_WAYLAND_FRACTIONAL_SCALING
nodist_libqt_wayland_plugin_la_SOURCES += \
	maininterface/viewporter-client-protocol.h \
	maininterface/viewporter-protocol.c

maininterface/viewporter-client-protocol.h: \
		$(WAYLAND_PROTOCOLS)/stable/viewporter/viewporter.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header $< $@

maininterface/viewporter-protocol.c: \
		$(WAYLAND_PROTOCOLS)/stable/viewporter/viewporter.xml \
		maininterface/viewporter-client-protocol.h
	$(AM_V_GEN)$(WAYLAND_SCANNER) private-code $< $@
endif

BUILT_SOURCES += $(nodist_libqt_wayland_plugin_la_SOURCES)

maininterface/xdg-shell-client-protocol.h: $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header $< $@

maininterface/xdg-shell-protocol.c: $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) private-code $< $@

#endif
#endif

if ENABLE_QT
gui_PLUGINS += libqt_wayland_plugin.la
endif

endif
endif

if !HAVE_WIN32
if HAVE_KF6WINDOWSYSTEM

libqt_plugin_la_CPPFLAGS += -DQT_HAS_KWINDOWSYSTEM_PLUGIN

libqt_kwindowsystem_plugin_la_CXXFLAGS = $(AM_CXXFLAGS) $(KF6WINDOWSYSTEM_CFLAGS)
libqt_kwindowsystem_plugin_la_LIBADD =  $(KF6WINDOWSYSTEM_LIBS) $(QT_LIBS)
libqt_kwindowsystem_plugin_la_LDFLAGS = $(AM_LDFLAGS) $(QT_LDFLAGS)
libqt_kwindowsystem_plugin_la_SOURCES = \
	maininterface/kwindowsystem_module.cpp

if ENABLE_QT
gui_PLUGINS += libqt_kwindowsystem_plugin.la
endif
endif
endif

# User interface compilation

uic_verbose = $(uic_verbose_$(V))
uic_verbose_ = $(uic_verbose__$(AM_DEFAULT_VERBOSITY))
uic_verbose_0 = @echo "  UIC     " $@;
uic_verbose__0 = $(uic_verbose_0)

MOC_CPPFLAGS = $(DEFS) \
	-I$(top_srcdir)/include -I$(top_builddir)/include -I$(top_builddir) \
	$(QT_MOC_INCLUDE_DIRS) \
	$(QT_CFLAGS) \
	$(libqt_plugin_la_CPPFLAGS)
if HAVE_DARWIN
MOC_CPPFLAGS += -DQ_OS_MAC
endif
if HAVE_WIN32
MOC_CPPFLAGS += -D_WIN32
endif

libqt_plugin_la_UI = \
	dialogs/extended/video_effects.ui \
	dialogs/fingerprint/fingerprintdialog.ui \
	dialogs/help/about.ui \
	dialogs/help/update.ui \
	dialogs/messages/messages_panel.ui \
	dialogs/open/open.ui \
	dialogs/open/open_capture.ui \
	dialogs/open/open_disk.ui \
	dialogs/open/open_file.ui \
	dialogs/open/open_net.ui \
	dialogs/podcast/podcast_configuration.ui \
	dialogs/preferences/sprefs_audio.ui \
	dialogs/preferences/sprefs_input.ui \
	dialogs/preferences/sprefs_interface.ui \
	dialogs/preferences/sprefs_medialibrary.ui \
	dialogs/preferences/sprefs_subtitles.ui \
	dialogs/preferences/sprefs_video.ui \
	dialogs/sout/profiles.ui \
	dialogs/sout/sout.ui \
	dialogs/vlm/vlm.ui
EXTRA_DIST += $(libqt_plugin_la_UI)

ui_%.h: %.ui
	$(AM_V_at)$(MKDIR_P) "ui"
	$(AM_V_at)rm -f $@ $@.tmp
	$(AM_V_at)echo "#define Q_(a,b) QString::fromUtf8(_(a))" > $@.tmp
	$(uic_verbose)$(UIC) -tr "Q_" $< >> $@.tmp
	$(AM_V_at)sed -e 's/Q_(\"_(\\\"\(.*\)\\\")"/Q_("\1"/' $@.tmp >$@
	$(AM_V_at)rm -f $@.tmp

# Resources

EXTRA_DIST += fonts/README.md

libqt_plugin_la_RES = \
	fonts/DMMono-Regular.ttf \
	fonts/DMMono-LICENSE.txt \
	fonts/Inter-Regular.otf \
	fonts/Inter-Light.otf \
	fonts/Inter-Medium.otf \
	fonts/Inter-SemiBold.otf \
	fonts/Inter-Bold.otf \
	fonts/Inter-Italic.otf \
	fonts/Inter-MediumItalic.otf \
	fonts/Inter-SemiBoldItalic.otf \
	fonts/Inter-BoldItalic.otf \
	fonts/LICENSE.txt \
	pixmaps/VLCIcons.ttf \
	pixmaps/addons/addon_default.svg \
	pixmaps/addons/addon_broken.svg \
	pixmaps/addons/addon_blue.svg \
	pixmaps/addons/addon_cyan.svg \
	pixmaps/addons/addon_green.svg \
	pixmaps/addons/addon_magenta.svg \
	pixmaps/addons/addon_red.svg \
	pixmaps/addons/addon_yellow.svg \
	pixmaps/addons/addon_score.svg \
	pixmaps/icons/active_indicator.svg \
	pixmaps/icons/add.svg \
	pixmaps/icons/ic_album_cover.svg \
	pixmaps/icons/ic_aspect_ratio.svg \
	pixmaps/icons/atob.svg \
	pixmaps/icons/ic_audiosub.svg \
	pixmaps/icons/back.svg \
	pixmaps/icons/ic_fluent_bookmark.svg \
	pixmaps/icons/breadcrumb_prev.svg \
	pixmaps/icons/breadcrumb_sep.svg \
	pixmaps/icons/check.svg \
	pixmaps/icons/chevron-down-dark.svg \
	pixmaps/icons/chevron-down-light.svg \
	pixmaps/icons/chevron-down-dark-disabled.svg \
	pixmaps/icons/chevron-down-light-disabled.svg \
	pixmaps/icons/clear.svg \
	pixmaps/icons/close.svg \
	pixmaps/icons/ic_fluent_full_screen_minimize.svg \
	pixmaps/icons/del.svg \
	pixmaps/icons/download.svg \
	pixmaps/icons/dropzone.svg \
	pixmaps/icons/dvd_menu.svg \
	pixmaps/icons/dvd_next.svg \
	pixmaps/icons/dvd_prev.svg \
	pixmaps/icons/ic_fluent_arrow_eject.svg \
	pixmaps/icons/ellipsis.svg \
	pixmaps/icons/ic_playlist_add.svg \
	pixmaps/icons/expand_inverted.svg \
	pixmaps/icons/expand.svg \
	pixmaps/icons/ic_fluent_options.svg \
	pixmaps/icons/ic_fluent_fast_forward.svg \
	pixmaps/icons/ic_fluent_next_frame.svg \
	pixmaps/icons/ic_fluent_previous_frame.svg \
	pixmaps/icons/ic_fluent_full_screen_maximize.svg \
	pixmaps/icons/ic_fluent_grid.svg \
	pixmaps/icons/history.svg \
	pixmaps/icons/ic_fluent_home.svg \
	pixmaps/icons/ic_fluent_info.svg \
	pixmaps/icons/copy.svg \
	pixmaps/icons/ic_fluent_apps_list.svg \
	pixmaps/icons/ic_fluent_more_vertical.svg \
	pixmaps/icons/multiselect.svg \
	pixmaps/icons/ok.svg \
	pixmaps/icons/ic_fluent_next.svg \
	pixmaps/icons/ic_pause_filled.svg \
	pixmaps/icons/pip.svg \
	pixmaps/icons/ic_playlist_clear.svg \
	pixmaps/icons/ic_playlist.svg \
	pixmaps/icons/ic_fluent_play.svg \
	pixmaps/icons/ic_fluent_play_reverse_filled.svg \
	pixmaps/icons/ic_fluent_arrow_move.svg \
	pixmaps/icons/ic_fluent_chevron_down_24.svg \
	pixmaps/icons/ic_fluent_chevron_left_24.svg \
	pixmaps/icons/ic_fluent_chevron_right_24.svg \
	pixmaps/icons/ic_fluent_chevron_up_24.svg \
	pixmaps/icons/ic_fluent_play_filled.svg \
	pixmaps/icons/podcast.svg \
	pixmaps/icons/ic_fluent_previous.svg \
	pixmaps/icons/profile_new.svg \
	pixmaps/icons/ic_fluent_record.svg \
	pixmaps/icons/remove.svg \
	pixmaps/icons/ic_fluent_cast.svg \
	pixmaps/icons/ic_fluent_arrow_repeat_all.svg \
	pixmaps/icons/ic_fluent_arrow_repeat_1.svg \
	pixmaps/icons/ic_fluent_rewind.svg \
	pixmaps/icons/ic_fluent_search.svg \
	pixmaps/icons/ic_fluent_arrow_shuffle.svg \
	pixmaps/icons/ic_fluent_skip_back_10.svg \
	pixmaps/icons/ic_fluent_skip_forward_10.svg \
	pixmaps/icons/ic_fluent_camera.svg \
	pixmaps/icons/ic_fluent_spacebar.svg \
	pixmaps/icons/ic_fluent_stop.svg \
	pixmaps/icons/stream.svg \
	pixmaps/icons/sync.svg \
	pixmaps/icons/ic_fluent_clock.svg \
	pixmaps/icons/ic_fluent_globe.svg \
	pixmaps/icons/topbar_filter.svg \
	pixmaps/icons/ic_fluent_music_note_2.svg \
	pixmaps/icons/ic_fluent_wifi_1.svg \
	pixmaps/icons/ic_fluent_arrow_sort.svg \
	pixmaps/icons/ic_fluent_filmstrip.svg \
	pixmaps/icons/ic_fluent_chevron_up.svg \
	pixmaps/icons/ic_fluent_chevron_down.svg \
	pixmaps/icons/ic_fluent_tv.svg \
	pixmaps/icons/ic_tvtelx.svg \
	pixmaps/icons/visualization.svg \
	pixmaps/icons/volume_high.svg \
	pixmaps/icons/volume_low.svg \
	pixmaps/icons/volume_medium.svg \
	pixmaps/icons/volume_muted.svg \
	pixmaps/icons/volume_zero.svg \
	pixmaps/icons/window_close.svg \
	pixmaps/icons/window_maximize.svg \
	pixmaps/icons/window_minimize.svg \
	pixmaps/icons/window_restore.svg \
	pixmaps/icons/ic_fluent_arrow_download_24_regular.svg \
	pixmaps/icons/ic_fluent_arrow_sync_24_regular.svg \
	pixmaps/icons/regex.svg \
	pixmaps/icons/ic_fluent_text_change_case_24_regular.svg \
	pixmaps/menu/redesign/play.svg \
	pixmaps/menu/redesign/stop.svg \
	pixmaps/menu/redesign/previous.svg \
	pixmaps/menu/redesign/next.svg \
	pixmaps/menu/redesign/record.svg \
	pixmaps/menu/redesign/hide-ui.svg \
	pixmaps/menu/redesign/playback.svg \
	pixmaps/menu/redesign/video.svg \
	pixmaps/menu/redesign/audio.svg \
	pixmaps/menu/redesign/music.svg \
	pixmaps/menu/redesign/subtitle.svg \
	pixmaps/menu/redesign/settings.svg \
	pixmaps/menu/redesign/playlist.svg \
	pixmaps/menu/redesign/open.svg \
	pixmaps/menu/redesign/quit.svg \
	pixmaps/menu/redesign/remove.svg \
	pixmaps/menu/redesign/refresh.svg \
	pixmaps/menu/add.svg \
	pixmaps/menu/capture-card.svg \
	pixmaps/menu/clear.svg \
	pixmaps/menu/disc.svg \
	pixmaps/menu/download.svg \
	pixmaps/menu/ic_fluent_arrow_eject.svg \
	pixmaps/menu/exit.svg \
	pixmaps/menu/ic_fluent_fast_forward.svg \
	pixmaps/menu/file.svg \
	pixmaps/menu/folder.svg \
	pixmaps/menu/help.svg \
	pixmaps/menu/info.svg \
	pixmaps/menu/media_library.svg \
	pixmaps/menu/messages.svg \
	pixmaps/menu/movie.svg \
	pixmaps/menu/music.svg \
	pixmaps/menu/network.svg \
	pixmaps/menu/ic_playlist.svg \
	pixmaps/menu/ic_fluent_next.svg \
	pixmaps/menu/ic_pause_filled.svg \
	pixmaps/menu/ic_fluent_play_filled.svg \
	pixmaps/menu/preferences.svg \
	pixmaps/menu/ic_fluent_previous.svg \
	pixmaps/menu/profile_new.svg \
	pixmaps/menu/ic_fluent_record.svg \
	pixmaps/menu/remove.svg \
	pixmaps/menu/ic_fluent_arrow_repeat_all.svg \
	pixmaps/menu/repeat_off.svg \
	pixmaps/menu/ic_fluent_arrow_repeat_1.svg \
	pixmaps/menu/ic_fluent_rewind.svg \
	pixmaps/menu/search_clear.svg \
	pixmaps/menu/ic_fluent_options.svg \
	pixmaps/menu/ic_fluent_skip_back_10.svg \
	pixmaps/menu/ic_fluent_skip_forward_10.svg \
	pixmaps/menu/ic_fluent_arrow_shuffle.svg \
	pixmaps/menu/ic_fluent_stop.svg \
	pixmaps/menu/stream.svg \
	pixmaps/menu/sync.svg \
	pixmaps/menu/update.svg \
	pixmaps/menu/volume-high.svg \
	pixmaps/menu/volume-low.svg \
	pixmaps/menu/volume-muted.svg \
	pixmaps/misc/cone.svg \
	pixmaps/misc/new_indicator.svg \
	pixmaps/misc/theme_dark.svg \
	pixmaps/misc/theme_daynight.svg \
	pixmaps/misc/theme_light.svg \
	pixmaps/misc/wait1.svg \
	pixmaps/misc/wait2.svg \
	pixmaps/misc/wait3.svg \
	pixmaps/misc/wait4.svg \
	pixmaps/placeholder/add_service.svg \
	pixmaps/placeholder/noart.png \
	pixmaps/placeholder/noart_album.svg \
	pixmaps/placeholder/noart_albumCover.svg \
	pixmaps/placeholder/noart_artist.svg \
	pixmaps/placeholder/noart_artistCover.svg \
	pixmaps/placeholder/noart_artist_small.svg \
	pixmaps/placeholder/noart_videoCover.svg \
	pixmaps/prefs/advprefs_audio.svg \
	pixmaps/prefs/advprefs_codec.svg \
	pixmaps/prefs/advprefs_extended.svg \
	pixmaps/prefs/advprefs_intf.svg \
	pixmaps/prefs/advprefs_playlist.svg \
	pixmaps/prefs/advprefs_sout.svg \
	pixmaps/prefs/advprefs_video.svg \
	pixmaps/prefs/sample_complete.png \
	pixmaps/prefs/sample_minimal.png \
	pixmaps/prefs/sample_skins.png \
	pixmaps/prefs/spref_audio.png \
	pixmaps/prefs/spref_hotkeys.png \
	pixmaps/prefs/spref_input.png \
	pixmaps/prefs/spref_interface.png \
	pixmaps/prefs/spref_medialibrary.png \
	pixmaps/prefs/spref_subtitles.png \
	pixmaps/prefs/spref_video.png \
	pixmaps/sd/assembleenationale.svg \
	pixmaps/sd/capture-card.svg \
	pixmaps/sd/directory.svg \
	pixmaps/sd/disc.svg \
	pixmaps/sd/file.svg \
	pixmaps/sd/fmc.svg \
	pixmaps/sd/frenchtv.svg \
	pixmaps/sd/harddisk.svg \
	pixmaps/sd/icecast.svg \
	pixmaps/sd/jamendo.svg \
	pixmaps/sd/katsomo.svg \
	pixmaps/sd/lan.svg \
	pixmaps/sd/library.svg \
	pixmaps/sd/metachannels.svg \
	pixmaps/sd/mtp.svg \
	pixmaps/sd/network.svg \
	pixmaps/sd/node.svg \
	pixmaps/sd/pictures.svg \
	pixmaps/sd/playlist.svg \
	pixmaps/sd/podcast.svg \
	pixmaps/sd/radio.svg \
	pixmaps/sd/screen.svg \
	pixmaps/sd/stream.svg \
	pixmaps/sd/unknown.svg

if HAVE_WIN32
libqt_plugin_la_RES += \
	pixmaps/win7/pause.svg \
	pixmaps/win7/prev.svg \
	pixmaps/win7/next.svg \
	pixmaps/win7/play.svg
endif

# QML modules
#
# Each QML module has a small section below. To add a new module "foo", append
# a section following this template:
#
#   libqml_module_foo_a_Module     = VLC.Foo
#   libqml_module_foo_a_ModulePath = VLC/Foo
#   libqml_module_foo_a_CXXFLAGS   = $(libqt_plugin_la_CXXFLAGS)
#   libqml_module_foo_a_CPPFLAGS   = $(libqt_plugin_la_CPPFLAGS)
#   libqml_module_foo_a_SOURCES    = $(QML_MODULE_foo_SRC)
#   libqml_module_foo_a_QML 			 = foo/qml/Bar.qml # ...
#   libqml_module_foo_a_Singleton_QML  = foo/qml/SingletonBar.qml # only when there are Singletons
#   nodist_libqml_module_foo_a_SOURCES = foo/qmlassets.cpp $(QML_MODULE_foo_NODIST_EXTRA)
#   QML_MODULE_LIBS += libqml_module_foo.a
#
# When exposing singletons, list those QML files in the proper variable so that
# they are declared as singletons in the generated qmldir.
#
# Then:
#   1. Add "foo" to libqt_plugin_la_QML_MODULES.
#   2. Add the QML file lists to libqt_plugin_la_ALL_QML.
#   3. Add a QML_MODULE_foo_SRC / QML_MODULE_foo_NODIST_EXTRA pair to the
#      "if HAVE_QMLCACHEGEN" block below.
#
noinst_LIBRARIES =
QML_MODULE_LIBS =

libqml_module_dialogs_a_Module = VLC.Dialogs
libqml_module_dialogs_a_ModulePath = VLC/Dialogs
libqml_module_dialogs_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_dialogs_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_dialogs_a_QML = \
	dialogs/dialogs/qml/CustomDialog.qml \
	dialogs/dialogs/qml/Dialogs.qml \
	dialogs/dialogs/qml/ModalDialog.qml \
	dialogs/dialogs/qml/MessageNotice.qml \
	dialogs/dialogs/qml/WindowDialog.qml \
	dialogs/toolbar/qml/EditorDNDDelegate.qml \
	dialogs/toolbar/qml/EditorDNDView.qml\
	dialogs/toolbar/qml/EditorDummyButton.qml \
	dialogs/toolbar/qml/EditorTabButton.qml \
	dialogs/toolbar/qml/ToolbarEditor.qml \
	dialogs/toolbar/qml/ToolbarEditorDialog.qml \
	dialogs/toolbar/qml/ToolbarEditorButtonList.qml
libqml_module_dialogs_a_Singleton_QML =
libqml_module_dialogs_a_SOURCES = $(QML_MODULE_dialogs_SRC)
nodist_libqml_module_dialogs_a_SOURCES = dialogs/qmlassets.cpp $(QML_MODULE_dialogs_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_dialogs.a

libqml_module_maininterface_a_Module = VLC.MainInterface
libqml_module_maininterface_a_ModulePath = VLC/MainInterface
libqml_module_maininterface_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_maininterface_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_maininterface_a_QML = \
	maininterface/qml/MenuTopbar.qml \
	maininterface/qml/LocalTopbar.qml \
	maininterface/qml/MainInterface.qml \
	maininterface/qml/MainViewLoader.qml \
	maininterface/qml/MainDisplay.qml \
	maininterface/qml/SideNavigationPane.qml \
	maininterface/qml/SideNavigationDelegate.qml \
	maininterface/qml/NoMedialibHome.qml \
	maininterface/qml/OpenFileSplitButton.qml

if QT_UPDATE_CHECK
libqml_module_maininterface_a_QML += maininterface/qml/UpdatePane.qml
endif

libqml_module_maininterface_a_Singleton_QML =
libqml_module_maininterface_a_SOURCES = $(QML_MODULE_maininterface_SRC)
nodist_libqml_module_maininterface_a_SOURCES = maininterface/qmlassets.cpp $(QML_MODULE_maininterface_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_maininterface.a

libqml_module_medialibrary_a_Module = VLC.MediaLibrary
libqml_module_medialibrary_a_ModulePath = VLC/MediaLibrary
libqml_module_medialibrary_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_medialibrary_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_medialibrary_a_QML = \
	medialibrary/qml/ArtistTopBanner.qml \
	medialibrary/qml/AudioGridItem.qml \
	medialibrary/qml/HomeDisplay.qml \
	medialibrary/qml/HomePage.qml \
	medialibrary/qml/MediaView.qml \
	medialibrary/qml/VideoGroupDisplay.qml \
	medialibrary/qml/MusicAlbums.qml \
	medialibrary/qml/MusicAlbumsDisplay.qml \
	medialibrary/qml/MusicAlbumsGridExpandDelegate.qml \
	medialibrary/qml/MusicArtist.qml \
	medialibrary/qml/MusicArtistDelegate.qml \
	medialibrary/qml/MusicArtistsAlbums.qml \
	medialibrary/qml/MusicAllArtists.qml \
	medialibrary/qml/MusicArtistsDisplay.qml \
	medialibrary/qml/MusicDisplay.qml \
	medialibrary/qml/MusicGenres.qml \
	medialibrary/qml/MusicGenresDisplay.qml \
	medialibrary/qml/MusicPlaylistsDisplay.qml \
	medialibrary/qml/MusicTrackListDisplay.qml \
	medialibrary/qml/MusicTracksDisplay.qml \
	medialibrary/qml/UrlListDisplay.qml \
	medialibrary/qml/VideoDisplay.qml \
	medialibrary/qml/VideoAll.qml \
	medialibrary/qml/VideoAllDisplay.qml \
	medialibrary/qml/VideoAllSubDisplay.qml \
	medialibrary/qml/PlaylistMediaList.qml \
	medialibrary/qml/PlaylistMedia.qml \
	medialibrary/qml/PlaylistMediaDisplay.qml \
	medialibrary/qml/VideoPlaylistsDisplay.qml \
	medialibrary/qml/VideoGridItem.qml \
	medialibrary/qml/VideoInfoExpandPanel.qml \
	medialibrary/qml/VideoListDisplay.qml \
	medialibrary/qml/VideoGridDisplay.qml \
	medialibrary/qml/MusicAlbumSectionDelegate.qml \
	medialibrary/qml/MLGlobalShortcuts.qml \
	medialibrary/qml/Bookmarks.qml \
	medialibrary/qml/MLContextMenu.qml \
	medialibrary/qml/MLDragItem.qml \
	medialibrary/qml/MLTableColumns.qml \
	medialibrary/qml/ScanProgressBar.qml
libqml_module_medialibrary_a_Singleton_QML =
libqml_module_medialibrary_a_SOURCES = $(QML_MODULE_medialibrary_SRC)
nodist_libqml_module_medialibrary_a_SOURCES = medialibrary/qmlassets.cpp $(QML_MODULE_medialibrary_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_medialibrary.a

libqml_module_menus_a_Module = VLC.Menus
libqml_module_menus_a_ModulePath = VLC/Menus
libqml_module_menus_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_menus_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_menus_a_QML = \
	menus/qml/GlobalShortcuts.qml \
	menus/qml/Menubar.qml \
	menus/qml/ShortcutExt.qml
libqml_module_menus_a_Singleton_QML =
libqml_module_menus_a_SOURCES = $(QML_MODULE_menus_SRC)
nodist_libqml_module_menus_a_SOURCES = menus/qmlassets.cpp $(QML_MODULE_menus_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_menus.a

libqml_module_network_a_Module = VLC.Network
libqml_module_network_a_ModulePath = VLC/Network
libqml_module_network_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_network_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_network_a_QML = \
	network/qml/AddressbarButton.qml \
	network/qml/BrowseDeviceView.qml \
	network/qml/BrowseDisplay.qml \
	network/qml/BrowseHomeDisplay.qml \
	network/qml/BrowseTreeDisplay.qml \
	network/qml/BrowseTreeHeader.qml \
	network/qml/DiscoverDisplay.qml \
	network/qml/DiscoverUrlDisplay.qml \
	network/qml/NetworkAddressbar.qml \
	network/qml/NetworkGridItem.qml \
	network/qml/NetworkThumbnailItem.qml \
	network/qml/ServicesHomeDisplay.qml \
	network/qml/ServicesManage.qml \
	network/qml/ServicesSources.qml
libqml_module_network_a_Singleton_QML =
libqml_module_network_a_SOURCES = $(QML_MODULE_network_SRC)
nodist_libqml_module_network_a_SOURCES = network/qmlassets.cpp $(QML_MODULE_network_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_network.a

libqml_module_player_a_Module = VLC.Player
libqml_module_player_a_ModulePath = VLC/Player
libqml_module_player_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_player_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_player_a_QML = \
	player/qml/ControlBar.qml \
	player/qml/redesign/RedesignBookmarks.qml \
	player/qml/redesign/RedesignBookmarksView.qml \
	player/qml/redesign/RedesignChaptersView.qml \
	player/qml/redesign/RedesignEpisodesView.qml \
	player/qml/redesign/RedesignSeriesMetadata.qml \
	player/qml/redesign/RedesignQueuePanel.qml \
	player/qml/redesign/RedesignLibrary.qml \
	player/qml/redesign/RedesignLibraryUnavailable.qml \
	player/qml/redesign/RedesignPlayer.qml \
	player/qml/redesign/RedesignPlayerLayout.qml \
	player/qml/redesign/RedesignPlayerChrome.qml \
	player/qml/redesign/RedesignCustomizationCatalog.qml \
	player/qml/redesign/RedesignCustomizationEditor.qml \
	player/qml/redesign/RedesignExtraControl.qml \
	player/qml/redesign/RedesignMediaFavorite.qml \
	player/qml/redesign/RedesignPlayerFavorite.qml \
	player/qml/redesign/RedesignPreferencesCatalog.qml \
	player/qml/redesign/RedesignPreferenceRow.qml \
	player/qml/redesign/RedesignComboBox.qml \
	player/qml/redesign/RedesignEqualizer.qml \
	player/qml/redesign/RedesignSpatializer.qml \
	player/qml/redesign/RedesignStartupVolume.qml \
	player/qml/redesign/RedesignConfirmationDialog.qml \
	player/qml/redesign/RedesignGotoTimeDialog.qml \
	player/qml/redesign/RedesignTime.qml \
	player/qml/redesign/RedesignNetworkDialog.qml \
	player/qml/redesign/RedesignPlaylistDialog.qml \
	player/qml/redesign/RedesignPlaylistProperties.qml \
	player/qml/redesign/RedesignPlaylistTrackTarget.qml \
	player/qml/redesign/RedesignTrackAssignmentDialog.qml \
	player/qml/redesign/RedesignSubtitleProviderSetup.qml \
	player/qml/redesign/RedesignSubtitleSourcePreferences.qml \
	player/qml/redesign/RedesignAddons.qml \
	player/qml/redesign/RedesignAddonChild.qml \
	player/qml/redesign/RedesignAddonField.qml \
	player/qml/redesign/RedesignAddonManagement.qml \
	player/qml/redesign/RedesignAddonDiscovery.qml \
	player/qml/redesign/RedesignAddonDiscoveryWindow.qml \
	player/qml/redesign/RedesignAddonSessions.qml \
	player/qml/redesign/RedesignAddonSourceConsentDialog.qml \
	player/qml/redesign/RedesignAddonSelection.qml \
	player/qml/redesign/RedesignAddonRecovery.qml \
	player/qml/redesign/RedesignAddonNode.qml \
	player/qml/redesign/RedesignAddonAssetImage.qml \
	player/qml/redesign/RedesignAddonProviders.qml \
	player/qml/redesign/RedesignAddonSettings.qml \
	player/qml/redesign/RedesignAddonSettingsDialog.qml \
	player/qml/redesign/RedesignAddonTheme.qml \
	player/qml/redesign/RedesignAddonThemeSelector.qml \
	player/qml/redesign/RedesignGeneralAppearance.qml \
	player/qml/redesign/RedesignAddonTrackPreparation.qml \
	player/qml/redesign/RedesignAddonView.qml \
	player/qml/redesign/RedesignAddonMount.qml \
	player/qml/redesign/RedesignAddonSections.qml \
	player/qml/redesign/RedesignAddonSurface.qml \
	player/qml/redesign/RedesignAddonReplacements.qml \
	player/qml/redesign/RedesignAddonWindow.qml \
	player/qml/redesign/RedesignAddonVisualizerSelector.qml \
	player/qml/redesign/AddonUi.js \
	player/qml/redesign/AddonSelectionContracts.js \
	player/qml/redesign/AddonTheme.js \
	player/qml/redesign/RedesignPlugins.qml \
	player/qml/redesign/RedesignTrackAssignments.qml \
	player/qml/redesign/RedesignFolderInfo.qml \
	player/qml/redesign/RedesignPodcastInfo.qml \
	player/qml/redesign/RedesignAddToPlaylistDialog.qml \
	player/qml/redesign/RedesignAddToPlaylist.qml \
	player/qml/redesign/RedesignNetworkStreams.qml \
	player/qml/redesign/RedesignNetworkStreamLibrary.qml \
	player/qml/redesign/RedesignPreferencesWindow.qml \
	player/qml/redesign/RedesignSettingSlider.qml \
	player/qml/redesign/RedesignSleepTimer.qml \
	player/qml/redesign/RedesignModelChoice.qml \
	player/qml/redesign/RedesignPlaybackSettings.qml \
	player/qml/redesign/RedesignTrackDefaults.qml \
	player/qml/redesign/RedesignAudioSettings.qml \
	player/qml/redesign/RedesignSubtitleSettings.qml \
	player/qml/redesign/RedesignVideoSettings.qml \
	player/qml/redesign/RedesignOnboardingView.qml \
	player/qml/redesign/RedesignOnboardingWindow.qml \
	player/qml/redesign/RedesignFilterSection.qml \
	player/qml/redesign/RedesignSwitchRow.qml \
	player/qml/redesign/RedesignStatistics.qml \
	player/qml/redesign/RedesignSettings.qml \
	player/qml/redesign/RedesignPlayerPreset.qml \
	player/qml/redesign/RedesignTrackPopup.qml \
	player/qml/redesign/RedesignShortcutEditor.qml \
	player/qml/redesign/RedesignShortcuts.qml \
	player/qml/redesign/RedesignTracks.qml \
	player/qml/redesign/RedesignButton.qml \
	player/qml/redesign/RedesignToolTip.qml \
	player/qml/redesign/RedesignIcon.qml \
	player/qml/redesign/RedesignSvg.qml \
	player/qml/redesign/RedesignActionMenu.qml \
	player/qml/redesign/RedesignSurfaceBackdrop.qml \
	player/qml/redesign/RedesignSoftwareShadow.qml \
	player/qml/redesign/RedesignDialogSurface.qml \
	player/qml/redesign/RedesignSeparator.qml \
	player/qml/redesign/RedesignSettingsDialog.qml \
	player/qml/redesign/RedesignLibraryArtwork.qml \
	player/qml/redesign/RedesignAudioArtwork.qml \
	player/qml/redesign/RedesignLibraryView.qml \
	player/qml/redesign/RedesignBreadcrumbs.qml \
	player/qml/redesign/RedesignLibraryPath.qml \
	player/qml/redesign/RedesignLibraryPins.qml \
	player/qml/redesign/RedesignLibraryNavigation.qml \
	player/qml/redesign/RedesignHomeView.qml \
	player/qml/redesign/RedesignLibraryDrag.qml \
	player/qml/redesign/RedesignLibraryVisits.qml \
	player/qml/redesign/RedesignLibrarySort.qml \
	player/qml/redesign/RedesignMenu.qml \
	player/qml/redesign/RedesignRecentMenuModel.qml \
	player/qml/redesign/RedesignPlayerView.qml \
	player/qml/redesign/RedesignQueue.qml \
	player/qml/redesign/RedesignSeekBar.qml \
	player/qml/redesign/RedesignSeekThumbnail.qml \
	player/qml/redesign/RedesignPlayerSeekBar.qml \
	player/qml/redesign/RedesignMiniPlayer.qml \
	player/qml/redesign/RedesignTheme.qml \
	player/qml/redesign/RedesignVolume.qml \
	player/qml/PlaybackSpeed.qml \
	player/qml/LyricsFlickable.qml \
	player/qml/MiniPlayer.qml \
	player/qml/PIPPlayer.qml \
	player/qml/Player.qml \
	player/qml/PlayerControlLayout.qml \
	player/qml/PlayerPlaylistVisibilityFSM.qml \
	player/qml/PlayerToolbarVisibilityFSM.qml \
	player/qml/MinimalView.qml \
	player/qml/ResumeDialog.qml \
	player/qml/SliderBar.qml \
	player/qml/TopBar.qml \
	player/qml/TracksMenu.qml \
	player/qml/TracksPage.qml \
	player/qml/TracksPageAudio.qml \
	player/qml/TracksPageSpeed.qml \
	player/qml/TracksPageSubtitle.qml \
	player/qml/TracksListPage.qml \
	player/qml/ControlLayout.qml \
	player/qml/ControlRepeater.qml \
	player/qml/NavigationBox.qml

libqml_module_player_a_Singleton_QML = \
	player/qml/PlayerControlbarControls.qml
libqml_module_player_a_SOURCES = $(QML_MODULE_player_SRC)
nodist_libqml_module_player_a_SOURCES = player/qmlassets.cpp $(QML_MODULE_player_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_player.a

libqml_module_playercontrols_a_Module = VLC.PlayerControls
libqml_module_playercontrols_a_ModulePath = VLC/PlayerControls
libqml_module_playercontrols_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_playercontrols_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_playercontrols_a_QML = \
	player/qml/controlbarcontrols/HighResolutionTimeWidget.qml \
	player/qml/controlbarcontrols/ArtworkInfoWidget.qml \
	player/qml/controlbarcontrols/AspectRatioWidget.qml \
	player/qml/controlbarcontrols/AtoBButton.qml \
	player/qml/controlbarcontrols/BackButton.qml \
	player/qml/controlbarcontrols/BookmarkButton.qml \
	player/qml/controlbarcontrols/ChapterNextButton.qml \
	player/qml/controlbarcontrols/ChapterPreviousButton.qml \
	player/qml/controlbarcontrols/DvdMenuButton.qml \
	player/qml/controlbarcontrols/ExpandingSpacerWidget.qml \
	player/qml/controlbarcontrols/ExtendedSettingsButton.qml \
	player/qml/controlbarcontrols/FasterButton.qml \
	player/qml/controlbarcontrols/FrameNextButton.qml \
	player/qml/controlbarcontrols/FramePrevButton.qml \
	player/qml/controlbarcontrols/FullscreenButton.qml \
	player/qml/controlbarcontrols/InfoButton.qml \
	player/qml/controlbarcontrols/LangButton.qml \
	player/qml/controlbarcontrols/LoopButton.qml \
	player/qml/controlbarcontrols/MenuButton.qml \
	player/qml/controlbarcontrols/NavigationBoxButton.qml \
	player/qml/controlbarcontrols/NavigationWidget.qml \
	player/qml/controlbarcontrols/NextButton.qml \
	player/qml/controlbarcontrols/OpenButton.qml \
	player/qml/controlbarcontrols/PlaybackSpeedButton.qml \
	player/qml/controlbarcontrols/PlayButton.qml \
	player/qml/controlbarcontrols/PlayerSwitchButton.qml \
	player/qml/controlbarcontrols/PlaylistButton.qml \
	player/qml/controlbarcontrols/PreviousButton.qml \
	player/qml/controlbarcontrols/ProgramButton.qml \
	player/qml/controlbarcontrols/QuitButton.qml \
	player/qml/controlbarcontrols/RandomButton.qml \
	player/qml/controlbarcontrols/RendererButton.qml \
	player/qml/controlbarcontrols/RecordButton.qml \
	player/qml/controlbarcontrols/ReverseButton.qml \
	player/qml/controlbarcontrols/SkipBackButton.qml \
	player/qml/controlbarcontrols/SkipForwardButton.qml \
	player/qml/controlbarcontrols/SlowerButton.qml \
	player/qml/controlbarcontrols/SnapshotButton.qml \
	player/qml/controlbarcontrols/SpacerWidget.qml \
	player/qml/controlbarcontrols/StopButton.qml \
	player/qml/controlbarcontrols/TeletextButton.qml \
	player/qml/controlbarcontrols/TeletextColorButton.qml \
	player/qml/controlbarcontrols/TeletextWidget.qml \
	player/qml/controlbarcontrols/VolumeWidget.qml \
	player/qml/controlbarcontrols/Fallback.qml
libqml_module_playercontrols_a_Singleton_QML =
libqml_module_playercontrols_a_SOURCES = $(QML_MODULE_playercontrols_SRC)
nodist_libqml_module_playercontrols_a_SOURCES = playercontrols/qmlassets.cpp $(QML_MODULE_playercontrols_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_playercontrols.a

libqml_module_playlist_a_Module = VLC.Playlist
libqml_module_playlist_a_ModulePath = VLC/Playlist
libqml_module_playlist_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_playlist_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_playlist_a_QML = \
	playlist/qml/PlaylistDelegate.qml \
	playlist/qml/PlaylistPane.qml \
	playlist/qml/PlaylistDetachedWindow.qml
libqml_module_playlist_a_Singleton_QML =
libqml_module_playlist_a_SOURCES = $(QML_MODULE_playlist_SRC)
nodist_libqml_module_playlist_a_SOURCES = playlist/qmlassets.cpp $(QML_MODULE_playlist_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_playlist.a

libqml_module_style_a_Module = VLC.Style
libqml_module_style_a_ModulePath = VLC/Style
libqml_module_style_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_style_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_style_a_QML =
libqml_module_style_a_Singleton_QML = \
	style/AcrylicController.qml \
	style/VLCStyle.qml
libqml_module_style_a_SOURCES = $(QML_MODULE_style_SRC)
nodist_libqml_module_style_a_SOURCES = style/qmlassets.cpp $(QML_MODULE_style_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_style.a

libqml_module_util_a_Module = VLC.Util
libqml_module_util_a_ModulePath = VLC/Util
libqml_module_util_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_util_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_util_a_QML = \
	util/qml/ModelSortSettingHandler.qml \
	util/qml/DefaultFlickableScrollHandler.qml \
	util/qml/ViewDragAutoScrollHandler.qml \
	util/qml/FSM.qml \
	util/qml/FSMState.qml \
	util/qml/NativeMenu.qml \
	util/qml/FadeControllerStateGroup.qml \
	util/qml/GridSizeHelper.qml \
	util/qml/DelayedBehavior.qml \
	util/qml/DelegateTouchTapHandler.qml
libqml_module_util_a_Singleton_QML = \
	util/qml/Helpers.qml
libqml_module_util_a_SOURCES = $(QML_MODULE_util_SRC)
nodist_libqml_module_util_a_SOURCES = util/qmlassets.cpp $(QML_MODULE_util_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_util.a

libqml_module_widgets_a_Module = VLC.Widgets
libqml_module_widgets_a_ModulePath = VLC/Widgets
libqml_module_widgets_a_CXXFLAGS = $(libqt_plugin_la_CXXFLAGS)
libqml_module_widgets_a_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
libqml_module_widgets_a_QML = \
	widgets/qml/LabelExt.qml \
	widgets/qml/ActionButtonOverlay.qml \
	widgets/qml/ActionButtonPrimary.qml \
	widgets/qml/BannerTabButton.qml \
	widgets/qml/BannerCone.qml \
	widgets/qml/BusyIndicatorExt.qml \
	widgets/qml/CaptionLabel.qml \
	widgets/qml/CheckButton.qml \
	widgets/qml/CheckedDelegate.qml \
	widgets/qml/ComboBoxExt.qml \
	widgets/qml/AcrylicBackground.qml \
	widgets/qml/FloatingNotification.qml \
	widgets/qml/AnimatedBackground.qml \
	widgets/qml/CSDThemeButton.qml \
	widgets/qml/CSDThemeButtonSet.qml \
	widgets/qml/CSDWindowButton.qml \
	widgets/qml/CSDWindowButtonSet.qml \
	widgets/qml/CSDTitlebarTapNDrapHandler.qml \
	widgets/qml/CSDMouseStealer.qml \
	widgets/qml/CurrentIndicator.qml \
	widgets/qml/DragItem.qml \
	widgets/qml/DrawerExt.qml \
	widgets/qml/ExpandGridView.qml \
	widgets/qml/ExpandGridItemView.qml \
	widgets/qml/EmptyLabel.qml \
	widgets/qml/EmptyLabelButton.qml \
	widgets/qml/EmptyLabelHint.qml \
	widgets/qml/GridItem.qml \
	widgets/qml/GridSortFilterControls.qml \
	widgets/qml/HorizontalResizeHandle.qml \
	widgets/qml/IconLabel.qml \
	widgets/qml/IconButton.qml \
	widgets/qml/IconToolButton.qml \
	widgets/qml/IconTrackButton.qml \
	widgets/qml/ImageToolButton.qml \
	widgets/qml/ListViewExt.qml \
	widgets/qml/TableViewExt.qml \
	widgets/qml/TableViewDelegateExt.qml \
	widgets/qml/TableRowDelegate.qml \
	widgets/qml/TableHeaderDelegate.qml \
	widgets/qml/DefaultShadow.qml \
	widgets/qml/ListLabel.qml \
	widgets/qml/MediaCover.qml \
	widgets/qml/SubtitleLabel.qml \
	widgets/qml/MenuCaption.qml \
	widgets/qml/MenuLabel.qml \
	widgets/qml/NavigableCol.qml \
	widgets/qml/NavigableRow.qml \
	widgets/qml/PlayCover.qml \
	widgets/qml/RoundButtonExt.qml \
	widgets/qml/ScaledImage.qml \
	widgets/qml/TextAutoScroller.qml \
	widgets/qml/SearchBox.qml \
	widgets/qml/SliderExt.qml \
	widgets/qml/SortControl.qml \
	widgets/qml/SplitButton.qml \
	widgets/qml/SpinBoxExt.qml \
	widgets/qml/StackViewExt.qml \
	widgets/qml/PageLoader.qml \
	widgets/qml/PageExt.qml \
	widgets/qml/ButtonExt.qml \
	widgets/qml/TableColumns.qml \
	widgets/qml/TextFieldExt.qml \
	widgets/qml/TextToolButton.qml \
	widgets/qml/TrackDelayButton.qml \
	widgets/qml/VideoProgressBar.qml \
	widgets/qml/VideoQualityLabels.qml \
	widgets/qml/ListSubtitleLabel.qml \
	widgets/qml/PointingTooltip.qml \
	widgets/qml/FrostedGlassEffect.qml \
	widgets/qml/ToolTipExt.qml \
	widgets/qml/DropShadowImage.qml \
	widgets/qml/DoubleShadow.qml \
	widgets/qml/FadingEdge.qml \
	widgets/qml/FadingEdgeForItemView.qml \
	widgets/qml/PopupIconToolButton.qml \
	widgets/qml/PartialEffect.qml \
	widgets/qml/ViewHeader.qml \
	widgets/qml/ProgressIndicator.qml \
	widgets/qml/ImageExt.qml \
	widgets/qml/EnhancedImageExt.qml \
	widgets/qml/ScrollBarExt.qml \
	widgets/qml/FastBlend.qml \
	widgets/qml/RadioButtonExt.qml \
	widgets/qml/RoundedRectangleShadow.qml \
	widgets/qml/VoronoiSnow.qml \
	widgets/qml/DualKawaseBlur.qml \
	widgets/qml/BackdropBlur.qml \
	widgets/qml/CheckBoxExt.qml \
	widgets/qml/ProgressBarExt.qml
libqml_module_widgets_a_Singleton_QML =
libqml_module_widgets_a_SOURCES = $(QML_MODULE_widgets_SRC)
nodist_libqml_module_widgets_a_SOURCES = widgets/qmlassets.cpp $(QML_MODULE_widgets_NODIST_EXTRA)
QML_MODULE_LIBS += libqml_module_widgets.a

if ENABLE_QT
libqt_plugin_la_LIBADD += $(QML_MODULE_LIBS)
noinst_LIBRARIES += $(QML_MODULE_LIBS)
endif

if HAVE_QMLCACHEGEN
QML_MODULE_dialogs_SRC = $(libqml_module_dialogs_a_QML) $(libqml_module_dialogs_a_Singleton_QML)
QML_MODULE_dialogs_NODIST_EXTRA = dialogs/qmlcache_loader.cpp
QML_MODULE_maininterface_SRC = $(libqml_module_maininterface_a_QML) $(libqml_module_maininterface_a_Singleton_QML)
QML_MODULE_maininterface_NODIST_EXTRA = maininterface/qmlcache_loader.cpp
QML_MODULE_medialibrary_SRC = $(libqml_module_medialibrary_a_QML) $(libqml_module_medialibrary_a_Singleton_QML)
QML_MODULE_medialibrary_NODIST_EXTRA = medialibrary/qmlcache_loader.cpp
QML_MODULE_menus_SRC = $(libqml_module_menus_a_QML) $(libqml_module_menus_a_Singleton_QML)
QML_MODULE_menus_NODIST_EXTRA = menus/qmlcache_loader.cpp
QML_MODULE_network_SRC = $(libqml_module_network_a_QML) $(libqml_module_network_a_Singleton_QML)
QML_MODULE_network_NODIST_EXTRA = network/qmlcache_loader.cpp
QML_MODULE_player_SRC = $(libqml_module_player_a_QML) $(libqml_module_player_a_Singleton_QML)
QML_MODULE_player_NODIST_EXTRA = player/qmlcache_loader.cpp
QML_MODULE_playercontrols_SRC = $(libqml_module_playercontrols_a_QML) $(libqml_module_playercontrols_a_Singleton_QML)
QML_MODULE_playercontrols_NODIST_EXTRA = playercontrols/qmlcache_loader.cpp
QML_MODULE_playlist_SRC = $(libqml_module_playlist_a_QML) $(libqml_module_playlist_a_Singleton_QML)
QML_MODULE_playlist_NODIST_EXTRA = playlist/qmlcache_loader.cpp
QML_MODULE_style_SRC = $(libqml_module_style_a_QML) $(libqml_module_style_a_Singleton_QML)
QML_MODULE_style_NODIST_EXTRA = style/qmlcache_loader.cpp
QML_MODULE_util_SRC = $(libqml_module_util_a_QML) $(libqml_module_util_a_Singleton_QML)
QML_MODULE_util_NODIST_EXTRA = util/qmlcache_loader.cpp
QML_MODULE_widgets_SRC = $(libqml_module_widgets_a_QML) $(libqml_module_widgets_a_Singleton_QML)
QML_MODULE_widgets_NODIST_EXTRA = widgets/qmlcache_loader.cpp
endif


libqt_plugin_la_QML_MODULES = \
	dialogs \
	maininterface \
	medialibrary \
	menus \
	network \
	player \
	playercontrols \
	playlist \
	style \
	util \
	widgets

libqt_plugin_la_ALL_QML = \
	$(libqml_module_dialogs_a_QML) \
	$(libqml_module_maininterface_a_QML) \
	$(libqml_module_medialibrary_a_QML) \
	$(libqml_module_menus_a_QML) \
	$(libqml_module_network_a_QML) \
	$(libqml_module_player_a_QML) \
	$(libqml_module_player_a_Singleton_QML) \
	$(libqml_module_playercontrols_a_QML) \
	$(libqml_module_playlist_a_QML) \
	$(libqml_module_style_a_QML) \
	$(libqml_module_style_a_Singleton_QML) \
	$(libqml_module_util_a_QML) \
	$(libqml_module_util_a_Singleton_QML) \
	$(libqml_module_widgets_a_QML)

# Shaders:
libqt_plugin_la_SHADER_FRAG = shaders/FadingEdge.frag \
                              shaders/Noise.frag \
                              shaders/RectFilter.frag \
                              shaders/SDFAARoundedTexture.frag \
                              shaders/SDFAARoundedTexture_cropsupport_bordersupport.frag \
                              shaders/DitheredTexture.frag \
                              shaders/FastBlend.frag \
                              shaders/FastBlend_additive.frag \
                              shaders/FastBlend_multiply.frag \
                              shaders/FastBlend_screen.frag \
                              shaders/RoundedRectangleShadow.frag \
                              shaders/RoundedRectangleShadow_hollow.frag \
                              shaders/VoronoiSnow.frag \
                              shaders/DualKawaseBlur_upsample.frag \
                              shaders/DualKawaseBlur_downsample.frag \
                              shaders/DualKawaseBlur_upsample_postprocess.frag

libqt_plugin_la_SHADER_VERT = shaders/FadingEdgeX.vert \
                              shaders/FadingEdgeY.vert \
                              shaders/SubTexture.vert

libqt_plugin_la_SHADER = $(libqt_plugin_la_SHADER_FRAG) $(libqt_plugin_la_SHADER_VERT)

if ENABLE_QT

BUILT_SOURCES += \
    $(libqt_plugin_la_SHADER_FRAG:.frag=.frag.qsb) \
    $(libqt_plugin_la_SHADER_VERT:.vert=.vert.qsb)

QSB_PARAMS = --glsl="300 es,130,150" --batchable -O
if HAVE_WIN32
QSB_PARAMS += --hlsl=50 -c
endif
if HAVE_DARWIN
QSB_PARAMS += --msl=12
endif

QSB_EXTRA_PATH =
if HAVE_FXC
QSB_EXTRA_PATH += $(FXC_PATH):
endif

qsb_verbose = $(qsb_verbose_$(V))
qsb_verbose_ = $(qsb_verbose__$(AM_DEFAULT_VERBOSITY))
qsb_verbose_0 = @echo "  QSB     " $@;
qsb_verbose__0 = $(qsb_verbose_0)

QSB_PARAMS_VERBOSE = $(QSB_PARAMS_VERBOSE_$(V))
QSB_PARAMS_VERBOSE_ = $(QSB_PARAMS_VERBOSE__$(AM_DEFAULT_VERBOSITY))
QSB_PARAMS_VERBOSE_0 = -s
QSB_PARAMS_VERBOSE__0 = $(QSB_PARAMS_VERBOSE_0)

EXTRA_DIST += shaders/Common.glsl

$(libqt_plugin_la_SHADER_FRAG:.frag=.frag.qsb) $(libqt_plugin_la_SHADER_VERT:.vert=.vert.qsb): shaders/Common.glsl

$(libqt_plugin_la_SHADER_FRAG:.frag=.frag.qsb):
	$(qsb_verbose)PATH="$(QSB_EXTRA_PATH)$$PATH" $(QSB) $(QSB_PARAMS) $(QSB_PARAMS_VERBOSE) -o $@ $(srcdir)/$(@:.qsb=)

$(libqt_plugin_la_SHADER_VERT:.vert=.vert.qsb):
	$(qsb_verbose)PATH="$(QSB_EXTRA_PATH)$$PATH" $(QSB) $(QSB_PARAMS) $(QSB_PARAMS_VERBOSE) -o $@ $(srcdir)/$(@:.qsb=)

shaders/shaders.qrc: $(libqt_plugin_la_SHADER) Makefile
	$(AM_V_GEN)$(PYTHON3) $(srcdir)/scripts/genqrc.py -o $@ --prefix shaders --sources $(libqt_plugin_la_SHADER_FRAG:.frag=.frag.qsb) $(libqt_plugin_la_SHADER_VERT:.vert=.vert.qsb)

shaders/shaders.cpp: shaders/shaders.qrc $(libqt_plugin_la_SHADER_FRAG:.frag=.frag.qsb) $(libqt_plugin_la_SHADER_VERT:.vert=.vert.qsb)
	$(AM_V_GEN) $(RCC) $(RCC_EXTRA_PARAMS) --name shaders -o $@ $<

libqt_plugin_la_SOURCES += $(libqt_plugin_la_SHADER)
CLEANFILES += shaders/shaders.qrc

lib_qt_plugin_la_QRC = assets.qrc

if HAVE_WIN32
lib_qt_plugin_la_QRC += windows.qrc

windows.cpp: $(libqt_plugin_la_RES)
windows.cpp: windows.qrc
	$(AM_V_GEN)$(RCC) $(RCC_EXTRA_PARAMS) --name windows -o $@ $<

nodist_libqt_plugin_la_SOURCES += windows.cpp
endif

EXTRA_DIST += $(lib_qt_plugin_la_QRC) $(libqt_plugin_la_RES) $(libqt_plugin_la_SHADER)

nodist_libqt_plugin_la_SOURCES += assets.cpp  shaders/shaders.cpp
endif

# Emit shader source dependencies so that the built shaders are properly
# registered with the sources as prerequisites
shaders/shaders.deps: Makefile
	$(AM_V_at)$(MKDIR_P) $(@D)
	$(AM_V_GEN){ \
	  for s in $(libqt_plugin_la_SHADER_FRAG) $(libqt_plugin_la_SHADER_VERT); do \
	    echo "$$s.qsb: $(srcdir)/$$s"; \
	  done; \
	} > $@.tmp && \
	if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

BUILT_SOURCES += shaders/shaders.deps
CLEANFILES    += shaders/shaders.deps

-include shaders/shaders.deps

if ENABLE_VLM
nodist_libqt_plugin_la_SOURCES += dialogs/vlm/vlm.moc.cpp
endif

# Dynamically generate the rules dependencies so that rebuilds are working
# properly. Since this is not GNU Make, we cannot generate the rules using
# text functions.
QML_RULES_DEPS = $(libqt_plugin_la_QML_MODULES:=/qml.deps)

# Since every QML modules is built into a given directory, we can use $(@D)
# to get the module name.
$(QML_RULES_DEPS): Makefile
	$(AM_V_at)$(MKDIR_P) $(@D)
	$(AM_V_GEN){ \
	  echo '$(@D)/qmldir.qrc: $(@D)/qmldir'; \
	  echo '$(@D)/res.qrc $(@D)/qmldir: $(@D)/qml_modules.stamp'; \
	  echo '$(@D)/qmlcachegen.rsp: $$(builddir)/$(@D)/res.qrc'; \
	  echo '$(@D)/qmlcache_loader.cpp: $(@D)/qmlcachegen.rsp'; \
	  echo '$(@D)/qmlassets.cpp: $$(builddir)/$(@D)/qmldir.qrc $$(builddir)/$(@D)/res.qrc'; \
	  echo '$(@D)/qmlassets.cpp: $$(libqml_module_$(@D)_a_QML) $$(libqml_module_$(@D)_a_Singleton_QML)'; \
	  for s in $(libqml_module_$(@D)_a_QML) $(libqml_module_$(@D)_a_Singleton_QML); do \
	    echo "$${s%.*}.cpp: "'$$(builddir)/$(@D)/res.qrc'; \
	  done; \
	} > $@.tmp && \
	if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

BUILT_SOURCES += $(QML_RULES_DEPS)
CLEANFILES += $(QML_RULES_DEPS)

-include $(QML_RULES_DEPS)

# QRC/qmldir generation depends on the list of files (defined here) but
# not the file contents. Use per-module stamp files so that changes to
# one module's file list don't trigger rebuilds in other modules.
QML_MODULE_STAMPS =
if ENABLE_QT
QML_MODULE_STAMPS += $(libqt_plugin_la_QML_MODULES:=/qml_modules.stamp)
endif

# Pre-expand target lists into named variables so that automake does not
# see the same '$(libqt_plugin_la_QML_MODULES' prefix repeated across
# conditionals and emit spurious "was already defined" warnings.
QML_MODULE_RES_QRC = $(libqt_plugin_la_QML_MODULES:=/res.qrc)
QML_MODULE_QMLDIR = $(libqt_plugin_la_QML_MODULES:=/qmldir)
QML_MODULE_QMLDIR_QRC = $(libqt_plugin_la_QML_MODULES:=/qmldir.qrc)

$(QML_MODULE_STAMPS): Makefile
	$(AM_V_at)$(MKDIR_P) $(@D); \
	echo '$(libqml_module_$(@D)_a_Module) $(libqml_module_$(@D)_a_ModulePath) $(libqml_module_$(@D)_a_QML) $(libqml_module_$(@D)_a_Singleton_QML)' > $@.tmp; \
	if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

$(QML_MODULE_RES_QRC):
	$(AM_V_GEN)$(PYTHON3) $(srcdir)/scripts/genqrc.py \
	-o $@ \
	-s $(srcdir) \
	--prefix qt/qml/$(libqml_module_$(@D)_a_ModulePath) \
	--sources $(libqml_module_$(@D)_a_QML) $(libqml_module_$(@D)_a_Singleton_QML)

$(QML_MODULE_QMLDIR):
	$(AM_V_GEN)$(PYTHON3) $(srcdir)/scripts/genqmldir.py \
	-o $@ \
	--prefix qt/qml \
	--module $(libqml_module_$(@D)_a_Module) \
	--sources $(libqml_module_$(@D)_a_QML) \
	--singletons $(libqml_module_$(@D)_a_Singleton_QML)

$(QML_MODULE_QMLDIR_QRC):
	$(AM_V_GEN)$(PYTHON3) $(srcdir)/scripts/genqrc.py -o $@ \
	--prefix qt/qml/$(libqml_module_$(@D)_a_ModulePath) --sources $<

CLEANFILES += $(QML_MODULE_STAMPS) $(QML_MODULE_RES_QRC) $(QML_MODULE_QMLDIR) $(QML_MODULE_QMLDIR_QRC) $(libqml_module_util_a_QML:.qml=.cpp)

if HAVE_QMLCACHEGEN

libqt_plugin_la_CPPFLAGS += -DQT_USE_QMLCACHEGEN

cachegen_verbose = $(cachegen_verbose_$(V))
cachegen_verbose_ = $(cachegen_verbose__$(AM_DEFAULT_VERBOSITY))
cachegen_verbose_0 = @echo " CACHEGEN " $@;
cachegen_verbose__0 = $(cachegen_verbose_0)

if HAVE_QT67
QML_CACHEGEN_EXTRA_ARGS=--validate-basic-blocks
endif

# Each .qml/.js/.mjs is compiled with its module's res.qrc as prerequisite. The
# per-module qrc isn't derivable from the target path, so pick it out of $^.
qml_resource = for f in $^; do case $$f in *.qrc) echo --resource=$$f;; esac; done

.qml.cpp:
	$(cachegen_verbose)$(QMLCACHEGEN) $(QML_CACHEGEN_EXTRA_ARGS) `$(qml_resource)` -o $@ $<

.js.cpp:
	$(cachegen_verbose)$(QMLCACHEGEN) $(QML_CACHEGEN_EXTRA_ARGS) `$(qml_resource)` -o $@ $<

.mjs.cpp:
	$(cachegen_verbose)$(QMLCACHEGEN) $(QML_CACHEGEN_EXTRA_ARGS) `$(qml_resource)` -o $@ $<

# Pre-expand target lists into named variables so that automake does not
# see the same $(libqt_plugin_la_QML_MODULES prefix repeated across
# conditionals and emit spurious "was already defined" warnings.
QML_MODULE_CACHEGEN_RSP = $(libqt_plugin_la_QML_MODULES:=/qmlcachegen.rsp)
QML_MODULE_CACHELOADER = $(libqt_plugin_la_QML_MODULES:=/qmlcache_loader.cpp)
QML_MODULE_CACHEGEN_ASSETS = $(libqt_plugin_la_QML_MODULES:=/qmlassets.cpp)

$(QML_MODULE_CACHEGEN_RSP):
	$(AM_V_GEN)$(PYTHON3) $(srcdir)/scripts/qrc2rsp.py --output $@ $^

$(QML_MODULE_CACHELOADER):
	$(cachegen_verbose)$(QMLCACHEGEN) --resource-name $(@D)_cachegen --resource=$(builddir)/$(@D)/res.qrc -o $@ @$<

# Keep the sources beside the compiled units. Qt must be able to fall back
# when cached units are disabled or rejected by the runtime.
$(QML_MODULE_CACHEGEN_ASSETS): Makefile
	$(AM_V_GEN)$(RCC) $(RCC_EXTRA_PARAMS) --name $(@D)_assets \
	-o $@ $(builddir)/$(@D)/qmldir.qrc $(builddir)/$(@D)/res.qrc


CLEANFILES += \
	$(QML_MODULE_CACHELOADER) \
	$(QML_MODULE_CACHEGEN_RSP) \
	$(QML_MODULE_CACHEGEN_ASSETS) \
	$(libqt_plugin_la_ALL_QML:.qml=.cpp)

else

#package all qml from the genreated QRC files

# See comment above QML_MODULE_RES_QRC for why this is a named variable.
QML_MODULE_ASSETS = $(libqt_plugin_la_QML_MODULES:=/qmlassets.cpp)

$(QML_MODULE_ASSETS):
	$(AM_V_GEN)$(RCC) $(RCC_EXTRA_PARAMS) --name $(@D)_assets \
	-o $@ $(builddir)/$(@D)/qmldir.qrc $(builddir)/$(@D)/res.qrc

nodist_libqt_plugin_la_SOURCES += $(QML_MODULE_ASSETS)
if ENABLE_QT
BUILT_SOURCES += $(QML_MODULE_RES_QRC) $(QML_MODULE_QMLDIR)
endif

endif

EXTRA_DIST += $(libqt_plugin_la_QML)

assets.cpp: $(libqt_plugin_la_RES)
assets.cpp: assets.qrc
	$(AM_V_GEN)$(RCC) $(RCC_EXTRA_PARAMS) --name assets -o $@ $<


if HAVE_QT_GTK
libqt_gtktheme_plugin_la_CXXFLAGS = $(AM_CXXFLAGS) $(GTK3_CFLAGS) $(QT_CFLAGS)
libqt_gtktheme_CPPFLAGS = $(AM_CPPFLAGS)
libqt_gtktheme_plugin_la_LIBADD = $(GTK3_LIBS) $(QT_LIBS)
libqt_gtktheme_plugin_la_LDFLAGS = $(AM_LDFLAGS) $(QT_LDFLAGS)
libqt_gtktheme_plugin_la_SOURCES = \
	style/gtkthemeprovider/gtk_compat.cpp \
	style/gtkthemeprovider/gtk_compat.h \
	style/gtkthemeprovider/gtkthemeprovider.cpp \
	style/gtkthemeprovider/gtk_util.cpp \
	style/gtkthemeprovider/gtk_util.h \
	style/gtkthemeprovider/nav_button_provider_gtk.cpp \
	style/gtkthemeprovider/nav_button_provider_gtk.h \
	style/gtkthemeprovider/scoped_gobject.h
if ENABLE_QT
gui_PLUGINS += libqt_gtktheme_plugin.la
endif
endif

if ENABLE_QT
gui_PLUGINS += libqt_plugin.la
BUILT_SOURCES += $(nodist_libqt_plugin_la_SOURCES)
if !HAVE_WIN32
if !HAVE_OS2
if !HAVE_IOS_OR_TVOS
if !HAVE_XROS
pkglibexec_PROGRAMS = vlc-qt-check
VLC_QT_CHECK_DEP = $(builddir)/../../vlc-qt-check

$(builddir)/../../vlc-qt-check: vlc-qt-check
	$(AM_V_GEN)cd ../../ && $(LN_S) -f gui/qt/vlc-qt-check . && chmod +x vlc-qt-check
endif
endif
endif
endif

base_model_test_SOURCES = tests/test_base_model.cpp \
        util/base_model.cpp util/base_model.hpp util/base_model_p.hpp

nodist_base_model_test_SOURCES = util/base_model.moc.cpp \
        util/listcache.moc.cpp \
        util/locallistcacheloader.moc.cpp

base_model_test_CXXFLAGS = $(AM_CXXFLAGS) $(QT_CFLAGS) -fPIC $(CXXFLAGS_qt)
base_model_test_LDADD = $(QT_LIBS) $(LIBS_qt)
base_model_test_LDFLAGS = $(AM_LDFLAGS) $(QT_LDFLAGS)
check_PROGRAMS = base_model_test
TESTS = base_model_test

if HAVE_WIN32
test_qt_win32_emutls_SOURCES = tests/test_win32_emutls.cpp util/win32_emutls.cpp
test_qt_win32_emutls_CPPFLAGS = $(AM_CPPFLAGS)
test_qt_win32_emutls_CXXFLAGS = $(AM_CXXFLAGS) $(QT_CFLAGS) $(CXXFLAGS_qt)
test_qt_win32_emutls_LDADD = $(QT_LIBS) $(LIBS_qt)
test_qt_win32_emutls_LDFLAGS = $(AM_LDFLAGS) $(QT_LDFLAGS)
check_PROGRAMS += test_qt_win32_emutls
TESTS += test_qt_win32_emutls
endif

if HAVE_QT_QTEST

test_qt_artwork_selection_SOURCES = tests/test_artwork_selection.cpp util/artwork_selection.cpp util/artwork_selection.hpp
nodist_test_qt_artwork_selection_SOURCES = tests/test_artwork_selection.moc
BUILT_SOURCES += tests/test_artwork_selection.moc
CLEANFILES += tests/test_artwork_selection.moc
test_qt_artwork_selection_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir)
test_qt_artwork_selection_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_artwork_selection_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_artwork_selection_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_artwork_selection
TESTS += test_qt_artwork_selection

test_qt_artwork_selected_image_SOURCES = tests/test_artwork_selected_image.cpp
nodist_test_qt_artwork_selected_image_SOURCES = tests/test_artwork_selected_image.moc
BUILT_SOURCES += tests/test_artwork_selected_image.moc
CLEANFILES += tests/test_artwork_selected_image.moc
test_qt_artwork_selected_image_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir)
test_qt_artwork_selected_image_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_artwork_selected_image_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_artwork_selected_image_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_artwork_selected_image
TESTS += test_qt_artwork_selected_image

test_qt_ml_joined_runner_SOURCES = tests/test_ml_joined_runner.cpp medialibrary/mljoinedthreadrunner.cpp medialibrary/mljoinedthreadrunner.hpp medialibrary/mltaskid.hpp medialibrary/mlthreadpool.cpp medialibrary/mlthreadpool.hpp
nodist_test_qt_ml_joined_runner_SOURCES = tests/test_ml_joined_runner.moc medialibrary/mlthreadpool.moc.cpp
BUILT_SOURCES += tests/test_ml_joined_runner.moc
CLEANFILES += tests/test_ml_joined_runner.moc
test_qt_ml_joined_runner_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_ml_joined_runner_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_ml_joined_runner_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_ml_joined_runner_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_ml_joined_runner
TESTS += test_qt_ml_joined_runner

test_qt_addon_native_authority_SOURCES = tests/test_addon_native_authority.cpp
nodist_test_qt_addon_native_authority_SOURCES = tests/test_addon_native_authority.moc
BUILT_SOURCES += tests/test_addon_native_authority.moc
CLEANFILES += tests/test_addon_native_authority.moc
test_qt_addon_native_authority_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_native_authority_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_native_authority_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_native_authority_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_native_authority
TESTS += test_qt_addon_native_authority

test_qt_addon_package_summaries_SOURCES = tests/test_addon_package_summaries.cpp
nodist_test_qt_addon_package_summaries_SOURCES = tests/test_addon_package_summaries.moc
BUILT_SOURCES += tests/test_addon_package_summaries.moc
CLEANFILES += tests/test_addon_package_summaries.moc
test_qt_addon_package_summaries_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_package_summaries_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_package_summaries_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_package_summaries_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_package_summaries
TESTS += test_qt_addon_package_summaries

test_qt_addon_repository_jobs_SOURCES = tests/test_addon_repository_jobs.cpp
nodist_test_qt_addon_repository_jobs_SOURCES = tests/test_addon_repository_jobs.moc
BUILT_SOURCES += tests/test_addon_repository_jobs.moc
CLEANFILES += tests/test_addon_repository_jobs.moc
test_qt_addon_repository_jobs_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_repository_jobs_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_repository_jobs_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_repository_jobs_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_repository_jobs
TESTS += test_qt_addon_repository_jobs

test_qt_addon_app_shutdown_SOURCES = tests/test_addon_app_shutdown.cpp
nodist_test_qt_addon_app_shutdown_SOURCES = tests/test_addon_app_shutdown.moc
BUILT_SOURCES += tests/test_addon_app_shutdown.moc
CLEANFILES += tests/test_addon_app_shutdown.moc
test_qt_addon_app_shutdown_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_app_shutdown_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_app_shutdown_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_app_shutdown_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_app_shutdown
TESTS += test_qt_addon_app_shutdown

test_qt_addon_stream_http_SOURCES = tests/test_addon_stream_http.cpp
nodist_test_qt_addon_stream_http_SOURCES = tests/test_addon_stream_http.moc
BUILT_SOURCES += tests/test_addon_stream_http.moc
CLEANFILES += tests/test_addon_stream_http.moc
test_qt_addon_stream_http_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"'
test_qt_addon_stream_http_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_stream_http_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_stream_http_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_stream_http
TESTS += test_qt_addon_stream_http

test_qt_addon_stream_proxy_SOURCES = tests/test_addon_stream_proxy.cpp
nodist_test_qt_addon_stream_proxy_SOURCES = tests/test_addon_stream_proxy.moc
BUILT_SOURCES += tests/test_addon_stream_proxy.moc
CLEANFILES += tests/test_addon_stream_proxy.moc
test_qt_addon_stream_proxy_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"'
test_qt_addon_stream_proxy_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_stream_proxy_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_stream_proxy_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_stream_proxy
TESTS += test_qt_addon_stream_proxy

test_qt_addon_stream_shutdown_SOURCES = tests/test_addon_stream_shutdown.cpp
nodist_test_qt_addon_stream_shutdown_SOURCES = tests/test_addon_stream_shutdown.moc
BUILT_SOURCES += tests/test_addon_stream_shutdown.moc
CLEANFILES += tests/test_addon_stream_shutdown.moc
test_qt_addon_stream_shutdown_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"'
test_qt_addon_stream_shutdown_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_stream_shutdown_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_stream_shutdown_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_stream_shutdown
TESTS += test_qt_addon_stream_shutdown


test_qt_addon_packages_SOURCES = tests/test_addon_packages.cpp
nodist_test_qt_addon_packages_SOURCES = tests/test_addon_packages.moc
BUILT_SOURCES += tests/test_addon_packages.moc
CLEANFILES += tests/test_addon_packages.moc
test_qt_addon_packages_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_packages_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_packages_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_packages_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_packages
TESTS += test_qt_addon_packages

test_qt_addon_runtime_SOURCES = tests/test_addon_runtime.cpp
nodist_test_qt_addon_runtime_SOURCES = tests/test_addon_runtime.moc
BUILT_SOURCES += tests/test_addon_runtime.moc
CLEANFILES += tests/test_addon_runtime.moc
test_qt_addon_runtime_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_runtime_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_runtime_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_runtime_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_runtime
TESTS += test_qt_addon_runtime

test_qt_addon_metadata_SOURCES = tests/test_addon_metadata.cpp
nodist_test_qt_addon_metadata_SOURCES = tests/test_addon_metadata.moc
BUILT_SOURCES += tests/test_addon_metadata.moc
CLEANFILES += tests/test_addon_metadata.moc
test_qt_addon_metadata_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_metadata_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_metadata_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_metadata_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_metadata
TESTS += test_qt_addon_metadata

test_qt_addon_broker_SOURCES = tests/test_addon_broker.cpp
nodist_test_qt_addon_broker_SOURCES = tests/test_addon_broker.moc
BUILT_SOURCES += tests/test_addon_broker.moc
CLEANFILES += tests/test_addon_broker.moc
test_qt_addon_broker_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_broker_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_broker_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_broker_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_broker
TESTS += test_qt_addon_broker


test_qt_addon_ui_registry_SOURCES = tests/test_addon_ui_registry.cpp
nodist_test_qt_addon_ui_registry_SOURCES = tests/test_addon_ui_registry.moc
BUILT_SOURCES += tests/test_addon_ui_registry.moc
CLEANFILES += tests/test_addon_ui_registry.moc
test_qt_addon_ui_registry_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_ui_registry_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_ui_registry_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_ui_registry_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_ui_registry
TESTS += test_qt_addon_ui_registry

test_qt_addon_ui_assets_SOURCES = tests/test_addon_ui_assets.cpp tests/addon_ui_asset_io_probe.hpp addons/addon_asset_image_provider.cpp addons/addon_asset_image_provider.hpp
nodist_test_qt_addon_ui_assets_SOURCES = tests/test_addon_ui_assets.moc
BUILT_SOURCES += tests/test_addon_ui_assets.moc
CLEANFILES += tests/test_addon_ui_assets.moc
test_qt_addon_ui_assets_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_ui_assets_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_ui_assets_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_ui_assets_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_ui_assets
TESTS += test_qt_addon_ui_assets

test_qt_addon_metadata_choices_SOURCES = tests/test_addon_metadata_choices.cpp
nodist_test_qt_addon_metadata_choices_SOURCES = tests/test_addon_metadata_choices.moc
BUILT_SOURCES += tests/test_addon_metadata_choices.moc
CLEANFILES += tests/test_addon_metadata_choices.moc
test_qt_addon_metadata_choices_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_metadata_choices_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_metadata_choices_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_metadata_choices_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_metadata_choices
TESTS += test_qt_addon_metadata_choices

test_qt_addon_metadata_service_SOURCES = tests/test_addon_metadata_service.cpp
nodist_test_qt_addon_metadata_service_SOURCES = tests/test_addon_metadata_service.moc
BUILT_SOURCES += tests/test_addon_metadata_service.moc
CLEANFILES += tests/test_addon_metadata_service.moc
test_qt_addon_metadata_service_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_metadata_service_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_metadata_service_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_metadata_service_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_metadata_service
TESTS += test_qt_addon_metadata_service

test_qt_addon_discovery_SOURCES = tests/test_addon_discovery.cpp
nodist_test_qt_addon_discovery_SOURCES = tests/test_addon_discovery.moc
BUILT_SOURCES += tests/test_addon_discovery.moc
CLEANFILES += tests/test_addon_discovery.moc
test_qt_addon_discovery_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_discovery_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_discovery_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_discovery_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_discovery
TESTS += test_qt_addon_discovery

test_qt_addon_theme_SOURCES = tests/test_addon_theme.cpp
nodist_test_qt_addon_theme_SOURCES = tests/test_addon_theme.moc
BUILT_SOURCES += tests/test_addon_theme.moc
CLEANFILES += tests/test_addon_theme.moc
test_qt_addon_theme_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_theme_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_theme_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_theme_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_theme
TESTS += test_qt_addon_theme

test_qt_addon_sessions_SOURCES = tests/test_addon_sessions.cpp
nodist_test_qt_addon_sessions_SOURCES = tests/test_addon_sessions.moc
BUILT_SOURCES += tests/test_addon_sessions.moc
CLEANFILES += tests/test_addon_sessions.moc
test_qt_addon_sessions_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_sessions_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_sessions_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_sessions_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_sessions
TESTS += test_qt_addon_sessions


test_qt_addon_audio_analysis_SOURCES = tests/test_addon_audio_analysis.cpp
nodist_test_qt_addon_audio_analysis_SOURCES = tests/test_addon_audio_analysis.moc
BUILT_SOURCES += tests/test_addon_audio_analysis.moc
CLEANFILES += tests/test_addon_audio_analysis.moc
test_qt_addon_audio_analysis_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_audio_analysis_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_audio_analysis_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_audio_analysis_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_audio_analysis
TESTS += test_qt_addon_audio_analysis


test_qt_addon_visual_scene_SOURCES = tests/test_addon_visual_scene.cpp
nodist_test_qt_addon_visual_scene_SOURCES = tests/test_addon_visual_scene.moc
BUILT_SOURCES += tests/test_addon_visual_scene.moc
CLEANFILES += tests/test_addon_visual_scene.moc
test_qt_addon_visual_scene_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_visual_scene_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_visual_scene_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_visual_scene_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_visual_scene
TESTS += test_qt_addon_visual_scene

test_qt_addon_visualizer_parameters_SOURCES = tests/test_addon_visualizer_parameters.cpp
nodist_test_qt_addon_visualizer_parameters_SOURCES = tests/test_addon_visualizer_parameters.moc
BUILT_SOURCES += tests/test_addon_visualizer_parameters.moc
CLEANFILES += tests/test_addon_visualizer_parameters.moc
test_qt_addon_visualizer_parameters_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_visualizer_parameters_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_visualizer_parameters_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_visualizer_parameters_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_visualizer_parameters
TESTS += test_qt_addon_visualizer_parameters

test_qt_addon_store_authority_SOURCES = tests/test_addon_store_authority.cpp
nodist_test_qt_addon_store_authority_SOURCES = tests/test_addon_store_authority.moc
BUILT_SOURCES += tests/test_addon_store_authority.moc
CLEANFILES += tests/test_addon_store_authority.moc
test_qt_addon_store_authority_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_store_authority_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_store_authority_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_store_authority_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_store_authority
TESTS += test_qt_addon_store_authority

test_qt_addon_stream_manifest_SOURCES = tests/test_addon_stream_manifest.cpp
nodist_test_qt_addon_stream_manifest_SOURCES = tests/test_addon_stream_manifest.moc
BUILT_SOURCES += tests/test_addon_stream_manifest.moc
CLEANFILES += tests/test_addon_stream_manifest.moc
test_qt_addon_stream_manifest_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_stream_manifest_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_stream_manifest_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_stream_manifest_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_stream_manifest
TESTS += test_qt_addon_stream_manifest

test_qt_addon_websocket_codec_SOURCES = tests/test_addon_websocket_codec.cpp
nodist_test_qt_addon_websocket_codec_SOURCES = tests/test_addon_websocket_codec.moc
BUILT_SOURCES += tests/test_addon_websocket_codec.moc
CLEANFILES += tests/test_addon_websocket_codec.moc
test_qt_addon_websocket_codec_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_websocket_codec_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_websocket_codec_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_websocket_codec_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_websocket_codec
TESTS += test_qt_addon_websocket_codec

check_PROGRAMS += addon_visualizer_process
addon_visualizer_process_SOURCES = tests/fixtures/addon_visualizer_process.c

check_PROGRAMS += addon_discovery_source_process
addon_discovery_source_process_SOURCES = tests/fixtures/addon_discovery_source_process.c
EXTRA_DIST += tests/fixtures/addon_visualizer_worker.rs tests/fixtures/addon_visualizer_worker_wasm.hpp

test_qt_addon_source_consent_SOURCES = tests/test_addon_source_consent.cpp
nodist_test_qt_addon_source_consent_SOURCES = tests/test_addon_source_consent.moc
BUILT_SOURCES += tests/test_addon_source_consent.moc
CLEANFILES += tests/test_addon_source_consent.moc
test_qt_addon_source_consent_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"' -DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE='"$(abs_builddir)/addon_visualizer_process$(EXEEXT)"'
test_qt_addon_source_consent_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_source_consent_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_source_consent_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_source_consent
TESTS += test_qt_addon_source_consent

test_qt_addon_visualizer_resources_SOURCES = tests/test_addon_visualizer_resources.cpp
nodist_test_qt_addon_visualizer_resources_SOURCES = tests/test_addon_visualizer_resources.moc
BUILT_SOURCES += tests/test_addon_visualizer_resources.moc
CLEANFILES += tests/test_addon_visualizer_resources.moc
test_qt_addon_visualizer_resources_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"' -DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE='"$(abs_builddir)/addon_visualizer_process$(EXEEXT)"'
test_qt_addon_visualizer_resources_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_visualizer_resources_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_visualizer_resources_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_visualizer_resources
TESTS += test_qt_addon_visualizer_resources

test_qt_addon_visualizer_worker_SOURCES = tests/test_addon_visualizer_worker.cpp
nodist_test_qt_addon_visualizer_worker_SOURCES = tests/test_addon_visualizer_worker.moc
BUILT_SOURCES += tests/test_addon_visualizer_worker.moc
CLEANFILES += tests/test_addon_visualizer_worker.moc
test_qt_addon_visualizer_worker_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"' -DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE='"$(abs_builddir)/addon_visualizer_process$(EXEEXT)"'
test_qt_addon_visualizer_worker_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_visualizer_worker_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_visualizer_worker_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_visualizer_worker
TESTS += test_qt_addon_visualizer_worker

test_qt_addon_visualizer_worker_runtime_SOURCES = tests/test_addon_visualizer_worker_runtime.cpp
nodist_test_qt_addon_visualizer_worker_runtime_SOURCES = tests/test_addon_visualizer_worker_runtime.moc
BUILT_SOURCES += tests/test_addon_visualizer_worker_runtime.moc
CLEANFILES += tests/test_addon_visualizer_worker_runtime.moc
test_qt_addon_visualizer_worker_runtime_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"' -DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE='"$(abs_builddir)/addon_visualizer_process$(EXEEXT)"'
test_qt_addon_visualizer_worker_runtime_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_visualizer_worker_runtime_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_visualizer_worker_runtime_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_visualizer_worker_runtime
TESTS += test_qt_addon_visualizer_worker_runtime
test_qt_addon_visualizer_worker_runtime_DEPENDENCIES = libqt_addon_host.la addon_visualizer_process

test_qt_addon_visualizer_render_queue_SOURCES = tests/test_addon_visualizer_render_queue.cpp
nodist_test_qt_addon_visualizer_render_queue_SOURCES = tests/test_addon_visualizer_render_queue.moc
BUILT_SOURCES += tests/test_addon_visualizer_render_queue.moc
CLEANFILES += tests/test_addon_visualizer_render_queue.moc
test_qt_addon_visualizer_render_queue_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"' -DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE='"$(abs_builddir)/addon_visualizer_process$(EXEEXT)"'
test_qt_addon_visualizer_render_queue_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_visualizer_render_queue_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_visualizer_render_queue_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_visualizer_render_queue
TESTS += test_qt_addon_visualizer_render_queue

test_qt_addon_native_source_resolver_SOURCES = tests/test_addon_native_source_resolver.cpp
nodist_test_qt_addon_native_source_resolver_SOURCES = tests/test_addon_native_source_resolver.moc
BUILT_SOURCES += tests/test_addon_native_source_resolver.moc
CLEANFILES += tests/test_addon_native_source_resolver.moc
test_qt_addon_native_source_resolver_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"' -DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE='"$(abs_builddir)/addon_visualizer_process$(EXEEXT)"'
test_qt_addon_native_source_resolver_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_native_source_resolver_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_native_source_resolver_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_native_source_resolver
TESTS += test_qt_addon_native_source_resolver

test_qt_addon_native_player_analysis_SOURCES = tests/test_addon_native_player_analysis.cpp
nodist_test_qt_addon_native_player_analysis_SOURCES = tests/test_addon_native_player_analysis.moc
BUILT_SOURCES += tests/test_addon_native_player_analysis.moc
CLEANFILES += tests/test_addon_native_player_analysis.moc
test_qt_addon_native_player_analysis_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"' -DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE='"$(abs_builddir)/addon_visualizer_process$(EXEEXT)"'
test_qt_addon_native_player_analysis_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_native_player_analysis_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_native_player_analysis_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_native_player_analysis
TESTS += test_qt_addon_native_player_analysis

test_qt_addon_visualizer_clock_SOURCES = tests/test_addon_visualizer_clock.cpp
nodist_test_qt_addon_visualizer_clock_SOURCES = tests/test_addon_visualizer_clock.moc
BUILT_SOURCES += tests/test_addon_visualizer_clock.moc
CLEANFILES += tests/test_addon_visualizer_clock.moc
test_qt_addon_visualizer_clock_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"' -DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE='"$(abs_builddir)/addon_visualizer_process$(EXEEXT)"'
test_qt_addon_visualizer_clock_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_visualizer_clock_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_visualizer_clock_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_visualizer_clock
TESTS += test_qt_addon_visualizer_clock

test_qt_addon_visualizer_surface_SOURCES = tests/test_addon_visualizer_surface.cpp addons/addon_visualizer_surface.cpp addons/addon_visualizer_surface.hpp
nodist_test_qt_addon_visualizer_surface_SOURCES = tests/test_addon_visualizer_surface.moc addons/addon_visualizer_surface.moc.cpp
BUILT_SOURCES += tests/test_addon_visualizer_surface.moc
CLEANFILES += tests/test_addon_visualizer_surface.moc
test_qt_addon_visualizer_surface_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"' -DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE='"$(abs_builddir)/addon_visualizer_process$(EXEEXT)"'
test_qt_addon_visualizer_surface_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_visualizer_surface_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_visualizer_surface_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_visualizer_surface
TESTS += test_qt_addon_visualizer_surface

test_qt_addon_visualizers_SOURCES = tests/test_addon_visualizers.cpp
nodist_test_qt_addon_visualizers_SOURCES = tests/test_addon_visualizers.moc
BUILT_SOURCES += tests/test_addon_visualizers.moc
CLEANFILES += tests/test_addon_visualizers.moc
test_qt_addon_visualizers_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE='"$(abs_builddir)/addon_visualizer_process$(EXEEXT)"'
test_qt_addon_visualizers_DEPENDENCIES = libqt_addon_host.la addon_visualizer_process
test_qt_addon_visualizers_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_visualizers_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_visualizers_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_visualizers
TESTS += test_qt_addon_visualizers


test_qt_addon_host_services_SOURCES = tests/test_addon_host_services.cpp
nodist_test_qt_addon_host_services_SOURCES = tests/test_addon_host_services.moc
BUILT_SOURCES += tests/test_addon_host_services.moc
CLEANFILES += tests/test_addon_host_services.moc
test_qt_addon_host_services_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_host_services_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_host_services_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_host_services_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_host_services
TESTS += test_qt_addon_host_services


test_qt_addon_repository_updates_SOURCES = tests/test_addon_repository_updates.cpp
nodist_test_qt_addon_repository_updates_SOURCES = tests/test_addon_repository_updates.moc
BUILT_SOURCES += tests/test_addon_repository_updates.moc
CLEANFILES += tests/test_addon_repository_updates.moc
test_qt_addon_repository_updates_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_repository_updates_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_repository_updates_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_repository_updates_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_repository_updates
TESTS += test_qt_addon_repository_updates

test_qt_addon_playable_gate_SOURCES = tests/test_addon_playable_gate.cpp
nodist_test_qt_addon_playable_gate_SOURCES = tests/test_addon_playable_gate.moc
BUILT_SOURCES += tests/test_addon_playable_gate.moc
CLEANFILES += tests/test_addon_playable_gate.moc
test_qt_addon_playable_gate_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_playable_gate_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_playable_gate_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_playable_gate_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_playable_gate
TESTS += test_qt_addon_playable_gate


test_qt_addon_artwork_image_SOURCES = tests/test_addon_artwork_image.cpp tests/addon_artwork_test_images.hpp
nodist_test_qt_addon_artwork_image_SOURCES = tests/test_addon_artwork_image.moc
BUILT_SOURCES += tests/test_addon_artwork_image.moc
CLEANFILES += tests/test_addon_artwork_image.moc
test_qt_addon_artwork_image_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir)
test_qt_addon_artwork_image_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_artwork_image_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_artwork_image_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_artwork_image
TESTS += test_qt_addon_artwork_image

test_qt_addon_native_artwork_assets_SOURCES = tests/test_addon_native_artwork_assets.cpp tests/addon_artwork_test_images.hpp
nodist_test_qt_addon_native_artwork_assets_SOURCES = tests/test_addon_native_artwork_assets.moc
BUILT_SOURCES += tests/test_addon_native_artwork_assets.moc
CLEANFILES += tests/test_addon_native_artwork_assets.moc
test_qt_addon_native_artwork_assets_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"'
test_qt_addon_native_artwork_assets_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_native_artwork_assets_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_native_artwork_assets_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_native_artwork_assets
TESTS += test_qt_addon_native_artwork_assets

test_qt_addon_artwork_assets_SOURCES = tests/test_addon_artwork_assets.cpp
nodist_test_qt_addon_artwork_assets_SOURCES = tests/test_addon_artwork_assets.moc
BUILT_SOURCES += tests/test_addon_artwork_assets.moc
CLEANFILES += tests/test_addon_artwork_assets.moc
test_qt_addon_artwork_assets_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_artwork_assets_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_artwork_assets_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_artwork_assets_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_artwork_assets
TESTS += test_qt_addon_artwork_assets


test_qt_addon_queue_transactions_SOURCES = tests/test_addon_queue_transactions.cpp
nodist_test_qt_addon_queue_transactions_SOURCES = tests/test_addon_queue_transactions.moc
BUILT_SOURCES += tests/test_addon_queue_transactions.moc
CLEANFILES += tests/test_addon_queue_transactions.moc
test_qt_addon_queue_transactions_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_queue_transactions_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_queue_transactions_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_queue_transactions_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_queue_transactions
TESTS += test_qt_addon_queue_transactions

test_qt_addon_discovery_service_SOURCES = tests/test_addon_discovery_service.cpp
nodist_test_qt_addon_discovery_service_SOURCES = tests/test_addon_discovery_service.moc
BUILT_SOURCES += tests/test_addon_discovery_service.moc
CLEANFILES += tests/test_addon_discovery_service.moc
test_qt_addon_discovery_service_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_discovery_service_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_discovery_service_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_discovery_service_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_discovery_service
TESTS += test_qt_addon_discovery_service

test_qt_addon_source_admissions_SOURCES = tests/test_addon_source_admissions.cpp
nodist_test_qt_addon_source_admissions_SOURCES = tests/test_addon_source_admissions.moc
BUILT_SOURCES += tests/test_addon_source_admissions.moc
CLEANFILES += tests/test_addon_source_admissions.moc
test_qt_addon_source_admissions_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_source_admissions_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_source_admissions_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_source_admissions_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_source_admissions
TESTS += test_qt_addon_source_admissions

test_qt_addon_storage_SOURCES = tests/test_addon_storage.cpp
nodist_test_qt_addon_storage_SOURCES = tests/test_addon_storage.moc
BUILT_SOURCES += tests/test_addon_storage.moc
CLEANFILES += tests/test_addon_storage.moc
test_qt_addon_storage_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_addon_storage_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_storage_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_storage_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_storage
TESTS += test_qt_addon_storage

test_qt_addon_native_queue_store_SOURCES = tests/test_addon_native_queue_store.cpp
nodist_test_qt_addon_native_queue_store_SOURCES = tests/test_addon_native_queue_store.moc
BUILT_SOURCES += tests/test_addon_native_queue_store.moc
CLEANFILES += tests/test_addon_native_queue_store.moc
test_qt_addon_native_queue_store_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"'
test_qt_addon_native_queue_store_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_native_queue_store_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_native_queue_store_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_native_queue_store
TESTS += test_qt_addon_native_queue_store


test_qt_addon_discovery_source_owner_SOURCES = tests/test_addon_discovery_source_owner.cpp
nodist_test_qt_addon_discovery_source_owner_SOURCES = tests/test_addon_discovery_source_owner.moc
BUILT_SOURCES += tests/test_addon_discovery_source_owner.moc
CLEANFILES += tests/test_addon_discovery_source_owner.moc
test_qt_addon_discovery_source_owner_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"' -DVLC_ADDON_NATIVE_BUILD_ROOT='"$(abs_top_builddir)"' -DVLC_ADDON_DISCOVERY_PROCESS_FIXTURE='"$(abs_builddir)/addon_discovery_source_process$(EXEEXT)"'
test_qt_addon_discovery_source_owner_DEPENDENCIES = libqt_addon_host.la addon_discovery_source_process
test_qt_addon_discovery_source_owner_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_discovery_source_owner_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_discovery_source_owner_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_discovery_source_owner
TESTS += test_qt_addon_discovery_source_owner

test_qt_addon_source_consent_ui_owner_SOURCES = tests/test_addon_source_consent_ui_owner.cpp
nodist_test_qt_addon_source_consent_ui_owner_SOURCES = tests/test_addon_source_consent_ui_owner.moc
BUILT_SOURCES += tests/test_addon_source_consent_ui_owner.moc
CLEANFILES += tests/test_addon_source_consent_ui_owner.moc
test_qt_addon_source_consent_ui_owner_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -DVLC_ADDON_SOURCE_ROOT='"$(abs_top_srcdir)"'
test_qt_addon_source_consent_ui_owner_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_source_consent_ui_owner_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_source_consent_ui_owner_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_addon_source_consent_ui_owner
TESTS += test_qt_addon_source_consent_ui_owner

if HAVE_LINUX
test_qt_addon_native_provider_context_SOURCES = tests/test_addon_native_provider_context.cpp tests/fixtures/addon_native_provider_fixture.c $(top_srcdir)/src/preparser/metadata_native_provider.c
nodist_test_qt_addon_native_provider_context_SOURCES = tests/test_addon_native_provider_context.moc
BUILT_SOURCES += tests/test_addon_native_provider_context.moc
CLEANFILES += tests/test_addon_native_provider_context.moc
test_qt_addon_native_provider_context_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -I$(top_srcdir)/src -DHAKONE_NATIVE_OUTCOME_WRAP
test_qt_addon_native_provider_context_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_native_provider_context_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_native_provider_context_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS) -Wl,--wrap=vlc_metadata_native_claim_AdmitOutcome
check_PROGRAMS += test_qt_addon_native_provider_context
TESTS += test_qt_addon_native_provider_context

tests/test_addon_native_provider_context.moc: $(srcdir)/tests/test_addon_native_provider_context.cpp
	$(moc_verbose)$(MOC) $(MOC_CPPFLAGS) -DHAKONE_NATIVE_OUTCOME_WRAP -o $@ $<

test_qt_addon_native_metadata_owner_SOURCES = tests/test_addon_native_metadata_owner.cpp tests/fixtures/addon_native_provider_fixture.c $(top_srcdir)/src/preparser/metadata_native_provider.c
nodist_test_qt_addon_native_metadata_owner_SOURCES = tests/test_addon_native_metadata_owner.moc
BUILT_SOURCES += tests/test_addon_native_metadata_owner.moc
CLEANFILES += tests/test_addon_native_metadata_owner.moc
test_qt_addon_native_metadata_owner_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -I$(top_srcdir) -I$(top_srcdir)/src
test_qt_addon_native_metadata_owner_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_addon_native_metadata_owner_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_addon_native_metadata_owner_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS) -Wl,--wrap=_Znwm -Wl,--wrap=_Znwj -Wl,--wrap=_ZdlPv -Wl,--wrap=_ZdlPvm -Wl,--wrap=_ZdlPvj
check_PROGRAMS += test_qt_addon_native_metadata_owner
TESTS += test_qt_addon_native_metadata_owner
endif

LIBVLC = ../../../lib/libvlc.la
QT_QTEST_COMMON_cppflags = $(libqt_plugin_la_CPPFLAGS) -I$(builddir)/tests -DTOP_BUILDDIR=\"$(abs_top_builddir)\"
QT_QTEST_COMMON_cxxflags = $(AM_CXXFLAGS) $(QT_CFLAGS) $(QT_QTEST_CFLAGS) -fPIC $(CXXFLAGS_qt)
QT_QTEST_COMMON_ldadd = $(QT_LIBS) $(LIBS_qt) $(QT_QTEST_LIBS) $(LIBVLCCORE) $(LIBVLC) $(LIBZ)
QT_QTEST_COMMON_ldflags = $(AM_LDFLAGS) $(QT_LDFLAGS) $(QT_QTEST_LDFLAGS)

if HAVE_WIN32
test_window_fullscreen_SOURCES = tests/test_window_fullscreen.cpp maininterface/window_fullscreen.hpp maininterface/window_csd_geometry.hpp util/win32_emutls.cpp
nodist_test_window_fullscreen_SOURCES = tests/test_window_fullscreen.moc
BUILT_SOURCES += tests/test_window_fullscreen.moc
CLEANFILES += tests/test_window_fullscreen.moc
test_window_fullscreen_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_window_fullscreen_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_window_fullscreen_LDADD = $(QT_QTEST_COMMON_ldadd)
test_window_fullscreen_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_window_fullscreen
TESTS += test_window_fullscreen

test_dcomp_backdrop_SOURCES = maininterface/compositor_dcomp_background.hpp tests/test_dcomp_backdrop.cpp maininterface/compositor_dcomp_backdrop.cpp maininterface/compositor_dcomp_backdrop.hpp util/video_backdrop_region.hpp
nodist_test_dcomp_backdrop_SOURCES = tests/test_dcomp_backdrop.moc
BUILT_SOURCES += tests/test_dcomp_backdrop.moc
CLEANFILES += tests/test_dcomp_backdrop.moc
test_dcomp_backdrop_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_dcomp_backdrop_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_dcomp_backdrop_LDADD = $(QT_QTEST_COMMON_ldadd) -ld3d11 -ldcomp -ldxguid
test_dcomp_backdrop_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_dcomp_backdrop
TESTS += test_dcomp_backdrop
endif

test_redesign_svg_raster_SOURCES = tests/test_redesign_svg_raster.cpp
nodist_test_redesign_svg_raster_SOURCES = tests/test_redesign_svg_raster.moc
BUILT_SOURCES += tests/test_redesign_svg_raster.moc
CLEANFILES += tests/test_redesign_svg_raster.moc
test_redesign_svg_raster_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DTOP_SRCDIR=\"$(abs_top_srcdir)\"
test_redesign_svg_raster_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_redesign_svg_raster_LDADD = $(QT_QTEST_COMMON_ldadd)
test_redesign_svg_raster_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_redesign_svg_raster
TESTS += test_redesign_svg_raster
EXTRA_DIST += tests/redesign-svg-raster.qml tests/reference-menu-file.svg

test_backdrop_blur_SOURCES = tests/test_backdrop_blur.cpp util/textureproviderobserver.cpp util/textureproviderobserver.hpp widgets/native/viewblockingrectangle.cpp widgets/native/viewblockingrectangle.hpp widgets/native/softwarebackdropblur.cpp widgets/native/softwarebackdropblur.hpp
nodist_test_backdrop_blur_SOURCES = tests/test_backdrop_blur.moc util/textureproviderobserver.moc.cpp widgets/native/viewblockingrectangle.moc.cpp widgets/native/softwarebackdropblur.moc.cpp shaders/shaders.cpp
BUILT_SOURCES += tests/test_backdrop_blur.moc
CLEANFILES += tests/test_backdrop_blur.moc
test_backdrop_blur_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DTOP_SRCDIR=\"$(abs_top_srcdir)\"
test_backdrop_blur_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_backdrop_blur_LDADD = $(QT_QTEST_COMMON_ldadd)
if HAVE_LIBATOMIC
test_backdrop_blur_CXXFLAGS += -DQT_HAS_LIBATOMIC
test_backdrop_blur_LDADD += -latomic
endif
test_backdrop_blur_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_backdrop_blur
TESTS += test_backdrop_blur

test_video_backdrop_SOURCES = tests/test_video_backdrop.cpp maininterface/videobackdrop.cpp maininterface/videobackdrop.hpp
nodist_test_video_backdrop_SOURCES = tests/test_video_backdrop.moc maininterface/videobackdrop.moc.cpp
BUILT_SOURCES += tests/test_video_backdrop.moc
CLEANFILES += tests/test_video_backdrop.moc
test_video_backdrop_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) -DTOP_SRCDIR=\"$(abs_top_srcdir)\"
test_video_backdrop_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_video_backdrop_LDADD = $(QT_QTEST_COMMON_ldadd)
test_video_backdrop_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_video_backdrop
TESTS += test_video_backdrop

test_video_backdrop_reporter_SOURCES = tests/test_video_backdrop_reporter.cpp util/video_backdrop_reporter.hpp util/video_backdrop_region.hpp widgets/native/viewblockingrectangle.hpp
nodist_test_video_backdrop_reporter_SOURCES = tests/test_video_backdrop_reporter.moc
BUILT_SOURCES += tests/test_video_backdrop_reporter.moc
CLEANFILES += tests/test_video_backdrop_reporter.moc
test_video_backdrop_reporter_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_video_backdrop_reporter_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_video_backdrop_reporter_LDADD = $(QT_QTEST_COMMON_ldadd)
test_video_backdrop_reporter_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_video_backdrop_reporter
TESTS += test_video_backdrop_reporter

test_viewblockingrectangle_SOURCES = tests/test_viewblockingrectangle.cpp widgets/native/viewblockingrectangle.cpp widgets/native/viewblockingrectangle.hpp
nodist_test_viewblockingrectangle_SOURCES = tests/test_viewblockingrectangle.moc widgets/native/viewblockingrectangle.moc.cpp
BUILT_SOURCES += tests/test_viewblockingrectangle.moc
CLEANFILES += tests/test_viewblockingrectangle.moc
test_viewblockingrectangle_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_viewblockingrectangle_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_viewblockingrectangle_LDADD = $(QT_QTEST_COMMON_ldadd)
test_viewblockingrectangle_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_viewblockingrectangle
TESTS += test_viewblockingrectangle

test_cover_generator_SOURCES = tests/test_cover_generator.cpp util/covergenerator.cpp util/covergenerator.hpp
nodist_test_cover_generator_SOURCES = tests/test_cover_generator.moc
BUILT_SOURCES += tests/test_cover_generator.moc
CLEANFILES += tests/test_cover_generator.moc
test_cover_generator_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_cover_generator_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_cover_generator_LDADD = $(QT_QTEST_COMMON_ldadd)
test_cover_generator_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_cover_generator
TESTS += test_cover_generator

test_series_model_SOURCES = tests/test_series_model.cpp player/series_model.cpp player/series_model.hpp
nodist_test_series_model_SOURCES = tests/test_series_model.moc player/series_model.moc.cpp
BUILT_SOURCES += tests/test_series_model.moc
CLEANFILES += tests/test_series_model.moc
test_series_model_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_series_model_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_series_model_LDADD = $(QT_QTEST_COMMON_ldadd)
test_series_model_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_series_model
TESTS += test_series_model

test_series_discovery_SOURCES = tests/test_series_discovery.cpp util/series_discovery.hpp
nodist_test_series_discovery_SOURCES = tests/test_series_discovery.moc
BUILT_SOURCES += tests/test_series_discovery.moc
CLEANFILES += tests/test_series_discovery.moc
test_series_discovery_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_series_discovery_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_series_discovery_LDADD = $(QT_QTEST_COMMON_ldadd)
test_series_discovery_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_series_discovery
TESTS += test_series_discovery

test_redesign_folder_model_SOURCES = tests/test_redesign_folder_model.cpp util/redesign_folder_model.cpp util/redesign_folder_model.hpp
nodist_test_redesign_folder_model_SOURCES = tests/test_redesign_folder_model.moc util/redesign_folder_model.moc.cpp
BUILT_SOURCES += tests/test_redesign_folder_model.moc
CLEANFILES += tests/test_redesign_folder_model.moc
test_redesign_folder_model_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_redesign_folder_model_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_redesign_folder_model_LDADD = $(QT_QTEST_COMMON_ldadd)
test_redesign_folder_model_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_redesign_folder_model
TESTS += test_redesign_folder_model

test_seek_preview_image_provider_SOURCES = tests/test_seek_preview_image_provider.cpp util/seek_preview_image_provider.hpp util/seek_preview_cache.hpp
nodist_test_seek_preview_image_provider_SOURCES = tests/test_seek_preview_image_provider.moc
BUILT_SOURCES += tests/test_seek_preview_image_provider.moc
CLEANFILES += tests/test_seek_preview_image_provider.moc
test_seek_preview_image_provider_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_seek_preview_image_provider_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_seek_preview_image_provider_LDADD = $(QT_QTEST_COMMON_ldadd)
test_seek_preview_image_provider_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_seek_preview_image_provider
TESTS += test_seek_preview_image_provider

test_subtitle_timeline_scanner_SOURCES = \
	tests/test_subtitle_timeline_scanner.cpp \
	player/subtitle_timeline_scanner.cpp player/subtitle_timeline_scanner.hpp
nodist_test_subtitle_timeline_scanner_SOURCES = tests/test_subtitle_timeline_scanner.moc
BUILT_SOURCES += tests/test_subtitle_timeline_scanner.moc
CLEANFILES += tests/test_subtitle_timeline_scanner.moc
test_subtitle_timeline_scanner_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_subtitle_timeline_scanner_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_subtitle_timeline_scanner_LDADD = $(QT_QTEST_COMMON_ldadd)
test_subtitle_timeline_scanner_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_subtitle_timeline_scanner
TESTS += test_subtitle_timeline_scanner

test_audio_waveform_scanner_SOURCES = \
	tests/test_audio_waveform_scanner.cpp \
	player/audio_waveform_scanner.cpp player/audio_waveform_scanner.hpp
nodist_test_audio_waveform_scanner_SOURCES = tests/test_audio_waveform_scanner.moc
BUILT_SOURCES += tests/test_audio_waveform_scanner.moc
CLEANFILES += tests/test_audio_waveform_scanner.moc
test_audio_waveform_scanner_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_audio_waveform_scanner_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_audio_waveform_scanner_LDADD = $(QT_QTEST_COMMON_ldadd)
test_audio_waveform_scanner_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_audio_waveform_scanner
TESTS += test_audio_waveform_scanner


# test_ml_model

test_ml_model_SOURCES = \
	tests/vlc_stub_modules.cpp tests/vlc_stub_modules.hpp \
	tests/test_ml_model.cpp \
	util/base_model.hpp util/base_model_p.hpp  util/base_model.cpp \
	util/listcache.hpp \
	util/vlctick.cpp util/vlctick.hpp \
	util/locallistcacheloader.hpp \
	medialibrary/mlqmltypes.hpp \
	medialibrary/mlbasemodel.hpp medialibrary/mlbasemodel.cpp \
	medialibrary/medialib.hpp medialibrary/medialib.cpp \
	medialibrary/mlthreadpool.hpp medialibrary/mlthreadpool.cpp \
	medialibrary/mljoinedthreadrunner.hpp medialibrary/mljoinedthreadrunner.cpp medialibrary/mltaskid.hpp \
	addons/addon_stream_lifetime.cpp addons/addon_stream_lifetime.hpp \
	medialibrary/mlmediastore.hpp medialibrary/mlmediastore.cpp \
	medialibrary/mlfileartwork.hpp medialibrary/mlfileartwork.cpp util/asynctask.hpp \
	medialibrary/mlfoldersmodel.hpp medialibrary/mlfoldersmodel.cpp \
	medialibrary/mlhelper.hpp medialibrary/mlhelper.cpp \
	playlist/playlist_controller.hpp playlist/playlist_controller.cpp \
	playlist/playlist_item.hpp playlist/playlist_item.cpp

nodist_test_ml_model_SOURCES = \
	tests/test_ml_model.moc \
	util/base_model.moc.cpp \
	util/listcache.moc.cpp \
	util/locallistcacheloader.moc.cpp \
	util/vlctick.moc.cpp \
	medialibrary/mlqmltypes.moc.cpp \
	medialibrary/mlbasemodel.moc.cpp \
	medialibrary/medialib.moc.cpp \
	medialibrary/mlthreadpool.moc.cpp \
	medialibrary/mlmediastore.moc.cpp \
	medialibrary/mlfileartwork.moc.cpp util/asynctask.moc.cpp \
	medialibrary/mlfoldersmodel.moc.cpp \
	playlist/playlist_controller.moc.cpp \
	playlist/playlist_item.moc.cpp

BUILT_SOURCES += tests/test_ml_model.moc
CLEANFILES += tests/test_ml_model.moc
test_ml_model_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_ml_model_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_ml_model_LDADD = $(QT_QTEST_COMMON_ldadd)
test_ml_model_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_ml_model
TESTS += test_ml_model

if HAVE_X
if HAVE_QT_XCB
if HAVE_XCB

test_x11_video_backdrop_SOURCES = tests/test_x11_video_backdrop.cpp \
	maininterface/compositor_x11_backdrop.cpp maininterface/compositor_x11_backdrop.hpp \
	maininterface/compositor_x11_utils.hpp util/video_backdrop_region.hpp
nodist_test_x11_video_backdrop_SOURCES = tests/test_x11_video_backdrop.moc
BUILT_SOURCES += tests/test_x11_video_backdrop.moc
CLEANFILES += tests/test_x11_video_backdrop.moc
test_x11_video_backdrop_CPPFLAGS = $(QT_QTEST_COMMON_cppflags) $(XCB_CFLAGS) $(XCB_RENDER_CFLAGS) $(XCB_COMPOSITE_CFLAGS)
test_x11_video_backdrop_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_x11_video_backdrop_LDADD = $(QT_QTEST_COMMON_ldadd) $(XCB_RENDER_LIBS) $(XCB_LIBS)
test_x11_video_backdrop_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_x11_video_backdrop
TESTS += test_x11_video_backdrop

test_x11_surface_size_SOURCES = \
	tests/test_x11_surface_size.cpp \
	maininterface/compositor_x11_uisurface.hpp maininterface/compositor_x11_uisurface.cpp \
	maininterface/compositor_common.hpp maininterface/compositor_common.cpp
nodist_test_x11_surface_size_SOURCES = \
	tests/test_x11_surface_size.moc \
	maininterface/compositor_x11_uisurface.moc.cpp \
	maininterface/compositor_common.moc.cpp
BUILT_SOURCES += tests/test_x11_surface_size.moc
CLEANFILES += tests/test_x11_surface_size.moc
test_x11_surface_size_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_x11_surface_size_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_x11_surface_size_LDADD = $(QT_QTEST_COMMON_ldadd)
test_x11_surface_size_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_x11_surface_size
TESTS += test_x11_surface_size

endif
endif
endif

# test_renderer_manager_model

test_renderer_manager_model_SOURCES = \
	tests/vlc_stub_modules.cpp tests/vlc_stub_modules.hpp \
	tests/test_renderer_manager_model.cpp \
    util/renderer_manager.hpp util/renderer_manager.cpp

nodist_test_renderer_manager_model_SOURCES = \
	tests/test_renderer_manager_model.moc \
    util/renderer_manager.moc.cpp

BUILT_SOURCES += tests/test_renderer_manager_model.moc
CLEANFILES += tests/test_renderer_manager_model.moc
test_renderer_manager_model_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_renderer_manager_model_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_renderer_manager_model_LDADD = $(QT_QTEST_COMMON_ldadd)
test_renderer_manager_model_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_renderer_manager_model
TESTS += test_renderer_manager_model

# test_thread_runner

test_thread_runner_SOURCES = \
	tests/test_thread_runner.cpp \
    medialibrary/mlthreadpool.cpp medialibrary/mlthreadpool.hpp

nodist_test_thread_runner_SOURCES = \
	tests/test_thread_runner.moc \
    medialibrary/mlthreadpool.moc.cpp

BUILT_SOURCES += tests/test_thread_runner.moc
CLEANFILES += tests/test_thread_runner.moc
test_thread_runner_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_thread_runner_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_thread_runner_LDADD = $(QT_QTEST_COMMON_ldadd)
test_thread_runner_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_thread_runner
TESTS += test_thread_runner

# test_vlcqtmessagehandler

test_vlcqtmessagehandler_SOURCES = tests/test_vlcqtmessagehandler.cpp util/vlcqtmessagehandler.cpp
nodist_test_vlcqtmessagehandler_SOURCES = tests/test_vlcqtmessagehandler.moc
BUILT_SOURCES += tests/test_vlcqtmessagehandler.moc
CLEANFILES += tests/test_vlcqtmessagehandler.moc
test_vlcqtmessagehandler_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_vlcqtmessagehandler_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_vlcqtmessagehandler_LDADD = $(QT_QTEST_COMMON_ldadd)
test_vlcqtmessagehandler_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_vlcqtmessagehandler
TESTS += test_vlcqtmessagehandler

# test_interface_translator

test_interface_translator_SOURCES = tests/test_interface_translator.cpp util/interface_translator.hpp util/interface_translator.cpp util/gettext_translator.hpp util/gettext_translator.cpp
nodist_test_interface_translator_SOURCES = tests/test_interface_translator.moc util/interface_translator.moc.cpp
BUILT_SOURCES += tests/test_interface_translator.moc
CLEANFILES += tests/test_interface_translator.moc
test_interface_translator_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_interface_translator_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_interface_translator_LDADD = $(QT_QTEST_COMMON_ldadd)
test_interface_translator_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_interface_translator
TESTS += test_interface_translator

# test_gettext_translator

test_gettext_translator_SOURCES = tests/test_gettext_translator.cpp util/gettext_translator.hpp util/gettext_translator.cpp
nodist_test_gettext_translator_SOURCES = tests/test_gettext_translator.moc
BUILT_SOURCES += tests/test_gettext_translator.moc
CLEANFILES += tests/test_gettext_translator.moc
test_gettext_translator_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_gettext_translator_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_gettext_translator_LDADD = $(QT_QTEST_COMMON_ldadd)
test_gettext_translator_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_gettext_translator
TESTS += test_gettext_translator

# test_embedded_file_browser

test_embedded_file_browser_SOURCES = tests/test_embedded_file_browser.cpp widgets/native/embedded_file_browser.hpp widgets/native/embedded_file_browser.cpp
nodist_test_embedded_file_browser_SOURCES = tests/test_embedded_file_browser.moc widgets/native/embedded_file_browser.moc.cpp
BUILT_SOURCES += tests/test_embedded_file_browser.moc
CLEANFILES += tests/test_embedded_file_browser.moc
test_embedded_file_browser_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_embedded_file_browser_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_embedded_file_browser_LDADD = $(QT_QTEST_COMMON_ldadd)
test_embedded_file_browser_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_embedded_file_browser
TESTS += test_embedded_file_browser

# test_playlist_tree_model

test_playlist_tree_model_SOURCES = tests/test_playlist_tree_model.cpp playlist/playlist_tree_model.hpp playlist/playlist_tree_model.cpp
nodist_test_playlist_tree_model_SOURCES = tests/test_playlist_tree_model.moc playlist/playlist_tree_model.moc.cpp
BUILT_SOURCES += tests/test_playlist_tree_model.moc
CLEANFILES += tests/test_playlist_tree_model.moc
test_playlist_tree_model_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_playlist_tree_model_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_playlist_tree_model_LDADD = $(QT_QTEST_COMMON_ldadd)
test_playlist_tree_model_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_playlist_tree_model
TESTS += test_playlist_tree_model

# test_video_window_geometry

test_video_window_geometry_SOURCES = tests/test_video_window_geometry.cpp util/video_window_geometry.hpp
nodist_test_video_window_geometry_SOURCES = tests/test_video_window_geometry.moc
BUILT_SOURCES += tests/test_video_window_geometry.moc
CLEANFILES += tests/test_video_window_geometry.moc
test_video_window_geometry_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_video_window_geometry_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_video_window_geometry_LDADD = $(QT_QTEST_COMMON_ldadd)
test_video_window_geometry_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_video_window_geometry
TESTS += test_video_window_geometry

# test_tray_notification_policy

test_tray_notification_policy_SOURCES = tests/test_tray_notification_policy.cpp dialogs/systray/notification_policy.hpp
nodist_test_tray_notification_policy_SOURCES = tests/test_tray_notification_policy.moc
BUILT_SOURCES += tests/test_tray_notification_policy.moc
CLEANFILES += tests/test_tray_notification_policy.moc
test_tray_notification_policy_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_tray_notification_policy_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_tray_notification_policy_LDADD = $(QT_QTEST_COMMON_ldadd)
test_tray_notification_policy_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_tray_notification_policy
TESTS += test_tray_notification_policy

# test_file_log_preferences

test_file_log_preferences_SOURCES = tests/test_file_log_preferences.cpp util/file_log_preferences.hpp
nodist_test_file_log_preferences_SOURCES = tests/test_file_log_preferences.moc
BUILT_SOURCES += tests/test_file_log_preferences.moc
CLEANFILES += tests/test_file_log_preferences.moc
test_file_log_preferences_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_file_log_preferences_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_file_log_preferences_LDADD = $(QT_QTEST_COMMON_ldadd)
test_file_log_preferences_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_file_log_preferences
TESTS += test_file_log_preferences

# test_preference_values
test_preference_values_SOURCES = tests/test_preference_values.cpp util/preference_values.hpp
nodist_test_preference_values_SOURCES = tests/test_preference_values.moc
BUILT_SOURCES += tests/test_preference_values.moc
CLEANFILES += tests/test_preference_values.moc
test_preference_values_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_preference_values_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_preference_values_LDADD = $(QT_QTEST_COMMON_ldadd)
test_preference_values_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_preference_values
TESTS += test_preference_values

# test_message_log

test_message_log_SOURCES = tests/test_message_log.cpp util/message_log.hpp
nodist_test_message_log_SOURCES = tests/test_message_log.moc
BUILT_SOURCES += tests/test_message_log.moc
CLEANFILES += tests/test_message_log.moc
test_message_log_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_message_log_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_message_log_LDADD = $(QT_QTEST_COMMON_ldadd)
test_message_log_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_message_log
TESTS += test_message_log

test_message_log_buffer_SOURCES = tests/test_message_log_buffer.cpp util/message_log_buffer.hpp
nodist_test_message_log_buffer_SOURCES = tests/test_message_log_buffer.moc
BUILT_SOURCES += tests/test_message_log_buffer.moc
CLEANFILES += tests/test_message_log_buffer.moc
test_message_log_buffer_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_message_log_buffer_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_message_log_buffer_LDADD = $(QT_QTEST_COMMON_ldadd)
test_message_log_buffer_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_message_log_buffer
TESTS += test_message_log_buffer

# test_warning_log_buffer

test_warning_log_buffer_SOURCES = tests/test_warning_log_buffer.cpp util/warning_log_buffer.hpp
nodist_test_warning_log_buffer_SOURCES = tests/test_warning_log_buffer.moc
BUILT_SOURCES += tests/test_warning_log_buffer.moc
CLEANFILES += tests/test_warning_log_buffer.moc
test_warning_log_buffer_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_warning_log_buffer_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_warning_log_buffer_LDADD = $(QT_QTEST_COMMON_ldadd)
test_warning_log_buffer_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_warning_log_buffer
TESTS += test_warning_log_buffer

# test_vlc_dialog_model

test_vlc_dialog_model_SOURCES = \
	tests/vlc_stub_modules.cpp tests/vlc_stub_modules.hpp \
	tests/test_vlc_dialog_model.cpp \
	dialogs/dialogs/dialogmodel.cpp

nodist_test_vlc_dialog_model_SOURCES = \
	tests/test_vlc_dialog_model.moc \
	dialogs/dialogs/dialogmodel.moc.cpp

BUILT_SOURCES += tests/test_vlc_dialog_model.moc
CLEANFILES += tests/test_vlc_dialog_model.moc
test_vlc_dialog_model_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_vlc_dialog_model_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_vlc_dialog_model_LDADD = $(QT_QTEST_COMMON_ldadd)
test_vlc_dialog_model_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_vlc_dialog_model
TESTS += test_vlc_dialog_model

# test_qt_postprocessing_preferences
test_qt_postprocessing_preferences_SOURCES = tests/vlc_stub_modules.cpp tests/test_postprocessing_preferences.cpp util/postprocessing_preferences.hpp
nodist_test_qt_postprocessing_preferences_SOURCES = tests/test_postprocessing_preferences.moc
BUILT_SOURCES += tests/test_postprocessing_preferences.moc
CLEANFILES += tests/test_postprocessing_preferences.moc
test_qt_postprocessing_preferences_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_postprocessing_preferences_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_postprocessing_preferences_LDADD = $(QT_QTEST_COMMON_ldadd)
test_qt_postprocessing_preferences_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_qt_postprocessing_preferences
TESTS += test_qt_postprocessing_preferences

# test_qt_csd_button_model
check_PROGRAMS += test_qt_csd_button_model
TESTS += test_qt_csd_button_model

test_qt_csd_button_model_SOURCES = \
	tests/vlc_stub_modules.cpp \
	tests/test_qt_csd_button_model.cpp \
	util/csdbuttonmodel.hpp \
	util/csdbuttonmodel.cpp
nodist_test_qt_csd_button_model_SOURCES = \
	tests/test_qt_csd_button_model.moc \
	util/csdbuttonmodel.moc.cpp
BUILT_SOURCES += tests/test_qt_csd_button_model.moc
CLEANFILES += tests/test_qt_csd_button_model.moc
test_qt_csd_button_model_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_csd_button_model_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_csd_button_model_LDADD = $(QT_QTEST_COMMON_ldadd)
test_qt_csd_button_model_LDFLAGS = $(QT_QTEST_COMMON_ldflags)


test_qt_track_folder_rules_SOURCES = tests/test_track_folder_rules.cpp ../../misc/medialibrary/track_rule_store.cpp
nodist_test_qt_track_folder_rules_SOURCES = tests/test_track_folder_rules.moc
BUILT_SOURCES += tests/test_track_folder_rules.moc
CLEANFILES += tests/test_track_folder_rules.moc
test_qt_track_folder_rules_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_track_folder_rules_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_track_folder_rules_LDADD = $(QT_QTEST_COMMON_ldadd)
test_qt_track_folder_rules_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_qt_track_folder_rules
TESTS += test_qt_track_folder_rules

test_qt_github_releases_SOURCES = \
	tests/test_github_releases.cpp util/release_info.cpp util/release_info.hpp \
	util/github_release_service.cpp util/github_release_service.hpp
nodist_test_qt_github_releases_SOURCES = tests/test_github_releases.moc util/github_release_service.moc.cpp
BUILT_SOURCES += tests/test_github_releases.moc
CLEANFILES += tests/test_github_releases.moc
test_qt_github_releases_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_github_releases_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_github_releases_LDADD = $(QT_QTEST_COMMON_ldadd)
test_qt_github_releases_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_qt_github_releases
TESTS += test_qt_github_releases

test_qt_media_identity_SOURCES = tests/test_media_identity.cpp util/media_identity.hpp util/media_fingerprint.hpp util/track_matching.hpp
nodist_test_qt_media_identity_SOURCES = tests/test_media_identity.moc
BUILT_SOURCES += tests/test_media_identity.moc
CLEANFILES += tests/test_media_identity.moc
test_qt_media_identity_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_media_identity_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_media_identity_LDADD = $(QT_QTEST_COMMON_ldadd)
test_qt_media_identity_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_qt_media_identity
TESTS += test_qt_media_identity

test_qt_subtitle_sources_SOURCES = \
	tests/test_subtitle_sources.cpp util/subtitle_source_service.cpp util/subtitle_source_service.hpp
nodist_test_qt_subtitle_sources_SOURCES = \
	tests/test_subtitle_sources.moc util/subtitle_source_service.moc.cpp
BUILT_SOURCES += tests/test_subtitle_sources.moc
CLEANFILES += tests/test_subtitle_sources.moc
test_qt_subtitle_sources_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_subtitle_sources_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_subtitle_sources_LDADD = libqt_addon_host.la $(QT_QTEST_COMMON_ldadd) $(ADDON_PLATFORM_LIBS)
test_qt_subtitle_sources_LDFLAGS = $(QT_QTEST_COMMON_ldflags) $(ADDON_PLATFORM_LDFLAGS)
check_PROGRAMS += test_qt_subtitle_sources
TESTS += test_qt_subtitle_sources

test_qt_track_list_model_SOURCES = \
	tests/test_track_list_model.cpp player/input_models.cpp player/input_models.hpp util/vlctick.cpp util/vlctick.hpp
nodist_test_qt_track_list_model_SOURCES = \
	tests/test_track_list_model.moc player/input_models.moc.cpp util/vlctick.moc.cpp
BUILT_SOURCES += tests/test_track_list_model.moc
CLEANFILES += tests/test_track_list_model.moc
test_qt_track_list_model_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_track_list_model_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_track_list_model_LDADD = $(QT_QTEST_COMMON_ldadd)
test_qt_track_list_model_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_qt_track_list_model
TESTS += test_qt_track_list_model

test_qt_effects_image_provider_SOURCES = \
	tests/test_effects_image_provider.cpp util/effects_image_provider.cpp util/effects_image_provider.hpp
nodist_test_qt_effects_image_provider_SOURCES = \
	tests/test_effects_image_provider.moc util/effects_image_provider.moc.cpp
BUILT_SOURCES += tests/test_effects_image_provider.moc
CLEANFILES += tests/test_effects_image_provider.moc
test_qt_effects_image_provider_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_effects_image_provider_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_effects_image_provider_LDADD = $(QT_QTEST_COMMON_ldadd)
test_qt_effects_image_provider_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_qt_effects_image_provider
TESTS += test_qt_effects_image_provider

test_qt_navigation_model_SOURCES = \
	tests/vlc_stub_modules.cpp \
	tests/test_navigation_model.cpp \
	maininterface/navigationmodel.hpp \
	maininterface/navigationmodel.cpp

nodist_test_qt_navigation_model_SOURCES = \
	tests/test_navigation_model.moc \
	maininterface/navigationmodel.moc.cpp

BUILT_SOURCES += tests/test_navigation_model.moc
CLEANFILES += tests/test_navigation_model.moc
test_qt_navigation_model_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_navigation_model_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_navigation_model_LDADD = $(QT_QTEST_COMMON_ldadd)
test_qt_navigation_model_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_qt_navigation_model
TESTS += test_qt_navigation_model

check_PROGRAMS += test_qt_artwork_discovery
TESTS += test_qt_artwork_discovery
test_qt_artwork_discovery_SOURCES = tests/test_artwork_discovery.cpp util/artwork_discovery.hpp util/artwork_store.hpp util/media_identity.hpp
nodist_test_qt_artwork_discovery_SOURCES = tests/test_artwork_discovery.moc
BUILT_SOURCES += tests/test_artwork_discovery.moc
CLEANFILES += tests/test_artwork_discovery.moc
test_qt_artwork_discovery_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_artwork_discovery_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_artwork_discovery_LDADD = $(QT_QTEST_COMMON_ldadd)
test_qt_artwork_discovery_LDFLAGS = $(QT_QTEST_COMMON_ldflags)

test_qt_filter_chain_edit_SOURCES = tests/test_filter_chain_edit.cpp util/filter_chain_edit.hpp
nodist_test_qt_filter_chain_edit_SOURCES = tests/test_filter_chain_edit.moc
BUILT_SOURCES += tests/test_filter_chain_edit.moc
CLEANFILES += tests/test_filter_chain_edit.moc
test_qt_filter_chain_edit_CPPFLAGS = $(QT_QTEST_COMMON_cppflags)
test_qt_filter_chain_edit_CXXFLAGS = $(QT_QTEST_COMMON_cxxflags)
test_qt_filter_chain_edit_LDADD = $(QT_QTEST_COMMON_ldadd)
test_qt_filter_chain_edit_LDFLAGS = $(QT_QTEST_COMMON_ldflags)
check_PROGRAMS += test_qt_filter_chain_edit
TESTS += test_qt_filter_chain_edit


endif

QML_LOG_COMPILER = $(builddir)/qml_test -input

if HAVE_QT_QUICK_TEST
TESTS += tests/tst_AddonSessions.qml tests/tst_AddonSourceConsent.qml
EXTRA_DIST += tests/tst_AddonInstaller.qml
TESTS += tests/tst_AddonInstaller.qml
EXTRA_DIST += tests/tst_AddonDiscovery.qml
TESTS += tests/tst_AddonDiscovery.qml
EXTRA_DIST += tests/tst_AddonSelections.qml tests/tst_AddonSelectionSurface.qml tests/tst_AddonRecovery.qml tests/tst_AddonTypedDialogs.qml
TESTS += tests/tst_AddonSelections.qml tests/tst_AddonSelectionSurface.qml tests/tst_AddonRecovery.qml tests/tst_AddonTypedDialogs.qml
EXTRA_DIST += tests/tst_AddonPlainText.qml
TESTS += tests/tst_AddonPlainText.qml
EXTRA_DIST += tests/tst_AddonMounts.qml
TESTS += tests/tst_AddonMounts.qml
EXTRA_DIST += tests/tst_AddonUi.qml tests/tst_AddonTheme.qml tests/tst_AddonThemeSelection.qml tests/tst_AddonThemeHost.qml tests/tst_AddonIntegration.qml tests/tst_AddonTrackPreparation.qml
TESTS += tests/tst_AddonUi.qml tests/tst_AddonTheme.qml tests/tst_AddonThemeSelection.qml tests/tst_AddonThemeHost.qml tests/tst_AddonIntegration.qml tests/tst_AddonTrackPreparation.qml
EXTRA_DIST += tests/tst_GeneralAppearance.qml
TESTS += tests/tst_GeneralAppearance.qml
EXTRA_DIST += tests/tst_RedesignCapture.qml
TESTS += tests/tst_RedesignCapture.qml
EXTRA_DIST += tests/tst_RedesignGotoTime.qml
TESTS += tests/tst_RedesignGotoTime.qml
qml_test_SOURCES = tests/qml_test.cpp tests/qml_test.hpp
nodist_qml_test_SOURCES = tests/qml_test.moc.cpp
CLEANFILES += $(nodist_qml_test_SOURCES)
qml_test_CPPFLAGS = $(libqt_plugin_la_CPPFLAGS)
qml_test_CXXFLAGS = $(AM_CXXFLAGS) $(QT_CFLAGS) -fPIC $(CXXFLAGS_qt) ${QT_QUICK_TEST_CFLAGS} -DQUICK_TEST_SOURCE_DIR="\"${srcdir}/tests\"" -I"${builddir}/tests"
qml_test_LDADD = $(QT_INDIRECT_LIBS) ${QT_QUICK_TEST_LIBS} $(QT_LIBS) $(LIBS_qt) libqml_module_util.a
qml_test_LDFLAGS = $(AM_LDFLAGS) $(QT_QUICK_TEST_LDFLAGS)
check_PROGRAMS += qml_test
EXTRA_DIST += tests/tst_FSM.qml tests/tst_Redesign.qml tests/tst_RedesignPreferences.qml tests/tst_RedesignPlayback.qml tests/tst_RedesignWaveform.qml tests/tst_RedesignAudio.qml tests/tst_RedesignVideo.qml tests/tst_RedesignOnboarding.qml tests/tst_RedesignMenu.qml
EXTRA_DIST += tests/tst_RedesignSubtitleTimeline.qml
EXTRA_DIST += tests/tst_RedesignTheme.qml
EXTRA_DIST += tests/tst_RedesignSliders.qml
EXTRA_DIST += tests/tst_RedesignActionMenu.qml
EXTRA_DIST += tests/tst_RedesignDialogSurfaces.qml
EXTRA_DIST += tests/tst_RedesignSettingsDialog.qml
EXTRA_DIST += tests/tst_RedesignShortcutEditor.qml
EXTRA_DIST += tests/tst_RedesignAppearance.qml tests/tst_RedesignCompactMenus.qml
EXTRA_DIST += tests/test_stereo_test_signal.cpp
TESTS += tests/tst_RedesignTheme.qml
TESTS += tests/tst_RedesignSliders.qml
TESTS += tests/tst_RedesignActionMenu.qml
TESTS += tests/tst_RedesignDialogSurfaces.qml
TESTS += tests/tst_RedesignSettingsDialog.qml
TESTS += tests/tst_RedesignShortcutEditor.qml
TESTS += tests/tst_RedesignAppearance.qml tests/tst_RedesignCompactMenus.qml
EXTRA_DIST += tests/tst_RedesignQueue.qml
EXTRA_DIST += tests/tst_RedesignIconSizing.qml
EXTRA_DIST += tests/tst_RedesignCursor.qml
EXTRA_DIST += tests/tst_RedesignBookmarks.qml
TESTS += tests/tst_RedesignBookmarks.qml
EXTRA_DIST += tests/tst_RedesignChapters.qml
TESTS += tests/tst_RedesignChapters.qml
EXTRA_DIST += tests/tst_RedesignEpisodes.qml
TESTS += tests/tst_RedesignEpisodes.qml
EXTRA_DIST += tests/tst_RedesignLibrary.qml
TESTS += tests/tst_RedesignLibrary.qml
EXTRA_DIST += tests/tst_RedesignAddons.qml
TESTS += tests/tst_RedesignAddons.qml
EXTRA_DIST += tests/tst_RedesignPlugins.qml
TESTS += tests/tst_RedesignPlugins.qml
TESTS += tests/tst_RedesignQueue.qml
TESTS += tests/tst_RedesignIconSizing.qml
TESTS += tests/tst_RedesignCursor.qml
EXTRA_DIST += tests/tst_RedesignNetwork.qml
TESTS += tests/tst_RedesignNetwork.qml
EXTRA_DIST += tests/tst_RedesignPlaylist.qml
TESTS += tests/tst_RedesignPlaylist.qml
EXTRA_DIST += tests/tst_RedesignPlaylistProperties.qml
TESTS += tests/tst_RedesignPlaylistProperties.qml
EXTRA_DIST += tests/tst_RedesignPlaylistTrackTarget.qml
TESTS += tests/tst_RedesignPlaylistTrackTarget.qml
EXTRA_DIST += tests/tst_RedesignMediaFavorite.qml tests/tst_RedesignCustomizationControls.qml \
	tests/tst_RedesignCustomizationEditor.qml tests/tst_RedesignAddToPlaylist.qml \
	tests/tst_RedesignPlayerLayout.qml tests/tst_RedesignPlayerChrome.qml \
	tests/tst_RedesignEqualizerScroll.qml tests/tst_RedesignChoices.qml tests/tst_RedesignPlayerPreset.qml \
	tests/tst_RedesignVolume.qml tests/tst_RedesignSpatializer.qml \
	tests/tst_RedesignSubtitles.qml tests/tst_RedesignToolTip.qml tests/tst_RedesignGlobalEqualizer.qml
TESTS += tests/tst_RedesignMediaFavorite.qml tests/tst_RedesignCustomizationControls.qml \
	tests/tst_RedesignCustomizationEditor.qml tests/tst_RedesignAddToPlaylist.qml \
	tests/tst_RedesignPlayerLayout.qml tests/tst_RedesignPlayerChrome.qml \
	tests/tst_RedesignEqualizerScroll.qml tests/tst_RedesignChoices.qml tests/tst_RedesignPlayerPreset.qml \
	tests/tst_RedesignVolume.qml tests/tst_RedesignSpatializer.qml \
	tests/tst_RedesignSubtitles.qml tests/tst_RedesignToolTip.qml tests/tst_RedesignGlobalEqualizer.qml
EXTRA_DIST += tests/tst_RedesignTrackAssignments.qml
TESTS += tests/tst_RedesignTrackAssignments.qml
EXTRA_DIST += tests/tst_RedesignFolderInfo.qml \
TESTS += tests/tst_RedesignFolderInfo.qml \
TESTS += tests/tst_RedesignSubtitleTimeline.qml
TESTS += tests/tst_FSM.qml tests/tst_Redesign.qml tests/tst_RedesignPreferences.qml tests/tst_RedesignPlayback.qml tests/tst_RedesignWaveform.qml tests/tst_RedesignAudio.qml tests/tst_RedesignVideo.qml tests/tst_RedesignOnboarding.qml tests/tst_RedesignMenu.qml
endif
endif

if ENABLE_QT
addon_referencedir = $(datadir)/vlc/addons/reference/thesubtitledb
addon_reference_DATA = $(top_srcdir)/contrib-addons/examples/thesubtitledb/addon.json $(top_srcdir)/contrib-addons/examples/thesubtitledb/LICENSE
addon_reference_distdir = $(datadir)/vlc/addons/reference/thesubtitledb/dist
addon_reference_dist_DATA = $(top_srcdir)/contrib-addons/examples/thesubtitledb/dist/addon.wasm
addon_reference_settingsdir = $(datadir)/vlc/addons/reference/thesubtitledb/settings
addon_reference_settings_DATA = $(top_srcdir)/contrib-addons/examples/thesubtitledb/settings/schema.json $(top_srcdir)/contrib-addons/examples/thesubtitledb/settings/ui.json
addon_reference_localesdir = $(datadir)/vlc/addons/reference/thesubtitledb/locales
addon_reference_locales_DATA = $(top_srcdir)/contrib-addons/examples/thesubtitledb/locales/en.json
addon_radio_browserdir = $(datadir)/vlc/addons/reference/radio-browser
addon_radio_browser_DATA = $(addprefix $(top_srcdir)/contrib-addons/examples/radio-browser/,addon.json LICENSE NOTICE.md THIRD_PARTY_NOTICES.md)
addon_radio_browser_distdir = $(datadir)/vlc/addons/reference/radio-browser/dist
addon_radio_browser_dist_DATA = $(top_srcdir)/contrib-addons/examples/radio-browser/dist/addon.wasm
addon_tvmazedir = $(datadir)/vlc/addons/reference/tvmaze
addon_tvmaze_DATA = $(top_srcdir)/contrib-addons/examples/tvmaze/addon.json $(top_srcdir)/contrib-addons/examples/tvmaze/LICENSE $(top_srcdir)/contrib-addons/examples/tvmaze/NOTICE.md
addon_tvmaze_distdir = $(datadir)/vlc/addons/reference/tvmaze/dist
addon_tvmaze_dist_DATA = $(top_srcdir)/contrib-addons/examples/tvmaze/dist/addon.wasm
addon_musicbrainzdir = $(datadir)/vlc/addons/reference/musicbrainz
addon_musicbrainz_DATA = $(addprefix $(top_srcdir)/contrib-addons/examples/musicbrainz/,addon.json LICENSE NOTICE.md THIRD_PARTY_NOTICES.md)
addon_musicbrainz_distdir = $(datadir)/vlc/addons/reference/musicbrainz/dist
addon_musicbrainz_dist_DATA = $(top_srcdir)/contrib-addons/examples/musicbrainz/dist/addon.wasm
addon_lastfmdir = $(datadir)/vlc/addons/reference/lastfm
addon_lastfm_DATA = $(addprefix $(top_srcdir)/contrib-addons/examples/lastfm/,addon.json LICENSE NOTICE.md THIRD_PARTY_NOTICES.md)
addon_lastfm_distdir = $(datadir)/vlc/addons/reference/lastfm/dist
addon_lastfm_dist_DATA = $(top_srcdir)/contrib-addons/examples/lastfm/dist/addon.wasm
addon_french_tvdir = $(datadir)/vlc/addons/reference/french-tv
addon_french_tv_DATA = $(addprefix $(top_srcdir)/contrib-addons/examples/french-tv/,addon.json LICENSE NOTICE.md THIRD_PARTY_NOTICES.md)
addon_french_tv_distdir = $(datadir)/vlc/addons/reference/french-tv/dist
addon_french_tv_dist_DATA = $(top_srcdir)/contrib-addons/examples/french-tv/dist/addon.wasm
addon_google_imagesdir = $(datadir)/vlc/addons/reference/google-images
addon_google_images_DATA = $(addprefix $(top_srcdir)/contrib-addons/examples/google-images/,addon.json LICENSE NOTICE.md THIRD_PARTY_NOTICES.md)
addon_google_images_distdir = $(datadir)/vlc/addons/reference/google-images/dist
addon_google_images_dist_DATA = $(top_srcdir)/contrib-addons/examples/google-images/dist/addon.wasm
endif

EXTRA_DIST += $(addon_reference_DATA) $(addon_reference_dist_DATA) $(addon_reference_settings_DATA) $(addon_reference_locales_DATA)
EXTRA_DIST += $(addon_tvmaze_DATA) $(addon_tvmaze_dist_DATA)
EXTRA_DIST += $(addon_musicbrainz_DATA) $(addon_musicbrainz_dist_DATA) $(addon_lastfm_DATA) $(addon_lastfm_dist_DATA)
EXTRA_DIST += $(addon_french_tv_DATA) $(addon_french_tv_dist_DATA) $(addon_google_images_DATA) $(addon_google_images_dist_DATA)
EXTRA_DIST += tests/build_addon_episode_tests.py tests/test_addon_episode_metadata.cpp tests/test_addon_artwork_consumers.cpp tests/addon_episode_probe.cpp tests/addon_metadata_probe.cpp tests/addon_artwork_probe.cpp util/artwork_approval.hpp
EXTRA_DIST += tests/build_addon_native_library_tests.py tests/test_addon_native_library_store.cpp

EXTRA_DIST += tests/stream-proxy/CMakeLists.txt
EXTRA_DIST += tests/addon_test_application_work.hpp

EXTRA_DIST += tests/app-shutdown/CMakeLists.txt

EXTRA_DIST += tests/ml-joined-runner/CMakeLists.txt

EXTRA_DIST += tests/native-authority/CMakeLists.txt

EXTRA_DIST += tests/store-authority/CMakeLists.txt

EXTRA_DIST += tests/addon_native_artwork_test_owner.hpp

EXTRA_DIST += \
	tests/session-runtime/CMakeLists.txt \
	tests/session-runtime/native_session_fixture.c \
	tests/session-runtime/native_session_fixture.h \
	tests/session-runtime/native_fixture_baseline.c \
	tests/session-runtime/session_test_relay.hpp \
	tests/test_addon_session_host.cpp \
	tests/test_addon_session_websocket.cpp \
	tests/test_addon_native_session_player.cpp

EXTRA_DIST += tests/test_addon_git_objects.cpp tests/fixtures/addon-git-http-fixture.py

EXTRA_DIST += tests/worker-lifetime/CMakeLists.txt

EXTRA_DIST += tests/session-ui/CMakeLists.txt tests/test_addon_session_ui.cpp tests/tst_AddonSessions.qml

EXTRA_DIST += tests/player-sessions/CMakeLists.txt tests/test_addon_player_sessions.cpp

EXTRA_DIST += tests/package-namespaces/CMakeLists.txt

EXTRA_DIST += tests/invocation-guard/CMakeLists.txt tests/test_addon_invocation_guard.cpp

EXTRA_DIST += tests/source-consent-ui/CMakeLists.txt tests/test_addon_source_consent_ui.cpp tests/tst_AddonSourceConsent.qml

EXTRA_DIST += tests/builtin-ids/CMakeLists.txt

EXTRA_DIST += tests/test_addon_metadata_family_plan.cpp tests/test_addon_native_provider_context.cpp \
	tests/fixtures/addon_native_provider_fixture.c tests/fixtures/addon_native_provider_fixture.h \
	tests/test_addon_native_metadata_owner.cpp

EXTRA_DIST += tests/discovery-source/CMakeLists.txt tests/discovery-source/native_module_baseline.cpp \
	tests/fixtures/addon_source_tls_fixture.hpp

EXTRA_DIST += $(addon_radio_browser_DATA) $(addon_radio_browser_dist_DATA) \
    $(addprefix $(top_srcdir)/contrib-addons/examples/radio-browser/,Cargo.toml Cargo.lock build.sh README.md src/lib.rs src/tests.rs tests/test_worker.py tests/native/CMakeLists.txt tests/native/live_probe.cpp)
if ENABLE_QT
if BUILD_ADDON_RUNTIME
.PHONY: check-radio-browser-worker
check-radio-browser-worker: vlc-addon-worker$(EXEEXT)
	$(AM_V_at)$(PYTHON3) -B $(top_srcdir)/contrib-addons/examples/radio-browser/tests/test_worker.py --worker "$(abs_builddir)/vlc-addon-worker$(EXEEXT)"
check-local: check-radio-browser-worker
endif
endif
