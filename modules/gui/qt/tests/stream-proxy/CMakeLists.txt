cmake_minimum_required(VERSION 3.20)
project(vlc_addon_stream_proxy_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Network Test)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
get_filename_component(VLC_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
set(VLC_ADDON_NATIVE_BUILD_ROOT "" CACHE PATH "The configured native VLC build directory")
set(ADDON_SOURCE "${VLC_SOURCE_ROOT}/modules/gui/qt/addons")
set(ADDON_TEST "${VLC_SOURCE_ROOT}/modules/gui/qt/tests")

add_library(addon_stream_proxy STATIC
    "${ADDON_SOURCE}/addon_stream_http.cpp"
    "${ADDON_SOURCE}/addon_public_network_policy.cpp"
    "${ADDON_SOURCE}/addon_stream_policy.cpp"
    "${ADDON_SOURCE}/addon_stream_sockets.cpp"
    "${ADDON_SOURCE}/addon_stream_lifetime.cpp"
    "${ADDON_SOURCE}/addon_stream_shutdown.cpp"
    "${ADDON_SOURCE}/addon_stream_dns.cpp"
    "${ADDON_SOURCE}/addon_stream_manifest.cpp"
    "${ADDON_SOURCE}/addon_stream_proxy.cpp")
target_include_directories(addon_stream_proxy PUBLIC "${VLC_SOURCE_ROOT}/modules/gui/qt")
target_compile_definitions(addon_stream_proxy PUBLIC
    QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS)
target_link_libraries(addon_stream_proxy PUBLIC Qt6::Core Qt6::Network Threads::Threads)
if(WIN32)
    target_link_libraries(addon_stream_proxy PRIVATE ws2_32)
endif()

enable_testing()
foreach(suite http proxy)
    add_executable(test_addon_stream_${suite} "${ADDON_TEST}/test_addon_stream_${suite}.cpp")
    target_link_libraries(test_addon_stream_${suite} PRIVATE addon_stream_proxy Qt6::Test)
    add_test(NAME addon_stream_${suite} COMMAND test_addon_stream_${suite})
    set_tests_properties(addon_stream_${suite} PROPERTIES TIMEOUT 90)
endforeach()

add_executable(test_addon_stream_shutdown
    "${ADDON_TEST}/test_addon_stream_shutdown.cpp"
    "${ADDON_SOURCE}/addon_stream_lifetime.cpp"
    "${ADDON_SOURCE}/addon_stream_shutdown.cpp"
    "${ADDON_SOURCE}/addon_stream_dns.cpp"
    "${ADDON_SOURCE}/addon_source_consent.cpp"
    "${ADDON_SOURCE}/addon_source_admissions.cpp"
    "${ADDON_SOURCE}/addon_store_authority.cpp"
    "${ADDON_SOURCE}/addon_queue_transactions.cpp"
    "${ADDON_SOURCE}/addon_queue_transactions.hpp")
target_include_directories(test_addon_stream_shutdown PRIVATE "${VLC_SOURCE_ROOT}/modules/gui/qt")
target_compile_definitions(test_addon_stream_shutdown PRIVATE
    QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS)
target_link_libraries(test_addon_stream_shutdown PRIVATE Qt6::Core Qt6::Network Qt6::Test Threads::Threads ZLIB::ZLIB)
if(WIN32)
    target_link_libraries(test_addon_stream_shutdown PRIVATE ws2_32)
endif()
add_test(NAME addon_stream_shutdown COMMAND test_addon_stream_shutdown)
set_tests_properties(addon_stream_shutdown PROPERTIES TIMEOUT 90)

if(VLC_ADDON_NATIVE_BUILD_ROOT)
    find_library(VLC_CORE_LIBRARY NAMES vlccore PATHS "${VLC_ADDON_NATIVE_BUILD_ROOT}/src" NO_DEFAULT_PATH REQUIRED)
    find_library(VLC_PUBLIC_LIBRARY NAMES vlc PATHS "${VLC_ADDON_NATIVE_BUILD_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    set(NATIVE_RESOLVER_SOURCES
        "${ADDON_SOURCE}/addon_native_source_resolver.cpp"
        "${ADDON_SOURCE}/addon_source_consent.cpp"
        "${ADDON_SOURCE}/addon_source_admissions.cpp"
        "${ADDON_SOURCE}/addon_store_authority.cpp"
        "${ADDON_SOURCE}/addon_queue_transactions.cpp"
        "${ADDON_SOURCE}/addon_queue_transactions.hpp")
    add_executable(test_addon_stream_native "${ADDON_TEST}/test_addon_stream_proxy.cpp"
        "${ADDON_SOURCE}/addon_stream_proxy_native.cpp" ${NATIVE_RESOLVER_SOURCES})
    target_compile_definitions(test_addon_stream_native PRIVATE VLC_ADDON_NATIVE_STREAM_TESTS)
    target_link_libraries(test_addon_stream_native PRIVATE "${VLC_PUBLIC_LIBRARY}")
    add_executable(test_addon_native_source_resolver "${ADDON_TEST}/test_addon_native_source_resolver.cpp" ${NATIVE_RESOLVER_SOURCES})
    foreach(target test_addon_stream_native test_addon_native_source_resolver)
        target_compile_definitions(${target} PRIVATE HAVE_CONFIG_H)
        target_include_directories(${target} PRIVATE "${VLC_SOURCE_ROOT}" "${VLC_SOURCE_ROOT}/include"
            "${VLC_ADDON_NATIVE_BUILD_ROOT}" "${VLC_ADDON_NATIVE_BUILD_ROOT}/include")
        target_link_libraries(${target} PRIVATE addon_stream_proxy Qt6::Test ZLIB::ZLIB "${VLC_CORE_LIBRARY}")
    endforeach()
    add_test(NAME addon_native_source_resolver COMMAND test_addon_native_source_resolver)
    add_test(NAME addon_stream_native COMMAND test_addon_stream_native
        nativeHelperReplacesCallerAuthorityWithTheCapturedAdmission
        corePlaybackDecodesPinnedTlsWaveAndReplaysTheSameStableItem
        corePlaybackDecodesAnHlsSegmentThroughOpaqueRoutes
        coreTeardownCancelsAReadWithoutNetworkOrGuiDelivery)
    set_tests_properties(addon_native_source_resolver addon_stream_native PROPERTIES TIMEOUT 90)
endif()
