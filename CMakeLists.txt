cmake_minimum_required(VERSION 3.22)

project(VoxChain VERSION 0.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VOXCHAIN_BUILD_TESTS "Build the DSP unit tests" ON)

# ---------------------------------------------------------------------------
# JUCE: use a local copy if JUCE_SOURCE_DIR is given, otherwise download it.
# ---------------------------------------------------------------------------
if(DEFINED JUCE_SOURCE_DIR)
    add_subdirectory(${JUCE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.10
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(VoxChain
    COMPANY_NAME                "VoxChain Audio"
    COMPANY_WEBSITE             "https://github.com/"
    BUNDLE_ID                   com.voxchainaudio.voxchain
    PLUGIN_MANUFACTURER_CODE    Vxca
    PLUGIN_CODE                 Vxc1
    FORMATS                     VST3
    PRODUCT_NAME                "VoxChain"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx "Pitch Shift" Dynamics)

set(VOXCHAIN_DSP_SOURCES
    Source/Modules/Pitch/PitchDetector.cpp
    Source/Modules/Pitch/ScaleQuantizer.cpp
    Source/Modules/Pitch/PsolaShifter.cpp)

target_sources(VoxChain PRIVATE
    ${VOXCHAIN_DSP_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters/ParameterLayout.cpp
    Source/Engine/ModuleChain.cpp
    Source/Engine/ModuleFactory.cpp
    Source/Modules/Pitch/PitchModule.cpp
    Source/Modules/HighPassModule.cpp
    Source/Modules/EqModule.cpp
    Source/Modules/CompressorModule.cpp
    Source/Modules/DeEsserModule.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/PitchDisplay.cpp
    Source/UI/ModuleStrip.cpp)

target_include_directories(VoxChain PRIVATE Source)

target_compile_definitions(VoxChain PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VoxChain
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Unit tests (console app, DSP only)
# ---------------------------------------------------------------------------
if(VOXCHAIN_BUILD_TESTS)
    juce_add_console_app(VoxChainTests PRODUCT_NAME "VoxChainTests")
    target_sources(VoxChainTests PRIVATE Tests/DspTests.cpp ${VOXCHAIN_DSP_SOURCES})
    target_include_directories(VoxChainTests PRIVATE Source)
    target_compile_definitions(VoxChainTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(VoxChainTests
        PRIVATE juce::juce_core juce::juce_audio_basics juce::juce_dsp
        PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
