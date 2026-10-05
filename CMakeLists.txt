cmake_minimum_required(VERSION 3.22)
project(Velo VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VELO_ENABLE_ASIO "Build with ASIO support (downloads the Steinberg ASIO SDK - see its licence)" ON)

include(FetchContent)

# ---------------------------------------------------------------- ASIO SDK ---
# Put an extracted SDK into ./asiosdk to avoid the download.
set(ASIO_ROOT_DIR "")
if(VELO_ENABLE_ASIO AND WIN32)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asiosdk")
        set(_asio_search "${CMAKE_CURRENT_SOURCE_DIR}/asiosdk")
    else()
        FetchContent_Declare(asiosdk
            URL https://download.steinberg.net/sdk_downloads/asiosdk_2.3.3_2019-06-14.zip)
        FetchContent_MakeAvailable(asiosdk)
        set(_asio_search "${asiosdk_SOURCE_DIR}")
    endif()
    file(GLOB_RECURSE _asio_header "${_asio_search}/*iasiodrv.h")
    if(_asio_header)
        list(GET _asio_header 0 _asio_header)
        get_filename_component(_asio_common "${_asio_header}" DIRECTORY)
        get_filename_component(ASIO_ROOT_DIR "${_asio_common}" DIRECTORY)
        set(JUCE_ASIO_SDK_PATH "${ASIO_ROOT_DIR}" CACHE PATH "" FORCE)
    else()
        message(WARNING "ASIO SDK headers not found - building without ASIO")
    endif()
endif()

# -------------------------------------------------------------------- JUCE ---
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

# UI (HTML/CSS/JS) is embedded into the exe and served to the WebView2 control.
juce_add_binary_data(VeloUI SOURCES
    ui/index.html
    ui/app.css
    ui/app.js)

juce_add_gui_app(Velo
    PRODUCT_NAME "Velo Audio"
    NEEDS_WEBVIEW2 TRUE)

target_sources(Velo PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/PluginManager.cpp
    Source/PluginScanner.cpp)

target_include_directories(Velo PRIVATE Source)
if(ASIO_ROOT_DIR)
    target_include_directories(Velo PRIVATE "${_asio_common}" "${ASIO_ROOT_DIR}/host/pc" "${ASIO_ROOT_DIR}/host")
endif()

target_compile_definitions(Velo PRIVATE
    JUCE_WASAPI=1
    JUCE_WASAPI_EXCLUSIVE=1
    JUCE_DIRECTSOUND=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

if(ASIO_ROOT_DIR)
    target_compile_definitions(Velo PRIVATE JUCE_ASIO=1)
endif()

target_link_libraries(Velo PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_data_structures
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_gui_extra
    VeloUI
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

if(MSVC)
    target_compile_options(Velo PRIVATE /MP /fp:fast)
endif()
