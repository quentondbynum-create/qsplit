cmake_minimum_required(VERSION 3.22)
project(QSplit VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pull JUCE automatically — first build downloads it (~150MB), then it's cached.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(QSplit
    COMPANY_NAME "Headroom Audio"
    PLUGIN_NAME "QSplit"
    DESCRIPTION "AI stem separator: drop in a song, audition clean stems, bounce to your DAW"
    PLUGIN_MANUFACTURER_CODE Hdrm
    PLUGIN_CODE Qspl
    BUNDLE_ID com.headroomaudio.qsplit
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "QSplit"
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_SYNTH FALSE
)

target_sources(QSplit PRIVATE
    Source/StemSeparator.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(QSplit PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# Headroom Audio logo embedded as binary data (small brand mark in the UI)
juce_add_binary_data(QSplitAssets SOURCES Source/Assets/headroom-logo.png)

target_link_libraries(QSplit PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    QSplitAssets
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)
