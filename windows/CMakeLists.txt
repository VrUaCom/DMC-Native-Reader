cmake_minimum_required(VERSION 3.22)
project(DMCNativeReaderWindows LANGUAGES CXX)

set(CORE ${CMAKE_CURRENT_LIST_DIR}/../app/src/main/cpp)
set(RENGINE ${CORE}/vendor/dmc-rengine-cpp)

add_executable(DMCNativeReader WIN32
    src/main.cpp
    ${CORE}/dmc_resource.cpp
    ${CORE}/decode_pipeline.cpp
    ${CORE}/inspection_format.cpp
    ${CORE}/portable_session.cpp
    ${CORE}/formats/dds.cpp
    ${CORE}/adapters/mod_adapter.cpp
    ${CORE}/adapters/scm_adapter.cpp
    ${CORE}/modules/module_registry.cpp
    ${CORE}/modules/module_model.cpp
    ${CORE}/modules/module_texture.cpp
    ${CORE}/view_renderer.cpp
    ${RENGINE}/src/binary/reader.cpp
    ${RENGINE}/src/formats/mod_skin.cpp
    ${RENGINE}/src/formats/mod/transform_domain.cpp
    ${RENGINE}/src/formats/mod/world_transform.cpp
    ${RENGINE}/src/formats/mod.cpp
    ${RENGINE}/src/formats/scm_layout.cpp
    ${RENGINE}/src/formats/scm_topology.cpp
    ${RENGINE}/src/formats/scm_validation.cpp
    ${RENGINE}/src/formats/scm_transform.cpp
    ${RENGINE}/src/formats/scm_hierarchy.cpp
    ${RENGINE}/src/formats/scm.cpp
)

target_include_directories(DMCNativeReader PRIVATE
    ${CORE}/include
    ${RENGINE}/include
)
target_compile_features(DMCNativeReader PRIVATE cxx_std_20)
target_compile_definitions(DMCNativeReader PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(DMCNativeReader PRIVATE /W4 /permissive- /EHsc)
elseif(MINGW)
    target_compile_options(DMCNativeReader PRIVATE -Wall -Wextra -Wpedantic)
    # wWinMain is the Unicode GUI entry point; MinGW needs -municode to select
    # the matching CRT startup shim. Keep the preview self-contained as well.
    target_link_options(DMCNativeReader PRIVATE -municode -static -static-libgcc -static-libstdc++)
endif()

target_link_libraries(DMCNativeReader PRIVATE user32 gdi32 comdlg32 shell32)

set_target_properties(DMCNativeReader PROPERTIES
    OUTPUT_NAME "DMC-Native-Reader"
)
