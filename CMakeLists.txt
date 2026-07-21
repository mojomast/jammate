cmake_minimum_required(VERSION 3.22)

project(GuitarRigNAM VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/JUCE)

# --- NeuralAmpModelerCore (Fase 1: integração de build) -----------------------
# O CMakeLists do Core é voltado às ferramentas/testes dele e não é consumível
# via add_subdirectory; compilamos os fontes diretamente numa static lib,
# como faz o NeuralAmpModelerPlugin oficial. Lista de fontes pinada ao v0.5.4.
set(NAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/NeuralAmpModelerCore")

add_library(nam_core STATIC
    ${NAM_CORE_DIR}/NAM/activations.cpp
    ${NAM_CORE_DIR}/NAM/container.cpp
    ${NAM_CORE_DIR}/NAM/conv1d.cpp
    ${NAM_CORE_DIR}/NAM/convnet.cpp
    ${NAM_CORE_DIR}/NAM/dsp.cpp
    ${NAM_CORE_DIR}/NAM/get_dsp.cpp
    ${NAM_CORE_DIR}/NAM/linear.cpp
    ${NAM_CORE_DIR}/NAM/lstm.cpp
    ${NAM_CORE_DIR}/NAM/ring_buffer.cpp
    ${NAM_CORE_DIR}/NAM/util.cpp
    ${NAM_CORE_DIR}/NAM/wavenet/a2_fast.cpp
    ${NAM_CORE_DIR}/NAM/wavenet/model.cpp
    ${NAM_CORE_DIR}/NAM/wavenet/slimmable.cpp)

# O Core exige C++20 (upstream: target_compile_features cxx_std_20).
target_compile_features(nam_core PRIVATE cxx_std_20)

# SYSTEM: suprime warnings dos headers de terceiros nos TUs do plugin
# (os warning flags recomendados do JUCE não valem para código que não é nosso).
target_include_directories(nam_core SYSTEM
    PUBLIC
        ${NAM_CORE_DIR}
        ${NAM_CORE_DIR}/Dependencies/eigen
        ${NAM_CORE_DIR}/Dependencies/nlohmann)

target_compile_definitions(nam_core
    PUBLIC
        # float (não double) — precisa ser idêntico na lib e em quem inclui
        # os headers do NAM, pois muda a assinatura de DSP::process.
        NAM_SAMPLE_FLOAT
        # Fast-path otimizado para modelos A2 (padrão do upstream).
        NAM_ENABLE_A2_FAST
    PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN)
# -----------------------------------------------------------------------------

juce_add_plugin(GuitarRigNAM
    COMPANY_NAME "Rapha"
    PRODUCT_NAME "GuitarRig NAM"
    PLUGIN_MANUFACTURER_CODE Raph
    PLUGIN_CODE Grnm
    FORMATS Standalone VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(GuitarRigNAM
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

target_compile_definitions(GuitarRigNAM
    PUBLIC
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

# ASIO opcional: o SDK da Steinberg não pode ser redistribuído, então só é
# habilitado se o usuário baixar o SDK e apontar ASIOSDK_DIR para ele.
set(ASIOSDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (folder containing common/iasiodrv.h)")

if(ASIOSDK_DIR AND EXISTS "${ASIOSDK_DIR}/common/iasiodrv.h")
    message(STATUS "ASIO SDK found at ${ASIOSDK_DIR} — building with JUCE_ASIO=1")
    target_include_directories(GuitarRigNAM PRIVATE "${ASIOSDK_DIR}/common")
    target_compile_definitions(GuitarRigNAM PUBLIC JUCE_ASIO=1)
else()
    message(WARNING
        "ASIO SDK not found (ASIOSDK_DIR not set or ${ASIOSDK_DIR}/common/iasiodrv.h missing). "
        "Building WITHOUT ASIO — only WASAPI/DirectSound will be available in the Standalone. "
        "To enable ASIO: download the SDK from https://www.steinberg.net/asiosdk, extract it to "
        "third_party/asiosdk (so that third_party/asiosdk/common/iasiodrv.h exists) and reconfigure "
        "with -DASIOSDK_DIR=\"<absolute-path>/third_party/asiosdk\".")
endif()

target_link_libraries(GuitarRigNAM
    PRIVATE
        nam_core
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
