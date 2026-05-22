cmake_minimum_required(VERSION 3.22)

project(of VERSION 0.1.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_PATH "../Granny/JUCE" CACHE PATH "Path to a local JUCE checkout")
add_subdirectory("${JUCE_PATH}" JUCE)

juce_add_gui_app(of
    PRODUCT_NAME "of"
    COMPANY_NAME "of"
    BUNDLE_ID "space.matd.of")

target_sources(of
    PRIVATE
        Source/DemoScripts.cpp
        Source/SuperColliderHost.cpp
        Source/Main.cpp)

target_compile_definitions(of
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(of
    PRIVATE
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
