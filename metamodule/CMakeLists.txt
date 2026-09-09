cmake_minimum_required(VERSION 3.22)

# MetaModule build of the NoSuchDevice VCV Rack plugin (TrigGate, DivTrig, Corrupter).
#
# Locate the 4ms MetaModule plugin SDK: -DMETAMODULE_SDK_DIR=..., then the
# METAMODULE_SDK_DIR environment variable, then a sibling checkout.
if(DEFINED ENV{METAMODULE_SDK_DIR} AND NOT DEFINED METAMODULE_SDK_DIR)
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
endif()
if(NOT DEFINED METAMODULE_SDK_DIR)
    set(METAMODULE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../metamodule-plugin-sdk")
endif()
if(NOT EXISTS "${METAMODULE_SDK_DIR}/plugin.cmake")
    message(FATAL_ERROR "MetaModule SDK not found at ${METAMODULE_SDK_DIR}. "
        "Clone https://github.com/4ms/metamodule-plugin-sdk (with submodules) and set METAMODULE_SDK_DIR.")
endif()
include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(NoSuchDeviceMetaModulePlugin VERSION 2.2.0 DESCRIPTION "No Such Device modules for MetaModule" LANGUAGES C CXX ASM)

set(PLUGIN_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(DSP_ROOT ${PLUGIN_ROOT}/vendor/corrupter-dsp)

add_library(NoSuchDevice STATIC)

target_sources(NoSuchDevice PRIVATE
    ${PLUGIN_ROOT}/src/plugin.cpp
    ${PLUGIN_ROOT}/src/TrigGate.cpp
    ${PLUGIN_ROOT}/src/DivTrig.cpp
    ${PLUGIN_ROOT}/src/Corrupter.cpp
    ${DSP_ROOT}/src/engine.cpp
    ${DSP_ROOT}/src/c_api.cpp
    ${DSP_ROOT}/src/pitch_quantizer.cpp
    ${DSP_ROOT}/src/internal/clock_engine.cpp
    ${DSP_ROOT}/src/internal/corrupt_engine.cpp
)

target_include_directories(NoSuchDevice PRIVATE
    ${PLUGIN_ROOT}/src
    ${DSP_ROOT}/include
    ${DSP_ROOT}/src
)

target_compile_options(NoSuchDevice PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_property(TARGET NoSuchDevice PROPERTY CXX_STANDARD 20)

if(NOT DEFINED INSTALL_DIR)
    set(INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/dist)
endif()

# plugin-mm.json must live next to this CMakeLists.txt (the create_plugin() caller's dir).
create_plugin(
    SOURCE_LIB      NoSuchDevice
    PLUGIN_NAME     NoSuchDevice
    PLUGIN_JSON     ${PLUGIN_ROOT}/plugin.json
    SOURCE_ASSETS   ${CMAKE_CURRENT_LIST_DIR}/assets
    DESTINATION     ${INSTALL_DIR}
)

# Also emit the release-style name: NoSuchDevice-v<version>.mmplugin
file(READ ${PLUGIN_ROOT}/plugin.json _plugin_json)
string(JSON _plugin_version GET "${_plugin_json}" version)
add_custom_command(TARGET plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${INSTALL_DIR}/NoSuchDevice.mmplugin
        ${INSTALL_DIR}/NoSuchDevice-v${_plugin_version}.mmplugin
    COMMENT "Copying NoSuchDevice.mmplugin -> NoSuchDevice-v${_plugin_version}.mmplugin"
)
