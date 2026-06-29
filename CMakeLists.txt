# Forge — top-level build
#
# Phase 0: prove the toolchain and get first sound out of the engine.
# Builds a minimal JUCE GUI app linked against Tracktion Engine (which bundles JUCE).

cmake_minimum_required(VERSION 3.22)

project(Forge VERSION 0.0.1 LANGUAGES C CXX)

# Match the Tracktion examples: static MSVC runtime so the exe is self-contained.
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS YES)
option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Enable Module Source Groups" ON)

# Bring in JUCE first (provides the juce_* CMake helpers), then the Tracktion
# modules dir, which calls juce_add_modules(...) to create the tracktion::* targets.
add_subdirectory(libs/tracktion_engine/modules/juce ${CMAKE_BINARY_DIR}/juce_build)
add_subdirectory(libs/tracktion_engine/modules     ${CMAKE_BINARY_DIR}/tracktion_build)

juce_add_gui_app(Forge
    PRODUCT_NAME "Forge"
    COMPANY_NAME "Forge"
    BUNDLE_ID    "com.forge.daw")

juce_generate_juce_header(Forge)

target_sources(Forge PRIVATE
    src/main.cpp
    src/services/files/ProjectSession.cpp
    src/engine/RecordController.cpp
    src/ui/transport/TransportBar.cpp
    src/ui/arrange/ArrangeView.cpp)

target_include_directories(Forge PRIVATE src)

target_compile_features(Forge PRIVATE cxx_std_20)

# Module flags used by the Tracktion examples (kept minimal for Phase 0).
target_compile_definitions(Forge PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

target_link_libraries(Forge PRIVATE
    tracktion::tracktion_core
    tracktion::tracktion_engine
    tracktion::tracktion_graph
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_recommended_warning_flags)
