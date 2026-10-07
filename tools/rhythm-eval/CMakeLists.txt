cmake_minimum_required(VERSION 3.16)

# rhythm-eval — the EVAL-002 beat-tracker evaluation CLI.
#
# Standalone so it does not touch the frozen root CMakeLists.txt and so a real
# backend can be linked in without dragging GPL code into jam-core:
#
#   cmake -S tools/rhythm-eval -B build/rhythm-eval -G Ninja
#   cmake --build build/rhythm-eval
#   build/rhythm-eval/rhythm-eval --corpus testdata/rhythm --out eval-out
#
# The pure metrics are also compiled directly into the jam-core test binary
# (tests/jam/RhythmEvalMetricsTests.cpp includes Metrics.cpp), because
# jam-core/CMakeLists.txt is frozen for EVAL-002 and only globs src/jam and
# tests/jam. This target is the thin CLI shell.

project(rhythm-eval CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(JAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(rhythm-eval
    main.cpp
    BackendRunner.cpp
    Metrics.cpp
    Manifest.cpp)

target_include_directories(rhythm-eval PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${JAM_ROOT}/src)

if (MSVC)
    target_compile_options(rhythm-eval PRIVATE /W4 /utf-8)
else()
    target_compile_options(rhythm-eval PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (UNIX)
    target_link_libraries(rhythm-eval PRIVATE ${CMAKE_DL_LIBS})
endif()
