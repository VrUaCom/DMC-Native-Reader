cmake_minimum_required(VERSION 3.22.1)
project(dmcviewer LANGUAGES CXX)

set(DMC_RENGINE_VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dmc-rengine-cpp)

add_library(dmcviewer SHARED
    app_native.cpp
    dmc_resource.cpp
    decode_pipeline.cpp
    inspection_format.cpp
    formats/dds.cpp
    adapters/mod_adapter.cpp
    adapters/scm_adapter.cpp
    modules/module_registry.cpp
    modules/module_model.cpp
    modules/module_texture.cpp
    view_renderer.cpp
    vendor/dmc-rengine-cpp/src/binary/reader.cpp
    vendor/dmc-rengine-cpp/src/formats/mod_skin.cpp
    vendor/dmc-rengine-cpp/src/formats/mod/transform_domain.cpp
    vendor/dmc-rengine-cpp/src/formats/mod/world_transform.cpp
    vendor/dmc-rengine-cpp/src/formats/mod.cpp
    vendor/dmc-rengine-cpp/src/formats/scm_layout.cpp
    vendor/dmc-rengine-cpp/src/formats/scm_topology.cpp
    vendor/dmc-rengine-cpp/src/formats/scm_validation.cpp
    vendor/dmc-rengine-cpp/src/formats/scm_transform.cpp
    vendor/dmc-rengine-cpp/src/formats/scm_hierarchy.cpp
    vendor/dmc-rengine-cpp/src/formats/scm.cpp
    vendor/dmc-rengine-cpp/src/profiles/dmc3/texture_slot_framing.cpp
)

target_include_directories(dmcviewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DMC_RENGINE_VENDOR}/include
)
target_compile_features(dmcviewer PRIVATE cxx_std_20)
target_compile_options(dmcviewer PRIVATE -Wall -Wextra -Wpedantic)
