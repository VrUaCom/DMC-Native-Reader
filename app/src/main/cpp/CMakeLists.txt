cmake_minimum_required(VERSION 3.22.1)
project(dmcviewer LANGUAGES CXX)

set(DMC_RENGINE_VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dmc-rengine-cpp)

# Canonical portable read-only dependency slice owned by DMC Rengine.
# Its own language/toolchain contract is external to this repository; Native
# Reader consumes it without modifying the submodule during the C++23 migration.
include(${DMC_RENGINE_VENDOR}/cmake/reader_core.cmake)

# Read-only MOT/PAC slice of the same pinned Rengine checkout. ReaderCore does
# not yet list these canonical sources, so Native Reader compiles them into a
# separate static target instead of editing the submodule. Every file here is a
# parser/evaluator; nothing in this slice writes or repacks resources.
set(DMC_NATIVE_READER_RENGINE_VIEWER_SOURCES
    ${DMC_RENGINE_VENDOR}/src/formats/mot/parser.cpp
    ${DMC_RENGINE_VENDOR}/src/analysis/mod/motion_group.cpp
    ${DMC_RENGINE_VENDOR}/src/analysis/mod/animation_binding.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/container.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/relative_slot_container.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/pac.cpp
)
add_library(dmc_native_reader_rengine_viewer STATIC
    ${DMC_NATIVE_READER_RENGINE_VIEWER_SOURCES})
target_link_libraries(dmc_native_reader_rengine_viewer PUBLIC DMCRengine::ReaderCore)
set_target_properties(dmc_native_reader_rengine_viewer PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
if(ANDROID)
    target_compile_options(dmc_native_reader_rengine_viewer PRIVATE
        -ffunction-sections -fdata-sections)
endif()

# Native Reader language authority is centralized here and remains target-scoped.
# Do not use repository-global CMAKE_CXX_STANDARD or Gradle -std= flags because
# those would also alter vendored dependency targets. Public is used only where
# a target's headers expose C++23 types to consumers.
function(dmc_native_reader_require_cpp23 target visibility)
    target_compile_features(${target} ${visibility} cxx_std_23)
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )
endfunction()

# Native Reader product core: platform-neutral C++23 modules only. Android JNI,
# Swift/ObjC++, Win32 and Web/WASM shells must link this target rather than
# enumerating parser/renderer sources themselves.
set(DMC_NATIVE_READER_CORE_SOURCES
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
    modules/ptx_runtime_compat.cpp
    modules/texture_companion.cpp
    modules/texture_set.cpp
    modules/workspace_graph.cpp
    modules/motion/skeleton_rig.cpp
    modules/motion/animated_local.cpp
    modules/motion/motion_clip.cpp
    modules/motion/motion_player.cpp
    modules/module_archive.cpp
    modules/pac_assembly.cpp
    spider/crusader.cpp
    spider/black_widow.cpp
    spider/model_placement_actions.cpp
    spider/session_compose_actions.cpp
    spider/session_texture_actions.cpp
    view_renderer.cpp
)

# Duplicate source entries can silently compile/link the same responsibility
# twice as the project grows. Treat that as an architecture/build error rather
# than relying on the linker to expose it later.
set(_dmc_native_reader_core_sources_unique ${DMC_NATIVE_READER_CORE_SOURCES})
list(REMOVE_DUPLICATES _dmc_native_reader_core_sources_unique)
list(LENGTH DMC_NATIVE_READER_CORE_SOURCES _dmc_native_reader_core_source_count)
list(LENGTH _dmc_native_reader_core_sources_unique _dmc_native_reader_core_unique_count)
if(NOT _dmc_native_reader_core_source_count EQUAL _dmc_native_reader_core_unique_count)
    message(FATAL_ERROR "Duplicate source entry in DMC_NATIVE_READER_CORE_SOURCES")
endif()
unset(_dmc_native_reader_core_sources_unique)
unset(_dmc_native_reader_core_source_count)
unset(_dmc_native_reader_core_unique_count)

add_library(dmc_native_reader_core STATIC
    ${DMC_NATIVE_READER_CORE_SOURCES}
)
add_library(DMCNativeReader::Core ALIAS dmc_native_reader_core)

dmc_native_reader_require_cpp23(dmc_native_reader_core PUBLIC)

target_include_directories(dmc_native_reader_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmc_native_reader_core
    PUBLIC
        DMCRengine::ReaderCore
        dmc_native_reader_rengine_viewer
)

