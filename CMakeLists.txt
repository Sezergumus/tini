cmake_minimum_required(VERSION 3.24)
project(NeuralFlowAmp VERSION 1.0.0)

# JUCE library is automatically downloaded from GitHub
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0 # Stable version
)
FetchContent_MakeAvailable(juce)

# Neural Amp Modeler core (MIT) and Eigen (MPL2), both downloaded at configure time like JUCE.
# The core's own CMake project (command line tools) is not used: SOURCE_SUBDIR points at a folder
# that does not exist so FetchContent only downloads it, and the library is built below.
# Eigen is pinned to the commit the NAM core release was tested with.
FetchContent_Declare(
    nam_core
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        v0.6.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""            # Eigen is fetched separately; AudioDSPTools is not needed
    SOURCE_SUBDIR  do_not_add_subdirectory
)
FetchContent_Declare(
    eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/bc3b39870ecb690a623a3f49149a358b95c5781d/eigen-bc3b39870ecb690a623a3f49149a358b95c5781d.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR  do_not_add_subdirectory
)
FetchContent_MakeAvailable(nam_core eigen)

file(GLOB NAM_CORE_SOURCES "${nam_core_SOURCE_DIR}/NAM/*.cpp" "${nam_core_SOURCE_DIR}/NAM/*/*.cpp")
# The core registers its model architectures (WaveNet, LSTM, ...) from static initialisers, which a
# linker drops from a static library unless the whole archive is linked (see WHOLE_ARCHIVE below).
add_library(nam_core_lib STATIC ${NAM_CORE_SOURCES})
target_include_directories(nam_core_lib SYSTEM PUBLIC
    "${nam_core_SOURCE_DIR}"
    "${nam_core_SOURCE_DIR}/Dependencies/nlohmann"
    "${eigen_SOURCE_DIR}")
target_compile_features(nam_core_lib PUBLIC cxx_std_20)
target_compile_definitions(nam_core_lib PUBLIC NAM_SAMPLE_FLOAT NOMINMAX)   # float processing, like the rest of the engine
set_target_properties(nam_core_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(nam_core_lib PRIVATE /W0 /bigobj "$<$<CONFIG:Release>:/O2>")
endif()

# Inter (SIL Open Font License, see third_party/fonts/Inter/OFL.txt), embedded in the binary so the UI
# looks the same on every machine. Regular and SemiBold are the only weights the UI uses.
juce_add_binary_data(InterFonts
    HEADER_NAME InterFontData.h
    NAMESPACE InterFontData
    SOURCES
        third_party/fonts/Inter/Inter-Regular.ttf
        third_party/fonts/Inter/Inter-SemiBold.ttf)
set_target_properties(InterFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Define the project as both VST3 plugin and standalone application
juce_add_plugin(NeuralFlowAmp
    COMPANY_NAME "YourName"
    # PRODUCT_NAME is the file name (Tini.exe, Tini.vst3) and also the name shown by the DAW and
    # in the window title. It has to stay plain ASCII: JUCE passes it to the compiler as a macro
    # on the command line, where a non-ASCII letter such as the dotless i is not read as UTF-8 and
    # the window title then crashes the app when it is drawn. The UI itself writes "tını".
    # The CMake target stays NeuralFlowAmp so existing build paths keep working.
    PRODUCT_NAME "Tini"
    BUNDLE_ID "com.yourname.tini"
    ICON_BIG   "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Branding/icon-512.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Branding/icon-64.png"
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
    Source/LatencyProbe.h
    Source/PresetManager.h
    Source/DSP/StereoDelay.h
    Source/UI/Theme.h
    Source/UI/NeuralLookAndFeel.h
    Source/UI/Widgets.h
    Source/UI/Panels.h
    Source/UI/Footer.h
    Source/UI/MainView.h
    Source/UI/BlockPages.h
    Source/UI/PresetBar.h
    Source/UI/PresetList.h
)

# Standalone-only helper that reports the real audio device latency to the UI.
# (JucePlugin_Build_Standalone is only defined on the format target, hence the separate file.)
target_sources(NeuralFlowAmp_Standalone PRIVATE Source/StandaloneBridge.cpp)

# Optional ASIO audio driver support (lowest latency on Windows).
# Steinberg's ASIO SDK is not bundled here (you accept its licence when you get it from
# https://www.steinberg.net/developers/ - it is offered under GPLv3 or the proprietary agreement).
# Unzip it anywhere and either
#   cmake -B build -DASIO_SDK_DIR=C:/SDKs/asiosdk
# or set an ASIO_SDK_DIR environment variable, or drop it in third_party/asiosdk.
# Without the SDK the plugin builds as before and only offers WASAPI / DirectSound.
if(WIN32)
    set(ASIO_SDK_DIR "$ENV{ASIO_SDK_DIR}" CACHE PATH "Folder of the Steinberg ASIO SDK (contains common/iasiodrv.h)")

    if(NOT ASIO_SDK_DIR)
        foreach(candidate
                "${CMAKE_SOURCE_DIR}/third_party/asiosdk"
                "C:/SDKs/asiosdk" "C:/asiosdk" "D:/SDKs/asiosdk" "D:/asiosdk")
            if(EXISTS "${candidate}/common/iasiodrv.h")
                set(ASIO_SDK_DIR "${candidate}")
                break()
            endif()
        endforeach()
    endif()

    if(ASIO_SDK_DIR AND EXISTS "${ASIO_SDK_DIR}/common/iasiodrv.h")
        message(STATUS "ASIO enabled (SDK: ${ASIO_SDK_DIR})")
        target_include_directories(NeuralFlowAmp PUBLIC "${ASIO_SDK_DIR}/common")
        target_compile_definitions(NeuralFlowAmp PUBLIC JUCE_ASIO=1)
    else()
        message(STATUS "ASIO disabled: ASIO SDK not found (set ASIO_SDK_DIR, see CMakeLists.txt)")
    endif()
endif()

# Required JUCE modules
target_link_libraries(NeuralFlowAmp PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp 
    juce::juce_gui_extra
    InterFonts
    $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core_lib>
)

# Special configuration for standalone (executable) version
juce_generate_juce_header(NeuralFlowAmp)

target_compile_definitions(NeuralFlowAmp PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)

# Headless DSP tests (run: build/NeuralFlowAmpTests_artefacts/Release/NeuralFlowAmp Tests.exe)
juce_add_console_app(NeuralFlowAmpTests PRODUCT_NAME "NeuralFlowAmp Tests")

target_sources(NeuralFlowAmpTests PRIVATE Tests/DspTests.cpp Tests/UiSnapshots.cpp)

target_compile_definitions(NeuralFlowAmpTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_link_libraries(NeuralFlowAmpTests PRIVATE
    NeuralFlowAmp
    InterFonts
    $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core_lib>
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
)

juce_generate_juce_header(NeuralFlowAmpTests)
