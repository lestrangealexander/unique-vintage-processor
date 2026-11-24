cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

project(UniqueVintageProcessor VERSION 0.6.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C++ runtime statically so the plugin needs no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(UVP_BUILD_HARNESS "Build the offline DSP test harness" OFF)

# JUCE: use a local checkout if given (-DUVP_JUCE_PATH=...), otherwise fetch the pinned release.
if(DEFINED UVP_JUCE_PATH)
    add_subdirectory(${UVP_JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.6
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(UVP_DSP_SOURCES
    Source/dsp/SpectralSuppressor.cpp
    Source/dsp/LinearPhaseSplit.cpp
    Source/dsp/HarmonicStage.cpp
    Source/dsp/TapeMachine.cpp
    Source/dsp/VintageEngine.cpp)

if(APPLE)
    set(UVP_FORMATS AU VST3)
else()
    set(UVP_FORMATS VST3)
endif()

juce_add_binary_data(UVPAssets SOURCES
    Resources/DMSans-Medium.ttf
    Resources/DMSans-SemiBold.ttf
    Resources/DMSans-Bold.ttf
    Resources/Pegasus.svg)

juce_add_plugin(UniqueVintageProcessor
    COMPANY_NAME "Sound Aquarium"
    COMPANY_WEBSITE ""
    BUNDLE_ID com.soundaquarium.uniquevintageprocessor
    PLUGIN_MANUFACTURER_CODE SqAu
    PLUGIN_CODE UvPr
    FORMATS ${UVP_FORMATS}
    PRODUCT_NAME "Unique Vintage Processor"
    DESCRIPTION "Vinyl-style resonance suppression, analog harmonics and tape colour"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(UniqueVintageProcessor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/Theme.cpp
    Source/ui/Widgets.cpp
    ${UVP_DSP_SOURCES})

target_include_directories(UniqueVintageProcessor PRIVATE Source)

if(MSVC)
    # sources are UTF-8; large translation units
    target_compile_options(UniqueVintageProcessor PRIVATE /utf-8 /bigobj)
endif()

target_compile_definitions(UniqueVintageProcessor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(UniqueVintageProcessor
    PRIVATE
        UVPAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(UVP_BUILD_HARNESS)
    juce_add_console_app(UVPHarness PRODUCT_NAME "UVPHarness")
    target_sources(UVPHarness PRIVATE Tests/Harness.cpp ${UVP_DSP_SOURCES})
    target_include_directories(UVPHarness PRIVATE Source)
    target_compile_definitions(UVPHarness PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(UVPHarness PRIVATE
        juce::juce_dsp juce::juce_audio_formats juce::juce_audio_basics juce::juce_core
        juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

    # developer tool: renders the editor to PNG (run under xvfb-run on a headless box)
    juce_add_console_app(UVPSnapshot PRODUCT_NAME "UVPSnapshot")
    target_sources(UVPSnapshot PRIVATE Tests/Snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp
        Source/ui/Theme.cpp Source/ui/Widgets.cpp ${UVP_DSP_SOURCES})
    target_include_directories(UVPSnapshot PRIVATE Source)
    target_compile_definitions(UVPSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(UVPSnapshot PRIVATE UVPAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    if(MSVC)
        target_compile_options(UVPSnapshot PRIVATE /utf-8 /bigobj)
    endif()

    # developer tool: plugin-level robustness checks
    juce_add_console_app(UVPStress PRODUCT_NAME "UVPStress")
    target_sources(UVPStress PRIVATE Tests/Stress.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp
        Source/ui/Theme.cpp Source/ui/Widgets.cpp ${UVP_DSP_SOURCES})
    target_include_directories(UVPStress PRIVATE Source)
    target_compile_definitions(UVPStress PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(UVPStress PRIVATE UVPAssets
        juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)

    # developer probe: J-A solver sub-step statistics
    juce_add_console_app(UVPJaStat PRODUCT_NAME "UVPJaStat")
    target_sources(UVPJaStat PRIVATE Tests/JaStat.cpp ${UVP_DSP_SOURCES})
    target_include_directories(UVPJaStat PRIVATE Source)
    target_compile_definitions(UVPJaStat PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 UVP_JA_STATS=1)
    target_link_libraries(UVPJaStat PRIVATE
        juce::juce_dsp juce::juce_audio_basics juce::juce_core
        juce::juce_recommended_config_flags)
endif()