target_compile_definitions(dmc_native_reader_core PUBLIC
    DMC_NATIVE_READER_CPP23=1
    DMC_NATIVE_READER_SPIDER_CPP=1
)
if(MSVC)
    target_compile_options(dmc_native_reader_core PRIVATE /W4)
else()
    target_compile_options(dmc_native_reader_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
set_target_properties(dmc_native_reader_core PROPERTIES
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

    dmc_native_reader_require_cpp23(dmcviewer PRIVATE)

    target_link_libraries(dmcviewer PRIVATE
        DMCNativeReader::Core
        jnigraphics
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

# Win32 is a thin shell too, mirroring the Android JNI boundary above: it owns
# only the window, file dialog and pixel blit. Parsing, inspection and
# rendering all stay in DMCNativeReader::Core.
if(WIN32)
    # Diagnostics build identity (master plan section 5.1.I). Best-effort:
    # falls back to "unknown" rather than failing the build when git isn't
    # on PATH or this tree isn't a git checkout (e.g. a source archive).
    set(DMC_NATIVE_READER_BUILD_SHA "unknown")
    find_package(Git QUIET)
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE _dmc_native_reader_git_sha
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _dmc_native_reader_git_result
        )
        if(_dmc_native_reader_git_result EQUAL 0 AND _dmc_native_reader_git_sha)
            set(DMC_NATIVE_READER_BUILD_SHA "${_dmc_native_reader_git_sha}")
        endif()
        unset(_dmc_native_reader_git_sha)
        unset(_dmc_native_reader_git_result)
    endif()

    add_executable(dmc_native_reader_win WIN32
        win_shell.cpp
    )

    target_link_libraries(dmc_native_reader_win PRIVATE
        DMCNativeReader::Core
        comdlg32
        shlwapi
        windowscodecs
    )
    target_compile_definitions(dmc_native_reader_win PRIVATE
        DMC_NATIVE_READER_BUILD_SHA="${DMC_NATIVE_READER_BUILD_SHA}"
    )

    dmc_native_reader_require_cpp23(dmc_native_reader_win PRIVATE)

    # Headless CLI companion (master plan section 5.1.G): same core, console
    # subsystem, no GUI/file-dialog dependencies.
    add_executable(dmc_native_reader_cli
        win_cli.cpp
    )
    target_link_libraries(dmc_native_reader_cli PRIVATE
        DMCNativeReader::Core
    )
    target_compile_definitions(dmc_native_reader_cli PRIVATE
        DMC_NATIVE_READER_BUILD_SHA="${DMC_NATIVE_READER_BUILD_SHA}"
    )
    dmc_native_reader_require_cpp23(dmc_native_reader_cli PRIVATE)
endif()

option(DMC_NATIVE_READER_BUILD_TESTS "Build portable native regressions" OFF)
if(DMC_NATIVE_READER_BUILD_TESTS)
    enable_testing()
    set(DMC_NATIVE_READER_TESTS
        black_widow_state composite_builder composite_mod_scene composite_placement core_model_pipeline cxx23_profile dds_ptx_v1
        em028_corpus_contract gdata_legacy mod_spatial_adapter module_registry
        png_export_session ptx_model_texture ptx_runtime_compat ptx_transaction render_scene scm_authority
        spider_event_execution spider_model_execution uv_gallery session_inspection tm2_legacy workspace_graph
        motion_playback pac_assembly
    )
    set(_dmc_native_reader_tests_unique ${DMC_NATIVE_READER_TESTS})
    list(REMOVE_DUPLICATES _dmc_native_reader_tests_unique)
    list(LENGTH DMC_NATIVE_READER_TESTS _dmc_native_reader_test_count)
    list(LENGTH _dmc_native_reader_tests_unique _dmc_native_reader_test_unique_count)
    if(NOT _dmc_native_reader_test_count EQUAL _dmc_native_reader_test_unique_count)
        message(FATAL_ERROR "Duplicate test entry in DMC_NATIVE_READER_TESTS")
    endif()
    unset(_dmc_native_reader_tests_unique)
    unset(_dmc_native_reader_test_count)
    unset(_dmc_native_reader_test_unique_count)

    foreach(test_name IN LISTS DMC_NATIVE_READER_TESTS)
        add_executable(${test_name}_test
            ../../test/native/${test_name}_test.cpp)
        dmc_native_reader_require_cpp23(${test_name}_test PRIVATE)
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
