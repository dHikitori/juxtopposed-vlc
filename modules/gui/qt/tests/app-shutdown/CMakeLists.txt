cmake_minimum_required(VERSION 3.20)
project(hakone_app_shutdown_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Test)
find_package(Threads REQUIRED)
get_filename_component(VLC_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
add_executable(test_addon_app_shutdown
    "${VLC_SOURCE_ROOT}/modules/gui/qt/tests/test_addon_app_shutdown.cpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/addons/addon_app_shutdown.cpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/addons/addon_stream_shutdown.cpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/addons/addon_stream_lifetime.cpp")
target_include_directories(test_addon_app_shutdown PRIVATE "${VLC_SOURCE_ROOT}/modules/gui/qt")
target_compile_definitions(test_addon_app_shutdown PRIVATE
    QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS)
target_link_libraries(test_addon_app_shutdown PRIVATE Qt6::Core Qt6::Test Threads::Threads)
enable_testing()
add_test(NAME addon_app_shutdown COMMAND test_addon_app_shutdown)
set_tests_properties(addon_app_shutdown PROPERTIES TIMEOUT 60)
