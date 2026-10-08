cmake_minimum_required(VERSION 3.16)

# BTrack 1.0.7 — vendored, NOT a submodule.
#
# WHY VENDORED RATHER THAN A SUBMODULE
#   Every other third-party component in this repository is a submodule. This one
#   is not, deliberately:
#     - the evaluation corpus and harness must run on a machine where
#       `git submodule update --init` has never been executed, and the GPL
#       boundary is far easier to audit when the code is physically present;
#     - a pinned SHA in .gitmodules is not, by itself, evidence of what was
#       actually reviewed. The copy in the tree is the evidence.
#   Consequence: updating BTrack means re-vendoring and re-reviewing by hand.
#
# LICENSE BOUNDARY — read this before adding a file.
#   BTrack is GNU GPL v3 (`LICENSE.txt`, © 2008-2014 Queen Mary University of
#   London, Adam Stark). This project is AGPL v3 (root `LICENSE`), and SPEC.md
#   §25.6 permits GPL-compatible linking only inside that already-open path. The
#   boundary is therefore real and must stay explicit:
#
#     - `btrack` and `jam-btrack` are SEPARATE static libraries from `jam-core`.
#       `src/jam/` — the Musical Clock, the Jam Director, the analysis ring — is
#       deliberately free of any third-party code, which is what lets the
#       musical-intelligence gates be built and tested on a machine with no
#       third-party audio libraries at all (ADR-0003 lane "core").
#     - `src/btrack/` is the ONLY place GPL third-party code is referenced.
#       Nothing under `src/jam/` may include a BTrack header. If you are about to
#       add one, you are about to break a licensing boundary, not a build rule.
#
#   kiss_fft130 (`libs/`) is BSD-licensed; its `COPYING` ships in this directory.
#
# BUILD SWITCH
#   OFF by default, so a default `jam-core` build stays dependency-free and the
#   GPL code is not compiled unless someone asks for it. The rhythm evaluation
#   harness and CI turn it on.

option(JAM_ENABLE_BTRACK "Build the GPL BTrack backend behind IRhythmTracker" OFF)

if (NOT JAM_ENABLE_BTRACK)
    return()
endif()

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/BTrack.cpp")
    message(FATAL_ERROR
        "JAM_ENABLE_BTRACK is ON but third_party/BTrack is empty. Vendor it first; "
        "see the header comment in this file.")
endif()

# --- libsamplerate (vendored, BSD-2-Clause) ----------------------------------
# BTrack's calculateTempo() path calls resampleOnsetDetectionFunction() on EVERY
# beat (src/BTrack.cpp:259), and that function uses libsamplerate's src_simple().
# An earlier version of this file disabled it as "offline-only"; that was WRONG
# and it made BTrack report a fixed 79.5 BPM for every input at every rate. See
# VENDORED-PATCHES.md. The function is load-bearing, so libsamplerate is vendored
# rather than removed and BTrack stays byte-identical to upstream.
#
# 9.2 MB of it is src/high_qual_coeffs.h, the SRC_SINC_BEST_QUALITY filter bank,
# which BTrack selects explicitly. Accepted: exact fidelity is worth the bytes
# when this benchmark decides the production beat tracker.
set(LSR_DIR ${JAM_ROOT}/third_party/libsamplerate)

