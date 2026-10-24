cmake_minimum_required(VERSION 3.22.1)
project(dmcviewer LANGUAGES C CXX)

# The debug APK is the one people install and try: the software renderer
# needs optimised native code in every configuration (-O0 runs it 5-10x
# slower). Later compile options win over the configuration's -O0.
if(ANDROID AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2)
endif()
# The renderer rasterises row bands on several cores.
find_package(Threads REQUIRED)

set(DMC_RENGINE_VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dmc-rengine-cpp)

# Canonical portable read-only dependency slice owned by DMC Rengine.
# Its own language/toolchain contract is external to this repository; Native
# Reader consumes it without modifying the submodule during the C++23 migration.
include(${DMC_RENGINE_VENDOR}/cmake/reader_core.cmake)

# Read-only MOT/PAC slice of the pinned Rengine checkout, rebuilt by Native
# Reader as C++23 (the target is defined after
# dmc_native_reader_require_cpp23 below). ReaderCore does not list these
# canonical sources yet; the submodule itself is never edited. Every file here
# is a parser/evaluator; nothing in this slice writes or repacks resources.
set(DMC_NATIVE_READER_RENGINE_VIEWER_SOURCES
    ${DMC_RENGINE_VENDOR}/src/formats/mot/parser.cpp
    ${DMC_RENGINE_VENDOR}/src/analysis/mod/motion_group.cpp
    ${DMC_RENGINE_VENDOR}/src/analysis/mod/animation_binding.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/container.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/relative_slot_container.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/pac.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/pnst.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/shw.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/hits.cpp
    # Stage and enemy families read by modules/module_families.cpp.
    ${DMC_RENGINE_VENDOR}/src/formats/lig2.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/dca.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/stage_txt.cpp
    ${DMC_RENGINE_VENDOR}/src/formats/so/graph.cpp
)

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

# Everything compiled into Native Reader is C++23, including the vendored
# Rengine ReaderCore. Its reader_core.cmake keeps declaring the older standard as a
# minimum feature; the target-scoped CXX_STANDARD 23 set here decides the
# actual language mode. The submodule itself is not edited.
dmc_native_reader_require_cpp23(dmc_rengine_reader_core PRIVATE)

# The pinned ReaderCore still expands the MOD local rotation as Rz*Ry*Rx; the
# executable builds Rx*Ry*Rz (dmc-rengine-cpp b403108). Swap in the Native
# Reader port of that one translation unit until the pin moves.
get_target_property(_dmc_reader_core_sources dmc_rengine_reader_core SOURCES)
list(FILTER _dmc_reader_core_sources EXCLUDE REGEX "/src/formats/mod/world_transform\\.cpp$")
list(APPEND _dmc_reader_core_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/modules/rengine_port/world_transform.cpp)
set_property(TARGET dmc_rengine_reader_core PROPERTY SOURCES ${_dmc_reader_core_sources})
unset(_dmc_reader_core_sources)

add_library(dmc_native_reader_rengine_viewer STATIC
    ${DMC_NATIVE_READER_RENGINE_VIEWER_SOURCES})
