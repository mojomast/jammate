cmake_minimum_required(VERSION 3.22)

project(GuitarCompanion VERSION 0.1.0 LANGUAGES C CXX)

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

# --- Embedded browser (TONE3000 picker) --------------------------------------
# The free API tier only allows browsing through TONE3000's own prompt flows, so
# the store opens their picker in an embedded WebView2 panel instead of throwing
# the user out to the system browser. JUCE finds WebView2 through the NuGet
# package; rather than making everyone install NuGet by hand, we fetch the
# pinned .nupkg (a plain zip) into third_party/webview2/ once. It is gitignored:
# nothing from Microsoft ends up in the repository.
#
# Turn it off with -DGUITAR_COMPANION_EMBEDDED_BROWSER=OFF (or if the download fails):
# the store then falls back to the system browser + the localhost:53682 listener,
# which is the path that always existed.
option(GUITAR_COMPANION_EMBEDDED_BROWSER "Embed TONE3000's picker via WebView2 (Windows)" ON)

# Version JUCE's own FindWebView2.cmake recommends.
set(GUITAR_COMPANION_WEBVIEW2_VERSION "1.0.3485.44")
set(GUITAR_COMPANION_HAS_EMBEDDED_BROWSER OFF)

if(GUITAR_COMPANION_EMBEDDED_BROWSER AND WIN32)
    set(_wv2_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2")
    set(_wv2_pkg "${_wv2_root}/Microsoft.Web.WebView2.${GUITAR_COMPANION_WEBVIEW2_VERSION}")

    if(NOT EXISTS "${_wv2_pkg}/build/native/include/WebView2.h")
        set(_wv2_zip "${CMAKE_CURRENT_BINARY_DIR}/webview2.nupkg")
        string(TOLOWER "microsoft.web.webview2.${GUITAR_COMPANION_WEBVIEW2_VERSION}" _wv2_file)
        message(STATUS "Downloading the WebView2 SDK (${GUITAR_COMPANION_WEBVIEW2_VERSION})...")
        file(DOWNLOAD
            "https://globalcdn.nuget.org/packages/${_wv2_file}.nupkg"
            "${_wv2_zip}"
            STATUS _wv2_status
            SHOW_PROGRESS)
        list(GET _wv2_status 0 _wv2_code)

        if(_wv2_code EQUAL 0)
            file(MAKE_DIRECTORY "${_wv2_pkg}")
            file(ARCHIVE_EXTRACT INPUT "${_wv2_zip}" DESTINATION "${_wv2_pkg}")
        else()
            list(GET _wv2_status 1 _wv2_msg)
            message(WARNING "Could not download the WebView2 SDK (${_wv2_msg}).")
        endif()

        file(REMOVE "${_wv2_zip}")
    endif()

    if(EXISTS "${_wv2_pkg}/build/native/include/WebView2.h")
        # Where JUCE's FindWebView2 globs for *Microsoft.Web.WebView2*.
        set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${_wv2_root}" CACHE PATH "" FORCE)
        set(GUITAR_COMPANION_HAS_EMBEDDED_BROWSER ON)
    else()
        message(WARNING
            "WebView2 SDK not available — building WITHOUT the embedded TONE3000 browser. "
            "The store falls back to the system browser. To fix, either re-run CMake with "
            "network access or point -DJUCE_WEBVIEW2_PACKAGE_LOCATION at a folder containing "
            "the Microsoft.Web.WebView2 NuGet package.")
    endif()
endif()

juce_add_plugin(GuitarCompanion
    COMPANY_NAME "Rapha"
    PRODUCT_NAME "Guitar Companion"
    PLUGIN_MANUFACTURER_CODE Raph
    # Changed with the rename, while the plugin is still pre-release: this is
    # the 4-char id a DAW stores in its projects, so anyone who already had a
    # session with the old "Grnm" build has to re-add the plugin. Doing it now
    # costs one person; doing it after v0.1 is public would cost everyone.
    PLUGIN_CODE Gtco
    FORMATS Standalone VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    NEEDS_WEBVIEW2 ${GUITAR_COMPANION_HAS_EMBEDDED_BROWSER}
    COPY_PLUGIN_AFTER_BUILD FALSE)

# Fontes do design (OFL) + samples de bateria GMRockKit (GPL, ver
# assets/drums/ORIGEM.txt) embutidos no binário.
juce_add_binary_data(GuitarCompanionAssets
    SOURCES
        assets/fonts/Archivo-Regular.ttf
        assets/fonts/Archivo-ExtraBold.ttf
        assets/fonts/SpaceGrotesk-Regular.ttf
        assets/fonts/SpaceGrotesk-Bold.ttf
        assets/fonts/JetBrainsMono-Regular.ttf
        assets/fonts/JetBrainsMono-Bold.ttf
        assets/fonts/Leland.otf
        assets/brand/tone3000-logo.png
        assets/brand/t3k-mark.png
        assets/drums/drum_kick_1.wav
        assets/drums/drum_kick_2.wav
        assets/drums/drum_kick_3.wav
        assets/drums/drum_snare_1.wav
        assets/drums/drum_snare_2.wav
        assets/drums/drum_snare_3.wav
        assets/drums/drum_hat_1.wav
        assets/drums/drum_hat_2.wav
        assets/drums/drum_hat_3.wav
        assets/drums/drum_hatpedal_1.wav
        assets/drums/drum_hatpedal_2.wav
        assets/drums/drum_hatpedal_3.wav
        assets/drums/drum_ride_1.wav
        assets/drums/drum_ride_2.wav
        assets/drums/drum_ride_3.wav
        assets/drums/drum_crash_1.wav
        assets/drums/drum_crash_2.wav
        assets/drums/drum_crash_3.wav
        assets/drums/drum_tom1_1.wav
        assets/drums/drum_tom1_2.wav
        assets/drums/drum_tom1_3.wav
        assets/drums/drum_tom2_1.wav
        assets/drums/drum_tom2_2.wav
        assets/drums/drum_tom2_3.wav
        assets/drums/drum_floor_1.wav
        assets/drums/drum_floor_2.wav
        assets/drums/drum_floor_3.wav)

target_sources(GuitarCompanion
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/PluginCatalog.cpp
        src/Tone3000Client.cpp
        src/ToneWebView.cpp
        src/StoreOverlay.cpp
        src/DrumEngine.cpp
        src/DrumLibrary.cpp
        src/DrumGenerator.cpp
        src/DrumOverlay.cpp
        src/SongOverlay.cpp
        src/AudioOverlay.cpp)

# Fontes usam literais UTF-8; sem /utf-8 o MSVC assume codepage 1252 e
# corrompe os acentos na UI.
target_compile_options(GuitarCompanion PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(GuitarCompanion
    PRIVATE
        # Sem isso, as macros min/max do windows.h quebram os headers do
        # AudioDSPTools (ex.: std::max no LanczosResampler.h).
        NOMINMAX
    PUBLIC
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
        # Embedded browser: TONE3000's picker runs inside the store window.
        # Stays 0 when the WebView2 SDK is missing, and ToneWebView compiles down
        # to "not supported" so the system-browser path takes over.
        # NOTE: JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING must be left UNDEFINED
        # when off - juce_gui_extra.h turns WebView2 on for any *defined* value,
        # including 0, and would then look for WebView2Loader.dll at runtime.
        JUCE_WEB_BROWSER=$<BOOL:${GUITAR_COMPANION_HAS_EMBEDDED_BROWSER}>
        $<$<BOOL:${GUITAR_COMPANION_HAS_EMBEDDED_BROWSER}>:JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1>
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Slot de plugin externo: hosting de VST3 de terceiros na cadeia.
        JUCE_PLUGINHOST_VST3=1)

# ASIO opcional: o SDK da Steinberg não pode ser redistribuído, então só é
# habilitado se o usuário baixar o SDK e apontar ASIOSDK_DIR para ele.
set(ASIOSDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (folder containing common/iasiodrv.h)")

if(ASIOSDK_DIR AND EXISTS "${ASIOSDK_DIR}/common/iasiodrv.h")
    message(STATUS "ASIO SDK found at ${ASIOSDK_DIR} — building with JUCE_ASIO=1")
    target_include_directories(GuitarCompanion PRIVATE "${ASIOSDK_DIR}/common")
    target_compile_definitions(GuitarCompanion PUBLIC JUCE_ASIO=1)
else()
    message(WARNING
        "ASIO SDK not found (ASIOSDK_DIR not set or ${ASIOSDK_DIR}/common/iasiodrv.h missing). "
        "Building WITHOUT ASIO — only WASAPI/DirectSound will be available in the Standalone. "
        "To enable ASIO: download the SDK from https://www.steinberg.net/asiosdk, extract it to "
        "third_party/asiosdk (so that third_party/asiosdk/common/iasiodrv.h exists) and reconfigure "
        "with -DASIOSDK_DIR=\"<absolute-path>/third_party/asiosdk\".")
endif()

target_link_libraries(GuitarCompanion
    PRIVATE
        # WHOLE_ARCHIVE é obrigatório: as arquiteturas do NAM se registram via
        # inicializadores estáticos (ConfigParserHelper) em objetos que nada
        # referencia — sem isso o linker os descarta e get_dsp() falha com
        # "No config parser registered for architecture".
        "$<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>"
        GuitarCompanionAssets
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
        # WebBrowserComponent lives here (already pulled in by juce_audio_utils;
        # named explicitly because the store now depends on it directly).
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# --- Unit tests (opt-in) ------------------------------------------------------
# OFF by default: the normal build is untouched and the test target never even
# exists (so it can never end up in ALL_BUILD). To run them, use a SEPARATE
# build directory:
#   cmake -B build-tests -DGUITAR_COMPANION_BUILD_TESTS=ON
#   cmake --build build-tests --config Release --target GuitarCompanionTests
#   ctest --test-dir build-tests -C Release --output-on-failure
# See tests/README.md.
option(GUITAR_COMPANION_BUILD_TESTS "Build the unit tests" OFF)

if(GUITAR_COMPANION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
