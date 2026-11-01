cmake_minimum_required(VERSION 3.20)
project(NeuralFlowAmp VERSION 1.0.0)

# JUCE library is automatically downloaded from GitHub
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0 # Stable version
)
FetchContent_MakeAvailable(juce)

# Define the project as both VST3 plugin and standalone application
juce_add_plugin(NeuralFlowAmp
    COMPANY_NAME "YourName"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    FORMATS Standalone VST3
)

# Include the source code files
target_sources(NeuralFlowAmp PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Crunch.cpp
    Source/Crunch.h
    Source/PedalCatalog.h
    Source/DSP/StereoDelay.h
    Source/UI/Theme.h
    Source/UI/NeuralLookAndFeel.h
    Source/UI/Widgets.h
    Source/UI/Panels.h
    Source/UI/MainView.h
)

# Required JUCE modules
target_link_libraries(NeuralFlowAmp PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp 
    juce::juce_gui_extra
)

# Special configuration for standalone (executable) version
juce_generate_juce_header(NeuralFlowAmp)

target_compile_definitions(NeuralFlowAmp PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)

# Headless DSP tests (run: build/NeuralFlowAmpTests_artefacts/Release/NeuralFlowAmp Tests.exe)
juce_add_console_app(NeuralFlowAmpTests PRODUCT_NAME "NeuralFlowAmp Tests")

target_sources(NeuralFlowAmpTests PRIVATE Tests/DspTests.cpp)

target_compile_definitions(NeuralFlowAmpTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(NeuralFlowAmpTests PRIVATE
    NeuralFlowAmp
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
)

juce_generate_juce_header(NeuralFlowAmpTests)
