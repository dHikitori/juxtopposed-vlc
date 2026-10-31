#
# Qt-based interface module
#

qt6 = import('qt6')
fs = import('fs')

qt_include_dir = include_directories('.')

addon_host_sources = files(
    'addons/addon_manifest.cpp',
    'addons/addon_manifest.hpp',
    'addons/addon_metadata_attempt.cpp',
    'addons/addon_metadata_attempt.hpp',
    'addons/addon_prepared_metadata_invocation.cpp',
    'addons/addon_prepared_metadata_invocation.hpp',
    'addons/addon_prepared_artwork_asset.hpp',
    'addons/addon_metadata_registry_epoch.cpp',
    'addons/addon_metadata_registry_epoch.hpp',
    'addons/addon_artwork_attempt_plan.cpp',
    'addons/addon_artwork_attempt_plan.hpp',
    'addons/addon_package_configuration.hpp',
    'addons/addon_metadata_completion_policy.cpp',
    'addons/addon_metadata_completion_policy.hpp',
    'addons/addon_metadata_identity.hpp',
    'addons/addon_package_store.cpp',
    'addons/addon_package_store.hpp',
    'addons/addon_store_authority.cpp',
    'addons/addon_store_authority.hpp',
    'addons/addon_store_process_fence.cpp',
    'addons/addon_store_process_fence.hpp',
    'addons/addon_store_profile_generation.cpp',
    'addons/addon_store_profile_generation.hpp',
    'addons/addon_store_profile_protocol.cpp',
    'addons/addon_store_profile_protocol.hpp',
    'addons/addon_store_profile_socket.cpp',
    'addons/addon_store_profile_socket.hpp',
    'addons/addon_store_profile_coordinator.cpp',
    'addons/addon_store_profile_coordinator.hpp',
    'addons/addon_store_profile_control.cpp',
    'addons/addon_store_profile_control.hpp',
    'addons/addon_store_profile_client.cpp',
    'addons/addon_store_profile_client.hpp',
    'addons/addon_store_profile_session.cpp',
    'addons/addon_store_profile_session.hpp',
    'addons/addon_profile_host.cpp',
    'addons/addon_profile_host.hpp',
    'addons/addon_process_launch.cpp',
    'addons/addon_process_launch.hpp',
    'addons/addon_precompiled_modules.cpp',
    'addons/addon_candidate_check.cpp',
    'addons/addon_candidate_check.hpp',
    'addons/addon_precompiled_modules.hpp',
    'addons/addon_native_wake.cpp',
    'addons/addon_native_wake.hpp',
    'addons/addon_native_notifications.cpp',
    'addons/addon_native_notifications.hpp',
    'addons/addon_catalogue_store_lock.hpp',
    'addons/addon_credentials.cpp',
    'addons/addon_credentials.hpp',
    'addons/addon_device_authorization.cpp',
    'addons/addon_manager_accounts.cpp',
    'addons/addon_guest_projections.cpp',
    'addons/addon_guest_projections.hpp',
    'addons/addon_account_setup.cpp',
    'addons/addon_account_setup.hpp',
    'addons/addon_device_authorization.hpp',
    'addons/addon_manager.cpp',
    'addons/addon_manager.hpp',
    'addons/addon_package_reader.hpp',
    'addons/addon_metadata_catalogue_source.hpp',
    'addons/addon_package_summaries.cpp',
    'addons/addon_package_summaries.hpp',
    'addons/addon_accepted_identities.cpp',
    'addons/addon_accepted_identities.hpp',
    'addons/addon_identity_choice.cpp',
    'addons/addon_identity_choice.hpp',
    'addons/addon_catalogue_view.cpp',
    'addons/addon_catalogue_view.hpp',
    'addons/addon_catalogue_view_manager.cpp',
    'addons/addon_display_catalogue_reader.hpp',
    'addons/addon_distribution_bootstrap.cpp',
    'addons/addon_distribution_bootstrap.hpp',
    'addons/addon_native_authority.hpp',
    'addons/addon_network.cpp',
    'addons/addon_network.hpp',
    'addons/addon_public_network_policy.cpp',
    'addons/addon_public_network_policy.hpp',
    'addons/addon_worker.cpp',
    'addons/addon_worker.hpp',
    'addons/addon_metadata_coordinator.cpp',
    'addons/addon_metadata_coordinator.hpp',
    'addons/addon_result_retention.hpp',
    'addons/addon_metadata_family_plan.cpp',
    'addons/addon_metadata_family_plan.hpp',
    'addons/addon_metadata_choices.cpp',
    'addons/addon_metadata_choices.hpp',
    'addons/addon_native_provider_context.cpp',
    'addons/addon_native_provider_context.hpp',
    'addons/addon_core_metadata_admission.cpp',
    'addons/addon_core_metadata_admission.hpp',
    'addons/addon_artwork_attempt_keeper.cpp',
    'addons/addon_artwork_attempt_keeper.hpp',
    'addons/addon_artwork_attempt_aggregate.cpp',
    'addons/addon_artwork_attempt_aggregate.hpp',
    'addons/addon_artwork_lookup_runner.cpp',
    'addons/addon_artwork_lookup_runner.hpp',
    'addons/addon_native_provider_artwork_assets.cpp',
    'addons/addon_native_provider_artwork_assets.hpp',
    'addons/addon_native_metadata_owner.cpp',
    'addons/addon_native_metadata_owner.hpp',
    'addons/addon_ui_registry.cpp',
    'addons/addon_ui_registry.hpp',
    'addons/addon_panel_icon.cpp',
    'addons/addon_panel_icon.hpp',
    'addons/addon_media_siblings.cpp',
    'addons/addon_media_siblings.hpp',
    'addons/addon_ui_assets.cpp',
    'addons/addon_ui_assets.hpp',
    'addons/addon_metadata_service.cpp',
    'addons/addon_metadata_service.hpp',
    'addons/addon_discovery_coordinator.cpp',
    'addons/addon_discovery_coordinator.hpp',
    'addons/addon_queue_transactions.cpp',
    'addons/addon_queue_transactions.hpp',
    'addons/addon_discovery_service.cpp',
    'addons/addon_discovery_service.hpp',
    'addons/addon_player_discovery.cpp',
    'addons/addon_player_discovery.hpp',
    'addons/addon_source_admissions.cpp',
    'addons/addon_source_admissions.hpp',
    'addons/addon_stream_manifest.cpp',
    'addons/addon_stream_manifest.hpp',
    'addons/addon_stream_http.cpp',
    'addons/addon_stream_http.hpp',
    'addons/addon_stream_policy.cpp',
    'addons/addon_stream_policy.hpp',
    'addons/addon_stream_sockets.cpp',
    'addons/addon_stream_sockets.hpp',
    'addons/addon_stream_lifetime.cpp',
    'addons/addon_stream_lifetime.hpp',
    'addons/addon_stream_shutdown.cpp',
    'addons/addon_stream_shutdown.hpp',
    'addons/addon_app_shutdown.cpp',
    'addons/addon_app_shutdown.hpp',
    'addons/addon_stream_dns.cpp',
    'addons/addon_stream_dns.hpp',
    'addons/addon_stream_proxy.cpp',
    'addons/addon_stream_proxy.hpp',
    'addons/addon_stream_proxy_native.cpp',
    'addons/addon_stream_proxy_native.hpp',
    'addons/addon_websocket_codec.cpp',
    'addons/addon_websocket_codec.hpp',
    'addons/addon_source_consent.cpp',
    'addons/addon_source_consent.hpp',
    'addons/addon_visualizer_catalog.cpp',
    'addons/addon_visualizer_catalog.hpp',
    'addons/addon_visualizer_resources.cpp',
    'addons/addon_visualizer_resources.hpp',
    'addons/addon_visualizer_worker.cpp',
    'addons/addon_visualizer_worker.hpp',
    'addons/addon_visualizer_render_queue.cpp',
    'addons/addon_visualizer_render_queue.hpp',
    'addons/addon_native_source_resolver.cpp',
    'addons/addon_native_source_resolver.hpp',
    'addons/addon_discovery_source_owner.cpp',
    'addons/addon_discovery_source_owner.hpp',
    'addons/addon_source_consent_ui_owner.cpp',
    'addons/addon_native_player_analysis.cpp',
    'addons/addon_native_player_analysis.hpp',
    'addons/addon_visualizer_worker_runtime.cpp',
    'addons/addon_visualizer_activation.hpp',
    'addons/addon_visualizer_clock.hpp',
    'addons/addon_native_queue_store.cpp',
    'addons/addon_native_queue_store.hpp',
    'addons/addon_native_library_store.cpp',
    'addons/addon_native_library_store.hpp',
    'addons/addon_native_library_targets.cpp',
    'addons/addon_native_library_targets.hpp',
    'addons/addon_theme_service.cpp',
    'addons/addon_theme_service.hpp',
    'addons/addon_theme_document.cpp',
    'addons/addon_theme_document.hpp',
    'addons/addon_sessions.cpp',
    'addons/addon_sessions.hpp',
    'addons/addon_session_player.hpp',
    'addons/addon_session_websocket.cpp',
    'addons/addon_session_websocket.hpp',
    'addons/addon_session_host.cpp',
    'addons/addon_session_host.hpp',
    'addons/addon_session_controller.cpp',
    'addons/addon_session_controller.hpp',
    'addons/addon_session_ui.cpp',
    'addons/addon_session_ui.hpp',
    'addons/addon_session_ui_manager.cpp',
    'addons/addon_source_consent_ui.cpp',
    'addons/addon_source_consent_ui.hpp',
    'addons/addon_native_session_player.cpp',
    'addons/addon_native_session_player.hpp',
    'addons/addon_player_sessions.cpp',
    'addons/addon_player_sessions.hpp',
    'addons/addon_audio_analysis.cpp',
    'addons/addon_audio_analysis.hpp',
    'addons/addon_visual_scene.cpp',
    'addons/addon_visual_scene.hpp',
    'addons/addon_visualizer_parameters.cpp',
    'addons/addon_visualizer_parameters.hpp',
    'addons/addon_visualizers.cpp',
    'addons/addon_visualizers.hpp',
    'addons/addon_settings_migrations.cpp',
    'addons/addon_settings_migrations.hpp',
    'addons/addon_storage.cpp',
    'addons/addon_storage.hpp',
    'addons/addon_storage_data.cpp',
    'addons/addon_storage_data.hpp',
    'addons/addon_host_services.cpp',
    'addons/addon_host_services.hpp',
    'addons/addon_repository_updates.cpp',
    'addons/addon_repository_updates.hpp',
    'addons/addon_repository_jobs.cpp',
    'addons/addon_repository_jobs.hpp',
    'addons/addon_repository_cancellation.hpp',
    'addons/addon_playable_gate.cpp',
    'addons/addon_playable_gate.hpp',
    'addons/addon_artwork_assets.cpp',
    'addons/addon_artwork_assets.hpp',
    'addons/addon_artwork_image.cpp',
    'addons/addon_artwork_image.hpp',
    'addons/addon_native_image_formats.hpp',
    'addons/addon_native_artwork_assets.cpp',
    'addons/addon_native_artwork_assets.hpp',
)
addon_host_moc_headers = files(
    'addons/addon_guest_projections.hpp',
    'addons/addon_media_siblings.hpp',
    'addons/addon_account_setup.hpp',
    'addons/addon_device_authorization.hpp',
    'addons/addon_native_metadata_owner.hpp',
    'addons/addon_identity_choice.hpp',
    'addons/addon_manager.hpp',
    'addons/addon_package_summaries.hpp',
    'addons/addon_catalogue_view.hpp',
    'addons/addon_network.hpp',
    'addons/addon_worker.hpp',
    'addons/addon_metadata_coordinator.hpp',
    'addons/addon_ui_registry.hpp',
    'addons/addon_ui_assets.hpp',
    'addons/addon_metadata_service.hpp',
    'addons/addon_discovery_coordinator.hpp',
    'addons/addon_queue_transactions.hpp',
    'addons/addon_discovery_service.hpp',
    'addons/addon_native_queue_store.hpp',
    'addons/addon_discovery_source_owner.hpp',
    'addons/addon_native_library_store.hpp',
    'addons/addon_native_library_targets.hpp',
    'addons/addon_theme_service.hpp',
    'addons/addon_sessions.hpp',
    'addons/addon_session_host.hpp',
    'addons/addon_visualizers.hpp',
    'addons/addon_session_ui.hpp',
    'addons/addon_player_sessions.hpp',
    'addons/addon_source_consent_ui.hpp',
    'addons/addon_visualizer_catalog.hpp',
    'addons/addon_visualizer_resources.hpp',
    'addons/addon_visualizer_worker.hpp',
    'addons/addon_visualizer_render_queue.hpp',
    'addons/addon_host_services.hpp',
    'addons/addon_playable_gate.hpp',
    'addons/addon_artwork_assets.hpp',
)


subtitle_consumer_key = custom_target('subtitle_consumer_key',
    output: 'subtitle_consumer_key.hpp',
    command: [find_program('python3'), files('scripts/subtitle-consumer-key.py'), '--output', '@OUTPUT@'],
    build_always_stale: true)

qt_extra_deps = []
qt_extra_flags = ['-DQT_STRICT_ITERATORS','-DQT_USE_QSTRINGBUILDER','-DQT_NO_CAST_TO_ASCII','-DQT_NO_CAST_FROM_BYTEARRAY']
qt_cppargs = []
qt_link_args = [gc_sections_linkargs]
if cpp.get_linker_id() == 'ld.bfd' and cpp.has_link_argument('-Wl,--no-keep-memory')
    # Reduce GNU ld memory use for the large Qt plugin.
    qt_link_args += '-Wl,--no-keep-memory'
endif

qt_dependency_method = host_machine.system() == 'darwin' ? 'config-tool' : 'auto'
qt6_dep = dependency('qt6',
    # Official macOS SDKs omit .pc files. Prefer qmake so a selected SDK is not
    # silently replaced by a different Qt version from Homebrew's pkg-config.
    method: qt_dependency_method,
    # Note that Qt < 6.2.5 does not provide pc files!
    version: '>=6.2',
    modules: [
        'Core', 'Gui', 'Widgets', 'Qml', 'Network',
        'Quick', 'QuickControls2'
        ],
    private_headers: true,
    required: get_option('qt'))

# Qt module doesn't provide the framework path to moc executable, so we
# need to help moc find them. It's needed for QtQml/ module registration.
if host_machine.system() == 'darwin' and qt6_dep.found()
    qt_libdir = qt6_dep.get_variable(pkgconfig: 'libdir', configtool: 'QT_INSTALL_LIBS')
    qt_extra_flags += '-F' + qt_libdir
    if host_machine.cpu_family() == 'aarch64' and qt6_dep.version().version_compare('<6.9')
        # Qt 6.8's qyieldcpu.h calls __yield without its ARM ACLE declaration
        # when newer Apple Clang recognizes it as a builtin.
        qt_extra_flags += ['-include', 'arm_acle.h']
    endif
    # The platform compositor messages the native video view directly.
    qt_link_args += '-lobjc'
endif

if qt6_dep.found()
    if not z_dep.found()
        error('The Qt add-on image host needs zlib. Install zlib or disable Qt.')
    endif
    qt_extra_deps += z_dep
endif

