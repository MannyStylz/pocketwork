cmake_minimum_required(VERSION 3.22)
project(POCKETWORK VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(POCKETWORK
    COMPANY_NAME "Manny Stylz"
    PLUGIN_MANUFACTURER_CODE MnSt
    PLUGIN_CODE Pw01
    FORMATS VST3
    PRODUCT_NAME "POCKETWORK"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(POCKETWORK)

target_sources(POCKETWORK PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/GrooveEngine.cpp
)

target_include_directories(POCKETWORK PRIVATE Source)

target_compile_definitions(POCKETWORK PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
)

target_link_libraries(POCKETWORK PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_extra
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
