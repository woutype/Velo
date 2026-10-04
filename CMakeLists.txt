cmake_minimum_required(VERSION 3.22)
project(Velo VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/asiosdk")
    file(GLOB_RECURSE ASIO_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/asiosdk/*iasiodrv.h")
    if(ASIO_HEADER)
        get_filename_component(ASIO_COMMON_DIR "${ASIO_HEADER}" DIRECTORY)
        get_filename_component(ASIO_ROOT_DIR "${ASIO_COMMON_DIR}" DIRECTORY)
        set(JUCE_ASIO_SDK_PATH "${ASIO_ROOT_DIR}" CACHE PATH "" FORCE)
    endif()
else()
    FetchContent_Declare(
        asiosdk
        URL https://download.steinberg.net/sdk_downloads/asiosdk_2.3.3_2019-06-14.zip
    )
    FetchContent_MakeAvailable(asiosdk)

    file(GLOB_RECURSE ASIO_HEADER "${asiosdk_SOURCE_DIR}/*iasiodrv.h")
    if(ASIO_HEADER)
        get_filename_component(ASIO_COMMON_DIR "${ASIO_HEADER}" DIRECTORY)
        get_filename_component(ASIO_ROOT_DIR "${ASIO_COMMON_DIR}" DIRECTORY)
        set(JUCE_ASIO_SDK_PATH "${ASIO_ROOT_DIR}" CACHE PATH "" FORCE)
    endif()
endif()

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(Velo
    PRODUCT_NAME "Velo Audio"
)

target_sources(Velo PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/PluginManager.cpp
)

target_include_directories(Velo PRIVATE
    Source
    "${ASIO_COMMON_DIR}"
    "${ASIO_ROOT_DIR}/host/pc"
    "${ASIO_ROOT_DIR}/host"
)

target_compile_definitions(Velo PRIVATE
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_PLUGINHOST_VST3=1
)

target_link_libraries(Velo PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
)