moc_headers = files(
    'addons/addon_visualizer_surface.hpp',
    'dialogs/bookmarks/bookmarks.hpp',
    'dialogs/dialogs/dialogmodel.hpp',
    'dialogs/dialogs_provider.hpp',
    'dialogs/epg/EPGChannels.hpp',
    'dialogs/epg/EPGProgram.hpp',
    'dialogs/epg/EPGRuler.hpp',
    'dialogs/epg/EPGView.hpp',
    'dialogs/epg/EPGWidget.hpp',
    'dialogs/epg/epg.hpp',
    'dialogs/extended/extended.hpp',
    'dialogs/extended/extended_panels.hpp',
    'dialogs/extensions/extensions.hpp',
    'dialogs/extensions/extensions_manager.hpp',
    'dialogs/fingerprint/chromaprint.hpp',
    'dialogs/fingerprint/fingerprintdialog.hpp',
    'dialogs/firstrun/firstrunwizard.hpp',
    'dialogs/gototime/gototime.hpp',
    'dialogs/help/aboutmodel.hpp',
    'dialogs/help/help.hpp',
    'dialogs/mediainfo/info_panels.hpp',
    'dialogs/mediainfo/info_widgets.hpp',
    'dialogs/mediainfo/mediainfo.hpp',
    'dialogs/messages/messages.hpp',
    'dialogs/open/open.hpp',
    'dialogs/open/open_panels.hpp',
    'dialogs/open/openurl.hpp',
    'dialogs/plugins/plugins.hpp',
    'dialogs/podcast/podcast_configuration.hpp',
    'dialogs/preferences/complete_preferences.hpp',
    'dialogs/preferences/expert_model.hpp',
    'dialogs/preferences/expert_view.hpp',
    'dialogs/preferences/preferences.hpp',
    'dialogs/preferences/redesign_preferences.hpp',
    'dialogs/preferences/preferences_widgets.hpp',
    'dialogs/preferences/simple_preferences.hpp',
    'dialogs/sout/convert.hpp',
    'dialogs/sout/profile_selector.hpp',
    'dialogs/sout/sout.hpp',
    'dialogs/sout/sout_widgets.hpp',
    'dialogs/systray/systray.hpp',
    'dialogs/toolbar/controlbar_profile.hpp',
    'dialogs/toolbar/controlbar_profile_model.hpp',
    'dialogs/playlists/playlists.hpp',
    'dialogs/vlm/vlm.hpp',
    'maininterface/compositor.hpp',
    'maininterface/compositor_common.hpp',
    'maininterface/compositor_dummy.hpp',
    'maininterface/compositor_platform.hpp',
    'maininterface/interface_window_handler.hpp',
    'maininterface/mainctx.hpp',
    'maininterface/mainctx_submodels.hpp',
    'maininterface/mainui.hpp',
    'maininterface/navigationmodel.hpp',
    'maininterface/videobackdrop.hpp',
    'maininterface/videosurface.hpp',
    'maininterface/video_window_handler.hpp',
    'medialibrary/medialib.hpp',
    'medialibrary/mlmediastore.hpp',
    'medialibrary/mlfileartwork.hpp',
    'medialibrary/mlalbummodel.hpp',
    'medialibrary/mlaudiomodel.hpp',
    'medialibrary/mlartistmodel.hpp',
    'medialibrary/mlbookmarkmodel.hpp',
    'medialibrary/mlbasemodel.hpp',
    'medialibrary/mlfoldersmodel.hpp',
    'medialibrary/mlgenremodel.hpp',
    'medialibrary/mlthreadpool.hpp',
    'medialibrary/mlqmltypes.hpp',
    'medialibrary/mlrecentmediamodel.hpp',
    'medialibrary/mlrecentaudiomodel.hpp',
    'medialibrary/mlplayedartistmodel.hpp',
    'medialibrary/mlrecentvideomodel.hpp',
    'medialibrary/mlurlmodel.hpp',
    'medialibrary/mlvideo.hpp',
    'medialibrary/mlmediamodel.hpp',
    'medialibrary/mlvideomodel.hpp',
    'medialibrary/mlplaylistlistmodel.hpp',
    'medialibrary/mlplaylistmodel.hpp',
    'medialibrary/mlvideofoldersmodel.hpp',
    'medialibrary/mlvideogroupsmodel.hpp',
    'medialibrary/thumbnailcollector.hpp',
    'menus/custom_menus.hpp',
    'menus/qml_menu_wrapper.hpp',
    'menus/menus.hpp',
    'network/addonsmodel.hpp',
    'network/devicesourceprovider.hpp',
    'network/networkbasemodel.hpp',
    'network/networkdevicemodel.hpp',
    'network/networksourcesmodel.hpp',
    'network/networkplaylistmodel.hpp',
    'network/networkmediamodel.hpp',
    'network/standardpathmodel.hpp',
    'style/systempalette.hpp',
    'style/vlcicons.hpp',
    'style/colorcontext.hpp',
    'player/input_models.hpp',
    'player/delay_estimator.hpp',
    'player/player_controller.hpp',
    'player/series_model.hpp',
    'player/episode_metadata.hpp',
    'player/player_controlbar_model.hpp',
    'player/control_list_model.hpp',
    'player/control_list_filter.hpp',
    'playlist/playlist_controller.hpp',
    'playlist/playlist_item.hpp',
    'playlist/playlist_model.hpp',
    'playlist/playlist_tree_model.hpp',
    'util/github_release_service.hpp',
    'util/subtitle_source_service.hpp',
    'util/asynctask.hpp',
    'util/accessible_compat_attached.hpp',
    'util/audio_device_model.hpp',
    'util/redesign_folder_model.hpp',
    'util/workerthreadset.hpp',
    'util/base_model.hpp',
    'util/color_scheme_model.hpp',
    'util/ui_notifier.hpp',
    'util/color_svg_image_provider.hpp',
    'util/vlcaccess_image_provider.hpp',
    'util/csdbuttonmodel.hpp',
    'util/csdmenu.hpp',
    'util/imageluminanceextractor.hpp',
    'util/keyhelper.hpp',
    'util/listcache.hpp',
    'util/locallistcacheloader.hpp',
    'util/navigation_history.hpp',
    'util/item_key_event_filter.hpp',
    'util/effects_image_provider.hpp',
    'util/flickable_scroll_handler.hpp',
    'util/renderer_manager.hpp',
    'util/validators.hpp',
    'util/varchoicemodel.hpp',
    'util/variables.hpp',
    'util/vlchotkeyconverter.hpp',
    'util/vlctick.hpp',
    'util/dismiss_popup_event_filter.hpp',
    'util/list_selection_model.hpp',
    'util/qsgtextureview.hpp',
    'util/textureproviderobserver.hpp',
    'util/kirigamiwheelhandler.hpp',
    'util/textureproviderindirection.hpp',
    'util/sgmanipulator.hpp',
    'util/pointingtooltipattached.hpp',
    'widgets/native/animators.hpp',
    'widgets/native/csdthemeimage.hpp',
    'widgets/native/customwidgets.hpp',
    'widgets/native/embedded_file_browser.hpp',
    'util/interface_translator.hpp',
    'widgets/native/interface_widgets.hpp',
    'widgets/native/navigation_attached.hpp',
    'widgets/native/mlfolderseditor.hpp',
    'widgets/native/searchlineedit.hpp',
    'widgets/native/viewblockingrectangle.hpp',
    'widgets/native/softwarebackdropblur.hpp',
)

if host_system == 'windows'
    moc_headers += files(
        'maininterface/mainctx_win32.hpp',
        'maininterface/compositor_win7.hpp',
        'maininterface/compositor_dcomp.hpp',
        'maininterface/compositor_dcomp_acrylicsurface.hpp',
    )
endif

if (x11_dep.found() and
    xcb_dep.found() and
    xcb_damage_dep.found() and
    xcb_xfixes_dep.found())
    moc_headers += files(
        'maininterface/compositor_x11.hpp',
        'maininterface/compositor_x11_renderclient.hpp',
        'maininterface/compositor_x11_renderwindow.hpp',
        'maininterface/compositor_x11_uisurface.hpp',
        'maininterface/compositor_x11_utils.hpp',
    )
endif

if have_wayland and cpp.has_header('qpa/qplatformnativeinterface.h', dependencies: qt6_dep)
    moc_headers += files('maininterface/compositor_wayland.hpp')
endif

qt_plugin_sources = files(
    'addons/addon_visualizer_surface.cpp',
    'addons/addon_visualizer_surface.hpp',
    'addons/addon_player_visualizers.cpp',
    'addons/addon_player_visualizers.hpp',
    'qt.cpp',
    'qt.hpp',
    'plugins.hpp',
    'dialogs/bookmarks/bookmarks.cpp',
    'dialogs/bookmarks/bookmarks.hpp',
    'dialogs/dialogs/dialogmodel.cpp',
    'dialogs/dialogs/dialogmodel.hpp',
    'dialogs/dialogs_provider.cpp',
    'dialogs/dialogs_provider.hpp',
    'dialogs/epg/EPGChannels.cpp',
    'dialogs/epg/EPGChannels.hpp',
    'dialogs/epg/EPGItem.cpp',
    'dialogs/epg/EPGItem.hpp',
    'dialogs/epg/EPGProgram.cpp',
    'dialogs/epg/EPGProgram.hpp',
    'dialogs/epg/EPGRuler.cpp',
    'dialogs/epg/EPGRuler.hpp',
    'dialogs/epg/EPGView.cpp',
    'dialogs/epg/EPGView.hpp',
    'dialogs/epg/EPGWidget.cpp',
    'dialogs/epg/EPGWidget.hpp',
    'dialogs/epg/epg.cpp',
    'dialogs/epg/epg.hpp',
    'dialogs/extended/extended.cpp',
    'dialogs/extended/extended.hpp',
    'dialogs/extended/extended_panels.cpp',
    'dialogs/extended/extended_panels.hpp',
    'dialogs/extensions/extensions.cpp',
    'dialogs/extensions/extensions.hpp',
    'dialogs/extensions/extensions_manager.cpp',
    'dialogs/extensions/extensions_manager.hpp',
    'dialogs/fingerprint/chromaprint.cpp',
    'dialogs/fingerprint/chromaprint.hpp',
    'dialogs/fingerprint/fingerprintdialog.cpp',
    'dialogs/fingerprint/fingerprintdialog.hpp',
    'dialogs/firstrun/firstrunwizard.cpp',
    'dialogs/firstrun/firstrunwizard.hpp',
    'dialogs/gototime/gototime.cpp',
    'dialogs/gototime/gototime.hpp',
    'dialogs/help/aboutmodel.hpp',
    'dialogs/help/help.cpp',
    'dialogs/help/help.hpp',
    'dialogs/mediainfo/info_panels.cpp',
    'dialogs/mediainfo/info_panels.hpp',
    'dialogs/mediainfo/info_widgets.cpp',
    'dialogs/mediainfo/info_widgets.hpp',
    'dialogs/mediainfo/mediainfo.cpp',
    'dialogs/mediainfo/mediainfo.hpp',
    'dialogs/messages/messages.cpp',
    'dialogs/messages/messages.hpp',
    'dialogs/open/open.cpp',
    'dialogs/open/open.hpp',
    'dialogs/open/open_panels.cpp',
    'dialogs/open/open_panels.hpp',
    'dialogs/open/openurl.cpp',
    'dialogs/open/openurl.hpp',
    'dialogs/plugins/plugins.cpp',
    'dialogs/plugins/plugins.hpp',
    'dialogs/podcast/podcast_configuration.cpp',
    'dialogs/podcast/podcast_configuration.hpp',
    'dialogs/preferences/complete_preferences.cpp',
    'dialogs/preferences/complete_preferences.hpp',
    'dialogs/preferences/expert_model.cpp',
    'dialogs/preferences/expert_model.hpp',
    'dialogs/preferences/expert_view.cpp',
    'dialogs/preferences/expert_view.hpp',
    'dialogs/preferences/redesign_preferences.cpp',
    'player/stereo_test_player.cpp',
    'player/stereo_test_player.hpp',
    'player/stereo_test_signal.hpp',
    'dialogs/preferences/preferences.cpp',
    'dialogs/preferences/preferences.hpp',
    'dialogs/preferences/preferences_widgets.cpp',
    'dialogs/preferences/preferences_widgets.hpp',
    'dialogs/preferences/simple_preferences.cpp',
    'dialogs/preferences/simple_preferences.hpp',
    'dialogs/sout/convert.cpp',
    'dialogs/sout/convert.hpp',
    'dialogs/sout/profile_selector.cpp',
    'dialogs/sout/profile_selector.hpp',
    'dialogs/sout/profiles.hpp',
    'dialogs/sout/sout.cpp',
    'dialogs/sout/sout.hpp',
    'dialogs/sout/sout_widgets.cpp',
    'dialogs/sout/sout_widgets.hpp',
    'dialogs/systray/systray.cpp',
    'dialogs/systray/notification_policy.hpp',
    'dialogs/systray/systray.hpp',
    'dialogs/toolbar/controlbar_profile.hpp',
    'dialogs/toolbar/controlbar_profile.cpp',
    'dialogs/toolbar/controlbar_profile_model.cpp',
    'dialogs/toolbar/controlbar_profile_model.hpp',
    'dialogs/vlm/vlm.cpp',
    'dialogs/playlists/playlists.cpp',
    'dialogs/playlists/playlists.hpp',
    'maininterface/compositor.hpp',
    'maininterface/compositor.cpp',
    'maininterface/compositor_common.hpp',
    'maininterface/compositor_common.cpp',
    'maininterface/compositor_accessibility.hpp',
    'maininterface/compositor_accessibility.cpp',
    'maininterface/compositor_dummy.hpp',
    'maininterface/compositor_dummy.cpp',
    'maininterface/compositor_platform.hpp',
    'maininterface/compositor_platform.cpp',
    'maininterface/interface_window_handler.cpp',
    'maininterface/interface_window_handler.hpp',
    'maininterface/mainctx.cpp',
    'maininterface/mainctx.hpp',
    'maininterface/mainctx_submodels.cpp',
    'maininterface/mainctx_submodels.hpp',
    'maininterface/mainui.cpp',
    'maininterface/addon_guest_sources.cpp',
    'maininterface/addon_guest_sources.hpp',
    'addons/addon_asset_image_provider.cpp',
    'addons/addon_asset_image_provider.hpp',
    'maininterface/mainui.hpp',
    'maininterface/navigationmodel.cpp',
    'maininterface/navigationmodel.hpp',
    'maininterface/videobackdrop.cpp',
    'maininterface/videobackdrop.hpp',
    'maininterface/videosurface.cpp',
    'maininterface/videosurface.hpp',
    'maininterface/video_window_handler.cpp',
    'maininterface/video_window_handler.hpp',
    'maininterface/windoweffects_module.hpp',
    'medialibrary/medialib.cpp',
    'medialibrary/medialib.hpp',
    'medialibrary/mlmediastore.cpp',
    'medialibrary/mlfileartwork.cpp',
    'medialibrary/mlmediastore.hpp',
    'medialibrary/mlalbum.cpp',
    'medialibrary/mlalbum.hpp',
    'medialibrary/mlalbummodel.cpp',
    'medialibrary/mlalbummodel.hpp',
    'medialibrary/mlaudio.cpp',
    'medialibrary/mlaudio.hpp',
    'medialibrary/mlaudiomodel.cpp',
    'medialibrary/mlaudiomodel.hpp',
    'medialibrary/mlartist.cpp',
    'medialibrary/mlartist.hpp',
    'medialibrary/mlartistmodel.cpp',
    'medialibrary/mlartistmodel.hpp',
    'medialibrary/mlbasemodel.cpp',
    'medialibrary/mlbasemodel.hpp',
    'medialibrary/mlbookmarkmodel.cpp',
    'medialibrary/mlbookmarkmodel.hpp',
    'medialibrary/mlevent.hpp',
    'medialibrary/mlfolder.cpp',
    'medialibrary/mlfolder.hpp',
    'medialibrary/mlfoldersmodel.cpp',
    'medialibrary/mlfoldersmodel.hpp',
    'medialibrary/mlgenre.cpp',
    'medialibrary/mlgenre.hpp',
    'medialibrary/mlgenremodel.cpp',
    'medialibrary/mlgenremodel.hpp',
    'medialibrary/mlgroup.cpp',
    'medialibrary/mlgroup.hpp',
    'medialibrary/mlhelper.cpp',
    'medialibrary/mlhelper.hpp',
    'medialibrary/mlthreadpool.cpp',
    'medialibrary/mljoinedthreadrunner.cpp',
    'medialibrary/mljoinedthreadrunner.hpp',
    'medialibrary/mltaskid.hpp',
    'medialibrary/mlthreadpool.hpp',
    'medialibrary/mlqmltypes.hpp',
    'medialibrary/mlmedia.hpp',
    'medialibrary/mlqueryparams.cpp',
    'medialibrary/mlqueryparams.hpp',
    'medialibrary/mlrecentmediamodel.cpp',
    'medialibrary/mlrecentmediamodel.hpp',
    'medialibrary/mlrecentaudiomodel.cpp',
    'medialibrary/mlplayedartistmodel.cpp',
    'medialibrary/mlrecentvideomodel.cpp',
    'medialibrary/mlrecentaudiomodel.hpp',
    'medialibrary/mlplayedartistmodel.hpp',
    'medialibrary/mlrecentvideomodel.hpp',
    'medialibrary/mlurlmodel.cpp',
    'medialibrary/mlurlmodel.hpp',
    'medialibrary/mlvideo.cpp',
    'medialibrary/mlvideo.hpp',
    'medialibrary/mlvideofoldersmodel.cpp',
    'medialibrary/mlvideofoldersmodel.hpp',
    'medialibrary/mlvideogroupsmodel.cpp',
    'medialibrary/mlvideogroupsmodel.hpp',
    'medialibrary/mlmediamodel.cpp',
    'medialibrary/mlmediamodel.hpp',
    'medialibrary/mlvideomodel.cpp',
    'medialibrary/mlvideomodel.hpp',
    'medialibrary/mlplaylist.cpp',
    'medialibrary/mlplaylist.hpp',
    'medialibrary/mlplaylistlistmodel.cpp',
    'medialibrary/mlplaylistlistmodel.hpp',
    'medialibrary/mlplaylistmedia.cpp',
    'medialibrary/mlplaylistmedia.hpp',
    'medialibrary/mlplaylistmodel.cpp',
    'medialibrary/mlplaylistmodel.hpp',
    'medialibrary/thumbnailcollector.hpp',
    'medialibrary/thumbnailcollector.cpp',
    'medialibrary/mlcustomcover.hpp',
    'medialibrary/mlcustomcover.cpp',
    'menus/custom_menus.cpp',
    'menus/custom_menus.hpp',
    'menus/qml_menu_wrapper.cpp',
    'menus/qml_menu_wrapper.hpp',
    'menus/menus.cpp',
    'menus/redesign_menu_style.cpp',
    'menus/menus.hpp',
    'network/addonsmodel.cpp',
    'network/addonsmodel.hpp',
    'network/mediatreelistener.cpp',
    'network/mediatreelistener.hpp',
    'network/devicesourceprovider.cpp',
    'network/devicesourceprovider.hpp',
    'network/networkbasemodel.cpp',
    'network/networkbasemodel.hpp',
    'network/networkdevicemodel.cpp',
    'network/networkdevicemodel.hpp',
    'network/networksourcesmodel.cpp',
    'network/networkplaylistmodel.cpp',
    'network/networksourcesmodel.hpp',
    'network/networkmediamodel.cpp',
    'network/networkmediamodel.hpp',
    'network/standardpathmodel.cpp',
    'network/standardpathmodel.hpp',
    'network/vlcmediasourcewrapper.hpp',
    'style/qtthemeprovider.hpp',
    'style/colorcontext.cpp',
    'style/colorcontext.hpp',
    'style/systempalette.cpp',
    'style/systempalette.hpp',
    'style/vlcicons.cpp',
    'style/vlcicons.hpp',
    'style/defaultthemeproviders.hpp',
    'style/systempalettethemeprovider.cpp',
    'player/input_models.cpp',
    'player/delay_estimator.cpp',
    'player/input_models.hpp',
    'player/player_controller.cpp',
    'player/series_model.cpp',
    'player/episode_metadata.cpp',
    'player/player_controller.hpp',
    'player/player_controller_p.hpp',
    'player/subtitle_timeline_scanner.cpp',
    'player/audio_waveform_scanner.cpp',
    'player/audio_waveform_scanner.hpp',
    'util/podcast_metadata.hpp',
    'player/subtitle_timeline_scanner.hpp',
    'util/seek_preview_cache.hpp',
    'util/seek_preview_image_provider.hpp',
    'util/subtitle_source_service.hpp',
    'util/subtitle_source_service.cpp',
    'util/github_release_service.cpp',
    'util/release_info.cpp',
    'util/release_info.hpp',
    'player/player_controlbar_model.cpp',
    'player/player_controlbar_model.hpp',
    'player/control_list_model.cpp',
    'player/control_list_model.hpp',
    'player/control_list_filter.cpp',
    'player/control_list_filter.hpp',
    'playlist/media.hpp',
    'playlist/playlist_common.hpp',
    'playlist/playlist_controller.cpp',
    'playlist/playlist_controller.hpp',
    'playlist/playlist_controller_p.hpp',
    'playlist/playlist_item.cpp',
    'playlist/playlist_item.hpp',
    'playlist/playlist_model.cpp',
    'playlist/playlist_tree_model.cpp',
    'playlist/playlist_model.hpp',
    'playlist/playlist_tree_model.hpp',
    'playlist/playlist_model_p.hpp',
    'util/asynctask.hpp',
    'util/accessible_compat_attached.cpp',
    'util/accessible_compat_attached.hpp',
    'util/audio_device_model.cpp',
    'util/redesign_folder_model.cpp',
    'util/audio_device_model.hpp',
    'util/workerthreadset.cpp',
    'util/workerthreadset.hpp',
    'util/base_model.cpp',
    'util/base_model.hpp',
    'util/base_model_p.hpp',
    'util/color_scheme_model.cpp',
    'util/color_scheme_model.hpp',
    'util/redesign_widget_theme.cpp',
    'util/redesign_widget_theme.hpp',
    'util/scroll_focus_tracker.cpp',
    'util/scroll_focus_tracker.hpp',
    'util/playlist_cover_store.hpp',
    'util/track_matching.hpp',
    'util/media_identity.hpp',
    'util/filter_chain_edit.hpp',
    'util/artwork_discovery.hpp',
    'util/artwork_approval.hpp',
    'util/artwork_selection.cpp',
    'util/artwork_selection.hpp',
    'util/artwork_store.hpp',
    'util/media_identity_ml.hpp',
    'util/media_fingerprint.hpp',
    'util/media_identity_vlc.hpp',
    'util/episode_title_cache.hpp',
    'util/subtitle_matching.hpp',
    'util/local_file_url.hpp',
    'util/gettext_translator.cpp',
    'util/gettext_translator.hpp',
    'util/interface_translator.cpp',
    'util/interface_translator.hpp',
    'util/ui_notifier.cpp',
    'util/ui_notifier.hpp',
    'util/color_svg_image_provider.cpp',
    'util/color_svg_image_provider.hpp',
    'util/vlcaccess_image_provider.cpp',
    'util/vlcaccess_image_provider.hpp',
    'util/covergenerator.cpp',
    'util/covergenerator.hpp',
    'util/csdbuttonmodel.cpp',
    'util/csdbuttonmodel.hpp',
    'util/csdmenu.cpp',
    'util/csdmenu.hpp',
    'util/imageluminanceextractor.cpp',
    'util/imageluminanceextractor.hpp',
    'util/imagehelper.cpp',
    'util/imagehelper.hpp',
    'util/keyhelper.cpp',
    'util/keyhelper.hpp',
    'util/listcache.hpp',
    'util/listcache.hxx',
    'util/listcacheloader.hpp',
    'util/locallistcacheloader.hpp',
    'util/navigation_history.cpp',
    'util/navigation_history.hpp',
    'util/item_key_event_filter.cpp',
    'util/item_key_event_filter.hpp',
    'util/flickable_scroll_handler.cpp',
    'util/flickable_scroll_handler.hpp',
    'util/qt_dirs.cpp',
    'util/qt_dirs.hpp',
    'util/proxycolumnmodel.hpp',
    'util/registry.cpp',
    'util/registry.hpp',
    'util/renderer_manager.cpp',
    'util/renderer_manager.hpp',
    'util/singleton.hpp',
    'util/soutchain.cpp',
    'util/soutchain.hpp',
    'util/validators.cpp',
    'util/validators.hpp',
    'util/varcommon_p.hpp',
    'util/varchoicemodel.cpp',
    'util/varchoicemodel.hpp',
    'util/variables.cpp',
    'util/variables.hpp',
    'util/vlctick.cpp',
    'util/vlctick.hpp',
    'util/vlchotkeyconverter.cpp',
    'util/vlchotkeyconverter.hpp',
    'util/postprocessing_preferences.hpp',
    'util/file_log_preferences.hpp',
    'util/preference_values.hpp',
    'util/message_log.hpp',
    'util/message_log_buffer.hpp',
    'util/video_window_geometry.hpp',
    'util/shared_input_item.hpp',
    'util/fast_gaussian_blur_template.h',
    'util/effects_image_provider.cpp',
    'util/effects_image_provider.hpp',
    'util/qsgtextureview.cpp',
    'util/qsgtextureview.hpp',
    'util/textureproviderobserver.cpp',
    'util/textureproviderobserver.hpp',
    'util/dismiss_popup_event_filter.cpp',
    'util/dismiss_popup_event_filter.hpp',
    'util/list_selection_model.cpp',
    'util/list_selection_model.hpp',
    'util/model_recovery_agent.hpp',
    'util/vlcqtmessagehandler.cpp',
    'util/vlcqtmessagehandler.hpp',
    'util/hang_reporter.cpp',
    'util/background_watcher.cpp',
    'util/background_watcher.hpp',
    'util/hang_reporter.hpp',
    'util/colorizedsvgicon.cpp',
    'util/colorizedsvgicon.hpp',
    'util/kirigamiwheelhandler.cpp',
    'util/kirigamiwheelhandler.hpp',
    'util/textureproviderindirection.cpp',
    'util/textureproviderindirection.hpp',
    'util/sgmanipulator.cpp',
    'util/sgmanipulator.hpp',
    'util/pointingtooltipattached.hpp',
    'widgets/native/animators.cpp',
    'widgets/native/animators.hpp',
    'widgets/native/embedded_file_browser.cpp',
    'widgets/native/embedded_file_browser.hpp',
    'widgets/native/customwidgets.cpp',
    'widgets/native/customwidgets.hpp',
    'widgets/native/csdthemeimage.cpp',
    'widgets/native/csdthemeimage.hpp',
    'widgets/native/interface_widgets.cpp',
    'widgets/native/interface_widgets.hpp',
    'widgets/native/navigation_attached.cpp',
    'widgets/native/navigation_attached.hpp',
    'widgets/native/mlfolderseditor.cpp',
    'widgets/native/mlfolderseditor.hpp',
    'widgets/native/qvlcframe.cpp',
    'widgets/native/qvlcframe.hpp',
    'widgets/native/searchlineedit.cpp',
    'widgets/native/searchlineedit.hpp',
    'widgets/native/viewblockingrectangle.cpp',
    'widgets/native/softwarebackdropblur.cpp',
    'widgets/native/softwarebackdropblur.hpp',
    'util/video_backdrop_region.hpp',
    'util/video_backdrop_reporter.hpp',
    'widgets/native/viewblockingrectangle.hpp',
    'widgets/native/doubleclickignoringitem.hpp',
)

