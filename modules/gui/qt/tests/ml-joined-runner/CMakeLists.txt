cmake_minimum_required(VERSION 3.20)
project(hakone_ml_joined_runner_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Test)
find_package(Threads REQUIRED)
get_filename_component(VLC_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
add_executable(test_ml_joined_runner
    "${VLC_SOURCE_ROOT}/modules/gui/qt/tests/test_ml_joined_runner.cpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/medialibrary/mljoinedthreadrunner.cpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/medialibrary/mlthreadpool.cpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/medialibrary/mlthreadpool.hpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/addons/addon_stream_lifetime.cpp")
add_executable(test_thread_runner
    "${VLC_SOURCE_ROOT}/modules/gui/qt/tests/test_thread_runner.cpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/medialibrary/mlthreadpool.cpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/medialibrary/mlthreadpool.hpp")
add_executable(test_addon_repository_jobs
    "${VLC_SOURCE_ROOT}/modules/gui/qt/tests/test_addon_repository_jobs.cpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/addons/addon_repository_jobs.cpp"
    "${VLC_SOURCE_ROOT}/modules/gui/qt/addons/addon_stream_lifetime.cpp")
foreach(target test_ml_joined_runner test_thread_runner test_addon_repository_jobs)
    target_include_directories(${target} PRIVATE "${VLC_SOURCE_ROOT}/modules/gui/qt")
    target_compile_definitions(${target} PRIVATE
        QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS)
    target_link_libraries(${target} PRIVATE Qt6::Core Qt6::Test Threads::Threads)
endforeach()
enable_testing()
add_test(NAME ml_joined_runner COMMAND test_ml_joined_runner)
add_test(NAME thread_runner COMMAND test_thread_runner)
add_test(NAME addon_repository_jobs COMMAND test_addon_repository_jobs)
set_tests_properties(ml_joined_runner thread_runner addon_repository_jobs PROPERTIES TIMEOUT 90)
