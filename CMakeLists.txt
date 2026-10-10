cmake_minimum_required(VERSION 3.22)

# macOS: build a universal binary (Apple Silicon + Intel) unless told otherwise.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

# Windows: link the C runtime statically so the plugin has no VC++ redist dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Macrocosm VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# To use a local JUCE checkout: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

option(MACROCOSM_BUILD_TESTS "Build the headless DSP test" ON)
option(MACROCOSM_BUILD_CANARY "Build a minimal pass-through plugin for CI diagnostics" OFF)

set(MACROCOSM_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MACROCOSM_FORMATS AU)
endif()

juce_add_plugin(Macrocosm
    PRODUCT_NAME "Macrocosm"
    COMPANY_NAME "Macrocosm Audio"
    BUNDLE_ID "com.macrocosmaudio.macrocosm"
    PLUGIN_MANUFACTURER_CODE Mcra
    PLUGIN_CODE Mcsm
    FORMATS ${MACROCOSM_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Delay Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(Macrocosm PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Macrocosm PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Macrocosm
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    # Keep symbols for Release builds so crash reports have readable stacks.
    target_compile_options(Macrocosm PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(Macrocosm PUBLIC $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()

if(MACROCOSM_BUILD_TESTS)
    enable_testing()
    add_executable(DspTest tests/DspTest.cpp)
    add_test(NAME DspTest COMMAND DspTest)
endif()

if(MACROCOSM_BUILD_CANARY)
    juce_add_plugin(Canary
        PRODUCT_NAME "Canary"
        COMPANY_NAME "Macrocosm Audio"
        PLUGIN_MANUFACTURER_CODE Mcra
        BUNDLE_ID "com.macrocosmaudio.canary"
        PLUGIN_CODE Cnry
        FORMATS VST3
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(Canary PRIVATE tests/canary/Canary.cpp)
    target_compile_definitions(Canary PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(Canary PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endif()
