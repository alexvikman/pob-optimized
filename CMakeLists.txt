cmake_minimum_required(VERSION 3.24)

project(PathOfBuildingNative
    VERSION 0.1.1
    DESCRIPTION "Native host for Path of Building (macOS, Windows)"
    LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version" FORCE)
    endif()
endif()

# Windows: link the C/C++ runtime statically (/MT) for every target, so the exe
# runs on a clean Windows without the Visual C++ Redistributable. LuaJIT's
# msvcbuild.bat static build already uses /MT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

enable_testing()

add_subdirectory(host)
