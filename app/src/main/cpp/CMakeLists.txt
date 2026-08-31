cmake_minimum_required(VERSION 3.22.1)
project(dmcviewer LANGUAGES CXX)

add_library(dmcviewer SHARED
    app_native.cpp
    dmc_resource.cpp
    decode_fixed.cpp
    decode_pipeline.cpp
    view_renderer.cpp
)

target_include_directories(dmcviewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(dmcviewer PRIVATE cxx_std_17)
target_compile_options(dmcviewer PRIVATE -Wall -Wextra -Wpedantic)
