cmake_minimum_required(VERSION 3.20)
project(hakone_session_ui_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
find_package(Threads REQUIRED)
get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(ADDONS "${SOURCE_ROOT}/modules/gui/qt/addons")
add_executable(test_addon_session_ui
    "${SOURCE_ROOT}/modules/gui/qt/tests/test_addon_session_ui.cpp"
    "${ADDONS}/addon_session_ui.cpp" "${ADDONS}/addon_session_ui.hpp"
    "${ADDONS}/addon_session_host.cpp" "${ADDONS}/addon_session_host.hpp"
    "${ADDONS}/addon_host_services.cpp" "${ADDONS}/addon_host_services.hpp"
    "${ADDONS}/addon_manifest.cpp" "${ADDONS}/addon_settings_migrations.cpp" "${ADDONS}/addon_storage_data.cpp"
    "${ADDONS}/addon_store_authority.cpp" "${ADDONS}/addon_session_websocket.cpp"
    "${ADDONS}/addon_websocket_codec.cpp" "${ADDONS}/addon_public_network_policy.cpp"
    "${ADDONS}/addon_stream_policy.cpp" "${ADDONS}/addon_stream_sockets.cpp"
    "${ADDONS}/addon_stream_lifetime.cpp" "${ADDONS}/addon_stream_dns.cpp")
target_include_directories(test_addon_session_ui PRIVATE "${SOURCE_ROOT}/modules/gui/qt")
target_compile_definitions(test_addon_session_ui PRIVATE QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS)
target_link_libraries(test_addon_session_ui PRIVATE Qt6::Core Qt6::Network Qt6::Test Threads::Threads)
add_library(session_ui_manager_syntax OBJECT "${ADDONS}/addon_session_ui_manager.cpp")
target_include_directories(session_ui_manager_syntax PRIVATE "${SOURCE_ROOT}/modules/gui/qt")
target_compile_definitions(session_ui_manager_syntax PRIVATE QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS)
target_link_libraries(session_ui_manager_syntax PRIVATE Qt6::Core Qt6::Network)
enable_testing()
add_test(NAME addon_session_ui COMMAND test_addon_session_ui)
