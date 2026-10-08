# INT-LIVE-001 portable live-jam pipeline driver.
#
# This file is NOT wired into the repository root by this task (shared
# build/CI is orchestrator-owned). To register it, the orchestrator adds AFTER
# the `jam-core` target exists:
#
#     add_subdirectory(tools/live-jam-pipeline)
#
# The driver links only jam-core (no JUCE, no audio device), so it can run on a
# CI runner with no audio libraries.

cmake_minimum_required(VERSION 3.16)

if (NOT TARGET jam-core)
    message(FATAL_ERROR
        "tools/live-jam-pipeline requires the jam-core target; add_subdirectory "
        "after jam-core.")
endif()

add_executable(jamLivePipelineDriver live_jam_pipeline_driver.cpp)
target_compile_features(jamLivePipelineDriver PRIVATE cxx_std_17)
target_link_libraries(jamLivePipelineDriver PRIVATE jam-core)

if (NOT MSVC)
    target_compile_options(jamLivePipelineDriver PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME jam.LiveJamPipeline COMMAND jamLivePipelineDriver)