if host_system == 'windows'
    qt_plugin_sources += files(
        'maininterface/mainctx_win32.cpp',
        'maininterface/mainctx_win32.hpp',
        'maininterface/compositor_win7.cpp',
        'maininterface/compositor_win7.hpp',
        'style/windowsthemeprovider.cpp',
        'util/csdmenu_win32.c',
        'util/csdmenu_win32.h',
        'util/win32_emutls.cpp',
        'maininterface/compositor_dcomp.cpp',
        'maininterface/compositor_dcomp_background.hpp',
        'maininterface/compositor_dcomp_backdrop.cpp',
        'maininterface/compositor_dcomp_backdrop.hpp',
        'maininterface/compositor_dcomp_acrylicsurface.cpp',
    )
endif

if (x11_dep.found() and
    xcb_dep.found() and
    xcb_damage_dep.found() and
    xcb_xfixes_dep.found())
    qt_plugin_sources += files(
        'maininterface/compositor_x11.cpp',
        'maininterface/compositor_x11_backdrop.cpp',
        'maininterface/compositor_x11_renderclient.cpp',
        'maininterface/compositor_x11_renderwindow.cpp',
        'maininterface/compositor_x11_uisurface.cpp',
        'maininterface/compositor_x11_utils.cpp',
    )
endif

if have_wayland and cpp.has_header('qpa/qplatformnativeinterface.h', dependencies: qt6_dep)
    qt_plugin_sources += files(
        'maininterface/compositor_wayland.cpp',
    )
endif

qrc_files = files('assets.qrc')
if host_system == 'windows'
  qrc_files += files('windows.qrc')
endif

ui_sources = files(
    'dialogs/extended/video_effects.ui',
    'dialogs/fingerprint/fingerprintdialog.ui',
    'dialogs/help/about.ui',
    'dialogs/help/update.ui',
    'dialogs/messages/messages_panel.ui',
    'dialogs/open/open.ui',
    'dialogs/open/open_capture.ui',
    'dialogs/open/open_disk.ui',
    'dialogs/open/open_file.ui',
    'dialogs/open/open_net.ui',
    'dialogs/podcast/podcast_configuration.ui',
    'dialogs/preferences/sprefs_audio.ui',
    'dialogs/preferences/sprefs_input.ui',
    'dialogs/preferences/sprefs_interface.ui',
    'dialogs/preferences/sprefs_medialibrary.ui',
    'dialogs/preferences/sprefs_subtitles.ui',
    'dialogs/preferences/sprefs_video.ui',
    'dialogs/sout/profiles.ui',
    'dialogs/sout/sout.ui',
    'dialogs/vlm/vlm.ui',
)

qml_modules = []

qml_modules += {
    'name' : 'VLC.Dialogs',
    'target' : 'dialogs',
    'outdir' : 'VLC/Dialogs',
    'sources': files(
        'dialogs/dialogs/qml/CustomDialog.qml',
        'dialogs/dialogs/qml/Dialogs.qml',
        'dialogs/dialogs/qml/ModalDialog.qml',
        'dialogs/dialogs/qml/MessageNotice.qml',
        'dialogs/dialogs/qml/WindowDialog.qml',
        'dialogs/toolbar/qml/EditorDNDDelegate.qml',
        'dialogs/toolbar/qml/EditorDNDView.qml',
        'dialogs/toolbar/qml/EditorDummyButton.qml',
        'dialogs/toolbar/qml/EditorTabButton.qml',
        'dialogs/toolbar/qml/ToolbarEditor.qml',
        'dialogs/toolbar/qml/ToolbarEditorDialog.qml',
        'dialogs/toolbar/qml/ToolbarEditorButtonList.qml'
    ),
}

qml_maininterface_extra_files = []

if get_option('update-check').allowed()
    qt_extra_flags += '-DUPDATE_CHECK'
    qml_maininterface_extra_files += 'maininterface/qml/UpdatePane.qml'
endif

qml_modules += {
    'name' : 'VLC.MainInterface',
    'target' : 'maininterface',
    'outdir' : 'VLC/MainInterface',
    'sources': files(
        'maininterface/qml/LocalTopbar.qml',
        'maininterface/qml/MenuTopbar.qml',
        'maininterface/qml/MainInterface.qml',
        'maininterface/qml/MainViewLoader.qml',
        'maininterface/qml/MainDisplay.qml',
        'maininterface/qml/NoMedialibHome.qml',
        'maininterface/qml/OpenFileSplitButton.qml',
        qml_maininterface_extra_files,
        'maininterface/qml/SideNavigationPane.qml',
        'maininterface/qml/SideNavigationDelegate.qml',
    ),
}
qml_modules += {
    'name' : 'VLC.MediaLibrary',
    'target' : 'medialibrary',
    'outdir' : 'VLC/MediaLibrary',
    'sources': files(
        'medialibrary/qml/ArtistTopBanner.qml',
        'medialibrary/qml/AudioGridItem.qml',
        'medialibrary/qml/HomeDisplay.qml',
        'medialibrary/qml/HomePage.qml',
        'medialibrary/qml/MediaView.qml',
        'medialibrary/qml/VideoGroupDisplay.qml',
        'medialibrary/qml/MusicAlbums.qml',
        'medialibrary/qml/MusicAlbumsDisplay.qml',
        'medialibrary/qml/MusicAlbumsGridExpandDelegate.qml',
        'medialibrary/qml/MusicArtist.qml',
        'medialibrary/qml/MusicArtistDelegate.qml',
        'medialibrary/qml/MusicArtistsAlbums.qml',
        'medialibrary/qml/MusicAllArtists.qml',
        'medialibrary/qml/MusicArtistsDisplay.qml',
        'medialibrary/qml/MusicDisplay.qml',
        'medialibrary/qml/MusicGenres.qml',
        'medialibrary/qml/MusicGenresDisplay.qml',
        'medialibrary/qml/MusicPlaylistsDisplay.qml',
        'medialibrary/qml/MusicTrackListDisplay.qml',
        'medialibrary/qml/MusicTracksDisplay.qml',
        'medialibrary/qml/UrlListDisplay.qml',
        'medialibrary/qml/VideoDisplay.qml',
        'medialibrary/qml/VideoAll.qml',
        'medialibrary/qml/VideoAllDisplay.qml',
        'medialibrary/qml/VideoAllSubDisplay.qml',
        'medialibrary/qml/PlaylistMediaList.qml',
        'medialibrary/qml/PlaylistMedia.qml',
        'medialibrary/qml/PlaylistMediaDisplay.qml',
        'medialibrary/qml/VideoPlaylistsDisplay.qml',
        'medialibrary/qml/VideoGridItem.qml',
        'medialibrary/qml/VideoInfoExpandPanel.qml',
        'medialibrary/qml/VideoListDisplay.qml',
        'medialibrary/qml/VideoGridDisplay.qml',
        'medialibrary/qml/MusicAlbumSectionDelegate.qml',
        'medialibrary/qml/MLGlobalShortcuts.qml',
        'medialibrary/qml/Bookmarks.qml',
        'medialibrary/qml/MLContextMenu.qml',
        'medialibrary/qml/MLDragItem.qml',
        'medialibrary/qml/MLTableColumns.qml',
        'medialibrary/qml/ScanProgressBar.qml',
    ),
}

qml_modules += {
    'name' : 'VLC.Menus',
    'target' : 'menus',
    'outdir' : 'VLC/Menus',
    'sources': files(
        'menus/qml/GlobalShortcuts.qml',
        'menus/qml/Menubar.qml',
        'menus/qml/ShortcutExt.qml'
    ),
}

qml_modules += {
    'name' : 'VLC.Network',
    'target' : 'network',
    'outdir' : 'VLC/Network',
    'sources': files(
        'network/qml/AddressbarButton.qml',
        'network/qml/BrowseDeviceView.qml',
        'network/qml/BrowseDisplay.qml',
        'network/qml/BrowseHomeDisplay.qml',
        'network/qml/BrowseTreeDisplay.qml',
        'network/qml/BrowseTreeHeader.qml',
        'network/qml/DiscoverDisplay.qml',
        'network/qml/DiscoverUrlDisplay.qml',
        'network/qml/NetworkAddressbar.qml',
        'network/qml/NetworkGridItem.qml',
        'network/qml/NetworkThumbnailItem.qml',
        'network/qml/ServicesHomeDisplay.qml',
        'network/qml/ServicesManage.qml',
        'network/qml/ServicesSources.qml'
    ),
}

