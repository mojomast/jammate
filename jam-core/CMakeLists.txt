cmake_minimum_required(VERSION 3.16)

# jam-core — platform-neutral musical intelligence.
#
# Purpose: let the rhythm-analysis chain (SPEC.md sections 6, 9, 10) be compiled
# and tested with no JUCE, no audio device, and no audio hardware. It compiles
# the SAME sources the JUCE plugin links; there is no second copy of the
# algorithm. DEVPLAN section 9 (CI-001) sanctions a platform-neutral jam-core
# build as its own CI job.
#
#   Standalone: cmake -S jam-core -B build -G Ninja && cmake --build build
#               && ctest --test-dir build --output-on-failure
#   In-tree:   pulled in by the top-level CMakeLists as the `jam-core` target.
#
# Three non-obvious design rules, all deliberate:
#
# 1. GLOB CONFIGURE_DEPENDS instead of an explicit source list.
#    Workers operate in parallel in isolated worktrees and must never touch this
#    file, because a shared list would make every wave collide on one line.
#    Discovered sources are auto-linked instead. CONFIGURE_DEPENDS re-globs on
#    build, so a newly added file is picked up without re-running cmake by hand.
#
# 2. jam-core must not depend on juce::.
#    If it does, the real-time contract can no longer be reviewed, fuzzed or
#    tested in isolation, and a build machine without audio libraries stops
#    being able to prove the musical-intelligence gates. Any change that adds a
#    JUCE dependency here is rejected at review.
#
# 3. JAM_ROOT is resolved, not assumed.
#    This file is configured BOTH standalone (`cmake -S jam-core`) and via
#    add_subdirectory() from the repository root, so `src/jam` is not reachable
#    from CMAKE_CURRENT_SOURCE_DIR in the standalone case. Deriving the root
#    from `..` works in both, which is why the seams live at the repository root
#    and not under jam-core/.
#
# ctest entries are generated from the JAM_TEST(suite, ...) registrations found in
# the test sources, so every suite automatically gets its own ctest name and no
# suite can exist without one.

get_filename_component(JAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

option(JAM_CORE_BUILD_TESTS "Build the platform-neutral jam-core test binary" ON)

file(GLOB JAM_CORE_SOURCES CONFIGURE_DEPENDS "${JAM_ROOT}/src/jam/*.cpp")
file(GLOB JAM_TEST_SOURCES CONFIGURE_DEPENDS "${JAM_ROOT}/tests/jam/*.cpp")

list(REMOVE_ITEM JAM_TEST_SOURCES "${JAM_ROOT}/tests/jam/JamTestMain.cpp")

if (NOT JAM_CORE_SOURCES)
    message(FATAL_ERROR
        "jam-core found no sources in ${JAM_ROOT}/src/jam/. A buildable state "
        "needs at least one translation unit (JamCore.cpp provides the "
        "identification/schema surface); check the checkout.")
endif()

add_library(jam-core STATIC ${JAM_CORE_SOURCES})

target_include_directories(jam-core PUBLIC ${JAM_ROOT}/src)
target_compile_features(jam-core PUBLIC cxx_std_17)

if (MSVC)
    target_compile_options(jam-core PRIVATE /W4 /utf-8)
    target_compile_definitions(jam-core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(jam-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)
target_link_libraries(jam-core PUBLIC Threads::Threads)

# --- tests -------------------------------------------------------------------

if (JAM_TEST_SOURCES)
    enable_testing()

    add_executable(jamTests
        ${JAM_ROOT}/tests/jam/JamTestMain.cpp
        ${JAM_TEST_SOURCES})

    target_include_directories(jamTests PRIVATE
        ${JAM_ROOT}/tests/jam
        ${JAM_ROOT}/tests)
    target_link_libraries(jamTests PRIVATE jam-core)

    if (NOT MSVC)
        target_compile_options(jamTests PRIVATE -Wall -Wextra)
    endif()

    # Discover suite names so each gets its own ctest entry. Extracting them from
    # the registrations themselves means a suite can never be added without also
    # being registered with ctest.
    set(JAM_DISCOVERED_SUITES "")
    foreach(jam_test_src IN LISTS JAM_TEST_SOURCES)
        file(STRINGS "${jam_test_src}" jam_reg_lines REGEX "JAM_TEST[ ]*\\(")
        foreach(jam_line IN LISTS jam_reg_lines)
            string(REGEX REPLACE
                ".*JAM_TEST[ ]*\\([ \t\r\n]*([A-Za-z_][A-Za-z0-9_]*).*"
                "\\1"
                jam_suite "${jam_line}")
            if(jam_suite AND NOT jam_suite STREQUAL jam_line)
                list(APPEND JAM_DISCOVERED_SUITES "${jam_suite}")
            endif()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES JAM_DISCOVERED_SUITES)

    if (NOT JAM_DISCOVERED_SUITES)
        message(FATAL_ERROR
            "jam-core test sources exist but no JAM_TEST(suite, ...) registrations "
            "were found; a test that cannot be selected by ctest cannot run.")
    endif()

    foreach(jam_suite IN LISTS JAM_DISCOVERED_SUITES)
        add_test(NAME jam.${jam_suite} COMMAND jamTests ${jam_suite}.)
    endforeach()
    list(LENGTH JAM_DISCOVERED_SUITES jam_suite_count)
    string(REPLACE ";" " " JAM_DISCOVERED_SUITES_DISPLAY "${JAM_DISCOVERED_SUITES}")
    message(STATUS "jam-core: registered ${jam_suite_count} test suite(s): "
                   "${JAM_DISCOVERED_SUITES_DISPLAY}")
else()
    message(STATUS "jam-core: no tests/jam/*.cpp found, test target skipped")
endif()