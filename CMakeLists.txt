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

# --- ONNX Runtime (prebuilt, for the Demucs Phase-2 AI engine) ---
# The default ORT graph optimizer hangs on the htdemucs graph, so the C++
# engine disables it (ORT_DISABLE_ALL) — verified 2026-10-15.
set(ORT_VERSION "1.23.2")
if (APPLE)
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-universal2-${ORT_VERSION}.tgz")
elseif (WIN32)
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-win-x64-${ORT_VERSION}.zip")
else()
    set(ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-x64-${ORT_VERSION}.tgz")
endif()
FetchContent_Declare(ORT URL ${ORT_URL})
FetchContent_MakeAvailable(ORT)
# Prebuilt layout: <ort>/include + <ort>/lib/{onnxruntime.lib,.dll,.dylib}
set(ORT_INCLUDE_DIR "${ort_SOURCE_DIR}/include")
if (APPLE)
    set(ORT_LIB "${ort_SOURCE_DIR}/lib/libonnxruntime.dylib")
elseif (WIN32)
    set(ORT_LIB "${ort_SOURCE_DIR}/lib/onnxruntime.lib")
    set(ORT_DLL "${ort_SOURCE_DIR}/lib/onnxruntime.dll")
else()
    set(ORT_LIB "${ort_SOURCE_DIR}/lib/libonnxruntime.so")
endif()

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
    Source/DemucsSeparator.h
    Source/DemucsSeparator.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(QSplit PRIVATE ${ORT_INCLUDE_DIR})

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
    juce::juce_graphics
    QSplitAssets
    ${ORT_LIB}
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)

# Bundle the ONNX Runtime shared library next to the plugin binaries.
if (APPLE)
    # Copy into each plugin bundle's MacOS folder post-build, then fix the
    # dylib's install name and the binary's rpath so dyld can actually find it
    # at load time. Without this, the plugin binary fails to load entirely
    # (auval: "couldn't be opened") even though the dylib sits right next to it.
    foreach (fmt AU VST3 Standalone)
        add_custom_command (TARGET QSplit_${fmt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ORT_LIB}"
                "$<TARGET_BUNDLE_DIR:QSplit_${fmt}>/Contents/MacOS/"
            COMMAND install_name_tool -id "@rpath/libonnxruntime.dylib"
                "$<TARGET_BUNDLE_DIR:QSplit_${fmt}>/Contents/MacOS/libonnxruntime.dylib"
            COMMAND install_name_tool -change "@rpath/libonnxruntime.1.23.2.dylib" "@rpath/libonnxruntime.dylib"
                "$<TARGET_BUNDLE_DIR:QSplit_${fmt}>/Contents/MacOS/QSplit"
            COMMAND install_name_tool -add_rpath "@loader_path"
                "$<TARGET_BUNDLE_DIR:QSplit_${fmt}>/Contents/MacOS/QSplit")
    endforeach()
elseif (WIN32)
    # Copy the DLL next to each plugin binary.
    foreach (fmt VST3 Standalone)
        add_custom_command (TARGET QSplit_${fmt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ORT_DLL}"
                "$<TARGET_FILE_DIR:QSplit_${fmt}>/")
    endforeach()
endif()
