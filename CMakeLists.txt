cmake_minimum_required(VERSION 3.22)
project(EducationalDAW VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point this at a real JUCE checkout when deploying to another machine.
set(HMPS_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "JUCE source directory")
add_subdirectory("${HMPS_JUCE_DIR}" bin/JUCE)

# The current keyboard/lesson prototype does not call Tracktion APIs.
option(HMPS_WITH_TRACKTION "Build Tracktion for future arranger functionality" ON)
if(HMPS_WITH_TRACKTION)
    set(HMPS_TRACKTION_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tracktion_engine" CACHE PATH "Tracktion source directory")
    add_subdirectory("${HMPS_TRACKTION_DIR}" bin/tracktion_engine)
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
)

target_link_libraries(EducationalDAW PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
)

if(HMPS_WITH_TRACKTION)
    target_link_libraries(EducationalDAW PRIVATE tracktion_engine)
endif()

# No embedded browser or network client is used by this instrument.
target_compile_definitions(EducationalDAW PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
juce_generate_juce_header(EducationalDAW)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(TARGETS EducationalDAW RUNTIME DESTINATION bin)
endif()
