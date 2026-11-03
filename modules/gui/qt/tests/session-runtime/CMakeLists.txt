cmake_minimum_required(VERSION 3.20)
project(hakone_session_runtime_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
find_package(Threads REQUIRED)
get_filename_component(VLC_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(ADDON_SOURCE "${VLC_SOURCE_ROOT}/modules/gui/qt/addons")
add_library(addon_session_transport STATIC
    "${ADDON_SOURCE}/addon_session_websocket.cpp"
    "${ADDON_SOURCE}/addon_websocket_codec.cpp"
    "${ADDON_SOURCE}/addon_public_network_policy.cpp"
    "${ADDON_SOURCE}/addon_stream_policy.cpp"
    "${ADDON_SOURCE}/addon_stream_sockets.cpp"
    "${ADDON_SOURCE}/addon_stream_lifetime.cpp"
    "${ADDON_SOURCE}/addon_stream_dns.cpp")
target_include_directories(addon_session_transport PUBLIC "${VLC_SOURCE_ROOT}/modules/gui/qt")
target_compile_definitions(addon_session_transport PUBLIC
    QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS)
target_link_libraries(addon_session_transport PUBLIC Qt6::Core Qt6::Network Threads::Threads)
if(WIN32)
    target_link_libraries(addon_session_transport PUBLIC ws2_32)
endif()
add_executable(test_addon_session_websocket "${VLC_SOURCE_ROOT}/modules/gui/qt/tests/test_addon_session_websocket.cpp")
target_link_libraries(test_addon_session_websocket PRIVATE addon_session_transport Qt6::Test)
add_library(addon_session_host STATIC
    "${ADDON_SOURCE}/addon_session_host.cpp" "${ADDON_SOURCE}/addon_session_host.hpp"
    "${ADDON_SOURCE}/addon_session_controller.cpp"
    "${ADDON_SOURCE}/addon_host_services.cpp" "${ADDON_SOURCE}/addon_host_services.hpp"
    "${ADDON_SOURCE}/addon_manifest.cpp"
    "${ADDON_SOURCE}/addon_store_authority.cpp"
    "${ADDON_SOURCE}/addon_settings_migrations.cpp"
    "${ADDON_SOURCE}/addon_storage_data.cpp")
target_link_libraries(addon_session_host PUBLIC addon_session_transport)
add_executable(test_addon_session_host "${VLC_SOURCE_ROOT}/modules/gui/qt/tests/test_addon_session_host.cpp")
target_link_libraries(test_addon_session_host PRIVATE addon_session_host Qt6::Test)
set(VLC_ADDON_NATIVE_BUILD_ROOT "" CACHE PATH "The configured native VLC build directory")
if(VLC_ADDON_NATIVE_BUILD_ROOT)
    add_library(addon_native_session_player OBJECT "${ADDON_SOURCE}/addon_native_session_player.cpp")
    target_compile_definitions(addon_native_session_player PRIVATE HAVE_CONFIG_H
        QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS)
    target_include_directories(addon_native_session_player PRIVATE "${VLC_SOURCE_ROOT}/include"
        "${VLC_ADDON_NATIVE_BUILD_ROOT}" "${VLC_ADDON_NATIVE_BUILD_ROOT}/include")
    target_link_libraries(addon_native_session_player PRIVATE Qt6::Core)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test_addon_native_session_player
            "${VLC_SOURCE_ROOT}/modules/gui/qt/tests/test_addon_native_session_player.cpp"
            "${ADDON_SOURCE}/addon_store_authority.cpp"
            native_session_fixture.c $<TARGET_OBJECTS:addon_native_session_player>)
        target_include_directories(test_addon_native_session_player PRIVATE "${VLC_SOURCE_ROOT}/modules/gui/qt"
            "${VLC_SOURCE_ROOT}/include" "${VLC_SOURCE_ROOT}/src" "${VLC_ADDON_NATIVE_BUILD_ROOT}" "${VLC_ADDON_NATIVE_BUILD_ROOT}/include")
        target_compile_definitions(test_addon_native_session_player PRIVATE HAVE_CONFIG_H
            TOP_BUILDDIR="${VLC_ADDON_NATIVE_BUILD_ROOT}" TOP_SRCDIR="${VLC_SOURCE_ROOT}")
        target_link_libraries(test_addon_native_session_player PRIVATE addon_session_transport Qt6::Test
            "${VLC_ADDON_NATIVE_BUILD_ROOT}/lib/libvlc.so" "${VLC_ADDON_NATIVE_BUILD_ROOT}/src/libvlccore.so")
        target_link_options(test_addon_native_session_player PRIVATE "-Wl,--export-dynamic")
        add_executable(test_native_session_fixture_baseline native_fixture_baseline.c native_session_fixture.c)
        target_include_directories(test_native_session_fixture_baseline PRIVATE "${VLC_SOURCE_ROOT}/include"
            "${VLC_SOURCE_ROOT}/src" "${VLC_ADDON_NATIVE_BUILD_ROOT}" "${VLC_ADDON_NATIVE_BUILD_ROOT}/include")
        target_compile_definitions(test_native_session_fixture_baseline PRIVATE HAVE_CONFIG_H
            TOP_BUILDDIR="${VLC_ADDON_NATIVE_BUILD_ROOT}" TOP_SRCDIR="${VLC_SOURCE_ROOT}")
        target_link_libraries(test_native_session_fixture_baseline PRIVATE
            "${VLC_ADDON_NATIVE_BUILD_ROOT}/lib/libvlc.so" "${VLC_ADDON_NATIVE_BUILD_ROOT}/src/libvlccore.so")
        target_link_options(test_native_session_fixture_baseline PRIVATE "-Wl,--export-dynamic")
    endif()
endif()
enable_testing()
add_test(NAME addon_session_websocket COMMAND test_addon_session_websocket)
add_test(NAME addon_session_host COMMAND test_addon_session_host)
set_tests_properties(addon_session_websocket PROPERTIES TIMEOUT 90)
set_tests_properties(addon_session_host PROPERTIES TIMEOUT 90)
if(TARGET test_addon_native_session_player)
    add_test(NAME addon_native_session_player COMMAND test_addon_native_session_player)
    set_tests_properties(addon_native_session_player PROPERTIES TIMEOUT 90)
endif()
