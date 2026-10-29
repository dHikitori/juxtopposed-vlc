# Standalone contract tests. The VLC integration is built by Meson/Automake.
cmake_minimum_required(VERSION 3.20)
project(vlc_addon_contract_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Network Test)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
enable_testing()
get_filename_component(VLC_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
add_library(vlc_addon_host STATIC
    addon_manifest.cpp addon_package_store.cpp addon_store_authority.cpp addon_credentials.cpp
    addon_store_process_fence.cpp addon_store_process_fence.hpp
    addon_store_profile_generation.cpp addon_store_profile_generation.hpp
    addon_native_wake.cpp addon_native_wake.hpp addon_catalogue_store_lock.hpp
    addon_native_notifications.cpp addon_native_notifications.hpp
    addon_catalogue_view.cpp addon_catalogue_view.hpp addon_catalogue_view_manager.cpp addon_display_catalogue_reader.hpp
    addon_distribution_bootstrap.cpp addon_distribution_bootstrap.hpp addon_package_summaries.cpp addon_package_summaries.hpp
    addon_metadata_attempt.cpp addon_metadata_attempt.hpp addon_metadata_completion_policy.cpp addon_metadata_completion_policy.hpp addon_metadata_identity.hpp
    addon_prepared_metadata_invocation.cpp addon_prepared_metadata_invocation.hpp
    addon_prepared_artwork_asset.hpp
    addon_metadata_catalogue_source.hpp
    addon_metadata_registry_epoch.cpp addon_metadata_registry_epoch.hpp
    addon_artwork_attempt_plan.cpp addon_artwork_attempt_plan.hpp addon_package_configuration.hpp
    addon_network.cpp addon_public_network_policy.cpp addon_worker.cpp addon_manager.cpp addon_native_authority.hpp addon_metadata_coordinator.cpp addon_ui_registry.cpp addon_ui_assets.cpp
    addon_discovery_coordinator.cpp addon_theme_service.cpp addon_theme_document.cpp addon_theme_document.hpp addon_sessions.cpp addon_audio_analysis.cpp
    addon_session_websocket.cpp addon_session_websocket.hpp addon_session_host.cpp addon_session_host.hpp
    addon_session_controller.cpp addon_session_controller.hpp addon_session_player.hpp
    addon_session_ui.cpp addon_session_ui.hpp addon_session_ui_manager.cpp
    addon_source_consent_ui.cpp addon_source_consent_ui.hpp
    addon_metadata_family_plan.cpp addon_metadata_family_plan.hpp
    addon_metadata_choices.cpp addon_metadata_choices.hpp
    addon_visual_scene.cpp addon_visualizer_parameters.cpp addon_visualizers.cpp addon_settings_migrations.cpp addon_storage.cpp addon_storage_data.cpp addon_host_services.cpp
    addon_visualizer_catalog.cpp addon_visualizer_resources.cpp addon_visualizer_worker.cpp
    addon_visualizer_worker_runtime.cpp addon_visualizer_render_queue.cpp
    addon_repository_updates.cpp addon_repository_jobs.cpp addon_playable_gate.cpp addon_artwork_assets.cpp addon_artwork_image.cpp addon_native_image_formats.hpp
    addon_queue_transactions.cpp addon_discovery_service.cpp addon_player_discovery.cpp addon_source_admissions.cpp addon_source_consent.cpp addon_stream_manifest.cpp addon_websocket_codec.cpp
    addon_stream_http.cpp addon_stream_policy.cpp addon_stream_sockets.cpp addon_stream_lifetime.cpp addon_stream_shutdown.cpp addon_stream_dns.cpp addon_stream_proxy.cpp addon_app_shutdown.cpp)
target_link_libraries(vlc_addon_host PUBLIC Qt6::Core Qt6::Gui Qt6::Network Threads::Threads ZLIB::ZLIB)
target_compile_definitions(vlc_addon_host PUBLIC QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS)
target_include_directories(vlc_addon_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(WIN32)
    target_link_libraries(vlc_addon_host PRIVATE Advapi32 ws2_32)
elseif(APPLE)
    target_link_libraries(vlc_addon_host PRIVATE "-framework Security")
endif()
add_executable(addon_visualizer_process "../tests/fixtures/addon_visualizer_process.c")
set_target_properties(addon_visualizer_process PROPERTIES AUTOMOC OFF)
foreach(name display_integration distribution_bootstrap catalogue_view display_catalogue package_summaries packages runtime metadata metadata_choices broker ui_registry discovery theme sessions audio_analysis visual_scene visualizer_parameters visualizers visualizer_resources visualizer_worker visualizer_worker_runtime visualizer_render_queue host_services repository_updates playable_gate artwork_assets artwork_image queue_transactions discovery_service source_admissions source_consent storage store_authority store_process_fence store_profile_generation store_profile_settings store_profile_packages catalogue_native native_notifications stream_manifest websocket_codec stream_http stream_proxy stream_shutdown app_shutdown repository_jobs native_authority)
    add_executable(test_addon_${name} "../tests/test_addon_${name}.cpp")
    target_link_libraries(test_addon_${name} PRIVATE vlc_addon_host Qt6::Test)
    target_compile_definitions(test_addon_${name} PRIVATE VLC_ADDON_SOURCE_ROOT="${VLC_SOURCE_ROOT}")
    add_test(NAME addon_${name} COMMAND test_addon_${name})
endforeach()
if((UNIX AND NOT APPLE) OR MINGW)
    target_compile_definitions(test_addon_store_profile_packages PRIVATE HAKONE_PROFILE_SAVEFILE_WRAP)
    target_link_options(test_addon_store_profile_packages PRIVATE "-Wl,--wrap=_ZN9QSaveFile6commitEv")
endif()
target_compile_definitions(test_addon_display_integration PRIVATE HAKONE_DISPLAY_SOURCE_ROOT="${VLC_SOURCE_ROOT}")
target_compile_definitions(test_addon_distribution_bootstrap PRIVATE HAKONE_STARTUP_SOURCE_ROOT="${VLC_SOURCE_ROOT}")
set_tests_properties(addon_display_integration addon_distribution_bootstrap PROPERTIES TIMEOUT 180)
set_tests_properties(addon_catalogue_view addon_display_catalogue addon_package_summaries PROPERTIES TIMEOUT 90)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(test_addon_display_catalogue PRIVATE HAKONE_DISPLAY_READER_LOCK_WRAP)
    target_link_options(test_addon_display_catalogue PRIVATE "-Wl,--wrap=open")
    target_compile_definitions(test_addon_display_integration PRIVATE HAKONE_DISPLAY_CREDENTIAL_CREATE_WRAP HAKONE_DISPLAY_ACQUIRE_WRAP)
    target_link_options(test_addon_display_integration PRIVATE
        "-Wl,--wrap=_ZN3vlc6addons15CredentialStore6createERK7QStringS4_RK10QByteArrayb"
        "-Wl,--wrap=_ZN3vlc6addons18AddonCatalogueView15acquireSettingsERK7QString")
    target_compile_definitions(test_addon_distribution_bootstrap PRIVATE HAKONE_BOOTSTRAP_RUN_WRAP)
    target_link_options(test_addon_distribution_bootstrap PRIVATE
        "-Wl,--wrap=_ZN3vlc6addons21distributionBootstrap3runENS0_12PackageStoreE5QListINS0_26DistributionBootstrapEntryEE")
endif()
add_executable(test_addon_metadata_catalogue "../tests/test_addon_metadata_catalogue.cpp")
target_link_libraries(test_addon_metadata_catalogue PRIVATE vlc_addon_host Qt6::Test)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(test_addon_metadata_catalogue PRIVATE HAKONE_CATALOGUE_OPEN_PROBE)
    target_link_options(test_addon_metadata_catalogue PRIVATE "-Wl,--wrap=open")
endif()
add_test(NAME addon_metadata_catalogue COMMAND test_addon_metadata_catalogue)
set_tests_properties(addon_metadata_catalogue PROPERTIES TIMEOUT 90)

# Keep the real worker case in this suite. A missing worker omits the target.
set(HAKONE_METADATA_ATTEMPT_WORKER "$ENV{VLC_ADDON_WORKER}" CACHE FILEPATH "Native worker for metadata completion tests")
if(NOT HAKONE_METADATA_ATTEMPT_WORKER AND DEFINED VLC_BUILD_DIR)
    set(HAKONE_METADATA_ATTEMPT_WORKER "${VLC_BUILD_DIR}/modules/gui/qt/vlc-addon-worker")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING AND EXISTS "${HAKONE_METADATA_ATTEMPT_WORKER}")
    add_executable(addon_metadata_attempt_process "../tests/fixtures/addon_metadata_attempt_process.c")
    set_target_properties(addon_metadata_attempt_process PROPERTIES AUTOMOC OFF)
    add_executable(test_addon_metadata_attempt "../tests/test_addon_metadata_attempt.cpp")
    target_link_libraries(test_addon_metadata_attempt PRIVATE vlc_addon_host Qt6::Test)
    target_compile_definitions(test_addon_metadata_attempt PRIVATE
        HAKONE_ATTEMPT_PROCESS_FIXTURE="$<TARGET_FILE:addon_metadata_attempt_process>"
        HAKONE_ATTEMPT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/metadata-attempt"
        HAKONE_ATTEMPT_RUST_WORKER="${HAKONE_METADATA_ATTEMPT_WORKER}"
        HAKONE_ATTEMPT_FRENCH_TV_WASM="${VLC_SOURCE_ROOT}/contrib-addons/examples/french-tv/dist/addon.wasm")
    add_dependencies(test_addon_metadata_attempt addon_metadata_attempt_process)
    add_test(NAME addon_metadata_attempt COMMAND test_addon_metadata_attempt)
    set_tests_properties(addon_metadata_attempt PROPERTIES TIMEOUT 180)
    add_executable(addon_prepared_metadata_process "../tests/fixtures/addon_prepared_metadata_process.c")
    set_target_properties(addon_prepared_metadata_process PROPERTIES AUTOMOC OFF)
    add_executable(test_addon_prepared_metadata_invocation "../tests/test_addon_prepared_metadata_invocation.cpp")
    target_link_libraries(test_addon_prepared_metadata_invocation PRIVATE vlc_addon_host Qt6::Test)
    target_compile_definitions(test_addon_prepared_metadata_invocation PRIVATE
        HAKONE_PREPARED_OWNER_WRAP HAKONE_METADATA_PREPARATION_WRAP HAKONE_ARTWORK_READ_WRAP
        HAKONE_PREPARED_PROCESS_FIXTURE="$<TARGET_FILE:addon_prepared_metadata_process>"
        HAKONE_PREPARED_RUST_WORKER="${HAKONE_METADATA_ATTEMPT_WORKER}"
        HAKONE_PREPARED_FRENCH_TV_WASM="${VLC_SOURCE_ROOT}/contrib-addons/examples/french-tv/dist/addon.wasm"
        HAKONE_ATTEMPT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/metadata-attempt")
    target_link_options(test_addon_prepared_metadata_invocation PRIVATE "-Wl,--wrap=_ZN3vlc6addons11AddonWorker23enablePreparedLifecycleEv"
        "-Wl,--wrap=_ZNK3vlc6addons12PackageStore23metadataCatalogueReaderEv"
        "-Wl,--wrap=_ZNK3vlc6addons12AddonManager21nativeAuthorityReaderERK7QString"
        "-Wl,--wrap=_ZN12QImageReader4readEv")
    add_dependencies(test_addon_prepared_metadata_invocation addon_prepared_metadata_process)
    add_test(NAME addon_prepared_metadata_invocation COMMAND test_addon_prepared_metadata_invocation)
    set_tests_properties(addon_prepared_metadata_invocation PROPERTIES TIMEOUT 180)
else()
    message(STATUS "Metadata completion fixture needs a native Linux worker path")
endif()

foreach(name session_websocket session_host session_ui source_consent_ui metadata_family_plan artwork_attempt_plan git_objects)
    add_executable(test_addon_${name} "../tests/test_addon_${name}.cpp")
    target_link_libraries(test_addon_${name} PRIVATE vlc_addon_host Qt6::Test)
    add_test(NAME addon_${name} COMMAND test_addon_${name})
    set_tests_properties(addon_${name} PROPERTIES TIMEOUT 90)
endforeach()
foreach(name visualizers visualizer_worker_runtime)
    target_compile_definitions(test_addon_${name} PRIVATE VLC_ADDON_VISUALIZER_PROCESS_FIXTURE="$<TARGET_FILE:addon_visualizer_process>")
    add_dependencies(test_addon_${name} addon_visualizer_process)
endforeach()
if(UNIX)
    add_executable(test_addon_invocation_guard "../tests/test_addon_invocation_guard.cpp")
    target_link_libraries(test_addon_invocation_guard PRIVATE vlc_addon_host Qt6::Test)
    target_compile_definitions(test_addon_invocation_guard PRIVATE VLC_ADDON_SOURCE_ROOT="${VLC_SOURCE_ROOT}")
    add_test(NAME addon_invocation_guard COMMAND test_addon_invocation_guard)
    set_tests_properties(addon_invocation_guard PROPERTIES TIMEOUT 90)
endif()
add_executable(test_artwork_selected_image "../tests/test_artwork_selected_image.cpp")
target_link_libraries(test_artwork_selected_image PRIVATE vlc_addon_host Qt6::Test)
add_test(NAME artwork_selected_image COMMAND test_artwork_selected_image)
set_tests_properties(artwork_selected_image PROPERTIES TIMEOUT 90)
add_executable(test_ml_joined_runner "../tests/test_ml_joined_runner.cpp"
    "../medialibrary/mljoinedthreadrunner.cpp" "../medialibrary/mljoinedthreadrunner.hpp"
    "../medialibrary/mltaskid.hpp" "../medialibrary/mlthreadpool.cpp" "../medialibrary/mlthreadpool.hpp")
target_link_libraries(test_ml_joined_runner PRIVATE vlc_addon_host Qt6::Test)
add_test(NAME ml_joined_runner COMMAND test_ml_joined_runner)
set_tests_properties(ml_joined_runner PROPERTIES TIMEOUT 90)
find_package(Qt6 6.2 QUIET COMPONENTS Quick Qml)
if(TARGET Qt6::Quick AND TARGET Qt6::Qml)
    add_executable(test_addon_ui_assets "../tests/test_addon_ui_assets.cpp" addon_asset_image_provider.cpp)
    target_link_libraries(test_addon_ui_assets PRIVATE vlc_addon_host Qt6::Quick Qt6::Qml Qt6::Test)
    target_compile_definitions(test_addon_ui_assets PRIVATE VLC_ADDON_SOURCE_ROOT="${VLC_SOURCE_ROOT}")
    add_test(NAME addon_ui_assets COMMAND test_addon_ui_assets)
    set_tests_properties(addon_ui_assets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(test_addon_visualizer_surface "../tests/test_addon_visualizer_surface.cpp" addon_visualizer_surface.cpp)
    target_link_libraries(test_addon_visualizer_surface PRIVATE vlc_addon_host Qt6::Quick Qt6::Qml Qt6::Test)
    add_test(NAME addon_visualizer_surface COMMAND test_addon_visualizer_surface)
    set_tests_properties(addon_visualizer_surface PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
set(VLC_BUILD_DIR "" CACHE PATH "VLC build directory for native metadata tests.")
if(VLC_BUILD_DIR)
    if(NOT EXISTS "${VLC_BUILD_DIR}/config.h")
        message(FATAL_ERROR "VLC_BUILD_DIR has no config.h. Configure VLC before this test build.")
    endif()
    find_library(VLC_CORE_LIBRARY NAMES vlccore
        PATHS "${VLC_BUILD_DIR}/src" "${VLC_BUILD_DIR}/src/.libs" NO_DEFAULT_PATH REQUIRED)
    add_library(vlc_addon_core UNKNOWN IMPORTED)
    set_target_properties(vlc_addon_core PROPERTIES IMPORTED_LOCATION "${VLC_CORE_LIBRARY}")
    add_library(vlc_addon_native_metadata STATIC addon_metadata_service.cpp addon_metadata_service.hpp
        addon_native_queue_store.cpp addon_native_queue_store.hpp
        addon_native_library_store.cpp addon_native_library_store.hpp
        addon_native_library_targets.cpp addon_native_library_targets.hpp
        addon_native_artwork_assets.cpp addon_native_artwork_assets.hpp
        addon_native_source_resolver.cpp addon_native_source_resolver.hpp
        addon_discovery_source_owner.cpp addon_discovery_source_owner.hpp
        addon_source_consent_ui_owner.cpp
        addon_stream_proxy_native.cpp addon_stream_proxy_native.hpp
        addon_native_player_analysis.cpp addon_native_player_analysis.hpp)
    target_sources(vlc_addon_native_metadata PRIVATE addon_native_session_player.cpp addon_native_session_player.hpp)
    target_sources(vlc_addon_native_metadata PRIVATE addon_player_sessions.cpp addon_player_sessions.hpp)
    target_sources(vlc_addon_native_metadata PRIVATE
        addon_native_provider_context.cpp addon_native_provider_context.hpp
        addon_native_provider_artwork_assets.cpp addon_native_provider_artwork_assets.hpp)
    target_sources(vlc_addon_native_metadata PRIVATE
        addon_native_metadata_owner.cpp addon_native_metadata_owner.hpp)
    target_sources(vlc_addon_native_metadata PRIVATE
        addon_core_metadata_admission.cpp addon_core_metadata_admission.hpp
        addon_artwork_attempt_keeper.cpp addon_artwork_attempt_keeper.hpp
        addon_artwork_attempt_aggregate.cpp addon_artwork_attempt_aggregate.hpp
        addon_artwork_lookup_runner.cpp addon_artwork_lookup_runner.hpp)
    target_link_libraries(vlc_addon_native_metadata PUBLIC vlc_addon_host vlc_addon_core)
    target_include_directories(vlc_addon_native_metadata PUBLIC
        "${VLC_SOURCE_ROOT}/include" "${VLC_BUILD_DIR}/include" "${VLC_BUILD_DIR}")
    target_compile_definitions(vlc_addon_native_metadata PUBLIC HAVE_CONFIG_H)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test_addon_metadata_registry_epoch "../tests/test_addon_metadata_registry_epoch.cpp"
            "../tests/fixtures/addon_artwork_attempt_keeper.c")
        target_include_directories(test_addon_metadata_registry_epoch PRIVATE "${VLC_SOURCE_ROOT}" "${VLC_SOURCE_ROOT}/src")
        target_link_libraries(test_addon_metadata_registry_epoch PRIVATE vlc_addon_native_metadata Qt6::Test)
        add_test(NAME addon_metadata_registry_epoch COMMAND test_addon_metadata_registry_epoch)
        set_tests_properties(addon_metadata_registry_epoch PROPERTIES TIMEOUT 90)
        add_executable(test_addon_artwork_attempt_keeper "../tests/test_addon_artwork_attempt_keeper.cpp"
            "../tests/fixtures/addon_artwork_attempt_keeper.c")
        target_include_directories(test_addon_artwork_attempt_keeper PRIVATE "${VLC_SOURCE_ROOT}" "${VLC_SOURCE_ROOT}/src")
        target_link_libraries(test_addon_artwork_attempt_keeper PRIVATE vlc_addon_native_metadata Qt6::Test)
        add_test(NAME addon_artwork_attempt_keeper COMMAND test_addon_artwork_attempt_keeper)
        set_tests_properties(addon_artwork_attempt_keeper PROPERTIES TIMEOUT 90)
    endif()
    add_executable(addon-provider-probe "../tests/addon_provider_probe.cpp")
    target_sources(addon-provider-probe PRIVATE "../util/subtitle_source_service.cpp" "../util/subtitle_source_service.hpp")
    target_link_libraries(addon-provider-probe PRIVATE vlc_addon_native_metadata)
    add_executable(test_artwork_selection "../tests/test_artwork_selection.cpp" "../util/artwork_selection.cpp" "../util/artwork_selection.hpp")
    target_include_directories(test_artwork_selection PRIVATE "${VLC_SOURCE_ROOT}")
    target_link_libraries(test_artwork_selection PRIVATE vlc_addon_native_metadata Qt6::Test)
    add_test(NAME artwork_selection COMMAND test_artwork_selection)
    set_tests_properties(artwork_selection PROPERTIES TIMEOUT 90)
    add_executable(test_addon_metadata_service "../tests/test_addon_metadata_service.cpp")
    target_link_libraries(test_addon_metadata_service PRIVATE vlc_addon_native_metadata Qt6::Test)
    add_test(NAME addon_metadata_service COMMAND test_addon_metadata_service)

    add_executable(test_addon_native_artwork_assets "../tests/test_addon_native_artwork_assets.cpp")
    target_link_libraries(test_addon_native_artwork_assets PRIVATE vlc_addon_native_metadata Qt6::Test)
    target_include_directories(test_addon_native_artwork_assets PRIVATE "${VLC_SOURCE_ROOT}")
    target_compile_definitions(test_addon_native_artwork_assets PRIVATE VLC_ADDON_NATIVE_BUILD_ROOT="${VLC_BUILD_DIR}")
    add_test(NAME addon_native_artwork_assets COMMAND test_addon_native_artwork_assets)

    find_library(VLC_PUBLIC_LIBRARY NAMES vlc
        PATHS "${VLC_BUILD_DIR}/lib" "${VLC_BUILD_DIR}/lib/.libs" NO_DEFAULT_PATH REQUIRED)
    target_link_libraries(test_addon_native_artwork_assets PRIVATE "${VLC_PUBLIC_LIBRARY}")
    add_executable(test_addon_stream_native "../tests/test_addon_stream_proxy.cpp")
    target_compile_definitions(test_addon_stream_native PRIVATE VLC_ADDON_NATIVE_STREAM_TESTS HAVE_CONFIG_H VLC_ADDON_NATIVE_BUILD_ROOT="${VLC_BUILD_DIR}")
    target_include_directories(test_addon_stream_native PRIVATE "${VLC_SOURCE_ROOT}")
    target_link_libraries(test_addon_stream_native PRIVATE vlc_addon_native_metadata Qt6::Test "${VLC_PUBLIC_LIBRARY}")
    add_test(NAME addon_stream_native COMMAND test_addon_stream_native
        nativeHelperReplacesCallerAuthorityWithTheCapturedAdmission
        corePlaybackDecodesPinnedTlsWaveAndReplaysTheSameStableItem
        corePlaybackDecodesAnHlsSegmentThroughOpaqueRoutes
        coreTeardownCancelsAReadWithoutNetworkOrGuiDelivery)
    set_tests_properties(addon_stream_native PROPERTIES TIMEOUT 90)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test_addon_native_provider_context "../tests/test_addon_native_provider_context.cpp"
            "../tests/fixtures/addon_native_provider_fixture.c"
            "${VLC_SOURCE_ROOT}/src/preparser/metadata_native_provider.c")
        target_include_directories(test_addon_native_provider_context PRIVATE "${VLC_SOURCE_ROOT}/src" "${VLC_SOURCE_ROOT}")
        target_link_libraries(test_addon_native_provider_context PRIVATE vlc_addon_native_metadata Qt6::Test "${VLC_PUBLIC_LIBRARY}")
        target_compile_definitions(test_addon_native_provider_context PRIVATE HAKONE_NATIVE_OUTCOME_WRAP)
        target_link_options(test_addon_native_provider_context PRIVATE "-Wl,--wrap=vlc_metadata_native_claim_AdmitOutcome")
        add_test(NAME addon_native_provider_context COMMAND test_addon_native_provider_context)
        set_tests_properties(addon_native_provider_context PROPERTIES TIMEOUT 90)
        if(TARGET addon_metadata_attempt_process)
            add_executable(test_addon_artwork_attempt_aggregate
                "../tests/test_addon_artwork_attempt_aggregate.cpp"
                "../tests/fixtures/addon_artwork_attempt_keeper.c"
                "../tests/fixtures/addon_native_provider_fixture.c"
                "${VLC_SOURCE_ROOT}/src/preparser/metadata_native_provider.c")
            target_include_directories(test_addon_artwork_attempt_aggregate PRIVATE "${VLC_SOURCE_ROOT}/src" "${VLC_SOURCE_ROOT}")
            target_link_libraries(test_addon_artwork_attempt_aggregate PRIVATE vlc_addon_native_metadata Qt6::Test "${VLC_PUBLIC_LIBRARY}")
            target_compile_definitions(test_addon_artwork_attempt_aggregate PRIVATE
                HAKONE_AGGREGATE_PROCESS_FIXTURE="$<TARGET_FILE:addon_metadata_attempt_process>"
                HAKONE_ATTEMPT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/metadata-attempt"
                HAKONE_AGGREGATE_RUST_WORKER="${HAKONE_METADATA_ATTEMPT_WORKER}"
                HAKONE_AGGREGATE_FRENCH_TV_WASM="${VLC_SOURCE_ROOT}/contrib-addons/examples/french-tv/dist/addon.wasm")
            add_dependencies(test_addon_artwork_attempt_aggregate addon_metadata_attempt_process)
            add_test(NAME addon_artwork_attempt_aggregate COMMAND test_addon_artwork_attempt_aggregate)
            set_tests_properties(addon_artwork_attempt_aggregate PROPERTIES TIMEOUT 180)
        endif()
        add_executable(test_addon_native_metadata_owner "../tests/test_addon_native_metadata_owner.cpp"
            "../tests/fixtures/addon_native_provider_fixture.c"
            "${VLC_SOURCE_ROOT}/src/preparser/metadata_native_provider.c")
        target_include_directories(test_addon_native_metadata_owner PRIVATE "${VLC_SOURCE_ROOT}/src" "${VLC_SOURCE_ROOT}")
        target_link_libraries(test_addon_native_metadata_owner PRIVATE vlc_addon_native_metadata Qt6::Test "${VLC_PUBLIC_LIBRARY}")
        target_link_options(test_addon_native_metadata_owner PRIVATE
            "-Wl,--wrap=_Znwm" "-Wl,--wrap=_Znwj" "-Wl,--wrap=_ZdlPv" "-Wl,--wrap=_ZdlPvm" "-Wl,--wrap=_ZdlPvj")
        add_test(NAME addon_native_metadata_owner COMMAND test_addon_native_metadata_owner)
        set_tests_properties(addon_native_metadata_owner PROPERTIES TIMEOUT 90)
        add_executable(test_addon_native_session_player "../tests/test_addon_native_session_player.cpp"
            "../tests/session-runtime/native_session_fixture.c")
        target_include_directories(test_addon_native_session_player PRIVATE "${VLC_SOURCE_ROOT}/src")
        target_compile_definitions(test_addon_native_session_player PRIVATE
            TOP_BUILDDIR="${VLC_BUILD_DIR}" TOP_SRCDIR="${VLC_SOURCE_ROOT}")
        target_link_libraries(test_addon_native_session_player PRIVATE vlc_addon_native_metadata Qt6::Test "${VLC_PUBLIC_LIBRARY}")
        target_link_options(test_addon_native_session_player PRIVATE "-Wl,--export-dynamic")
        add_test(NAME addon_native_session_player COMMAND test_addon_native_session_player)
        set_tests_properties(addon_native_session_player PROPERTIES TIMEOUT 90)
        add_executable(test_addon_player_sessions "../tests/test_addon_player_sessions.cpp"
            "../tests/session-runtime/native_session_fixture.c")
        target_include_directories(test_addon_player_sessions PRIVATE "${VLC_SOURCE_ROOT}/src")
        target_compile_definitions(test_addon_player_sessions PRIVATE
            TOP_BUILDDIR="${VLC_BUILD_DIR}" TOP_SRCDIR="${VLC_SOURCE_ROOT}")
        target_link_libraries(test_addon_player_sessions PRIVATE vlc_addon_native_metadata Qt6::Test "${VLC_PUBLIC_LIBRARY}")
        target_link_options(test_addon_player_sessions PRIVATE "-Wl,--export-dynamic")
        add_test(NAME addon_player_sessions COMMAND test_addon_player_sessions)
        set_tests_properties(addon_player_sessions PROPERTIES TIMEOUT 90)
    endif()

    target_compile_definitions(test_addon_visualizer_worker_runtime PRIVATE VLC_ADDON_NATIVE_BUILD_ROOT="${VLC_BUILD_DIR}")
    foreach(name native_source_resolver native_player_analysis visualizer_clock)
        add_executable(test_addon_${name} "../tests/test_addon_${name}.cpp")
        target_link_libraries(test_addon_${name} PRIVATE vlc_addon_native_metadata Qt6::Test "${VLC_PUBLIC_LIBRARY}")
        target_include_directories(test_addon_${name} PRIVATE "${VLC_SOURCE_ROOT}")
        target_compile_definitions(test_addon_${name} PRIVATE VLC_ADDON_NATIVE_BUILD_ROOT="${VLC_BUILD_DIR}")
        add_test(NAME addon_${name} COMMAND test_addon_${name})
    endforeach()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(test_addon_native_player_analysis PRIVATE HAKONE_ANALYSIS_OBSERVER_WRAP)
        foreach(symbol vlc_player_AddTimer vlc_player_RemoveTimer vlc_player_aout_AddListener vlc_player_aout_RemoveListener vlc_player_AddListener vlc_player_RemoveListener)
            target_link_options(test_addon_native_player_analysis PRIVATE "-Wl,--wrap=${symbol}")
        endforeach()
    endif()
    add_executable(test_addon_native_queue_store "../tests/test_addon_native_queue_store.cpp")
    target_link_libraries(test_addon_native_queue_store PRIVATE vlc_addon_native_metadata Qt6::Test "${VLC_PUBLIC_LIBRARY}")
    target_include_directories(test_addon_native_queue_store PRIVATE "${VLC_SOURCE_ROOT}")
    target_compile_definitions(test_addon_native_queue_store PRIVATE VLC_ADDON_NATIVE_BUILD_ROOT="${VLC_BUILD_DIR}")
    add_test(NAME addon_native_queue_store COMMAND test_addon_native_queue_store)

    add_executable(addon_discovery_source_process "../tests/fixtures/addon_discovery_source_process.c")
    set_target_properties(addon_discovery_source_process PROPERTIES AUTOMOC OFF)
    foreach(name discovery_source_owner source_consent_ui_owner)
        add_executable(test_addon_${name} "../tests/test_addon_${name}.cpp")
        target_link_libraries(test_addon_${name} PRIVATE vlc_addon_native_metadata Qt6::Test "${VLC_PUBLIC_LIBRARY}")
        target_include_directories(test_addon_${name} PRIVATE "${VLC_SOURCE_ROOT}")
        target_compile_definitions(test_addon_${name} PRIVATE
            VLC_ADDON_SOURCE_ROOT="${VLC_SOURCE_ROOT}" VLC_ADDON_NATIVE_BUILD_ROOT="${VLC_BUILD_DIR}")
        add_test(NAME addon_${name} COMMAND test_addon_${name})
        set_tests_properties(addon_${name} PROPERTIES TIMEOUT 90)
    endforeach()

    target_compile_definitions(test_addon_discovery_source_owner PRIVATE VLC_ADDON_DISCOVERY_PROCESS_FIXTURE="$<TARGET_FILE:addon_discovery_source_process>")
    add_dependencies(test_addon_discovery_source_owner addon_discovery_source_process)

    find_package(Python3 QUIET COMPONENTS Interpreter)
    set(radio_worker "${VLC_BUILD_DIR}/modules/gui/qt/vlc-addon-worker")
    if(WIN32)
        string(APPEND radio_worker ".exe")
    endif()
    if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING AND EXISTS "${radio_worker}")
        add_test(NAME addon_radio_browser_worker COMMAND "${Python3_EXECUTABLE}" -B
            "${VLC_SOURCE_ROOT}/contrib-addons/examples/radio-browser/tests/test_worker.py" --worker "${radio_worker}")
        set_tests_properties(addon_radio_browser_worker PROPERTIES TIMEOUT 90)
    endif()

    find_package(SQLite3 QUIET)
    if(TARGET SQLite::SQLite3)
        add_executable(test_addon_native_library_store "../tests/test_addon_native_library_store.cpp")
        target_link_libraries(test_addon_native_library_store PRIVATE vlc_addon_native_metadata Qt6::Test "${VLC_PUBLIC_LIBRARY}" SQLite::SQLite3)
        target_include_directories(test_addon_native_library_store PRIVATE "${VLC_SOURCE_ROOT}")
        target_compile_definitions(test_addon_native_library_store PRIVATE VLC_ADDON_NATIVE_BUILD_ROOT="${VLC_BUILD_DIR}")
        add_test(NAME addon_native_library_store COMMAND test_addon_native_library_store)
    endif()

    add_executable(test_metadata_backend "${VLC_SOURCE_ROOT}/test/src/misc/metadata_backend.c")
    set_target_properties(test_metadata_backend PROPERTIES AUTOMOC OFF)
    target_include_directories(test_metadata_backend PRIVATE
        "${VLC_SOURCE_ROOT}/include" "${VLC_BUILD_DIR}/include" "${VLC_BUILD_DIR}")
    target_compile_definitions(test_metadata_backend PRIVATE HAVE_CONFIG_H)
    target_link_libraries(test_metadata_backend PRIVATE vlc_addon_core Threads::Threads)
    add_test(NAME metadata_backend COMMAND test_metadata_backend)
else()
    message(STATUS "Set VLC_BUILD_DIR to add native metadata tests and the provider probe.")
endif()

set_tests_properties(addon_stream_http addon_stream_proxy addon_stream_shutdown addon_app_shutdown addon_repository_jobs addon_native_authority PROPERTIES TIMEOUT 90)
