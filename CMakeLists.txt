cmake_minimum_required(VERSION 3.22)

project(AI-Comp-Interface
    VERSION     1.0.0
    LANGUAGES   CXX C
)

# ==============================================================================
#  Locate JUCE
# ==============================================================================
# Pass -DJUCE_DIR=/path/to/JUCE on the command line, or let the script
# auto-detect from common locations.
#
# JUCE_DIR must point to the JUCE *root* (the folder that contains
# CMakeLists.txt and modules/).
# ==============================================================================

if(NOT DEFINED JUCE_DIR)
    # Local JUCE folder in this repository (preferred)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    # Relative to this CMakeLists.txt (interface/) — matches ../../juce from .jucer
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../juce/CMakeLists.txt")
        set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../juce")
    # ~/Documents/JUCE (Projucer default install on macOS)
    elseif(EXISTS "$ENV{HOME}/Documents/JUCE/CMakeLists.txt")
        set(JUCE_DIR "$ENV{HOME}/Documents/JUCE")
    # D:\JUCE (Windows, from .jucer VS2026 exporter)
    elseif(EXISTS "D:/JUCE/CMakeLists.txt")
        set(JUCE_DIR "D:/JUCE")
    else()
        message(FATAL_ERROR
            "JUCE not found. Set -DJUCE_DIR=/path/to/JUCE on the CMake command line.\n"
            "JUCE_DIR must point to the JUCE root (the folder containing CMakeLists.txt and modules/)."
        )
    endif()
endif()

message(STATUS "Using JUCE from: ${JUCE_DIR}")
add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")

# ==============================================================================
#  GUI Application Target
# ==============================================================================

juce_add_gui_app(AICompInterface
    PRODUCT_NAME                "AI-Comp-Interface"
    VERSION                     "1.0.0"
    BUNDLE_ID                   "com.yourcompany.AICompInterface"
    MICROPHONE_PERMISSION_ENABLED   TRUE
    MICROPHONE_PERMISSION_TEXT      "This app requires microphone access for vocal recording."
    NEEDS_STORE_KIT             FALSE
)

juce_generate_juce_header(AICompInterface)

# ==============================================================================
#  Source Files
# ==============================================================================

target_sources(AICompInterface PRIVATE
    # Main entry point
    interface/Source/Main.cpp

    # Core component (split across several translation units)
    interface/Source/MainComponent.cpp
    interface/Source/MainComponent.h
    interface/Source/MainComponent_AudioAndRecording.cpp
    interface/Source/MainComponent_Comping.cpp
    interface/Source/MainComponent_Interaction.cpp
    interface/Source/MainComponent_Saving.cpp
    interface/Source/MainComponent_Views.cpp

    # Custom UI (Neon look-and-feel)
    interface/Source/NeonUI.cpp
    interface/Source/NeonUI.h

    # Project persistence
    interface/Source/ProjectState.cpp
    interface/Source/ProjectState.h

    # Realtime comp audition
    interface/Source/CompedAuditionSource.cpp
    interface/Source/CompedAuditionSource.h

    # Utility header (paths, Python helpers)
    interface/Source/AppPaths.h
)

# ==============================================================================
#  C++ Standard & Compile Options
# ==============================================================================

target_compile_features(AICompInterface PRIVATE cxx_std_17)

# ==============================================================================
#  Preprocessor Definitions
# ==============================================================================
# Most JUCE_MODULE_AVAILABLE_* and JUCE_STANDALONE_APPLICATION defines are
# added automatically by the JUCE CMake helpers.  Only project-specific
# options need to be listed here.

target_compile_definitions(AICompInterface PUBLIC
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_ASIO=1
    JUCE_ASIO_USE_EXTERNAL_SDK=0
    JUCE_WEB_BROWSER=0
)

# ==============================================================================
#  Custom JUCE Module: juce_audio_processors_headless
# ==============================================================================
# This is a non-standard module that lives alongside the regular JUCE modules.
# If it exists, register it so it can be linked below.

set(_headless_module_path "${JUCE_DIR}/modules/juce_audio_processors_headless")
if(EXISTS "${_headless_module_path}")
    juce_add_module("${_headless_module_path}")
    message(STATUS "Found custom module: juce_audio_processors_headless")
else()
    message(STATUS
        "Optional custom module juce_audio_processors_headless not found at:\n"
        "  ${_headless_module_path}\n"
        "Continuing without it. If needed, set JUCE_DIR so that\n"
        "  \${JUCE_DIR}/modules/juce_audio_processors_headless exists."
    )
endif()

# ==============================================================================
#  Link JUCE Modules
# ==============================================================================

target_link_libraries(AICompInterface PRIVATE
    # Standard JUCE modules
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra

    # Recommended JUCE helper targets
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    juce::juce_recommended_lto_flags
)

# Link the custom module only if it was found
if(TARGET juce_audio_processors_headless)
    target_link_libraries(AICompInterface PRIVATE juce_audio_processors_headless)
endif()

# ==============================================================================
#  Platform-Specific Settings
# ==============================================================================

if(APPLE)
    # macOS deployment target (matches Xcode project)
    set_target_properties(AICompInterface PROPERTIES
        MACOSX_DEPLOYMENT_TARGET "10.13"
    )

    # macOS frameworks (JUCE handles most of these automatically through
    # module dependencies, but we list them explicitly here to match the
    # Xcode project exactly)
    target_link_libraries(AICompInterface PRIVATE
        "-framework Accelerate"
        "-framework AudioToolbox"
        "-framework Cocoa"
        "-framework CoreAudio"
        "-framework CoreAudioKit"
        "-framework CoreMIDI"
        "-framework DiscRecording"
        "-framework Foundation"
        "-framework IOKit"
        "-framework QuartzCore"
        "-framework Security"
        "-framework WebKit"
    )

    # Weak-linked frameworks (Metal / MetalKit — available on 10.11+ but
    # not required on older deployment targets)
    target_link_libraries(AICompInterface PRIVATE
        "-weak_framework Metal"
        "-weak_framework MetalKit"
    )
endif()

