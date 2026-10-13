cmake_minimum_required(VERSION 3.22)

# macOS: build a universal binary (Apple Silicon + Intel) unless told otherwise.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

# Windows: link the C runtime statically so the plugin has no VC++ redist dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Tessera VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# To use a local JUCE checkout: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

option(TESSERA_BUILD_TESTS "Build the headless DSP test" ON)

set(TESSERA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TESSERA_FORMATS AU)
endif()

juce_add_plugin(Tessera
    PRODUCT_NAME "Tessera"
    COMPANY_NAME "Tessera Audio"
    BUNDLE_ID "com.tesseraaudio.tessera"
    PLUGIN_MANUFACTURER_CODE Tsra
    PLUGIN_CODE Tess
    FORMATS ${TESSERA_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Delay Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(Tessera PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Tessera PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Tessera
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    # Keep symbols for Release builds so crash reports have readable stacks.
    target_compile_options(Tessera PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(Tessera PUBLIC $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()

if(TESSERA_BUILD_TESTS)
    enable_testing()
    add_executable(DspTest tests/DspTest.cpp)
    add_test(NAME DspTest COMMAND DspTest)
endif()