qml_modules += {
   'name' : 'VLC.Player',
   'target' : 'player',
   'outdir' : 'VLC/Player',
   'sources': files(
       'player/qml/ControlBar.qml',
       'player/qml/redesign/RedesignBookmarks.qml',
       'player/qml/redesign/RedesignBookmarksView.qml',
       'player/qml/redesign/RedesignChaptersView.qml',
       'player/qml/redesign/RedesignSeriesMetadata.qml',
       'player/qml/redesign/RedesignQueuePanel.qml',
       'player/qml/redesign/RedesignLibrary.qml',
       'player/qml/redesign/RedesignLibraryUnavailable.qml',
       'player/qml/redesign/RedesignPlayer.qml',
       'player/qml/redesign/RedesignPlayerLayout.qml',
       'player/qml/redesign/RedesignPlayerChrome.qml',
       'player/qml/redesign/RedesignCustomizationCatalog.qml',
       'player/qml/redesign/RedesignCustomizationEditor.qml',
       'player/qml/redesign/RedesignExtraControl.qml',
       'player/qml/redesign/RedesignMediaFavorite.qml',
       'player/qml/redesign/RedesignPlayerFavorite.qml',
       'player/qml/redesign/RedesignPreferenceRow.qml',
       'player/qml/redesign/RedesignComboBox.qml',
       'player/qml/redesign/RedesignEqualizer.qml',
       'player/qml/redesign/RedesignSpatializer.qml',
       'player/qml/redesign/RedesignStartupVolume.qml',
       'player/qml/redesign/RedesignConfirmationDialog.qml',
       'player/qml/redesign/RedesignGotoTimeDialog.qml',
       'player/qml/redesign/RedesignIdentityDialog.qml',
       'player/qml/redesign/RedesignTime.qml',
       'player/qml/redesign/RedesignNetworkDialog.qml',
       'player/qml/redesign/RedesignPlaylistDialog.qml',
       'player/qml/redesign/RedesignPlaylistProperties.qml',
       'player/qml/redesign/RedesignPlaylistTrackTarget.qml',
       'player/qml/redesign/RedesignTrackAssignmentDialog.qml',
       'player/qml/redesign/RedesignSubtitleProviderSetup.qml',
       'player/qml/redesign/RedesignSubtitleSourcePreferences.qml',
       'player/qml/redesign/RedesignAddons.qml',
       'player/qml/redesign/RedesignAddonChild.qml',
       'player/qml/redesign/RedesignAddonField.qml',
       'player/qml/redesign/RedesignAccountSetupDialog.qml',
       'player/qml/redesign/RedesignAddonManagement.qml',
       'player/qml/redesign/RedesignAddonDiscovery.qml',
       'player/qml/redesign/RedesignAddonDiscoveryWindow.qml',
       'player/qml/redesign/RedesignAddonSessions.qml',
       'player/qml/redesign/RedesignAddonSourceConsentDialog.qml',
       'player/qml/redesign/RedesignAddonSelection.qml',
       'player/qml/redesign/RedesignAddonRecovery.qml',
       'player/qml/redesign/RedesignAddonNode.qml',
       'player/qml/redesign/RedesignAddonAssetImage.qml',
       'player/qml/redesign/RedesignAddonFeatureDemand.qml',
       'player/qml/redesign/RedesignAddonProviders.qml',
       'player/qml/redesign/RedesignAddonSettings.qml',
       'player/qml/redesign/RedesignAddonSettingsDialog.qml',
       'player/qml/redesign/RedesignAddonTheme.qml',
       'player/qml/redesign/RedesignAddonThemeSelector.qml',
       'player/qml/redesign/RedesignGeneralAppearance.qml',
       'player/qml/redesign/RedesignAddonView.qml',
       'player/qml/redesign/RedesignAddonMount.qml',
       'player/qml/redesign/RedesignAddonPanel.qml',
       'player/qml/redesign/RedesignAddonSections.qml',
       'player/qml/redesign/RedesignAddonSurface.qml',
       'player/qml/redesign/RedesignAddonReplacements.qml',
       'player/qml/redesign/RedesignAddonWindow.qml',
       'player/qml/redesign/RedesignAddonVisualizerSelector.qml',
       'player/qml/redesign/RedesignAddonTrackPreparation.qml',
       'player/qml/redesign/AddonUi.js',
       'player/qml/redesign/AddonSelectionContracts.js',
       'player/qml/redesign/AddonTheme.js',
       'player/qml/redesign/RedesignPlugins.qml',
       'player/qml/redesign/RedesignTrackAssignments.qml',
       'player/qml/redesign/RedesignFolderInfo.qml',
       'player/qml/redesign/RedesignPodcastInfo.qml',
       'player/qml/redesign/RedesignAddToPlaylistDialog.qml',
       'player/qml/redesign/RedesignAddToPlaylist.qml',
       'player/qml/redesign/RedesignNetworkStreams.qml',
       'player/qml/redesign/RedesignNetworkStreamLibrary.qml',
       'player/qml/redesign/RedesignPreferencesCatalog.qml',
       'player/qml/redesign/RedesignPreferencesWindow.qml',
       'player/qml/redesign/RedesignSettingSlider.qml',
       'player/qml/redesign/RedesignSleepTimer.qml',
       'player/qml/redesign/RedesignModelChoice.qml',
       'player/qml/redesign/RedesignPlaybackSettings.qml',
       'player/qml/redesign/RedesignTrackDefaults.qml',
       'player/qml/redesign/RedesignAudioSettings.qml',
       'player/qml/redesign/RedesignSubtitleSettings.qml',
       'player/qml/redesign/RedesignVideoSettings.qml',
       'player/qml/redesign/RedesignOnboardingView.qml',
       'player/qml/redesign/RedesignOnboardingWindow.qml',
       'player/qml/redesign/RedesignFilterSection.qml',
       'player/qml/redesign/RedesignSwitchRow.qml',
       'player/qml/redesign/RedesignStatistics.qml',
       'player/qml/redesign/RedesignSettings.qml',
       'player/qml/redesign/RedesignPlayerPreset.qml',
       'player/qml/redesign/RedesignTrackPopup.qml',
       'player/qml/redesign/RedesignShortcutEditor.qml',
       'player/qml/redesign/RedesignShortcuts.qml',
       'player/qml/redesign/RedesignTracks.qml',
       'player/qml/redesign/RedesignButton.qml',
       'player/qml/redesign/RedesignToolTip.qml',
       'player/qml/redesign/RedesignIcon.qml',
       'player/qml/redesign/RedesignSvg.qml',
       'player/qml/redesign/RedesignActionMenu.qml',
       'player/qml/redesign/RedesignSurfaceBackdrop.qml',
       'player/qml/redesign/RedesignSoftwareShadow.qml',
       'player/qml/redesign/RedesignDialogSurface.qml',
       'player/qml/redesign/RedesignSeparator.qml',
       'player/qml/redesign/RedesignSettingsDialog.qml',
       'player/qml/redesign/RedesignLibraryArtwork.qml',
       'player/qml/redesign/RedesignAudioArtwork.qml',
       'player/qml/redesign/RedesignLibraryView.qml',
       'player/qml/redesign/RedesignBreadcrumbs.qml',
       'player/qml/redesign/RedesignLibraryPath.qml',
       'player/qml/redesign/RedesignLibraryPins.qml',
       'player/qml/redesign/RedesignLibraryNavigation.qml',
       'player/qml/redesign/RedesignHomeView.qml',
       'player/qml/redesign/RedesignLibraryDrag.qml',
       'player/qml/redesign/RedesignLibraryVisits.qml',
       'player/qml/redesign/RedesignLibrarySort.qml',
       'player/qml/redesign/RedesignMenu.qml',
       'player/qml/redesign/RedesignRecentMenuModel.qml',
       'player/qml/redesign/RedesignPlayerView.qml',
       'player/qml/redesign/RedesignQueue.qml',
       'player/qml/redesign/RedesignSeekBar.qml',
       'player/qml/redesign/RedesignSeekThumbnail.qml',
       'player/qml/redesign/RedesignPlayerSeekBar.qml',
       'player/qml/redesign/RedesignMiniPlayer.qml',
       'player/qml/redesign/RedesignTheme.qml',
       'player/qml/redesign/RedesignVolume.qml',
       'player/qml/PlaybackSpeed.qml',
       'player/qml/LyricsFlickable.qml',
       'player/qml/MiniPlayer.qml',
       'player/qml/PIPPlayer.qml',
       'player/qml/Player.qml',
       'player/qml/PlayerControlLayout.qml',
       'player/qml/PlayerPlaylistVisibilityFSM.qml',
       'player/qml/PlayerToolbarVisibilityFSM.qml',
       'player/qml/MinimalView.qml',
       'player/qml/ResumeDialog.qml',
       'player/qml/SliderBar.qml',
       'player/qml/TopBar.qml',
       'player/qml/TracksMenu.qml',
       'player/qml/TracksPage.qml',
       'player/qml/TracksPageAudio.qml',
       'player/qml/TracksPageSpeed.qml',
       'player/qml/TracksPageSubtitle.qml',
       'player/qml/TracksListPage.qml',
       'player/qml/ControlLayout.qml',
       'player/qml/ControlRepeater.qml',
       'player/qml/NavigationBox.qml',
   ),
   'singletons' : files(
       'player/qml/PlayerControlbarControls.qml',
   )
}

qml_modules += {
   'name' : 'VLC.PlayerControls',
   'target' : 'playercontrols',
   'outdir' : 'VLC/PlayerControls',
   'sources': files(
       'player/qml/controlbarcontrols/HighResolutionTimeWidget.qml',
       'player/qml/controlbarcontrols/ArtworkInfoWidget.qml',
       'player/qml/controlbarcontrols/AspectRatioWidget.qml',
       'player/qml/controlbarcontrols/AtoBButton.qml',
       'player/qml/controlbarcontrols/BackButton.qml',
       'player/qml/controlbarcontrols/BookmarkButton.qml',
       'player/qml/controlbarcontrols/ChapterNextButton.qml',
       'player/qml/controlbarcontrols/ChapterPreviousButton.qml',
       'player/qml/controlbarcontrols/DvdMenuButton.qml',
       'player/qml/controlbarcontrols/ExpandingSpacerWidget.qml',
       'player/qml/controlbarcontrols/ExtendedSettingsButton.qml',
       'player/qml/controlbarcontrols/FasterButton.qml',
       'player/qml/controlbarcontrols/FrameNextButton.qml',
       'player/qml/controlbarcontrols/FramePrevButton.qml',
       'player/qml/controlbarcontrols/FullscreenButton.qml',
       'player/qml/controlbarcontrols/InfoButton.qml',
       'player/qml/controlbarcontrols/LangButton.qml',
       'player/qml/controlbarcontrols/LoopButton.qml',
       'player/qml/controlbarcontrols/MenuButton.qml',
       'player/qml/controlbarcontrols/NavigationBoxButton.qml',
       'player/qml/controlbarcontrols/NavigationWidget.qml',
       'player/qml/controlbarcontrols/NextButton.qml',
       'player/qml/controlbarcontrols/OpenButton.qml',
       'player/qml/controlbarcontrols/PlaybackSpeedButton.qml',
       'player/qml/controlbarcontrols/PlayButton.qml',
       'player/qml/controlbarcontrols/PlayerSwitchButton.qml',
       'player/qml/controlbarcontrols/PlaylistButton.qml',
       'player/qml/controlbarcontrols/PreviousButton.qml',
       'player/qml/controlbarcontrols/ProgramButton.qml',
       'player/qml/controlbarcontrols/QuitButton.qml',
       'player/qml/controlbarcontrols/RandomButton.qml',
       'player/qml/controlbarcontrols/RendererButton.qml',
       'player/qml/controlbarcontrols/RecordButton.qml',
       'player/qml/controlbarcontrols/ReverseButton.qml',
       'player/qml/controlbarcontrols/SkipBackButton.qml',
       'player/qml/controlbarcontrols/SkipForwardButton.qml',
       'player/qml/controlbarcontrols/SlowerButton.qml',
       'player/qml/controlbarcontrols/SnapshotButton.qml',
       'player/qml/controlbarcontrols/SpacerWidget.qml',
       'player/qml/controlbarcontrols/StopButton.qml',
       'player/qml/controlbarcontrols/TeletextButton.qml',
       'player/qml/controlbarcontrols/TeletextColorButton.qml',
       'player/qml/controlbarcontrols/TeletextWidget.qml',
       'player/qml/controlbarcontrols/VolumeWidget.qml',
       'player/qml/controlbarcontrols/Fallback.qml',
   ),
}

qml_modules += {
    'name' : 'VLC.Playlist',
    'target' : 'playlist',
    'outdir' : 'VLC/Playlist',
    'sources': files(
        'playlist/qml/PlaylistDelegate.qml',
        'playlist/qml/PlaylistPane.qml',
        'playlist/qml/PlaylistDetachedWindow.qml',
    ),
}

qml_modules += {
    'name' : 'VLC.Style',
    'target' : 'style',
    'outdir' : 'VLC/Style',
    'singletons': files(
        'style/AcrylicController.qml',
        'style/VLCStyle.qml',
    ),
}

qml_modules += {
    'name' : 'VLC.Util',
    'target' : 'util',
    'outdir' : 'VLC/Util',
    'sources': files(
        'util/qml/ModelSortSettingHandler.qml',
        'util/qml/DefaultFlickableScrollHandler.qml',
        'util/qml/ViewDragAutoScrollHandler.qml',
        'util/qml/FSM.qml',
        'util/qml/FSMState.qml',
        'util/qml/GridSizeHelper.qml',
        'util/qml/NativeMenu.qml',
        'util/qml/FadeControllerStateGroup.qml',
        'util/qml/DelayedBehavior.qml',
        'util/qml/DelegateTouchTapHandler.qml'
    ),
    'singletons': files(
        'util/qml/Helpers.qml',
    ),
}

qml_modules += {
    'name' : 'VLC.Widgets',
    'target' : 'widgets',
    'outdir' : 'VLC/Widgets',
    'sources': files(
        'widgets/qml/LabelExt.qml',
        'widgets/qml/ActionButtonOverlay.qml',
        'widgets/qml/ActionButtonPrimary.qml',
        'widgets/qml/BannerTabButton.qml',
        'widgets/qml/BannerCone.qml',
        'widgets/qml/BusyIndicatorExt.qml',
        'widgets/qml/CaptionLabel.qml',
        'widgets/qml/CheckButton.qml',
        'widgets/qml/CheckedDelegate.qml',
        'widgets/qml/ComboBoxExt.qml',
        'widgets/qml/AcrylicBackground.qml',
        'widgets/qml/FloatingNotification.qml',
        'widgets/qml/AnimatedBackground.qml',
        'widgets/qml/CSDThemeButton.qml',
        'widgets/qml/CSDThemeButtonSet.qml',
        'widgets/qml/CSDWindowButton.qml',
        'widgets/qml/CSDWindowButtonSet.qml',
        'widgets/qml/CSDTitlebarTapNDrapHandler.qml',
        'widgets/qml/CSDMouseStealer.qml',
        'widgets/qml/CurrentIndicator.qml',
        'widgets/qml/DragItem.qml',
        'widgets/qml/DrawerExt.qml',
        'widgets/qml/ExpandGridView.qml',
        'widgets/qml/ExpandGridItemView.qml',
        'widgets/qml/EmptyLabel.qml',
        'widgets/qml/EmptyLabelButton.qml',
        'widgets/qml/EmptyLabelHint.qml',
        'widgets/qml/GridItem.qml',
        'widgets/qml/GridSortFilterControls.qml',
        'widgets/qml/HorizontalResizeHandle.qml',
        'widgets/qml/IconLabel.qml',
        'widgets/qml/IconButton.qml',
        'widgets/qml/IconToolButton.qml',
        'widgets/qml/IconTrackButton.qml',
        'widgets/qml/ImageToolButton.qml',
        'widgets/qml/ListViewExt.qml',
        'widgets/qml/TableViewExt.qml',
        'widgets/qml/TableViewDelegateExt.qml',
        'widgets/qml/TableRowDelegate.qml',
        'widgets/qml/TableHeaderDelegate.qml',
        'widgets/qml/DefaultShadow.qml',
        'widgets/qml/ListLabel.qml',
        'widgets/qml/MediaCover.qml',
        'widgets/qml/SubtitleLabel.qml',
        'widgets/qml/MenuCaption.qml',
        'widgets/qml/MenuLabel.qml',
        'widgets/qml/NavigableCol.qml',
        'widgets/qml/NavigableRow.qml',
        'widgets/qml/PlayCover.qml',
        'widgets/qml/RoundButtonExt.qml',
        'widgets/qml/SplitButton.qml',
        'widgets/qml/ScaledImage.qml',
        'widgets/qml/TextAutoScroller.qml',
        'widgets/qml/SearchBox.qml',
        'widgets/qml/SliderExt.qml',
        'widgets/qml/SortControl.qml',
        'widgets/qml/SpinBoxExt.qml',
        'widgets/qml/StackViewExt.qml',
        'widgets/qml/PageLoader.qml',
        'widgets/qml/PageExt.qml',
        'widgets/qml/ButtonExt.qml',
        'widgets/qml/TableColumns.qml',
        'widgets/qml/TextFieldExt.qml',
        'widgets/qml/TextToolButton.qml',
        'widgets/qml/TrackDelayButton.qml',
        'widgets/qml/VideoProgressBar.qml',
        'widgets/qml/VideoQualityLabels.qml',
        'widgets/qml/ListSubtitleLabel.qml',
        'widgets/qml/PointingTooltip.qml',
        'widgets/qml/FrostedGlassEffect.qml',
        'widgets/qml/ToolTipExt.qml',
        'widgets/qml/DropShadowImage.qml',
        'widgets/qml/DoubleShadow.qml',
        'widgets/qml/FadingEdge.qml',
        'widgets/qml/FadingEdgeForItemView.qml',
        'widgets/qml/PopupIconToolButton.qml',
        'widgets/qml/PartialEffect.qml',
        'widgets/qml/ViewHeader.qml',
        'widgets/qml/ProgressIndicator.qml',
        'widgets/qml/ImageExt.qml',
        'widgets/qml/EnhancedImageExt.qml',
        'widgets/qml/ScrollBarExt.qml',
        'widgets/qml/FastBlend.qml',
        'widgets/qml/RadioButtonExt.qml',
        'widgets/qml/RoundedRectangleShadow.qml',
        'widgets/qml/VoronoiSnow.qml',
        'widgets/qml/DualKawaseBlur.qml',
        'widgets/qml/BackdropBlur.qml',
        'widgets/qml/CheckBoxExt.qml',
        'widgets/qml/ProgressBarExt.qml',
    ),
}