# libsamplerate's sources #include "config.h", which autotools normally generates.
# Upstream's own CMakeLists.txt generates it from config.h.cmake; we do exactly
# that rather than hand-writing a header, so the feature macros stay honest.
set(PACKAGE     "libsamplerate")
set(VERSION     "0.2.2")
# Every libsamplerate source wraps its config.h include in #ifdef HAVE_CONFIG_H,
# exactly as upstream's CMakeLists.txt sets with add_definitions(-DHAVE_CONFIG_H).
# Without this the header is skipped, HAVE_STDBOOL_H is never defined, and
# common.h's `#ifdef HAVE_STDBOOL_H #include <stdbool.h>` is skipped too - so
# `bool` is an unknown type in src_linear.c and src_zoh.c.
# Only check_include_file is used: check_symbol_exists needs a project() context
# (it pulls in CheckFunctionExists/try_compile), and jam-core deliberately has no
# project() of its own. HAVE_SIGNAL is only consulted by libsamplerate to allow
# interrupting a long SINC conversion with SIGALRM, which an audio product must
# not do anyway, so it is simply left undefined.
include(CheckIncludeFile)
check_include_file (stdbool.h     HAVE_STDBOOL_H)
check_include_file (unistd.h      HAVE_UNISTD_H)
check_include_file (immintrin.h   HAVE_IMMINTRIN_H)
check_include_file (sys/times.h   HAVE_SYS_TIMES_H)
# BTrack asks for SRC_SINC_BEST_QUALITY, so the best converter is mandatory.
# The fast and medium converters are cheap and come along for the ride.
set(ENABLE_SINC_FAST_CONVERTER  1)
set(ENABLE_SINC_MEDIUM_CONVERTER 1)
set(ENABLE_SINC_BEST_CONVERTER  1)
configure_file (${LSR_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

add_library(samplerate STATIC
    ${LSR_DIR}/src/samplerate.c
    ${LSR_DIR}/src/src_linear.c
    ${LSR_DIR}/src/src_sinc.c
    ${LSR_DIR}/src/src_zoh.c)
# config.h and HAVE_CONFIG_H belong to libsamplerate's implementation. Exporting
# them to a JUCE product makes its bundled PNG code include this unrelated
# config.h (and inherit C-only feature macros such as restrict).
target_include_directories(samplerate
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
    PUBLIC ${LSR_DIR}/src)
target_compile_definitions(samplerate PRIVATE HAVE_CONFIG_H=1)
set_target_properties(samplerate PROPERTIES
    C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if (NOT MSVC)
    target_compile_options(samplerate PRIVATE -w)   # upstream quality, not ours
endif()

# --- kiss_fft (vendored inside BTrack) ---------------------------------------
add_library(kiss_fft STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/kiss_fft130/kiss_fft.c)
target_include_directories(kiss_fft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/kiss_fft130)
set_target_properties(kiss_fft PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

# --- BTrack ------------------------------------------------------------------
# Upstream is C++11 and carries its own warnings; do not impose -Werror here.
add_library(btrack STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BTrack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OnsetDetectionFunction.cpp)
target_include_directories(btrack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(btrack PUBLIC kiss_fft samplerate)
# VENDOR BUILD CONFIG, not a source patch. OnsetDetectionFunction.h includes
# kiss_fft.h only under this define; upstream's own src/CMakeLists.txt sets it.
# Without it the build fails with 'complexOut was not declared'.
target_compile_definitions(btrack PUBLIC USE_KISS_FFT=1)
set_target_properties(btrack PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- our adapter -------------------------------------------------------------
# GLib is an optional BTrack feature (non-causal offline mode); this product is
# causal and real-time, so the code path is compiled out.
file(GLOB JAM_BTRACK_SOURCES CONFIGURE_DEPENDS
     "${JAM_ROOT}/src/btrack/*.cpp")
if (NOT JAM_BTRACK_SOURCES)
    message(FATAL_ERROR
        "JAM_ENABLE_BTRACK is ON but no sources exist in src/btrack/. The adapter "
        "has not been written yet.")
endif()

add_library(jam-btrack STATIC ${JAM_BTRACK_SOURCES})
target_include_directories(jam-btrack PUBLIC
    ${JAM_ROOT}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(jam-btrack PUBLIC btrack jam-core)
set_target_properties(jam-btrack PROPERTIES POSITION_INDEPENDENT_CODE ON)

message(STATUS
    "jam-btrack: BTrack 1.0.7 (GPLv3) enabled — remember DEPENDENCIES.md must "
    "record this as LINKED, and its NOTICE obligations ship with the product.")
