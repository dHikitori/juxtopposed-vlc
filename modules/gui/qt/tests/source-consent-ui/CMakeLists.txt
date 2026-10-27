cmake_minimum_required(VERSION 3.20)
project(hakone_source_consent_ui_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Test)
get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
add_executable(test_addon_source_consent_ui
    "${SOURCE_ROOT}/modules/gui/qt/addons/addon_source_consent_ui.cpp"
    "${SOURCE_ROOT}/modules/gui/qt/addons/addon_source_consent_ui.hpp"
    "${SOURCE_ROOT}/modules/gui/qt/tests/test_addon_source_consent_ui.cpp")
target_include_directories(test_addon_source_consent_ui PRIVATE "${SOURCE_ROOT}/modules/gui/qt")
target_compile_definitions(test_addon_source_consent_ui PRIVATE
    QT_USE_QSTRINGBUILDER QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY QT_STRICT_ITERATORS)
target_link_libraries(test_addon_source_consent_ui PRIVATE Qt6::Core Qt6::Test)
option(HAKONE_SOURCE_CONSENT_UI_SANITIZERS "Enable focused source consent UI sanitizers." OFF)
if(HAKONE_SOURCE_CONSENT_UI_SANITIZERS)
    target_compile_options(test_addon_source_consent_ui PRIVATE
        -fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -O1 -g1)
    target_link_options(test_addon_source_consent_ui PRIVATE -fsanitize=address,undefined,float-cast-overflow)
endif()
enable_testing()
add_test(NAME addon_source_consent_ui COMMAND test_addon_source_consent_ui)
set_tests_properties(addon_source_consent_ui PROPERTIES TIMEOUT 60)
