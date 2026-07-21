cmake_minimum_required(VERSION 3.22)

project(GuitarRigNAM VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# NOTA: third_party/NeuralAmpModelerCore é apenas clonado/pinado nesta fase.
# A integração de build do NAM Core é a Fase 1 — não adicionar aqui ainda.
add_subdirectory(third_party/JUCE)

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
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
