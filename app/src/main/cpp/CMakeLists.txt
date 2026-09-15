cmake_minimum_required(VERSION 3.22.1)
project(dmcviewer LANGUAGES CXX)

set(DMC_RENGINE_VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dmc-rengine-cpp)

# Canonical portable read-only dependency slice owned by DMC Rengine.
# Its own language/toolchain contract is external to this repository; Native
# Reader consumes it without modifying the submodule during the C++23 migration.
include(${DMC_RENGINE_VENDOR}/cmake/reader_core.cmake)

# Native Reader product core: platform-neutral C++23 modules only. Android JNI,
# Swift/ObjC++, Win32 and Web/WASM shells must link this target rather than
# enumerating parser/renderer sources themselves.
add_library(dmc_native_reader_core STATIC
    dmc_resource.cpp
    decode_pipeline.cpp
    inspection_format.cpp
    adapters/mod_adapter.cpp
    adapters/scm_adapter.cpp
    modules/composite_builder.cpp
    modules/composite_placement.cpp
    modules/mod_attachment_resolver.cpp
    modules/model_texture_binding.cpp
    modules/resource_session.cpp
    modules/session_gallery.cpp
    modules/session_inspection.cpp
    modules/uv_gallery.cpp
    modules/scene_projection.cpp
    modules/module_registry.cpp
    modules/module_model.cpp
    modules/module_texture.cpp
    modules/module_evt.cpp
    modules/ptx_framing_compat.cpp
    modules/texture_companion.cpp
    modules/texture_set.cpp
    modules/workspace_graph.cpp
    spider/black_widow.cpp
    spider/model_placement_actions.cpp
    spider/session_compose_actions.cpp
    spider/session_texture_actions.cpp
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

# C++ language authority is target-scoped. Do not use repository-global
# CMAKE_CXX_STANDARD or Gradle -std= flags: those would also alter vendored
# dependency targets. Native Reader requires strict ISO C++23 with no silent
# fallback to an older standard.
target_compile_features(dmc_native_reader_core PUBLIC cxx_std_23)
target_compile_definitions(dmc_native_reader_core PUBLIC
    DMC_NATIVE_READER_CPP23=1
    DMC_NATIVE_READER_SPIDER_CPP=1
)
target_compile_options(dmc_native_reader_core PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(dmc_native_reader_core PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# Android is deliberately a thin shell. It owns JNI/FD/lifecycle integration
# and direct Bitmap transport only; application decisions, resource parsing,
# texture attachment and render contracts stay in DMCNativeReader::Core.
# Portable modules and Rengine ReaderCore remain static link-time components;
# the APK must contain exactly one runtime DSO: libdmcviewer.so.
if(ANDROID)
    target_compile_options(dmc_native_reader_core PRIVATE
        -ffunction-sections
        -fdata-sections
    )

    add_library(dmcviewer SHARED
        app_native.cpp
    )

    target_link_libraries(dmcviewer PRIVATE
        DMCNativeReader::Core
        jnigraphics
    )

    target_compile_features(dmcviewer PRIVATE cxx_std_23)
    set_target_properties(dmcviewer PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
    target_compile_options(dmcviewer PRIVATE
        -Wall -Wextra -Wpedantic
        -ffunction-sections
        -fdata-sections
    )
    # This DSO is an Android JNI boundary, not a public C++ plugin ABI.
    # Keep every declared JNI entry point while hiding implementation symbols;
    # discard unreachable static-module sections instead of shipping duplicate
    # shim/core libraries.
    target_link_options(dmcviewer PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/jni_exports.map"
        "-Wl,--gc-sections"
    )
    set_property(TARGET dmcviewer APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/jni_exports.map")
endif()

option(DMC_NATIVE_READER_BUILD_TESTS "Build portable native regressions" OFF)
if(DMC_NATIVE_READER_BUILD_TESTS)
    enable_testing()
    foreach(test_name IN ITEMS
        black_widow_state composite_builder composite_mod_scene composite_placement core_model_pipeline cxx23_profile dds_ptx_v1
        em028_corpus_contract gdata_legacy mod_spatial_adapter module_registry
        png_export_session ptx_model_texture ptx_transaction render_scene scm_authority
        spider_event_execution spider_model_execution uv_gallery session_inspection tm2_legacy workspace_graph)
        add_executable(${test_name}_test
            ../../test/native/${test_name}_test.cpp)
        target_link_libraries(${test_name}_test PRIVATE DMCNativeReader::Core)
        set_target_properties(${test_name}_test PROPERTIES
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
        # These regressions use assert; keep checks active in Release builds.
        if(MSVC)
            target_compile_options(${test_name}_test PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_name}_test PRIVATE -UNDEBUG)
        endif()
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
    endforeach()
endif()
