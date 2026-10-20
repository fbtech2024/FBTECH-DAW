cmake_minimum_required(VERSION 3.22)
# libreria di avvio statica: il programma non richiede i "Visual C++ Redistributable"
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(FBTECH_DAW VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# Versione di JUCE (verificata: 8.0.6). Si puo' cambiare da riga di comando: -DJUCE_REF=...
set(JUCE_REF "8.0.6" CACHE STRING "Versione (tag) di JUCE")
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG ${JUCE_REF} GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(FBTechDAW
    PRODUCT_NAME "FBTECH DAW"
    COMPANY_NAME "FBTECH"
    VERSION 0.1.0
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
juce_generate_juce_header(FBTechDAW)

target_sources(FBTechDAW PRIVATE src/Main.cpp src/MainComponent.cpp src/AudioEngine.cpp)
target_include_directories(FBTechDAW PRIVATE src)

# ASIO: serve il kit di Steinberg (gratuito): si mette nella cartella asiosdk/ (vedi asiosdk/LEGGIMI_ASIO.txt).
# Senza kit il programma si compila lo stesso, con WASAPI e DirectSound, e lo dice chiaramente.
set(ASIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/asiosdk")
set(FBD_HAS_ASIO 0)
if(EXISTS "${ASIO_DIR}/common/iasiodrv.h")
    set(FBD_HAS_ASIO 1)
    target_include_directories(FBTechDAW PRIVATE "${ASIO_DIR}/common" "${ASIO_DIR}/host" "${ASIO_DIR}/host/pc")
    message(STATUS "FBTECH DAW: kit ASIO trovato -> ASIO INCLUSO")
else()
    message(WARNING "FBTECH DAW: kit ASIO NON trovato in asiosdk/ -> compilo SENZA ASIO (solo WASAPI e DirectSound). Vedi asiosdk/LEGGIMI_ASIO.txt")
endif()

target_compile_definitions(FBTechDAW PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_ASIO=${FBD_HAS_ASIO}
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    FBD_HAS_ASIO=${FBD_HAS_ASIO})

# i file sorgente sono UTF-8 (accenti e simboli nei testi)
target_compile_options(FBTechDAW PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(FBTechDAW PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_audio_devices
    juce::juce_gui_basics
    juce::juce_events
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
