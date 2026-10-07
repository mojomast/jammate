cmake_minimum_required(VERSION 3.16)

# aubio 0.4.9 — vendored, NOT a submodule.
#
# WHY VENDORED RATHER THAN A SUBMODULE
#   Same reasoning as third_party/BTrack: the rhythm evaluation harness must run
#   on a machine where `git submodule update --init` has never run, and the GPL
#   boundary is easier to audit when the code is physically present. The pinned
#   commit is in PINNED_SHA; the copy in the tree is the evidence. Updating aubio
#   means re-vendoring and re-reviewing by hand.
#
# LICENSE BOUNDARY — read this before adding a file.
#   aubio is GNU GPL v3-or-later (`COPYING`). This project is AGPL v3 (root
#   `LICENSE`), and SPEC.md §25.6 permits GPL-compatible linking only inside that
#   already-open path. The boundary is structural and identical to BTrack's:
#
#     - `aubio` and `jam-aubio` are SEPARATE static libraries from `jam-core`.
#       `src/jam/` stays free of third-party code so the musical-intelligence
#       gates build and test with no third-party audio libraries at all.
#     - `src/aubio/` is the ONLY place aubio is referenced. Nothing under
#       `src/jam/` may include an aubio header. If you are about to add one, you
#       are about to break a licensing boundary, not a build rule.
#     - `jam-core` must build with JAM_ENABLE_AUBIO=OFF and then produce ZERO
#       aubio symbols; this target is OFF by default.
#
# NO MANDATORY EXTERNAL DEPENDENCY: with no FFT/IO backend configured, aubio
# uses its bundled Ooura FFT (src/spectral/ooura_fft8g.c). See
# VENDORED-PATCHES.md.

option(JAM_ENABLE_AUBIO "Build the GPL aubio backend behind IRhythmTracker" OFF)

if (NOT JAM_ENABLE_AUBIO)
    return()
endif()

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/tempo/tempo.c")
    message(FATAL_ERROR
        "JAM_ENABLE_AUBIO is ON but third_party/aubio/src is empty. Vendor it "
        "first; see the header comment in this file.")
endif()

# --- generated config.h (upstream's waf generates this) ----------------------
# Only check_include_file is used: check_symbol_exists needs a project() context
# and jam-core deliberately has none. HAVE_C99_VARARGS_MACROS and
# HAVE_MEMCPY_HACKS are unconditional: every compiler this project supports has
# C99 __VA_ARGS__ and memcpy, and upstream only uses them as a fast path.
include(CheckIncludeFile)
check_include_file (stdlib.h   HAVE_STDLIB_H)
check_include_file (stdio.h    HAVE_STDIO_H)
check_include_file (math.h     HAVE_MATH_H)
check_include_file (string.h   HAVE_STRING_H)
check_include_file (errno.h    HAVE_ERRNO_H)
check_include_file (limits.h   HAVE_LIMITS_H)
check_include_file (stdarg.h   HAVE_STDARG_H)
check_include_file (complex.h  HAVE_COMPLEX_H)
set(HAVE_C99_VARARGS_MACROS 1)
set(HAVE_MEMCPY_HACKS 1)
configure_file (${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

set(AUBIO_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# --- library ------------------------------------------------------------------
# The aubio_tempo_t dependency closure only. src/io, src/pitch, src/notes and
# src/synth are vendored but deliberately not compiled (io needs libsndfile/libav;
# none of them is referenced by tempo/onset/beattracking). Keeping the list
# explicit, rather than globbing, makes it obvious what a licence review covers.
add_library(aubio STATIC
    ${AUBIO_DIR}/src/fvec.c
    ${AUBIO_DIR}/src/cvec.c
    ${AUBIO_DIR}/src/lvec.c
    ${AUBIO_DIR}/src/fmat.c
    ${AUBIO_DIR}/src/mathutils.c
    ${AUBIO_DIR}/src/musicutils.c
    ${AUBIO_DIR}/src/vecutils.c
    ${AUBIO_DIR}/src/spectral/fft.c
    ${AUBIO_DIR}/src/spectral/phasevoc.c
    ${AUBIO_DIR}/src/spectral/specdesc.c
    ${AUBIO_DIR}/src/spectral/ooura_fft8g.c
    ${AUBIO_DIR}/src/spectral/awhitening.c
    ${AUBIO_DIR}/src/spectral/statistics.c
    ${AUBIO_DIR}/src/onset/onset.c
    ${AUBIO_DIR}/src/onset/peakpicker.c
    ${AUBIO_DIR}/src/tempo/tempo.c
    ${AUBIO_DIR}/src/tempo/beattracking.c
    ${AUBIO_DIR}/src/utils/hist.c
    ${AUBIO_DIR}/src/utils/scale.c
    ${AUBIO_DIR}/src/utils/parameter.c
    ${AUBIO_DIR}/src/utils/log.c
    ${AUBIO_DIR}/src/utils/windll.c
    ${AUBIO_DIR}/src/temporal/biquad.c
    ${AUBIO_DIR}/src/temporal/filter.c
    ${AUBIO_DIR}/src/temporal/a_weighting.c
    ${AUBIO_DIR}/src/temporal/c_weighting.c)

target_include_directories(aubio PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${AUBIO_DIR}/src)
target_compile_definitions(aubio PRIVATE HAVE_CONFIG_H=1)
set_target_properties(aubio PROPERTIES
    C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if (NOT MSVC)
    target_compile_options(aubio PRIVATE -w)   # upstream quality, not ours
endif()

# --- our adapter --------------------------------------------------------------
# This is the only target that may expose the GPL headers to a consumer. The
# discriminator JAM_AUBIO_ENABLED is PUBLIC so the aubio test target (and only
# it) compiles tests/jam/AubioBackendTests.cpp; the dependency-free jamTests
# target never sees it, so the test file compiles to nothing there.
file(GLOB JAM_AUBIO_SOURCES CONFIGURE_DEPENDS "${JAM_ROOT}/src/aubio/*.cpp")
if (NOT JAM_AUBIO_SOURCES)
    message(FATAL_ERROR
        "JAM_ENABLE_AUBIO is ON but no sources exist in src/aubio/. The adapter "
        "has not been written yet.")
endif()

add_library(jam-aubio STATIC ${JAM_AUBIO_SOURCES})
target_include_directories(jam-aubio PUBLIC ${JAM_ROOT}/src)
target_link_libraries(jam-aubio PUBLIC aubio jam-core)
target_compile_definitions(jam-aubio PUBLIC JAM_AUBIO_ENABLED=1)
set_target_properties(jam-aubio PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (MSVC)
    target_compile_options(jam-aubio PRIVATE /W4 /utf-8)
else()
    target_compile_options(jam-aubio PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS
    "jam-aubio: aubio 0.4.9 (GPLv3-or-later) enabled — DEPENDENCIES.md must "
    "record this as LINKED, and its NOTICE obligations ship with the product.")
