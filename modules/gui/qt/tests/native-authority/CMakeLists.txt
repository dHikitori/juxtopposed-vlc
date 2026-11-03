cmake_minimum_required(VERSION 3.20)
project(hakone_native_authority_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Test)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
get_filename_component(VLC_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(HAKONE_HOST_ARCHIVE "" CACHE FILEPATH "Normal add-on host archive.")
if(NOT EXISTS "${HAKONE_HOST_ARCHIVE}")
    message(FATAL_ERROR "Set HAKONE_HOST_ARCHIVE to a built normal add-on host archive.")
endif()
set(manager_source "${VLC_SOURCE_ROOT}/modules/gui/qt/addons/addon_manager.cpp")
set(credentials_source "${VLC_SOURCE_ROOT}/modules/gui/qt/addons/addon_credentials.cpp")
# The normal archive supplies the current host metaobjects.
set_source_files_properties("${manager_source}" "${credentials_source}" PROPERTIES SKIP_AUTOMOC ON)
add_executable(test_addon_native_authority
    "${VLC_SOURCE_ROOT}/modules/gui/qt/tests/test_addon_native_authority.cpp"
    "${manager_source}" "${credentials_source}")
target_include_directories(test_addon_native_authority PRIVATE "${VLC_SOURCE_ROOT}/modules/gui/qt")
target_compile_definitions(test_addon_native_authority PRIVATE
    QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS
    VLC_ADDON_SOURCE_ROOT="${VLC_SOURCE_ROOT}")
target_link_libraries(test_addon_native_authority PRIVATE "${HAKONE_HOST_ARCHIVE}"
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Test Threads::Threads ZLIB::ZLIB)
if(WIN32)
    target_link_libraries(test_addon_native_authority PRIVATE Advapi32 ws2_32)
elseif(APPLE)
    target_link_libraries(test_addon_native_authority PRIVATE "-framework Security")
endif()
option(HAKONE_AUTHORITY_SANITIZERS "Enable focused authority sanitizers." OFF)
if(HAKONE_AUTHORITY_SANITIZERS)
    target_compile_options(test_addon_native_authority PRIVATE
        -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -O1 -g1)
    target_link_options(test_addon_native_authority PRIVATE -fsanitize=address,undefined,float-cast-overflow)
endif()
enable_testing()
add_test(NAME addon_native_authority COMMAND test_addon_native_authority)
set_tests_properties(addon_native_authority PROPERTIES TIMEOUT 90)
