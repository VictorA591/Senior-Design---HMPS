cmake_minimum_required(VERSION 3.22)
project(EducationalDAW VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point this at a real JUCE checkout when deploying to another machine.
set(HMPS_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "JUCE source directory")
add_subdirectory("${HMPS_JUCE_DIR}" bin/JUCE)

# Tracktion is required for the session timeline and MIDI clip playback.
option(HMPS_WITH_TRACKTION "Build the required Tracktion session engine" ON)
if(NOT HMPS_WITH_TRACKTION)
    message(FATAL_ERROR "HMPS requires Tracktion Engine for session playback.")
endif()
if(HMPS_WITH_TRACKTION)
    set(HMPS_TRACKTION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tracktion_engine" CACHE PATH "Tracktion source directory")
    add_subdirectory("${HMPS_TRACKTION_DIR}" bin/tracktion_engine)
endif()

juce_add_binary_data(HmpsPianoData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Assets/Piano/C4.flac
        Assets/Piano/Ds4.flac
        Assets/Piano/Fs4.flac
        Assets/Piano/A4.flac
        Assets/Piano/C5.flac
)
set_target_properties(HmpsPianoData PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

option(HMPS_BUILD_VST3 "Also build the portable HMPS Keys VST3 instrument" ON)
if(HMPS_BUILD_VST3)
    juce_add_plugin(HMPSKeys
        PRODUCT_NAME "HMPS Keys"
        COMPANY_NAME "HMPS"
        BUNDLE_ID "org.hmps.keys"
        PLUGIN_MANUFACTURER_CODE Hmps
        PLUGIN_CODE Hkys
        FORMATS VST3
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Synth
    )
    target_sources(HMPSKeys PRIVATE HmpsKeysPlugin.cpp KeyboardInstrument.h)
    target_link_libraries(HMPSKeys PRIVATE HmpsPianoData juce::juce_audio_utils)
    target_compile_definitions(HMPSKeys PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
endif()

juce_add_gui_app(EducationalDAW
    PRODUCT_NAME "Educational DAW Engine"
    VERSION "1.0.0"
    BUNDLE_ID "com.education.dawengine"
    IS_COPY_PROTECTED FALSE
)

target_sources(EducationalDAW PRIVATE
    Source/Main.cpp
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    PianoLesson.h
    TutorialGameComponent.h
    StepLessonComponent.h
    SessionArrangement.h
    TransportButton.h
    PluginSupport.h
    TracktionSession.h
    ExternalMidiInput.h
    KeyboardInstrument.h
    SoundPicker.h
)

target_link_libraries(EducationalDAW PRIVATE
    HmpsPianoData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
)

if(HMPS_WITH_TRACKTION)
    target_link_libraries(EducationalDAW PRIVATE tracktion_engine)
endif()

# No embedded browser or network client is used by this instrument.
target_compile_definitions(EducationalDAW PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
juce_generate_juce_header(EducationalDAW)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(TARGETS EducationalDAW RUNTIME DESTINATION bin)
endif()

# Build the deliverable instrument alongside the app without installing into system folders.
if(HMPS_BUILD_VST3)
    add_dependencies(EducationalDAW HMPSKeys_VST3)
endif()