qml_tests = {
    'tst_AddonSelections': files('tests/tst_AddonSelections.qml'),
    'tst_AddonSelectionSurface': files('tests/tst_AddonSelectionSurface.qml'),
    'tst_AddonRecovery': files('tests/tst_AddonRecovery.qml'),
    'tst_AddonTypedDialogs': files('tests/tst_AddonTypedDialogs.qml'),
    'tst_AddonPlainText': files('tests/tst_AddonPlainText.qml'),
    'tst_AddonDiscovery': files('tests/tst_AddonDiscovery.qml'),
    'tst_AddonSessions': files('tests/tst_AddonSessions.qml'),
    'tst_AddonSourceConsent': files('tests/tst_AddonSourceConsent.qml'),
    'tst_AddonInstaller': files('tests/tst_AddonInstaller.qml'),
    'tst_AddonTrackPreparation': files('tests/tst_AddonTrackPreparation.qml'),
    'tst_AddonUi': files('tests/tst_AddonUi.qml'),
    'tst_AddonTheme': files('tests/tst_AddonTheme.qml'),
    'tst_AddonThemeSelection': files('tests/tst_AddonThemeSelection.qml'),
    'tst_GeneralAppearance': files('tests/tst_GeneralAppearance.qml'),
    'tst_AddonThemeHost': files('tests/tst_AddonThemeHost.qml'),
    'tst_AddonMounts': files('tests/tst_AddonMounts.qml'),
    'tst_AddonIntegration': files('tests/tst_AddonIntegration.qml'),
    'tst_FSM': files('tests/tst_FSM.qml'),
    'tst_Redesign': files('tests/tst_Redesign.qml'),
    'tst_RedesignMediaFavorite': files('tests/tst_RedesignMediaFavorite.qml'),
    'tst_RedesignCustomizationControls': files('tests/tst_RedesignCustomizationControls.qml'),
    'tst_RedesignCustomizationEditor': files('tests/tst_RedesignCustomizationEditor.qml'),
    'tst_RedesignCapture': files('tests/tst_RedesignCapture.qml'),
    'tst_RedesignGotoTime': files('tests/tst_RedesignGotoTime.qml'),
    'tst_RedesignInlineTime': files('tests/tst_RedesignInlineTime.qml'),
    'tst_RedesignAddToPlaylist': files('tests/tst_RedesignAddToPlaylist.qml'),
    'tst_RedesignPlayerLayout': files('tests/tst_RedesignPlayerLayout.qml'),
    'tst_RedesignPlayerChrome': files('tests/tst_RedesignPlayerChrome.qml'),
    'tst_RedesignCursor': files('tests/tst_RedesignCursor.qml'),
    'tst_RedesignTheme': files('tests/tst_RedesignTheme.qml'),
    'tst_RedesignSliders': files('tests/tst_RedesignSliders.qml'),
    'tst_RedesignActionMenu': files('tests/tst_RedesignActionMenu.qml'),
    'tst_RedesignDialogSurfaces': files('tests/tst_RedesignDialogSurfaces.qml'),
    'tst_RedesignSettingsDialog': files('tests/tst_RedesignSettingsDialog.qml'),
    'tst_RedesignShortcutEditor': files('tests/tst_RedesignShortcutEditor.qml'),
    'tst_RedesignAppearance': files('tests/tst_RedesignAppearance.qml'),
    'tst_RedesignCompactMenus': files('tests/tst_RedesignCompactMenus.qml'),
    'tst_RedesignLibrary': files('tests/tst_RedesignLibrary.qml'),
    'tst_RedesignQueue': files('tests/tst_RedesignQueue.qml'),
    'tst_RedesignIconSizing': files('tests/tst_RedesignIconSizing.qml'),
    'tst_RedesignBookmarks': files('tests/tst_RedesignBookmarks.qml'),
    'tst_RedesignChapters': files('tests/tst_RedesignChapters.qml'),
    'tst_RedesignNetwork': files('tests/tst_RedesignNetwork.qml'),
    'tst_RedesignPlaylist': files('tests/tst_RedesignPlaylist.qml'),
    'tst_RedesignPlaylistProperties': files('tests/tst_RedesignPlaylistProperties.qml'),
    'tst_RedesignPlaylistTrackTarget': files('tests/tst_RedesignPlaylistTrackTarget.qml'),
    'tst_RedesignTrackAssignments': files('tests/tst_RedesignTrackAssignments.qml'),
    'tst_RedesignFolderInfo': files('tests/tst_RedesignFolderInfo.qml'),
    'tst_RedesignPreferences': files('tests/tst_RedesignPreferences.qml'),
    'tst_RedesignAddons': files('tests/tst_RedesignAddons.qml'),
    'tst_RedesignPlugins': files('tests/tst_RedesignPlugins.qml'),
    'tst_RedesignPlayback': files('tests/tst_RedesignPlayback.qml'),
    'tst_RedesignWaveform': files('tests/tst_RedesignWaveform.qml'),
    'tst_RedesignSubtitleTimeline': files('tests/tst_RedesignSubtitleTimeline.qml'),
    'tst_RedesignAudio': files('tests/tst_RedesignAudio.qml'),
    'tst_RedesignGlobalEqualizer': files('tests/tst_RedesignGlobalEqualizer.qml'),
    'tst_RedesignEqualizerScroll': files('tests/tst_RedesignEqualizerScroll.qml'),
    'tst_RedesignChoices': files('tests/tst_RedesignChoices.qml'),
    'tst_RedesignSpatializer': files('tests/tst_RedesignSpatializer.qml'),
    'tst_RedesignVideo': files('tests/tst_RedesignVideo.qml'),
    'tst_RedesignOnboarding': files('tests/tst_RedesignOnboarding.qml'),
    'tst_RedesignPlayerPreset': files('tests/tst_RedesignPlayerPreset.qml'),
    'tst_RedesignMenu': files('tests/tst_RedesignMenu.qml'),
    'tst_RedesignToolTip': files('tests/tst_RedesignToolTip.qml'),
    'tst_RedesignVolume': files('tests/tst_RedesignVolume.qml'),
    'tst_RedesignSubtitles': files('tests/tst_RedesignSubtitles.qml'),
}

qmllibs = []