target_link_libraries(dmc_native_reader_rengine_viewer PUBLIC DMCRengine::ReaderCore)
dmc_native_reader_require_cpp23(dmc_native_reader_rengine_viewer PRIVATE)
set_target_properties(dmc_native_reader_rengine_viewer PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
if(ANDROID)
    target_compile_options(dmc_native_reader_rengine_viewer PRIVATE
        -ffunction-sections -fdata-sections)
endif()

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
    modules/environment_collision.cpp
    modules/session_gallery.cpp
    modules/view_benchmark.cpp
    modules/session_inspection.cpp
    modules/uv_gallery.cpp
    modules/dmc3_shaders.cpp
    modules/scene_projection.cpp
    modules/module_registry.cpp
    modules/module_model.cpp
    modules/module_texture.cpp
    modules/module_evt.cpp
    modules/dds_bcn.cpp
    modules/dds_bcn_encode.cpp
    modules/texture_reencode.cpp
    modules/ptx_framing_compat.cpp
    modules/ptx_runtime_compat.cpp
    modules/texture_companion.cpp
    modules/texture_set.cpp
    modules/workspace_graph.cpp
    modules/motion/skeleton_rig.cpp
    modules/motion/animated_local.cpp
    modules/motion/motion_clip.cpp
    modules/motion/effect_runtime.cpp
    modules/motion/effect_profile_registry.cpp
    modules/motion/motion_player.cpp
    modules/motion/part_attachment.cpp
    modules/motion/cloth_chain.cpp
    modules/motion/model_cloth.cpp
    modules/motion/uv_scroll.cpp
    modules/motion/motion_script.cpp
    modules/motion/motion_chart.cpp
    modules/raster_card.cpp
    modules/format_views.cpp
    modules/collision_shapes.cpp
    modules/collision_debug.cpp
    modules/neutral_texture.cpp
    modules/stage_room.cpp
    modules/module_hits.cpp
    modules/effect_bank.cpp
    modules/particle_sprt.cpp
    modules/generator.cpp
    modules/motion/enemy_effects.cpp
    modules/module_effects.cpp
    modules/font.cpp
    modules/module_font.cpp
    modules/stage_config.cpp
    modules/stage_markers.cpp
    modules/module_stage_config.cpp
    modules/sound.cpp
    modules/video.cpp
    modules/messages.cpp
    modules/module_families.cpp
    modules/demo.cpp
    modules/demo_session.cpp
    modules/module_demo.cpp
    modules/camera_control.cpp
    modules/frame_profile.cpp
    modules/module_archive.cpp
    modules/module_shadow.cpp
    modules/module_scripts.cpp
    modules/shadow_hull.cpp
    modules/pac_assembly.cpp
    spider/crusader.cpp
    spider/black_widow.cpp
    spider/model_placement_actions.cpp
    spider/session_compose_actions.cpp
    spider/session_texture_actions.cpp
    spider/texture_reencode_actions.cpp
    spider/stage_actions.cpp
    spider/app_info.cpp
    spider/companion_menu.cpp
    spider/collision_report.cpp
    spider/camera.cpp
    view_renderer.cpp
)

