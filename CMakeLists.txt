cmake_minimum_required(VERSION 3.22)

project(AeroFX90 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add JUCE subdirectory
add_subdirectory(JUCE)

# Define the SFX90 plugin target
juce_add_plugin(AeroFX90
    COMPANY_NAME "Digital Symphony Instruments"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Digital Symphony Instruments"
    BUNDLE_ID "com.digitalsymphonyinstruments.sfx90"
    PLUGIN_MANUFACTURER_CODE DSym
    PLUGIN_CODE SX90
    FORMATS VST3 Standalone AU
    PRODUCT_NAME "SFX90"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES "Fx" "Reverb" "Delay" "Modulation" "Pitch Shift"
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
)

target_sources(AeroFX90
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/DSP/Spx90Types.h
        Source/DSP/Spx90PresetDatabase.cpp
        Source/DSP/Spx90PresetDatabase.h
        Source/DSP/ConverterEngine.h
        Source/DSP/Ym3807LfoBank.h
        Source/DSP/DramBuffer.h
        Source/DSP/ModDelayAlgorithms.h
        Source/DSP/EqPanFreezeAlgorithms.h
        Source/DSP/ReverbAlgorithms.h
        Source/DSP/PitchAlgorithms.h
        Source/DSP/Spx90DspEngine.cpp
        Source/DSP/Spx90DspEngine.h
)

target_include_directories(AeroFX90
    PRIVATE
        Source
        Source/DSP
)

target_compile_definitions(AeroFX90
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(AeroFX90
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Headless DSP Verification Executable to test all 30 presets and converter behavior
add_executable(AeroFX90_Tests
    Source/Tests/DspVerificationMain.cpp
    Source/DSP/Spx90PresetDatabase.cpp
    Source/DSP/Spx90DspEngine.cpp
)

target_include_directories(AeroFX90_Tests
    PRIVATE
        Source
        Source/DSP
)

target_compile_definitions(AeroFX90_Tests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(AeroFX90_Tests
    PRIVATE
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_core
)