if qt6_dep.found()
    qt_rcc_extra_arguments = []
    qt_zstd_check = '''
    #include <qconfig.h>
    #if QT_FEATURE_zstd != 1
    #error "Qt can not decompress zstd. Resources are going to be compressed using zlib."
    #endif
    '''
    if not cpp.compiles(qt_zstd_check, dependencies: qt6_dep, name: 'Qt supports decompressing zstd')
        qt_rcc_extra_arguments += '--no-zstd'
    endif

    atomic_lib = cc.find_library('atomic', required: false)
    if atomic_lib.found()
        qt_extra_deps += atomic_lib
        qt_extra_flags += '-DQT_HAS_LIBATOMIC'
    endif

    prog_python = find_program('python3')
    # Build the application worker separately from experimental Rust VLC modules.
    addon_worker_dependencies = []
    # Tests that start LibVLC load the plug-ins of this build.
    addon_worker_environment = {'VLC_PLUGIN_PATH': vlc_build_root / 'modules'}
    if not get_option('addon-runtime').disabled()
        addon_cargo = find_program('cargo', native: true, required: get_option('addon-runtime'))
        if addon_cargo.found()
            addon_worker_name = host_system == 'windows' ? 'vlc-addon-worker.exe' : 'vlc-addon-worker'
            addon_worker_command = [prog_python, files('scripts/build-addon-worker.py'),
                '--cargo', addon_cargo.full_path(), '--manifest', '@INPUT0@', '--output', '@OUTPUT@',
                '--system', host_system, '--cpu', host_machine.cpu_family(),
                '--abi', cc.get_argument_syntax() == 'msvc' ? 'msvc' : 'gnu']
            if get_option('addon-runtime-target') != ''
                addon_worker_command += ['--target', get_option('addon-runtime-target')]
            endif
            if meson.is_cross_build()
                addon_worker_command += '--cross'
                addon_linker = get_option('addon-runtime-linker')
                if addon_linker == ''
                    addon_cc_command = cc.cmd_array()
                    if addon_cc_command.length() != 1
                        error('If the C compiler uses a wrapper, set addon-runtime-linker to the target linker.')
                    endif
                    addon_linker = addon_cc_command[0]
                endif
                addon_worker_command += ['--linker', addon_linker]
            endif
            addon_worker_target = custom_target('vlc-addon-worker',
                input: files('addons/worker/Cargo.toml', 'addons/worker/Cargo.lock', 'addons/worker/src/main.rs'),
                output: addon_worker_name, command: addon_worker_command,
                build_by_default: true, install: true, install_dir: bindir_path)
            addon_worker_dependencies += addon_worker_target
            addon_worker_environment += {'VLC_ADDON_WORKER': addon_worker_target.full_path()}
            qt_cppargs += '-DVLC_ADDON_WORKER_BUILD_PATH="' + addon_worker_target.full_path() + '"'
        endif
    endif
    # The host stores credentials. Add-on packages do not contain credential-store code.
    if host_system == 'windows'
        qt_extra_deps += cc.find_library('advapi32')
    elif host_system == 'darwin'
        qt_extra_deps += dependency('appleframeworks', modules: ['Security'])
    endif
    # Install the reference Wasm file without a source build.
    addon_reference_root = meson.project_source_root() / 'contrib-addons/examples/thesubtitledb'
    addon_distribution_paths = custom_target('addon_distribution_paths',
        output: 'addon_distribution_paths.hpp',
        command: [prog_python, files('scripts/addon-distribution-paths.py'),
            '--root', meson.project_source_root() / 'contrib-addons/examples', '--output', '@OUTPUT@'])
    foreach addon_resource : ['addon.json', 'LICENSE', 'dist/addon.wasm', 'settings/schema.json', 'settings/ui.json', 'locales/en.json']
        addon_resource_directory = fs.parent(addon_resource)
        install_data(addon_reference_root / addon_resource,
            install_dir: get_option('datadir') / 'vlc/addons/reference/thesubtitledb' / addon_resource_directory)
    endforeach
    foreach addon_resource : ['addon.json', 'LICENSE', 'NOTICE.md', 'dist/addon.wasm']
        install_data(meson.project_source_root() / 'contrib-addons/examples/tvmaze' / addon_resource,
            install_dir: get_option('datadir') / 'vlc/addons/reference/tvmaze' / fs.parent(addon_resource))
    endforeach
    foreach artwork_provider : ['musicbrainz', 'lastfm', 'french-tv', 'google-images']
        foreach addon_resource : ['addon.json', 'LICENSE', 'NOTICE.md', 'THIRD_PARTY_NOTICES.md', 'dist/addon.wasm']
            install_data(meson.project_source_root() / 'contrib-addons/examples' / artwork_provider / addon_resource,
                install_dir: get_option('datadir') / 'vlc/addons/reference' / artwork_provider / fs.parent(addon_resource))
        endforeach
    endforeach
    foreach addon_resource : ['addon.json', 'LICENSE', 'NOTICE.md', 'THIRD_PARTY_NOTICES.md', 'dist/addon.wasm']
        install_data(meson.project_source_root() / 'contrib-addons/examples/radio-browser' / addon_resource,
            install_dir: get_option('datadir') / 'vlc/addons/reference/radio-browser' / fs.parent(addon_resource))
    endforeach
    foreach addon_resource : ['addon.json', 'LICENSE', 'NOTICE.md', 'dist/addon.wasm', 'views/episodes.json', 'icons/episodes.svg']
        install_data(meson.project_source_root() / 'contrib-addons/examples/episodes' / addon_resource,
            install_dir: get_option('datadir') / 'vlc/addons/reference/episodes' / fs.parent(addon_resource))
    endforeach
    if addon_worker_dependencies != [] and not meson.is_cross_build()
        test('addon_radio_browser_worker', prog_python,
            args: ['-B', meson.project_source_root() / 'contrib-addons/examples/radio-browser/tests/test_worker.py', '--worker', addon_worker_target.full_path()],
            depends: addon_worker_dependencies, suite: ['qt', 'addons', addon_test_labels['addon_radio_browser_worker']], timeout: 90)
        test('addon_tvmaze_worker', prog_python,
            args: ['-B', meson.project_source_root() / 'contrib-addons/examples/tvmaze/tests/test_worker.py', '--worker', addon_worker_target.full_path()],
            depends: addon_worker_dependencies, suite: ['qt', 'addons', addon_test_labels['addon_tvmaze_worker']], timeout: 90)
        test('addon_episodes_worker', prog_python,
            args: ['-B', meson.project_source_root() / 'contrib-addons/examples/episodes/tests/test_worker.py', '--worker', addon_worker_target.full_path()],
            depends: addon_worker_dependencies, suite: ['qt', 'addons', addon_test_labels['addon_episodes_worker']], timeout: 90)
        # Each core Lua artwork script against its add-on; skipped without Lua 5.4.
        addon_parity_lua = find_program('lua5.4', 'lua', required: false)
        test('addon_artwork_parity', prog_python,
            args: ['-B', meson.project_source_root() / 'contrib-addons/sdk/artwork-providers/tests/test_parity.py',
                '--worker', addon_worker_target.full_path()]
                + (addon_parity_lua.found() ? ['--lua', addon_parity_lua.full_path()] : []),
            depends: addon_worker_dependencies, suite: ['qt', 'addons', addon_test_labels['addon_artwork_parity']], timeout: 180)
    endif
    modules_qrc = []
    foreach module : qml_modules
        #generates 'res.qrc', 'qmldir.qrc', 'qmldir',
        run_command(
            prog_python,
            meson.current_source_dir() / 'scripts/genqrc.py',
            '-o', meson.current_build_dir() / module['outdir'] / 'res.qrc',
            '--prefix', 'qt/qml' / module['outdir'],
            '--sources', module.get('sources', []) + module.get('singletons', []),
            check: true,
        )
        run_command(
            prog_python,
            meson.current_source_dir() / 'scripts/genqmldir.py',
            '-o', meson.current_build_dir() / module['outdir'] / 'qmldir',
            '--module', module['name'],
            '--prefix', 'qt/qml',
            '--sources', module.get('sources', []),
            '--singletons', module.get('singletons', []),
            check: true,
        )
        run_command(
            prog_python,
            meson.current_source_dir() / 'scripts/genqrc.py',
            '-o', meson.current_build_dir() / module['outdir'] / 'qmldir.qrc',
            '--prefix', 'qt/qml' / module['outdir'],
            '--sources', [meson.current_build_dir() / module['outdir'] / 'qmldir'],
            check: true,
        )
        module_qrc = qt6.preprocess(
            module['target'] + '_assets',
            method: qt_dependency_method,
            qresources: files(
                meson.current_build_dir() / module['outdir'] / 'res.qrc',
                meson.current_build_dir() / module['outdir'] / 'qmldir.qrc',
            ),
            include_directories: qt_include_dir,
            dependencies: qt6_dep,
            rcc_extra_arguments: qt_rcc_extra_arguments
        )

        #FIXME copy every files to the outdir module so they can be used by qt tooling

        qmllibs += static_library(
            'qml_module' + module['target'],
            module_qrc,
            dependencies: qt6_dep)
    endforeach

    qt_static_check = '''
    #include <qconfig.h>
    #ifndef QT_STATIC
    # error Qt using shared libraries
    #endif
    '''

    if cpp.compiles(qt_static_check, dependencies: qt6_dep, name: 'Qt6 compiled with static libraries')
        qt_install_bin_directory = qt6_dep.get_variable(pkgconfig: 'bindir', configtool: 'QT_INSTALL_BINS')
        qmake6 = find_program(qt_install_bin_directory + '/qmake6', required: true)
        if qmake6.found()
            qmake_additional_params = []
            if get_option('buildtype') == 'debug'
                qmake_additional_params += '--debug'
            endif
            qtcflags = run_command(
                prog_python,
                meson.current_source_dir() / 'scripts/static_dirs.py',
                '--qmake', qmake6.full_path(),
                '--pro', meson.current_source_dir() / 'qt6.pro',
                '--builddir', meson.current_build_dir(),
                '--cflags',
                qmake_additional_params,
                check: false,
            )
            if qtcflags.returncode() == 0
                qt_extra_flags += qtcflags.stdout().split()
            else
                warning('Failed to get static Qt compilation flags: ' + qtcflags.stderr())
            endif
            qtlflags = run_command(
                prog_python,
                meson.current_source_dir() / 'scripts/static_dirs.py',
                '--qmake', qmake6.full_path(),
                '--pro', meson.current_source_dir() / 'qt6.pro',
                '--builddir', meson.current_build_dir(),
                '--libs', '--ldflags',
                qmake_additional_params,
                check: false,
            )
            if qtlflags.returncode() == 0
                qt_link_args += qtlflags.stdout().split()
            else
                warning('Failed to get static Qt link flags: ' + qtlflags.stderr())
            endif
        else
            warning('Failed to find qmake6 to get static Qt variables')
        endif
    endif

    if host_system == 'windows'
        vlc_tests += {
            'name': 'test_qt_win32_emutls',
            'sources': files('tests/test_win32_emutls.cpp', 'util/win32_emutls.cpp'),
            'suite': ['qt'],
            'dependencies': [qt6_dep, qt_extra_deps],
            'cpp_args': qt_extra_flags,
            'link_args': qt_link_args,
        }
    endif

    qt6pre_qrc = qt6.preprocess(
        method: qt_dependency_method,
        qresources: qrc_files,
        include_directories: qt_include_dir,
        dependencies: qt6_dep,
        rcc_extra_arguments: qt_rcc_extra_arguments)

    qt_sources = files('qt.cpp')

    subdir('shaders')

    # Qt plugins
    if host_system == 'windows'
        qt_extra_deps += [
            cc.find_library('dwmapi'),
            cc.find_library('comctl32'),
        ]
    endif

    qt_xcb_check = '''
    #include <qtgui-config.h>
    #if QT_FEATURE_xcb != 1
    #error "Qt doesn't have xcb support"
    #endif
    '''
    qt_features_xcb = cpp.compiles(qt_xcb_check, dependencies: qt6_dep, name: 'Qt features xcb')

    if x11_dep.found() and xcb_dep.found() and qt_features_xcb
        qt_extra_deps += xcb_dep
        qt_extra_deps += x11_dep
        qt_extra_flags += '-DQT_HAS_XCB'
        if xcb_damage_dep.found() and xcb_xfixes_dep.found()
            qt_extra_deps += [
                xcb_render_dep,
                xcb_composite_dep,
                xcb_damage_dep,
                xcb_xfixes_dep
            ]
            qt_extra_flags += '-DQT_HAS_X11_COMPOSITOR'
        endif
    endif

    if get_option('qt_qml_debug')
        qt_extra_flags += '-DQT_QML_DEBUG'
    endif

    #check private headers
    if cpp.has_header('private/qquickitem_p.h', dependencies: qt6_dep) \
      and cpp.has_header('private/qquickwindow_p.h', dependencies: qt6_dep)
        qt_extra_flags += '-DQT_DECLARATIVE_PRIVATE'
    endif

    if cpp.has_header('qpa/qplatformnativeinterface.h', dependencies: qt6_dep)
        qt_extra_flags += '-DQT_GUI_PRIVATE'
    endif

    qt_wayland_gui_private = cpp.has_header(
        'qpa/qplatformnativeinterface.h', dependencies: qt6_dep)

    if have_wayland and qt_wayland_gui_private
        qt_extra_flags += '-DQT_HAS_WAYLAND_COMPOSITOR'
        qt_extra_deps += wayland_deps[0]

        qt_wl_xdg_shell_xml = wayland_protocols_dir / 'stable/xdg-shell/xdg-shell.xml'

        qt_wl_xdg_shell_hdr = custom_target(
            'qt-wayland-xdg-shell-client-header',
            output: 'xdg-shell-client-protocol.h',
            input: qt_wl_xdg_shell_xml,
            command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
        )
        qt_wl_xdg_shell_code = custom_target(
            'qt-wayland-xdg-shell-code',
            output: 'xdg-shell-protocol.c',
            input: qt_wl_xdg_shell_xml,
            command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
        )

        qt_wayland_sources = [
            files(
                'maininterface/qt_wayland_plugin.c',
                'maininterface/compositor_wayland_module.c',
                'maininterface/compositor_wayland_module.h',
                'util/csdmenu_wayland.c',
                'util/csdmenu_wayland.h',
                'util/csdmenu_module.h',
            ),
            qt_wl_xdg_shell_hdr,
            qt_wl_xdg_shell_code,
        ]

        qt_wayland_cargs = ['-DQT_HAS_WAYLAND_PROTOCOLS']

        if qt6_dep.version().version_compare('>= 6.5')
            qt_wayland_cargs += '-DQT_HAS_WAYLAND_FRACTIONAL_SCALING'
            qt_wl_viewporter_xml = wayland_protocols_dir / 'stable/viewporter/viewporter.xml'
            qt_wl_viewporter_hdr = custom_target(
                'qt-wayland-viewporter-client-header',
                output: 'viewporter-client-protocol.h',
                input: qt_wl_viewporter_xml,
                command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
            )
            qt_wl_viewporter_code = custom_target(
                'qt-wayland-viewporter-code',
                output: 'viewporter-protocol.c',
                input: qt_wl_viewporter_xml,
                command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
            )
            qt_wayland_sources += [qt_wl_viewporter_hdr, qt_wl_viewporter_code]
        endif

        vlc_modules += {
            'name': 'qt_wayland',
            'sources': qt_wayland_sources,
            'c_args': qt_wayland_cargs,
            'include_directories': [qt_include_dir],
            'dependencies': [wayland_deps[0], m_lib],
            'plugin_dir' : 'gui',
        }
    endif

    if host_system == 'windows'
        qt_cppargs += libcom_cppflags
    endif

    #Qt GTK theme module
    gtk_dep = dependency(
        'gtk+-3.0',
        version: '>=3.20',
        required: get_option('qt_gtk').disable_auto_if(host_system in ['windows', 'darwin'])
    )

    vlc_modules += {
        'name' : 'qt_gtktheme',
        'sources' : files(
            'style/gtkthemeprovider/gtk_compat.cpp',
            'style/gtkthemeprovider/gtk_compat.h',
            'style/gtkthemeprovider/gtkthemeprovider.cpp',
            'style/gtkthemeprovider/gtk_util.cpp',
            'style/gtkthemeprovider/gtk_util.h',
            'style/gtkthemeprovider/nav_button_provider_gtk.cpp',
            'style/gtkthemeprovider/nav_button_provider_gtk.h',
            'style/gtkthemeprovider/scoped_gobject.h'
        ),
        'dependencies' : [gtk_dep, qt6_dep],
        'plugin_dir' : 'gui',
        'enabled' : gtk_dep.found()
    }

    if gtk_dep.found()
        qt_extra_flags += '-DQT_HAS_GTK'
    endif

    if host_system == 'windows'
        qt_plugin_sources += files('maininterface/win32windoweffects_module.cpp',
                              'maininterface/win32windoweffects_module.hpp')
    else
        kf6WindowSystem_dep = dependency('KF6WindowSystem',
                                         version: '>=6.0.0',
                                         required: false)
        if (kf6WindowSystem_dep.found())
            vlc_modules += {
                'name' : 'qtwindoweffects',
                'sources' : files(
                    'maininterface/windoweffects_module.hpp',
                    'maininterface/kwindowsystem_module.cpp',
                ),
                'dependencies' : [kf6WindowSystem_dep, qt6_dep],
                'plugin_dir' : 'gui',
            }
        endif
    endif

    vlc_modules += {
        'name' : 'qt_x11',
        'include_directories' : qt_include_dir,
        'sources' : files(
            'maininterface/qt_x11_plugin.c',
            'maininterface/compositor_x11_utils.hpp',
            'maininterface/compositor_x11_utils.cpp',
            'util/csdmenu_module.h',
            'util/csdmenu_x11.cpp',
            'util/csdmenu_x11.h'
        ),
        'dependencies' : [xcb_dep, xcb_render_dep, xcb_xfixes_dep],
        'plugin_dir' : 'gui',
        'enabled': xcb_dep.found() and xcb_render_dep.found() and xcb_xfixes_dep.found(),
    }

    addon_host_moc = qt6.preprocess(
        method: qt_dependency_method,
        moc_headers: addon_host_moc_headers,
        moc_extra_arguments: ['-DHAVE_CONFIG_H=1', qt_extra_flags],
        include_directories: [qt_include_dir, vlc_include_dirs],
        dependencies: qt6_dep)
    addon_host_library = static_library('vlc_addon_host', addon_host_sources, addon_host_moc,
        include_directories: [qt_include_dir, vlc_include_dirs],
        dependencies: [qt6_dep, qt_extra_deps, libvlccore_deps, threads_dep],
        cpp_args: [qt_extra_flags, qt_cppargs, '-D__PLUGIN__'],
        pic: true,
        build_by_default: false)
    # The package validator for add-on authors: the host's install rules,
    # without running package code.
    addon_validate = executable('vlc-addon-validate', files('addons/tools/vlc_addon_validate.cpp'),
        include_directories: [qt_include_dir, vlc_include_dirs],
        dependencies: [qt6_dep, qt_extra_deps, libvlccore_deps, threads_dep],
        link_with: [addon_host_library, libvlccore],
        cpp_args: [qt_extra_flags, qt_cppargs],
        install: false, build_by_default: false)
    addon_preview = executable('vlc-addon-preview', files('addons/tools/vlc_addon_preview.cpp'),
        include_directories: [qt_include_dir, vlc_include_dirs],
        dependencies: [qt6_dep, qt_extra_deps, libvlccore_deps, threads_dep],
        link_with: [addon_host_library, libvlccore],
        cpp_args: [qt_extra_flags, qt_cppargs],
        install: false, build_by_default: false)
    addon_validate_examples = []
    foreach example : ['discovery-fixture', 'french-tv', 'google-images', 'lastfm', 'midnight', 'musicbrainz', 'radio-browser', 'thesubtitledb', 'tvmaze', 'visual-scenes']
        addon_validate_examples += meson.project_source_root() / 'contrib-addons/examples' / example
    endforeach
    test('addon_validate_examples', addon_validate,
        args: ['--ui', meson.current_source_dir() / 'player/qml/redesign/AddonUi.js'] + addon_validate_examples,
        suite: ['qt', 'addons', addon_test_labels['addon_validate_examples']], timeout: 60)
    test('addon_validate_refuses', addon_validate, args: [meson.current_source_dir() / 'tests/fixtures/invalid-addon'],
        should_fail: true, suite: ['qt', 'addons', addon_test_labels['addon_validate_refuses']], timeout: 30)
    addon_preview_args = ['--qml', meson.current_source_dir() / 'player/qml/redesign', '--output', meson.current_build_dir() / 'addon-preview']
    test('addon_preview', addon_preview,
        args: addon_preview_args + [meson.current_source_dir() / 'tests/fixtures/preview-addon'],
        env: ['QT_QPA_PLATFORM=offscreen'], suite: ['qt', 'addons', addon_test_labels['addon_preview']], timeout: 60)
    test('addon_preview_theme', addon_preview,
        args: addon_preview_args + [meson.project_source_root() / 'contrib-addons/examples/midnight'],
        env: ['QT_QPA_PLATFORM=offscreen'], suite: ['qt', 'addons', addon_test_labels['addon_preview_theme']], timeout: 60)
    # The schemas, their generated Rust types and the author templates.
    test('addon_schema_corpus_python', prog_python, args: ['-B', meson.project_source_root() / 'contrib-addons/sdk/schemas/check-corpus.py'],
        suite: ['qt', 'addons', addon_test_labels['addon_schema_corpus_python']], timeout: 60)
    test('addon_schema_rust_drift', prog_python, args: ['-B', meson.project_source_root() / 'contrib-addons/sdk/schemas/generate-rust.py', '--check'],
        suite: ['qt', 'addons', addon_test_labels['addon_schema_rust_drift']], timeout: 30)
    if addon_worker_dependencies != []
        test('addon_templates', prog_python,
            args: ['-B', meson.project_source_root() / 'contrib-addons/sdk/templates/check-templates.py', '--validator', addon_validate.full_path(),
                '--ui', meson.current_source_dir() / 'player/qml/redesign/AddonUi.js', '--worker', addon_worker_target.full_path()],
            depends: [addon_validate, addon_worker_dependencies], env: ['RUSTUP_TOOLCHAIN=stable'],
            suite: ['qt', 'addons', addon_test_labels['addon_templates']], timeout: 600)
    endif

    qt6pre_files = qt6.preprocess(ui_files: ui_sources,
        method: qt_dependency_method,
        moc_headers: moc_headers,
        # MOC must see the same configured feature guards as the C++ compiler.
        moc_extra_arguments: ['-DHAVE_CONFIG_H=1', qt_extra_flags],
        include_directories: [qt_include_dir, vlc_include_dirs],
        dependencies: qt6_dep,
        rcc_extra_arguments: qt_rcc_extra_arguments)

    # TODO support qmlcachegen

    # Test builds only: the smoke test drives add-on actions through this owner.
    if get_option('addon-test-driver')
        qt_plugin_sources += files('addons/addon_test_driver.cpp', 'addons/addon_test_driver.hpp')
        qt_cppargs += '-DVLC_ADDON_TEST_DRIVER'
    endif

    vlc_modules += {
        'name' : 'qt',
        'sources' : [qt6pre_files, qt6pre_qrc, shader_targets, shaders_processed, qt_sources, qt_plugin_sources, subtitle_consumer_key, addon_distribution_paths],
        'extra_files' : [shader_files],
        'dependencies' : [qt_extra_deps, qt6_dep, corefoundation_dep],
        'include_directories' : qt_include_dir,
        'c_args' : qt_extra_flags,
        'cpp_args' : [qt_extra_flags, qt_cppargs],
        'link_args' : qt_link_args,
        'link_with' : [qmllibs, addon_host_library],
        'plugin_dir' : 'gui',
    }

    vlc_tests += {
        'name': 'base_model_test',
        'sources': files(
            'tests/test_base_model.cpp',
            'util/base_model.cpp',
            'util/base_model.hpp',
            'util/base_model_p.hpp'),
        'moc_headers': files(
            'util/base_model.hpp',
            'util/listcache.hpp',
            'util/locallistcacheloader.hpp'),
        'suite': ['qt'],
        'link_with': [libvlccore],
        'link_args': qt_link_args,
        'dependencies': [qt6_dep, qt_extra_deps],
    }

    qtest_qt6_dep = dependency('qt6', version: '=' + qt6_dep.version(), modules: ['Test'], required: false)
    if qtest_qt6_dep.found()
        addon_visualizer_process = executable('addon_visualizer_process', files('tests/fixtures/addon_visualizer_process.c'), install: false, build_by_default: false)
        addon_discovery_source_process = executable('addon_discovery_source_process', files('tests/fixtures/addon_discovery_source_process.c'), install: false, build_by_default: false)
        addon_stream_native_tests = get_option('adaptive').allowed() and gcrypt_dep.found() and z_dep.found() and libdvbpsi_dep.found() and avcodec_dep.found()
        foreach addon_test : ['display_integration', 'distribution_bootstrap', 'catalogue_view', 'display_catalogue', 'packages', 'package_summaries', 'runtime', 'metadata', 'metadata_choices', 'artwork_attempt_plan', 'broker', 'ui_registry', 'metadata_service', 'discovery', 'theme', 'sessions', 'audio_analysis', 'visual_scene', 'visualizer_parameters', 'store_authority', 'catalogue_native', 'native_notifications', 'stream_manifest', 'websocket_codec', 'visualizers', 'host_services', 'repository_updates', 'playable_gate', 'artwork_assets', 'artwork_image', 'native_artwork_assets', 'queue_transactions', 'discovery_service', 'source_admissions', 'storage', 'store_process_fence', 'store_profile_generation', 'store_profile_protocol', 'store_profile_peer', 'store_profile_socket', 'store_profile_coordinator', 'store_profile_client', 'store_profile_session', 'store_profile_writes', 'profile_host', 'worker_health', 'candidate_check', 'accounts', 'guest_projections', 'media_siblings', 'schema_corpus', 'identity_choice', 'store_profile_settings', 'store_profile_packages', 'store_profile_lifecycle', 'source_consent', 'visualizer_resources', 'visualizer_worker', 'visualizer_worker_runtime', 'visualizer_render_queue', 'native_source_resolver', 'native_player_analysis', 'visualizer_clock', 'stream_http', 'stream_proxy', 'stream_shutdown', 'app_shutdown', 'repository_jobs', 'native_authority']
            native_stream = addon_test == 'stream_proxy' and addon_stream_native_tests
            addon_timeout = addon_test in ['catalogue_view', 'display_catalogue', 'package_summaries', 'stream_http', 'stream_proxy', 'stream_shutdown', 'app_shutdown', 'repository_jobs', 'native_authority', 'native_source_resolver', 'store_profile_settings', 'store_profile_packages', 'store_profile_lifecycle', 'playable_gate', 'runtime', 'store_profile_coordinator', 'store_profile_client', 'store_profile_session', 'profile_host', 'worker_health', 'candidate_check', 'ui_registry'] ? 90 : 30
            if addon_test in ['display_integration', 'distribution_bootstrap']
                addon_timeout = 180
            endif
            display_defines = []
            display_links = []
            display_roots = []
            if addon_test == 'display_integration'
                display_roots += ['-DHAKONE_DISPLAY_SOURCE_ROOT="' + meson.project_source_root() + '"']
                if host_system == 'linux'
                    display_defines += ['-DHAKONE_DISPLAY_CREDENTIAL_CREATE_WRAP', '-DHAKONE_DISPLAY_ACQUIRE_WRAP']
                    display_links += ['-Wl,--wrap=_ZN3vlc6addons15CredentialStore6createERK7QStringS4_RK10QByteArrayb', '-Wl,--wrap=_ZN3vlc6addons18AddonCatalogueView15acquireSettingsERK7QString']
                endif
            elif addon_test == 'distribution_bootstrap'
                display_roots += ['-DHAKONE_STARTUP_SOURCE_ROOT="' + meson.project_source_root() + '"']
                if host_system == 'linux'
                    display_defines += ['-DHAKONE_BOOTSTRAP_RUN_WRAP']
                    display_links += ['-Wl,--wrap=_ZN3vlc6addons21distributionBootstrap3runENS0_12PackageStoreE5QListINS0_26DistributionBootstrapEntryEE']
                endif
            endif
            if addon_test == 'display_catalogue' and host_system == 'linux'
                display_defines += ['-DHAKONE_DISPLAY_READER_LOCK_WRAP']
                display_links += ['-Wl,--wrap=open', '-Wl,--wrap=open64']
            endif
            if addon_test in ['store_profile_packages', 'store_profile_lifecycle'] and host_system == 'linux'
                display_defines += ['-DHAKONE_PROFILE_SAVEFILE_WRAP']
                display_links += ['-Wl,--wrap=_ZN9QSaveFile6commitEv']
            endif
            if addon_test == 'native_player_analysis' and host_system == 'linux'
                display_defines += ['-DHAKONE_ANALYSIS_OBSERVER_WRAP']
                display_links += ['-Wl,--wrap=vlc_player_AddTimer', '-Wl,--wrap=vlc_player_RemoveTimer', '-Wl,--wrap=vlc_player_aout_AddListener', '-Wl,--wrap=vlc_player_aout_RemoveListener', '-Wl,--wrap=vlc_player_AddListener', '-Wl,--wrap=vlc_player_RemoveListener']
            endif
            vlc_tests += {
                'name': 'test_qt_addon_' + addon_test,
                'sources': files('tests/test_addon_' + addon_test + '.cpp'),
                'moc_sources': files('tests/test_addon_' + addon_test + '.cpp'),
                'moc_args': (native_stream ? ['-DVLC_ADDON_NATIVE_STREAM_TESTS'] : []) + display_defines,
                'timeout': addon_timeout,
                'module_depends': native_stream ? ['adaptive', 'ts', 'araw', 'adummy', 'wav', 'avcodec'] : [],
                'link_with': addon_test in ['native_artwork_assets', 'native_source_resolver', 'native_player_analysis', 'visualizer_clock', 'stream_proxy'] ? [addon_host_library, libvlccore, libvlc] : [addon_host_library, libvlccore],
                'suite': ['qt', 'addons'],
                'include_directories': addon_test in ['native_artwork_assets', 'native_source_resolver', 'native_player_analysis', 'visualizer_clock', 'stream_proxy'] ? [qt_include_dir, include_directories('../../..')] : qt_include_dir,
                'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
                'cpp_args': ['-DVLC_ADDON_SOURCE_ROOT="' + meson.project_source_root() + '"', '-DVLC_ADDON_NATIVE_BUILD_ROOT="' + meson.project_build_root() + '"', '-DVLC_ADDON_VISUALIZER_PROCESS_FIXTURE="' + addon_visualizer_process.full_path() + '"'] + (native_stream ? ['-DVLC_ADDON_NATIVE_STREAM_TESTS'] : []) + display_defines + display_roots,
                'link_args': qt_link_args + display_links,
                'depends': [addon_worker_dependencies, addon_visualizer_process],
                'env': addon_worker_environment,
            }
        endforeach
        vlc_tests += {
            'name': 'test_qt_addon_metadata_catalogue',
            'sources': files('tests/test_addon_metadata_catalogue.cpp'),
            'moc_sources': files('tests/test_addon_metadata_catalogue.cpp'),
            'moc_args': host_system == 'linux' ? ['-DHAKONE_CATALOGUE_OPEN_PROBE'] : [],
            'cpp_args': host_system == 'linux' ? ['-DHAKONE_CATALOGUE_OPEN_PROBE'] : [],
            'link_with': [addon_host_library, libvlccore],
            'suite': ['qt', 'addons'],
            'timeout': 90,
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'link_args': qt_link_args + (host_system == 'linux' ? ['-Wl,--wrap=open', '-Wl,--wrap=open64'] : []),
        }
        if host_system == 'linux' and addon_worker_dependencies != [] and not meson.is_cross_build()
            addon_metadata_attempt_process = executable('addon_metadata_attempt_process',
                files('tests/fixtures/addon_metadata_attempt_process.c'), install: false, build_by_default: false)
            vlc_tests += {
                'name': 'test_qt_addon_metadata_attempt',
                'sources': files('tests/test_addon_metadata_attempt.cpp'),
                'moc_sources': files('tests/test_addon_metadata_attempt.cpp'),
                'link_with': [addon_host_library, libvlccore],
                'suite': ['qt', 'addons'],
                'timeout': 180,
                'include_directories': qt_include_dir,
                'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
                'cpp_args': [
                    '-DHAKONE_ATTEMPT_PROCESS_FIXTURE="' + addon_metadata_attempt_process.full_path() + '"',
                    '-DHAKONE_ATTEMPT_FIXTURE_DIR="' + meson.current_source_dir() / 'tests/fixtures/metadata-attempt' + '"',
                    '-DHAKONE_ATTEMPT_RUST_WORKER="' + addon_worker_target.full_path() + '"',
                    '-DHAKONE_ATTEMPT_FRENCH_TV_WASM="' + meson.project_source_root() / 'contrib-addons/examples/french-tv/dist/addon.wasm' + '"',
                ],
                'link_args': qt_link_args,
                'depends': [addon_worker_dependencies, addon_metadata_attempt_process],
                'env': addon_worker_environment,
            }
            addon_prepared_metadata_process = executable('addon_prepared_metadata_process',
                files('tests/fixtures/addon_prepared_metadata_process.c'), install: false, build_by_default: false)
            vlc_tests += {
                'name': 'test_qt_addon_prepared_metadata_invocation',
                'sources': files('tests/test_addon_prepared_metadata_invocation.cpp'),
                'moc_sources': files('tests/test_addon_prepared_metadata_invocation.cpp'),
                'moc_args': ['-DHAKONE_PREPARED_OWNER_WRAP', '-DHAKONE_METADATA_PREPARATION_WRAP', '-DHAKONE_ARTWORK_READ_WRAP'],
                'link_with': [addon_host_library, libvlccore],
                'suite': ['qt', 'addons'],
                'timeout': 180,
                'include_directories': qt_include_dir,
                'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
                'cpp_args': [
                    '-DHAKONE_PREPARED_OWNER_WRAP', '-DHAKONE_METADATA_PREPARATION_WRAP', '-DHAKONE_ARTWORK_READ_WRAP',
                    '-DHAKONE_PREPARED_PROCESS_FIXTURE="' + addon_prepared_metadata_process.full_path() + '"',
                    '-DHAKONE_PREPARED_RUST_WORKER="' + addon_worker_target.full_path() + '"',
                    '-DHAKONE_PREPARED_FRENCH_TV_WASM="' + meson.project_source_root() / 'contrib-addons/examples/french-tv/dist/addon.wasm' + '"',
                    '-DHAKONE_ATTEMPT_FIXTURE_DIR="' + meson.current_source_dir() / 'tests/fixtures/metadata-attempt' + '"',
                ],
                'link_args': qt_link_args + ['-Wl,--wrap=_ZN3vlc6addons11AddonWorker23enablePreparedLifecycleEv', '-Wl,--wrap=_ZNK3vlc6addons12PackageStore23metadataCatalogueReaderEv', '-Wl,--wrap=_ZNK3vlc6addons12AddonManager21nativeAuthorityReaderERK7QString', '-Wl,--wrap=_ZN12QImageReader4readEv'],
                'depends': [addon_worker_dependencies, addon_prepared_metadata_process],
                'env': addon_worker_environment,
            }
        endif
        if host_system == 'linux'
            vlc_tests += {
                'name': 'test_qt_addon_metadata_registry_epoch',
                'sources': files('tests/test_addon_metadata_registry_epoch.cpp', 'tests/fixtures/addon_artwork_attempt_keeper.c'),
                'moc_sources': files('tests/test_addon_metadata_registry_epoch.cpp'),
                'link_with': [addon_host_library, libvlccore, libvlc],
                'suite': ['qt', 'addons'],
                'timeout': 90,
                'include_directories': [qt_include_dir, include_directories('../../..', '../../../src')],
                'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
                'link_args': qt_link_args,
            }
            vlc_tests += {
                'name': 'test_qt_addon_artwork_attempt_keeper',
                'sources': files('tests/test_addon_artwork_attempt_keeper.cpp', 'tests/fixtures/addon_artwork_attempt_keeper.c'),
                'moc_sources': files('tests/test_addon_artwork_attempt_keeper.cpp'),
                'link_with': [addon_host_library, libvlccore, libvlc],
                'suite': ['qt', 'addons'],
                'timeout': 90,
                'include_directories': [qt_include_dir, include_directories('../../..', '../../../src')],
                'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
                'link_args': qt_link_args,
            }
            if addon_worker_dependencies != [] and not meson.is_cross_build()
                vlc_tests += {
                    'name': 'test_qt_addon_artwork_attempt_aggregate',
                    'sources': files('tests/test_addon_artwork_attempt_aggregate.cpp', 'tests/fixtures/addon_artwork_attempt_keeper.c', 'tests/fixtures/addon_native_provider_fixture.c', '../../../src/preparser/metadata_native_provider.c'),
                    'moc_sources': files('tests/test_addon_artwork_attempt_aggregate.cpp'),
                    'link_with': [addon_host_library, libvlccore, libvlc],
                    'suite': ['qt', 'addons'],
                    'timeout': 180,
                    'include_directories': [qt_include_dir, include_directories('../../..', '../../../src')],
                    'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
                    'cpp_args': [
                        '-DHAKONE_AGGREGATE_PROCESS_FIXTURE="' + addon_metadata_attempt_process.full_path() + '"',
                        '-DHAKONE_ATTEMPT_FIXTURE_DIR="' + meson.current_source_dir() / 'tests/fixtures/metadata-attempt' + '"',
                        '-DHAKONE_AGGREGATE_RUST_WORKER="' + addon_worker_target.full_path() + '"',
                        '-DHAKONE_AGGREGATE_FRENCH_TV_WASM="' + meson.project_source_root() / 'contrib-addons/examples/french-tv/dist/addon.wasm' + '"',
                    ],
                    'link_args': qt_link_args,
                    'depends': [addon_worker_dependencies, addon_metadata_attempt_process],
                    'env': addon_worker_environment,
                }
            endif
            foreach addon_test : ['native_provider_context', 'native_metadata_owner']
                vlc_tests += {
                    'name': 'test_qt_addon_' + addon_test,
                    'sources': files('tests/test_addon_' + addon_test + '.cpp',
                        'tests/fixtures/addon_native_provider_fixture.c',
                        '../../../src/preparser/metadata_native_provider.c'),
                    'moc_sources': files('tests/test_addon_' + addon_test + '.cpp'),
                    'moc_args': addon_test == 'native_provider_context' ? ['-DHAKONE_NATIVE_OUTCOME_WRAP'] : [],
                    'cpp_args': addon_test == 'native_provider_context' ? ['-DHAKONE_NATIVE_OUTCOME_WRAP'] : [],
                    'link_with': [addon_host_library, libvlccore, libvlc],
                    'suite': ['qt', 'addons'],
                    'timeout': 90,
                    'include_directories': [qt_include_dir, include_directories('../../..', '../../../src')],
                    'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
                    'link_args': qt_link_args + (addon_test == 'native_metadata_owner' ? ['-Wl,--wrap=_Znwm', '-Wl,--wrap=_Znwj', '-Wl,--wrap=_ZdlPv', '-Wl,--wrap=_ZdlPvm', '-Wl,--wrap=_ZdlPvj'] : ['-Wl,--wrap=vlc_metadata_native_claim_AdmitOutcome']),
                }
            endforeach
        endif
        vlc_tests += {
            'name': 'test_qt_addon_visualizer_surface',
            'sources': files('tests/test_addon_visualizer_surface.cpp', 'addons/addon_visualizer_surface.cpp'),
            'moc_sources': files('tests/test_addon_visualizer_surface.cpp'),
            'moc_headers': files('addons/addon_visualizer_surface.hpp'),
            'link_with': [addon_host_library, libvlccore],
            'suite': ['qt', 'addons'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'link_args': qt_link_args,
            'env': ['QT_QPA_PLATFORM=offscreen', 'QT_QUICK_BACKEND=software'],
        }
        vlc_tests += {
            'name': 'test_qt_addon_ui_assets',
            'sources': files('tests/test_addon_ui_assets.cpp', 'addons/addon_asset_image_provider.cpp'),
            'moc_sources': files('tests/test_addon_ui_assets.cpp'),
            'link_with': [addon_host_library, libvlccore],
            'suite': ['qt', 'addons'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'cpp_args': ['-DVLC_ADDON_SOURCE_ROOT="' + meson.project_source_root() + '"'],
            'link_args': qt_link_args,
            'env': ['QT_QPA_PLATFORM=offscreen', 'QT_QUICK_BACKEND=software'],
        }
        vlc_tests += {
            'name': 'test_qt_addon_native_queue_store',
            'sources': files('tests/test_addon_native_queue_store.cpp'),
            'moc_sources': files('tests/test_addon_native_queue_store.cpp'),
            'link_with': [addon_host_library, libvlccore, libvlc],
            'suite': ['qt', 'addons'],
            'include_directories': [qt_include_dir, include_directories('../../..')],
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'cpp_args': ['-DVLC_ADDON_NATIVE_BUILD_ROOT="' + meson.project_build_root() + '"'],
            'link_args': qt_link_args,
        }
        foreach addon_test : ['discovery_source_owner', 'source_consent_ui_owner']
            vlc_tests += {
                'name': 'test_qt_addon_' + addon_test,
                'sources': files('tests/test_addon_' + addon_test + '.cpp'),
                'moc_sources': files('tests/test_addon_' + addon_test + '.cpp'),
                'link_with': [addon_host_library, libvlccore, libvlc],
                'suite': ['qt', 'addons'],
                'timeout': 90,
                'module_depends': addon_test == 'discovery_source_owner' ? ['http', 'https', 'console_logger', 'prefetch', 'record', 'wav', 'araw', 'adummy', 'float_mixer', 'scaletempo', 'audio_format', 'ugly_resampler'] : [],
                'include_directories': [qt_include_dir, include_directories('../../..')],
                'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
                'cpp_args': ['-DVLC_ADDON_SOURCE_ROOT="' + meson.project_source_root() + '"', '-DVLC_ADDON_NATIVE_BUILD_ROOT="' + meson.project_build_root() + '"'] + (addon_test == 'discovery_source_owner' ? ['-DVLC_ADDON_DISCOVERY_PROCESS_FIXTURE="' + addon_discovery_source_process.full_path() + '"'] : []),
                'depends': addon_test == 'discovery_source_owner' ? [addon_discovery_source_process] : [],
                'link_args': qt_link_args,
                'env': addon_worker_environment,
            }
        endforeach
        if host_system == 'windows'
            vlc_tests += {
                'name': 'test_qt_window_fullscreen',
                'sources': files('tests/test_window_fullscreen.cpp', 'util/win32_emutls.cpp'),
                'moc_sources': files('tests/test_window_fullscreen.cpp'),
                'suite': ['qt'],
                'include_directories': qt_include_dir,
                'dependencies': [qt6_dep, qtest_qt6_dep, qt_extra_deps],
                'link_args': qt_link_args,
            }
            vlc_tests += {
                'name': 'test_qt_dcomp_backdrop',
                'sources': files('tests/test_dcomp_backdrop.cpp', 'maininterface/compositor_dcomp_backdrop.cpp'),
                'moc_sources': files('tests/test_dcomp_backdrop.cpp'),
                'suite': ['qt'],
                'include_directories': qt_include_dir,
                'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep,
                                 cc.find_library('d3d11'), cc.find_library('dcomp'), cc.find_library('dxguid')],
            }
        endif
        vlc_tests += {
            'name': 'test_qt_redesign_svg_raster',
            'sources': files('tests/test_redesign_svg_raster.cpp'),
            'moc_sources': files('tests/test_redesign_svg_raster.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'env': {'QT_QPA_PLATFORM': 'offscreen', 'QT_QUICK_BACKEND': 'software'},
        }
        vlc_tests += {
            'name': 'test_qt_backdrop_blur',
            'sources': [files('tests/test_backdrop_blur.cpp', 'util/textureproviderobserver.cpp', 'widgets/native/viewblockingrectangle.cpp', 'widgets/native/softwarebackdropblur.cpp'), shaders_processed, shader_targets],
            'moc_sources': files('tests/test_backdrop_blur.cpp'),
            'moc_headers': files('util/textureproviderobserver.hpp', 'widgets/native/viewblockingrectangle.hpp', 'widgets/native/softwarebackdropblur.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_video_backdrop',
            'sources': files('tests/test_video_backdrop.cpp', 'maininterface/videobackdrop.cpp'),
            'moc_sources': files('tests/test_video_backdrop.cpp'),
            'moc_headers': files('maininterface/videobackdrop.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_video_backdrop_reporter',
            'sources': files('tests/test_video_backdrop_reporter.cpp'),
            'moc_sources': files('tests/test_video_backdrop_reporter.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_viewblockingrectangle',
            'sources': files('tests/test_viewblockingrectangle.cpp', 'widgets/native/viewblockingrectangle.cpp'),
            'moc_sources': files('tests/test_viewblockingrectangle.cpp'),
            'moc_headers': files('widgets/native/viewblockingrectangle.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_track_folder_rules',
            'sources': files('tests/test_track_folder_rules.cpp', '../../misc/medialibrary/track_rule_store.cpp'),
            'moc_sources': files('tests/test_track_folder_rules.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_with': [libvlccore],
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_github_releases',
            'sources': files('tests/test_github_releases.cpp', 'util/release_info.cpp', 'util/github_release_service.cpp'),
            'moc_sources': files('tests/test_github_releases.cpp'),
            'moc_headers': files('util/github_release_service.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'link_args': qt_link_args,
        }
        vlc_tests += {
            'name': 'test_qt_artwork_discovery',
            'sources': files('tests/test_artwork_discovery.cpp'),
            'moc_sources': files('tests/test_artwork_discovery.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_media_identity',
            'sources': files('tests/test_media_identity.cpp'),
            'moc_sources': files('tests/test_media_identity.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_filter_chain_edit',
            'sources': files('tests/test_filter_chain_edit.cpp'),
            'moc_sources': files('tests/test_filter_chain_edit.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_with': [libvlccore],
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_series_discovery',
            'sources': files('tests/test_series_discovery.cpp'),
            'moc_sources': files('tests/test_series_discovery.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_series_model',
            'sources': files('tests/test_series_model.cpp', 'player/series_model.cpp', 'util/background_watcher.cpp'),
            'moc_sources': files('tests/test_series_model.cpp'),
            'moc_headers': files('player/series_model.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        subtitle_preparation_args = []
        subtitle_preparation_link_args = []
        if host_system == 'linux'
            subtitle_preparation_args = ['-DHAKONE_METADATA_PREPARATION_WRAP']
            subtitle_preparation_link_args = ['-Wl,--wrap=_ZNK3vlc6addons12PackageStore23metadataCatalogueReaderEv', '-Wl,--wrap=_ZNK3vlc6addons12AddonManager21nativeAuthorityReaderERK7QString']
        endif
        vlc_tests += {
            'name': 'test_qt_subtitle_sources',
            'sources': files('tests/test_subtitle_sources.cpp', 'util/subtitle_source_service.cpp'),
            'moc_sources': files('tests/test_subtitle_sources.cpp'),
            'moc_args': subtitle_preparation_args,
            'cpp_args': subtitle_preparation_args,
            'link_args': qt_link_args + subtitle_preparation_link_args,
            'moc_headers': files('util/subtitle_source_service.hpp'),
            'link_with': [addon_host_library, libvlccore],
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_track_list_model',
            'sources': files('tests/test_track_list_model.cpp', 'player/input_models.cpp', 'util/vlctick.cpp'),
            'moc_sources': files('tests/test_track_list_model.cpp'),
            'moc_headers': files('player/input_models.hpp', 'util/vlctick.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_with': [libvlccore],
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_audio_device_model',
            'sources': files('tests/test_audio_device_model.cpp', 'util/audio_device_model.cpp'),
            'moc_sources': files('tests/test_audio_device_model.cpp'),
            'moc_headers': files('util/audio_device_model.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_stereo_test_signal',
            'sources': files('tests/test_stereo_test_signal.cpp'),
            'moc_sources': files('tests/test_stereo_test_signal.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_cover_generator',
            'sources': files('tests/test_cover_generator.cpp', 'util/covergenerator.cpp'),
            'moc_sources': files('tests/test_cover_generator.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_redesign_folder_model',
            'sources': files('tests/test_redesign_folder_model.cpp', 'util/redesign_folder_model.cpp'),
            'moc_sources': files('tests/test_redesign_folder_model.cpp'),
            'moc_headers': files('util/redesign_folder_model.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_effects_image_provider',
            'sources': files('tests/test_effects_image_provider.cpp', 'util/effects_image_provider.cpp'),
            'moc_sources': files('tests/test_effects_image_provider.cpp'),
            'moc_headers': files('util/effects_image_provider.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_seek_preview_image_provider',
            'sources': files('tests/test_seek_preview_image_provider.cpp'),
            'moc_sources': files('tests/test_seek_preview_image_provider.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_with': [libvlccore, libvlc],
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
        vlc_tests += {
            'name': 'test_qt_subtitle_timeline_scanner',
            'sources': files('tests/test_subtitle_timeline_scanner.cpp', 'player/subtitle_timeline_scanner.cpp'),
            'moc_sources': files('tests/test_subtitle_timeline_scanner.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_with': [libvlccore, libvlc],
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'module_depends': ['filesystem', 'subtitle', 'subsdec', 'http'],
            'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
        }
        if x11_dep.found() and xcb_dep.found() and qt_features_xcb
            if xcb_render_dep.found()
                vlc_tests += {
                    'name': 'test_qt_x11_video_backdrop',
                    'sources': files('tests/test_x11_video_backdrop.cpp', 'maininterface/compositor_x11_backdrop.cpp'),
                    'moc_sources': files('tests/test_x11_video_backdrop.cpp'),
                    'suite': ['qt'],
                    'include_directories': qt_include_dir,
                    'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep, xcb_dep, xcb_render_dep],
                }
            endif
            vlc_tests += {
                'name': 'test_qt_x11_surface_size',
                'sources': files(
                    'tests/test_x11_surface_size.cpp',
                    'maininterface/compositor_x11_uisurface.cpp',
                    'maininterface/compositor_common.cpp'),
                'moc_sources': files('tests/test_x11_surface_size.cpp'),
                'moc_headers': files(
                    'maininterface/compositor_x11_uisurface.hpp',
                    'maininterface/compositor_common.hpp'),
                'suite': ['qt'],
                'include_directories': qt_include_dir,
                'link_with': [libvlccore, libvlc],
                'link_args': qt_link_args,
                'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            }
        endif

        vlc_tests += {
            'name': 'test_qt_audio_waveform_scanner',
            'sources': files('tests/test_audio_waveform_scanner.cpp', 'player/audio_waveform_scanner.cpp'),
            'moc_sources': files('tests/test_audio_waveform_scanner.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_with': [libvlccore, libvlc],
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'module_depends': ['filesystem', 'wav', 'araw', 'ugly_resampler', 'simple_channel_mixer', 'trivial_channel_mixer', 'audio_format'],
            'env': ['VLC_PLUGIN_PATH=' + vlc_build_root / 'modules'],
        }
        vlc_tests += {
            'name': 'test_qt_ml_model',
            'module_depends': ['export'],
            'sources': files(
                'tests/test_ml_model.cpp',
                'tests/vlc_stub_modules.cpp',
                'util/base_model.cpp',
                'util/vlctick.cpp',
                'medialibrary/mlbasemodel.cpp',
                'medialibrary/medialib.cpp',
                'medialibrary/mlthreadpool.cpp',
                'medialibrary/mljoinedthreadrunner.cpp',
                'medialibrary/mljoinedthreadrunner.hpp',
                'medialibrary/mltaskid.hpp',
                'addons/addon_stream_lifetime.cpp',
                'medialibrary/mlmediastore.cpp',
                'medialibrary/mlfileartwork.cpp',
                'util/background_watcher.cpp',
                'medialibrary/mlfoldersmodel.cpp',
                'medialibrary/mlhelper.cpp',
                'playlist/playlist_controller.cpp',
                'playlist/playlist_item.cpp',
            ),
            'moc_sources': files(
                'tests/test_ml_model.cpp'
            ),
            'moc_headers': files(
                'tests/vlc_stub_modules.cpp',
                'util/base_model.hpp',
                'util/listcache.hpp',
                'util/locallistcacheloader.hpp',
                'util/vlctick.hpp',
                'medialibrary/mlqmltypes.hpp',
                'medialibrary/mlbasemodel.hpp',
                'medialibrary/medialib.hpp',
                'medialibrary/mlthreadpool.hpp',
                'medialibrary/mlmediastore.hpp',
                'medialibrary/mlfileartwork.hpp',
                'util/asynctask.hpp',
                'medialibrary/mlfoldersmodel.hpp',
                'playlist/playlist_controller.hpp',
                'playlist/playlist_item.hpp',
            ),
            'suite': ['qt'],
            'include_directories' : qt_include_dir,
            'link_with': [libvlccore, libvlc],
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_navigation_model',
            'sources': files(
                'tests/vlc_stub_modules.cpp',
                'tests/test_navigation_model.cpp',
                'maininterface/navigationmodel.hpp',
                'maininterface/navigationmodel.cpp'
            ),
            'moc_sources': files(
                'tests/test_navigation_model.cpp'
            ),
            'moc_headers': files(
                'maininterface/navigationmodel.hpp'
            ),
            'suite': ['qt'],
            'include_directories' : qt_include_dir,
            'link_with': [libvlccore, libvlc],
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep]
        }

        vlc_tests += {
            'name': 'test_qt_csd_button_model',
            'sources': files(
                'tests/vlc_stub_modules.cpp',
                'tests/test_qt_csd_button_model.cpp',
                'util/csdbuttonmodel.hpp',
                'util/csdbuttonmodel.cpp'
            ),
            'moc_sources': files(
                'tests/test_qt_csd_button_model.cpp'
            ),
            'moc_headers': files(
                'util/csdbuttonmodel.hpp'
            ),
            'suite': ['qt'],
            'include_directories' : qt_include_dir,
            'link_with': [libvlccore, libvlc],
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep]
        }

        vlc_tests += {
            'name': 'test_qt_postprocessing_preferences',
            'sources': files('tests/vlc_stub_modules.cpp', 'tests/test_postprocessing_preferences.cpp'),
            'moc_sources': files('tests/test_postprocessing_preferences.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_with': [libvlccore, libvlc],
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep]
        }

        vlc_tests += {
            'name': 'test_qt_preference_values',
            'sources': files('tests/test_preference_values.cpp'),
            'moc_sources': files('tests/test_preference_values.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_renderer_manager',
            'sources': files(
                'tests/vlc_stub_modules.cpp',
                'tests/test_renderer_manager_model.cpp',
                'util/renderer_manager.hpp',
                'util/renderer_manager.cpp'
            ),
            'moc_sources': files(
                'tests/test_renderer_manager_model.cpp'
            ),
            'moc_headers': files(
                'util/renderer_manager.hpp'
            ),
            'suite': ['qt'],
            'include_directories' : qt_include_dir,
            'link_with': [libvlccore, libvlc],
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep]
        }

        vlc_tests += {
            'name': 'test_qt_ml_joined_runner',
            'sources': files('tests/test_ml_joined_runner.cpp', 'medialibrary/mljoinedthreadrunner.cpp', 'medialibrary/mlthreadpool.cpp'),
            'moc_sources': files('tests/test_ml_joined_runner.cpp'),
            'moc_headers': files('medialibrary/mlthreadpool.hpp'),
            'include_directories': qt_include_dir,
            'suite': ['qt', 'addons'],
            'link_with': [addon_host_library, libvlccore],
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'link_args': qt_link_args,
            'timeout': 90,
        }
        vlc_tests += {
            'name': 'test_qt_artwork_selection',
            'sources': files('tests/test_artwork_selection.cpp', 'util/artwork_selection.cpp'),
            'moc_sources': files('tests/test_artwork_selection.cpp'),
            'include_directories': [qt_include_dir, include_directories('../../..')],
            'suite': ['qt', 'addons'],
            'link_with': [addon_host_library, libvlccore],
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'link_args': qt_link_args,
            'timeout': 90,
        }
        vlc_tests += {
            'name': 'test_qt_artwork_selected_image',
            'sources': files('tests/test_artwork_selected_image.cpp'),
            'moc_sources': files('tests/test_artwork_selected_image.cpp'),
            'include_directories': qt_include_dir,
            'suite': ['qt', 'addons'],
            'link_with': [addon_host_library, libvlccore],
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
            'link_args': qt_link_args,
            'timeout': 90,
        }

        vlc_tests += {
            'name': 'test_qt_thread_runner',
            'sources': files(
                'tests/test_thread_runner.cpp',
                'medialibrary/mlthreadpool.cpp',
                'medialibrary/mlthreadpool.hpp'
            ),
            'moc_sources': files(
                'tests/test_thread_runner.cpp'
            ),
            'moc_headers': files(
                'medialibrary/mlthreadpool.hpp'
            ),
            'suite': ['qt'],
            'include_directories' : qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep]
        }


        vlc_tests += {
            'name': 'test_qt_message_handler',
            'sources': files('tests/test_vlcqtmessagehandler.cpp', 'util/vlcqtmessagehandler.cpp'),
            'moc_sources': files('tests/test_vlcqtmessagehandler.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_with': [libvlccore, libvlc],
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_interface_translator',
            'sources': files('tests/test_interface_translator.cpp', 'util/interface_translator.cpp', 'util/gettext_translator.cpp'),
            'moc_sources': files('tests/test_interface_translator.cpp'),
            'moc_headers': files('util/interface_translator.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_gettext_translator',
            'sources': files('tests/test_gettext_translator.cpp', 'util/gettext_translator.cpp'),
            'moc_sources': files('tests/test_gettext_translator.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_embedded_file_browser',
            'sources': files('tests/test_embedded_file_browser.cpp', 'widgets/native/embedded_file_browser.cpp'),
            'moc_sources': files('tests/test_embedded_file_browser.cpp'),
            'moc_headers': files('widgets/native/embedded_file_browser.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_playlist_tree_model',
            'sources': files('tests/test_playlist_tree_model.cpp', 'playlist/playlist_tree_model.cpp'),
            'moc_sources': files('tests/test_playlist_tree_model.cpp'),
            'moc_headers': files('playlist/playlist_tree_model.hpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_video_window_geometry',
            'sources': files('tests/test_video_window_geometry.cpp'),
            'moc_sources': files('tests/test_video_window_geometry.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_tray_notification_policy',
            'sources': files('tests/test_tray_notification_policy.cpp'),
            'moc_sources': files('tests/test_tray_notification_policy.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_file_log_preferences',
            'sources': files('tests/test_file_log_preferences.cpp'),
            'moc_sources': files('tests/test_file_log_preferences.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_warning_log_buffer',
            'sources': files('tests/test_warning_log_buffer.cpp'),
            'moc_sources': files('tests/test_warning_log_buffer.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_message_log',
            'sources': files('tests/test_message_log.cpp'),
            'moc_sources': files('tests/test_message_log.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_message_log_buffer',
            'sources': files('tests/test_message_log_buffer.cpp'),
            'moc_sources': files('tests/test_message_log_buffer.cpp'),
            'suite': ['qt'],
            'include_directories': qt_include_dir,
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qtest_qt6_dep],
        }

        vlc_tests += {
            'name': 'test_qt_dialog_model',
            'sources': files(
                'tests/vlc_stub_modules.cpp',
                'tests/test_vlc_dialog_model.cpp',
                'dialogs/dialogs/dialogmodel.hpp',
                'dialogs/dialogs/dialogmodel.cpp'
            ),
            'moc_sources': files(
                'tests/test_vlc_dialog_model.cpp'
            ),
            'moc_headers': files(
                'dialogs/dialogs/dialogmodel.hpp'
            ),
            'suite': ['qt'],
            'include_directories' : qt_include_dir,
            'link_with': [libvlccore, libvlc],
            'link_args': qt_link_args,
            'dependencies': [qt6_dep, qt_extra_deps, qtest_qt6_dep],
        }
    endif

    quicktest_qt6_dep = dependency('qt6', version: '=' + qt6_dep.version(), modules: ['QuickTest'], required: false)
    if quicktest_qt6_dep.found() and  qtest_qt6_dep.found()
        qml_test_moc = qt6.compile_moc(
            method: qt_dependency_method,
            headers : files('tests/qml_test.hpp'),
            dependencies : [quicktest_qt6_dep, qt6_dep]
        )
        qml_test = executable(
          'qml_test',
          [files('tests/qml_test.cpp', 'tests/qml_test.hpp'), qml_test_moc],
          qt6pre_qrc,
          build_by_default: false,
          dependencies: [quicktest_qt6_dep, qt6_dep, qtest_qt6_dep],
          # qml_test.hpp keeps joined add-on work alive through shutdown.
          link_with: [qmllibs, addon_host_library],
          cpp_args: ['-DQUICK_TEST_SOURCE_DIR="' + meson.current_source_dir() + '/tests"']
        )
        foreach test_name, test_file : qml_tests
              test(test_name, qml_test, args: ['-input', test_file], suite:'qt')
        endforeach
    endif

endif