# The DMC3 shaders (shaders/dmc3/*.vert, *.frag) go into the core as text:
# one generated table of file name and source, rewritten only when a file
# changes, so the library and the files cannot drift apart.
file(GLOB _dmc3_shader_files CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/dmc3/*.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/dmc3/*.frag)
list(SORT _dmc3_shader_files)
set(_dmc3_shader_text "// Generated by CMake from shaders/dmc3/; do not edit.\n")
string(APPEND _dmc3_shader_text "#include \"dmcresource/dmc3_shaders.h\"\n\n")
string(APPEND _dmc3_shader_text "namespace dmcresource::dmc3::detail {\nnamespace {\n")
string(APPEND _dmc3_shader_text "constexpr EmbeddedSource kSources[] = {\n")
foreach(_dmc3_shader_file IN LISTS _dmc3_shader_files)
    get_filename_component(_dmc3_shader_name "${_dmc3_shader_file}" NAME)
    file(READ "${_dmc3_shader_file}" _dmc3_shader_source)
    string(APPEND _dmc3_shader_text "    {\"${_dmc3_shader_name}\", R\"glsl(${_dmc3_shader_source})glsl\"},\n")
endforeach()
string(APPEND _dmc3_shader_text "};\n}  // namespace\n\n")
string(APPEND _dmc3_shader_text "std::span<const EmbeddedSource> embedded_sources() noexcept { return kSources; }\n\n")
string(APPEND _dmc3_shader_text "}  // namespace dmcresource::dmc3::detail\n")
set(_dmc3_shader_out ${CMAKE_CURRENT_BINARY_DIR}/generated/dmc3_shader_sources.cpp)
file(WRITE ${_dmc3_shader_out}.in "${_dmc3_shader_text}")
configure_file(${_dmc3_shader_out}.in ${_dmc3_shader_out} COPYONLY)
list(APPEND DMC_NATIVE_READER_CORE_SOURCES ${_dmc3_shader_out})
unset(_dmc3_shader_text)
unset(_dmc3_shader_source)
unset(_dmc3_shader_files)

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

# Ogg Vorbis decoder (third_party/stb_vorbis, public domain), plain C.
add_library(dmc_native_reader_stb_vorbis STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb_vorbis/stb_vorbis.c
)
target_compile_definitions(dmc_native_reader_stb_vorbis PRIVATE STB_VORBIS_NO_STDIO STB_VORBIS_NO_PUSHDATA_API)
if(MSVC)
    target_compile_options(dmc_native_reader_stb_vorbis PRIVATE /w)
else()
    target_compile_options(dmc_native_reader_stb_vorbis PRIVATE -w)
endif()

# Movies (the HD release's .wmv: ASF, Windows Media video and audio): a
# minimal FFmpeg (LGPL 2.1 or later), its signed release tarball fetched at
# build time and pinned by hash, configured with only the ASF demuxer and
# the Windows Media decoders and linked statically. third_party/ffmpeg.
option(DMC_NATIVE_READER_FFMPEG "Decode WMV movies with a minimal FFmpeg" ON)
set(DMC_NATIVE_READER_FFMPEG_LIBS "")
if(DMC_NATIVE_READER_FFMPEG AND NOT MSVC)
    include(ExternalProject)
    set(DMC_FFMPEG_VERSION 7.1.1)
    set(DMC_FFMPEG_SHA256 733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1)
    set(DMC_FFMPEG_INSTALL ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-install)
    set(DMC_FFMPEG_CONFIGURE
        --prefix=${DMC_FFMPEG_INSTALL}
        --disable-everything --disable-programs --disable-doc --disable-network
        --disable-autodetect --disable-avdevice --disable-swscale --disable-swresample
        --disable-postproc --disable-avfilter --disable-debug
        --enable-static --disable-shared --enable-pic
        --enable-demuxer=asf
        --enable-decoder=wmv1,wmv2,wmv3,vc1,msmpeg4v3,wmav1,wmav2,wmapro
        --enable-parser=vc1
        --cc=${CMAKE_C_COMPILER}
    )
    if(ANDROID)
        if(ANDROID_ABI STREQUAL "arm64-v8a")
            set(_dmc_ffmpeg_arch aarch64)
        elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
            set(_dmc_ffmpeg_arch arm)
        elseif(ANDROID_ABI STREQUAL "x86_64")
            set(_dmc_ffmpeg_arch x86_64)
        else()
            set(_dmc_ffmpeg_arch x86)
        endif()
        list(APPEND DMC_FFMPEG_CONFIGURE
            --enable-cross-compile --target-os=android --arch=${_dmc_ffmpeg_arch}
            "--extra-cflags=--target=${CMAKE_C_COMPILER_TARGET} --sysroot=${CMAKE_SYSROOT}"
            "--extra-ldflags=--target=${CMAKE_C_COMPILER_TARGET} --sysroot=${CMAKE_SYSROOT}"
            --ar=${CMAKE_AR} --ranlib=${CMAKE_RANLIB} --nm=${CMAKE_NM} --strip=${CMAKE_STRIP})
        if(NOT _dmc_ffmpeg_arch MATCHES "^(aarch64|arm)$")
            list(APPEND DMC_FFMPEG_CONFIGURE --disable-x86asm)
        endif()
    else()
        list(APPEND DMC_FFMPEG_CONFIGURE --disable-x86asm)
    endif()
    find_program(DMC_MAKE NAMES gmake make REQUIRED)
    set(_dmc_ffmpeg_download_options "")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
        list(APPEND _dmc_ffmpeg_download_options DOWNLOAD_EXTRACT_TIMESTAMP ON)
    endif()
    ExternalProject_Add(dmc_ffmpeg
        URL https://ffmpeg.org/releases/ffmpeg-${DMC_FFMPEG_VERSION}.tar.xz
        URL_HASH SHA256=${DMC_FFMPEG_SHA256}
        ${_dmc_ffmpeg_download_options}
        PREFIX ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg
        CONFIGURE_COMMAND <SOURCE_DIR>/configure ${DMC_FFMPEG_CONFIGURE}
        BUILD_COMMAND ${DMC_MAKE} -j8
        INSTALL_COMMAND ${DMC_MAKE} install
        BUILD_BYPRODUCTS
            ${DMC_FFMPEG_INSTALL}/lib/libavformat.a
            ${DMC_FFMPEG_INSTALL}/lib/libavcodec.a
            ${DMC_FFMPEG_INSTALL}/lib/libavutil.a
    )
    file(MAKE_DIRECTORY ${DMC_FFMPEG_INSTALL}/include)
    foreach(_dmc_ffmpeg_lib avformat avcodec avutil)
        add_library(dmc_ffmpeg_${_dmc_ffmpeg_lib} STATIC IMPORTED)
        set_target_properties(dmc_ffmpeg_${_dmc_ffmpeg_lib} PROPERTIES
            IMPORTED_LOCATION ${DMC_FFMPEG_INSTALL}/lib/lib${_dmc_ffmpeg_lib}.a
            INTERFACE_INCLUDE_DIRECTORIES ${DMC_FFMPEG_INSTALL}/include)
        add_dependencies(dmc_ffmpeg_${_dmc_ffmpeg_lib} dmc_ffmpeg)
        list(APPEND DMC_NATIVE_READER_FFMPEG_LIBS dmc_ffmpeg_${_dmc_ffmpeg_lib})
    endforeach()
endif()

add_library(dmc_native_reader_core STATIC
    ${DMC_NATIVE_READER_CORE_SOURCES}
)
if(DMC_NATIVE_READER_FFMPEG_LIBS)
    target_compile_definitions(dmc_native_reader_core PRIVATE DMC_NATIVE_READER_FFMPEG=1)
    target_link_libraries(dmc_native_reader_core PRIVATE ${DMC_NATIVE_READER_FFMPEG_LIBS} m)
endif()
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
        Threads::Threads
    PRIVATE
        dmc_native_reader_stb_vorbis
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

# Diagnostics build identity shared by platform shells only. Best-effort:
# source archives/non-git builders fall back to "unknown" without failing.
if(ANDROID OR WIN32)
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
endif()

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
        android/gles_view_backend.cpp
    )

    dmc_native_reader_require_cpp23(dmcviewer PRIVATE)

    target_compile_definitions(dmcviewer PRIVATE
        DMC_NATIVE_READER_BUILD_SHA="${DMC_NATIVE_READER_BUILD_SHA}"
    )

    target_link_libraries(dmcviewer PRIVATE
        DMCNativeReader::Core
        jnigraphics
        android
        EGL
        GLESv3
    )

    target_include_directories(dmcviewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
        em028_corpus_contract effect_visualization gdata_legacy mod_spatial_adapter module_registry
        png_export_session ptx_model_texture ptx_runtime_compat ptx_transaction render_scene scm_authority
        spider_event_execution spider_model_execution uv_gallery session_inspection tm2_legacy workspace_graph
        effect_runtime motion_playback pac_assembly player_coat particle generator enemy_effects ptx_single_level bcn_formats gallery_mosaic texture_reencode_action gpu_view_frame view_benchmark fly_camera font stage_config families camera_control demo video photo_camera dmc3_shaders
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
        target_compile_definitions(${test_name}_test PRIVATE
            DMC_NATIVE_READER_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
        add_test(NAME ${test_name} COMMAND ${test_name}_test)
    endforeach()
endif()
