cmake_minimum_required(VERSION 3.20)

project(OmniOS
    VERSION 0.1.0
    DESCRIPTION "Play everything."
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# The compatibility engine, library model and package format. No UI toolkit
# dependency, so it builds and is testable anywhere the launcher is developed.
add_library(omnios_core STATIC
    src/omnios/Detector.cpp
    src/omnios/GameLibrary.cpp
    src/omnios/GameScanner.cpp
    src/omnios/Json.cpp
    src/omnios/Manifest.cpp
    src/omnios/Paths.cpp
    src/omnios/Platform.cpp
    src/omnios/Router.cpp)

target_include_directories(omnios_core PUBLIC src)

if(MSVC)
    target_compile_options(omnios_core PRIVATE /W4)
else()
    target_compile_options(omnios_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Command-line front end. Drives the core the same way the launcher will, so
# every phase is usable before the QML shell exists.
add_executable(omnictl src/omnictl/main.cpp)
target_link_libraries(omnictl PRIVATE omnios_core)

# The Phase 10 QML shell. Optional on purpose: Qt6 is not installed on the
# Windows host this profile is authored from, and the core plus its 68 tests
# must stay buildable there. The ISO build runs in an Arch container where Qt6
# is present, so the image always ships the real launcher.
option(OMNIOS_BUILD_LAUNCHER "Build the Qt6/QML launcher when Qt6 is available" ON)

if(OMNIOS_BUILD_LAUNCHER)
    find_package(Qt6 6.5 COMPONENTS Core Gui Quick QuickControls2 QUIET)
    if(Qt6_FOUND)
        message(STATUS "Qt6 ${Qt6_VERSION} found - building omni-launcher")
        add_subdirectory(src/launcher)
    else()
        message(STATUS "Qt6 not found - skipping omni-launcher (core and omnictl still build)")
    endif()
endif()

enable_testing()
add_subdirectory(tests)
