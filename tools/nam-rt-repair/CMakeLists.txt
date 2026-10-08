# RT-003 standalone build for the NAM LSTM real-time allocation repair.
#
# Builds TWO independent NAM static libraries with identical production flags:
#   * nam_core_original — pinned upstream third_party/NeuralAmpModelerCore
#   * nam_core          — generated patched overlay (same basename the probe and
#                         the product use, so it can be dropped into the probe
#                         scratch as libnam_core.a)
# and two copies of the deterministic differential test, one per library.
#
# This project does not build JUCE. It is intentionally separate from the root
# build so the repair can be reproduced and reviewed in isolation. The root
# CMakeLists.txt applies the same overlay to the real nam_core target.
#
#   cmake -S tools/nam-rt-repair -B <scratch> -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build <scratch> -j
#   ctest --test-dir <scratch> --output-on-failure

cmake_minimum_required(VERSION 3.22)
project(NamRtRepair LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# --- locate the pinned NAM checkout ------------------------------------------
# Explicit, portable: default to this worktree's submodule; if it is not
# populated, fail loudly and ask for -DNAM_CORE_DIR rather than guessing a
# host-specific path.
set(NAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/NeuralAmpModelerCore"
    CACHE PATH "Pinned NeuralAmpModelerCore checkout")
if(NOT EXISTS "${NAM_CORE_DIR}/NAM/lstm.cpp")
  message(FATAL_ERROR
    "RT-003: NAM_CORE_DIR does not look like a NAM checkout: ${NAM_CORE_DIR}\n"
    "Pass -DNAM_CORE_DIR=<pinned NeuralAmpModelerCore checkout> explicitly.")
endif()

# --- shared NAM source list (13 files, as the product builds them) -----------
set(NAM_SOURCES
  activations.cpp
  container.cpp
  conv1d.cpp
  convnet.cpp
  dsp.cpp
  get_dsp.cpp
  linear.cpp
  lstm.cpp
  ring_buffer.cpp
  util.cpp
  wavenet/a2_fast.cpp
  wavenet/model.cpp
  wavenet/slimmable.cpp)

# --- overlay generation (fail-closed SHA256) ---------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/nam-rt/NamRtPatch.cmake")
nam_rt_generate_overlay("${NAM_CORE_DIR}")

function(nam_rt_configure_target target include_root)
  target_compile_features(${target} PRIVATE cxx_std_20)
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(${target} SYSTEM PUBLIC
    "${include_root}"
    "${NAM_CORE_DIR}"
    "${NAM_CORE_DIR}/Dependencies/eigen"
    "${NAM_CORE_DIR}/Dependencies/nlohmann")
  target_compile_definitions(${target}
    PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endfunction()

set(_orig_abs "")
foreach(s ${NAM_SOURCES})
  list(APPEND _orig_abs "${NAM_CORE_DIR}/NAM/${s}")
endforeach()
set(_patched_abs "")
foreach(s ${NAM_SOURCES})
  list(APPEND _patched_abs "${NAM_RT_NAM_DIR}/${s}")
endforeach()

add_library(nam_core_original STATIC ${_orig_abs})
nam_rt_configure_target(nam_core_original "${NAM_CORE_DIR}")

add_library(nam_core STATIC ${_patched_abs})
nam_rt_configure_target(nam_core "${NAM_RT_INCLUDE_ROOT}")

# --- deterministic differential test (one process per library) ---------------
add_executable(nam_lstm_diff_original src/NamLstmDiffTest.cpp)
target_link_libraries(nam_lstm_diff_original PRIVATE nam_core_original)

add_executable(nam_lstm_diff_patched src/NamLstmDiffTest.cpp)
target_link_libraries(nam_lstm_diff_patched PRIVATE nam_core)

enable_testing()
add_test(NAME nam_rt_diff COMMAND
  "${CMAKE_CURRENT_SOURCE_DIR}/run_diff_test.sh"
  --original "$<TARGET_FILE:nam_lstm_diff_original>"
  --patched  "$<TARGET_FILE:nam_lstm_diff_patched>"
  --out      "${CMAKE_BINARY_DIR}/diff-artifacts")

# --- real-processor probe evidence assertion --------------------------------
# Validates the committed patched-vs-control probe artifacts (see
# docs/research/NAM-LSTM-RT-REPAIR.md for how they are produced).
# python3 is required: the differential comparator and this check are Python.
find_program(RT003_PYTHON3 NAMES python3 REQUIRED)
add_test(NAME nam_rt_probe_verifier_unit COMMAND "${RT003_PYTHON3}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_probe_repair.py")
set(_ev "${CMAKE_CURRENT_SOURCE_DIR}/../../docs/research/nam-rt-repair")
add_test(NAME nam_rt_probe_repair COMMAND "${RT003_PYTHON3}"
  "${CMAKE_CURRENT_SOURCE_DIR}/check_probe_repair.py"
  --patched-csv "${_ev}/probe/processor-probe-combos.csv"
  --baseline-csv "${_ev}/probe-baseline/processor-probe-combos.csv"
  --patched-findings "${_ev}/probe/findings.json"
  --baseline-findings "${_ev}/probe-baseline/findings.json"
  --out-json "${CMAKE_BINARY_DIR}/probe-repair-verification.json")

# --- differential comparator acceptance unit tests ---------------------------
add_test(NAME nam_rt_compare_unit COMMAND "${RT003_PYTHON3}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_compare_diff.py")

# --- RT-005 activation differential test (one process per library) -----------
# Same source compiled twice: pinned upstream vs generated patched overlay.
add_executable(nam_activation_diff_original src/NamActivationDiffTest.cpp)
target_link_libraries(nam_activation_diff_original PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core_original>")
target_compile_options(nam_activation_diff_original PRIVATE -fvisibility=default -Wno-frame-address)

add_executable(nam_activation_diff_patched src/NamActivationDiffTest.cpp)
target_link_libraries(nam_activation_diff_patched PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>")
target_compile_options(nam_activation_diff_patched PRIVATE -fvisibility=default -Wno-frame-address)

add_test(NAME nam_rt_activation_diff COMMAND
  "${CMAKE_CURRENT_SOURCE_DIR}/run_activation_diff_test.sh"
  --original "$<TARGET_FILE:nam_activation_diff_original>"
  --patched  "$<TARGET_FILE:nam_activation_diff_patched>"
  --model    "${NAM_CORE_DIR}/example_models/wavenet_a2_max.nam"
  --out      "${CMAKE_BINARY_DIR}/activation-diff-artifacts")

# --- RT-005 activation comparator acceptance unit tests ----------------------
add_test(NAME nam_rt_activation_compare_unit COMMAND "${RT003_PYTHON3}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_compare_activation_diff.py")

# --- patch idempotence / stale-hash checks -----------------------------------
add_test(NAME nam_rt_patch_checks COMMAND
  "${CMAKE_CURRENT_SOURCE_DIR}/check_patch.sh"
  --nam-core-dir "${NAM_CORE_DIR}"
  --overlay-dir  "${NAM_RT_INCLUDE_ROOT}/NAM"
  --patch        "${CMAKE_CURRENT_SOURCE_DIR}/../../patches/nam/lstm-rt-alloc.patch"
  --activation-patch "${CMAKE_CURRENT_SOURCE_DIR}/../../patches/nam/activation-rt-alloc.patch"
  --cmake        "${CMAKE_COMMAND}"
  --source-dir   "${CMAKE_CURRENT_SOURCE_DIR}"
  --workdir      "${CMAKE_BINARY_DIR}/patch-checks")
