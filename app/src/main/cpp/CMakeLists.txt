cmake_minimum_required(VERSION 3.22.1)
project(dmcviewer LANGUAGES CXX)

set(DMC_RENGINE_VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dmc-rengine-cpp)

# Native Reader 1.0: keep the Android shell linked only to the canonical
# portable ReaderCore slice; format algorithms stay out of Android UI code.
# Canonical product slice owned by DMC Rengine. Android does not enumerate or
# reimplement format parser sources; the same target is reusable by iOS,
# Windows and Web/WASM shells.
include(${DMC_RENGINE_VENDOR}/cmake/reader_core.cmake)

add_library(dmcviewer SHARED
    app_native.cpp
    dmc_resource.cpp
    decode_pipeline.cpp
    inspection_format.cpp
    adapters/mod_adapter.cpp
    adapters/scm_adapter.cpp
    modules/model_texture_binding.cpp
    modules/module_registry.cpp
    modules/module_model.cpp
    modules/module_texture.cpp
    modules/texture_companion.cpp
    spider/black_widow.cpp
    view_renderer.cpp
)

target_include_directories(dmcviewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dmcviewer PRIVATE
    DMCRengine::ReaderCore
)

target_compile_features(dmcviewer PRIVATE cxx_std_20)
target_compile_options(dmcviewer PRIVATE -Wall -Wextra -Wpedantic)
