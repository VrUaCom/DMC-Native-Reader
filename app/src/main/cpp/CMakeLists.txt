cmake_minimum_required(VERSION 3.22.1)
project(dmcviewer LANGUAGES CXX)

set(DMC_RENGINE_VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dmc-rengine-cpp)

# Canonical portable read-only dependency slice owned by DMC Rengine.
include(${DMC_RENGINE_VENDOR}/cmake/reader_core.cmake)

# Native Reader product core: platform-neutral C++20 modules only. Android JNI,
# Swift/ObjC++, Win32 and Web/WASM shells must link this target rather than
# enumerating parser/renderer sources themselves.
add_library(dmc_native_reader_core STATIC
    dmc_resource.cpp
    decode_pipeline.cpp
    inspection_format.cpp
    adapters/mod_adapter.cpp
    adapters/scm_adapter.cpp
    modules/model_texture_binding.cpp
    modules/resource_session.cpp
    modules/scene_projection.cpp
    modules/module_registry.cpp
    modules/module_model.cpp
    modules/module_texture.cpp
    modules/ptx_framing_compat.cpp
    modules/texture_companion.cpp
    modules/texture_set.cpp
    spider/black_widow.cpp
    view_renderer.cpp
)
add_library(DMCNativeReader::Core ALIAS dmc_native_reader_core)

target_include_directories(dmc_native_reader_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmc_native_reader_core
    PUBLIC
        DMCRengine::ReaderCore
)

target_compile_features(dmc_native_reader_core PUBLIC cxx_std_20)
target_compile_options(dmc_native_reader_core PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(dmc_native_reader_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Android is deliberately a thin shell. It owns JNI/FD/lifecycle integration
# only; application decisions, resource parsing, texture attachment and render
# contracts stay in DMCNativeReader::Core.
if(ANDROID)
    add_library(dmcviewer SHARED
        app_native.cpp
    )

    target_link_libraries(dmcviewer PRIVATE
        DMCNativeReader::Core
    )

    target_compile_features(dmcviewer PRIVATE cxx_std_20)
    target_compile_options(dmcviewer PRIVATE -Wall -Wextra -Wpedantic)
    # This DSO is an Android JNI boundary, not a public C++ plugin ABI.
    # Keep every declared JNI entry point while hiding implementation symbols.
    target_link_options(dmcviewer PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/jni_exports.map")
    set_property(TARGET dmcviewer APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/jni_exports.map")
endif()

option(DMC_NATIVE_READER_BUILD_TESTS "Build portable native regressions" OFF)
if(DMC_NATIVE_READER_BUILD_TESTS)
    enable_testing()
    foreach(test_name IN ITEMS
        black_widow_state core_model_pipeline dds_ptx_v1
        mod_spatial_adapter module_registry ptx_model_texture render_scene)
        add_executable(${test_name}_test
            ../../test/native/${test_name}_test.cpp)
        target_link_libraries(${test_name}_test PRIVATE DMCNativeReader::Core)
        # These regressions use assert; keep checks active in Release builds.
        if(MSVC)
            target_compile_options(${test_name}_test PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_name}_test PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
    endforeach()
endif()
