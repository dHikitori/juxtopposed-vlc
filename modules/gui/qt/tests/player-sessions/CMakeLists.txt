cmake_minimum_required(VERSION 3.20)
project(hakone_player_session_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(ADDONS "${SOURCE_ROOT}/modules/gui/qt/addons")
set(HAKONE_HOST_ARCHIVE "" CACHE FILEPATH "Current normal add-on host archive.")
set(VLC_BUILD_DIR "" CACHE PATH "Configured VLC build with current shared libraries.")
if(NOT EXISTS "${HAKONE_HOST_ARCHIVE}" OR NOT EXISTS "${VLC_BUILD_DIR}/config.h")
    message(FATAL_ERROR "Set HAKONE_HOST_ARCHIVE and VLC_BUILD_DIR to verified current builds.")
endif()
add_executable(test_addon_player_sessions
    "${SOURCE_ROOT}/modules/gui/qt/tests/test_addon_player_sessions.cpp"
    "${SOURCE_ROOT}/modules/gui/qt/tests/session-runtime/native_session_fixture.c"
    "${ADDONS}/addon_player_sessions.cpp" "${ADDONS}/addon_player_sessions.hpp"
    "${ADDONS}/addon_session_ui.cpp" "${ADDONS}/addon_session_ui.hpp"
    "${ADDONS}/addon_session_ui_manager.cpp"
    "${ADDONS}/addon_native_session_player.cpp")
target_compile_definitions(test_addon_player_sessions PRIVATE HAVE_CONFIG_H
    QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS
    TOP_BUILDDIR="${VLC_BUILD_DIR}" TOP_SRCDIR="${SOURCE_ROOT}")
target_include_directories(test_addon_player_sessions PRIVATE
    "${SOURCE_ROOT}/modules/gui/qt" "${SOURCE_ROOT}/include" "${SOURCE_ROOT}/src"
    "${VLC_BUILD_DIR}" "${VLC_BUILD_DIR}/include")
target_link_libraries(test_addon_player_sessions PRIVATE "${HAKONE_HOST_ARCHIVE}"
    "${VLC_BUILD_DIR}/lib/libvlc.so" "${VLC_BUILD_DIR}/src/libvlccore.so"
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Test Threads::Threads ZLIB::ZLIB)
target_link_options(test_addon_player_sessions PRIVATE "-Wl,--export-dynamic")
option(HAKONE_PLAYER_SESSION_SANITIZERS "Enable focused session assembly sanitizers." OFF)
if(HAKONE_PLAYER_SESSION_SANITIZERS)
    # The fixture uses the current normal Core, which has its own focused tests.
    target_compile_options(test_addon_player_sessions PRIVATE
        -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -O1 -g1)
    target_link_options(test_addon_player_sessions PRIVATE -fsanitize=address,undefined,float-cast-overflow)
endif()
enable_testing()
add_test(NAME addon_player_sessions COMMAND test_addon_player_sessions)
set_tests_properties(addon_player_sessions PROPERTIES TIMEOUT 90)
