cmake_minimum_required(VERSION 3.22)
project(LiveJamUiHarness LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# Reuses read-only Release JUCE module objects from an existing product build;
# only the new UI sources below are compiled. No product archive, processor,
# audio device or NAM object is linked.
set(JUCE_SOURCE_DIR "" CACHE PATH "JUCE source checkout")
set(JUCE_REUSE_BUILD "" CACHE PATH "Existing Release product build (read-only JUCE objects)")
if(NOT JUCE_SOURCE_DIR OR NOT JUCE_REUSE_BUILD)
    message(FATAL_ERROR "Set JUCE_SOURCE_DIR and JUCE_REUSE_BUILD; see docs/research/LIVE-JAM-UI.md")
endif()

set(module_objects)
foreach(module juce_core juce_events juce_data_structures juce_graphics juce_gui_basics juce_gui_extra)
    file(GLOB objects "${JUCE_REUSE_BUILD}/CMakeFiles/GuitarCompanion.dir/third_party/JUCE/modules/${module}/*.o")
    if(NOT objects)
        message(FATAL_ERROR "Missing prebuilt ${module} objects in ${JUCE_REUSE_BUILD}")
    endif()
    list(APPEND module_objects ${objects})
endforeach()

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_executable(LiveJamUiHarness
    main.cpp
    "${PROJECT_ROOT}/tests/JamLiveUiTests.cpp"
    "${PROJECT_ROOT}/src/ui/JamOverlay.cpp"
    "${PROJECT_ROOT}/src/ui/JamLivePresenter.cpp"
    ${module_objects})

target_include_directories(LiveJamUiHarness PRIVATE
    "${JUCE_SOURCE_DIR}/modules"
    "${PROJECT_ROOT}/src"
    "${PROJECT_ROOT}/tests")

target_compile_definitions(LiveJamUiHarness PRIVATE
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    NDEBUG=1)

target_compile_options(LiveJamUiHarness PRIVATE -Wall -Wextra -Wpedantic)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FONTS REQUIRED freetype2 fontconfig)
find_package(Threads REQUIRED)
target_link_libraries(LiveJamUiHarness PRIVATE
    ${FONTS_LIBRARIES} Threads::Threads ${CMAKE_DL_LIBS} rt)
