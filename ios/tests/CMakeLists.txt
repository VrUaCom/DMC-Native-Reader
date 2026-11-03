cmake_minimum_required(VERSION 3.22.1)
project(dmc_ios_bridge_tests LANGUAGES CXX)

# Host build of the iOS shell's portable C++ boundary against the real
# DMCNativeReader::Core. Runs on Linux and on macOS; needs no Apple SDK.
#
# Kept outside app/src/main/cpp/CMakeLists.txt on purpose: the canonical native
# test inventory there is an exact-count Phase-2 evidence contract, and this
# shell-level regression must not silently change it.

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp core)

add_executable(reader_bridge_test
    reader_bridge_test.cpp
    fixtures_model.cpp
    fixtures_texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../DMCReader/Bridge/reader_bridge.cpp
)
dmc_native_reader_require_cpp23(reader_bridge_test PRIVATE)
target_include_directories(reader_bridge_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../DMCReader/Bridge
)
target_link_libraries(reader_bridge_test PRIVATE DMCNativeReader::Core)
# The borrowed canonical fixtures rely on assert; keep it active.
target_compile_options(reader_bridge_test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME reader_bridge COMMAND reader_bridge_test)
