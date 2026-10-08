# Unit tests (opt-in, see GUITAR_COMPANION_BUILD_TESTS in the top-level CMakeLists).
#
# Console target linking ONLY the drum sources: PluginProcessor.cpp includes
# PluginEditor.h and would drag the whole UI (and the NAM core) in with it.
juce_add_console_app(GuitarCompanionTests
    PRODUCT_NAME "GuitarCompanionTests")

target_sources(GuitarCompanionTests
    PRIVATE
        TestMain.cpp
        DrumSpecTests.cpp
        DrumLibraryTests.cpp
        DrumGeneratorTests.cpp
        DrumCodecTests.cpp
        DrumMidiTests.cpp
        DrumFoundationTests.cpp
        DrumClockBridgeTests.cpp
        DrumHeapProbe.cpp
        # production code under test
        ${PROJECT_SOURCE_DIR}/src/DrumEngine.cpp
        ${PROJECT_SOURCE_DIR}/src/DrumLibrary.cpp
        ${PROJECT_SOURCE_DIR}/src/DrumGenerator.cpp)

target_include_directories(GuitarCompanionTests PRIVATE ${PROJECT_SOURCE_DIR}/src)

# Same as the plugin: without /utf-8 MSVC reads the UTF-8 literals as codepage
# 1252 and the UTF-8 validity checks in the tests would be meaningless.
target_compile_options(GuitarCompanionTests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(GuitarCompanionTests
    PRIVATE
        NOMINMAX
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(GuitarCompanionTests
    PRIVATE
        # BinaryData.h: DrumEngine.cpp decodes the embedded GMRockKit samples,
        # so the symbols have to be there even though the tests never play audio.
        GuitarCompanionAssets
        jam-core
        juce::juce_audio_formats
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags)

# One ctest entry per suite (the argument is a substring filter on the case
# names; the runner fails if a filter matches nothing, so a renamed case can
# never pass silently).
add_test(NAME drums.parseSpec  COMMAND GuitarCompanionTests spec_)
add_test(NAME drums.library    COMMAND GuitarCompanionTests library_)
add_test(NAME drums.generator  COMMAND GuitarCompanionTests generator_)
add_test(NAME drums.barCodec   COMMAND GuitarCompanionTests codec_)
add_test(NAME drums.midiCapacity COMMAND GuitarCompanionTests midi_)
add_test(NAME drums.foundation COMMAND GuitarCompanionTests foundation_)
add_test(NAME drums.clockBridge COMMAND GuitarCompanionTests intdrum_)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(GuitarCompanionTests PRIVATE DRUM_MIDI_HEAP_PROBE=1)
    target_link_options(GuitarCompanionTests PRIVATE
        -Wl,--wrap=malloc -Wl,--wrap=realloc -Wl,--wrap=free)
endif()
