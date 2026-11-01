cmake_minimum_required(VERSION 3.16)

project(GTAscii VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AV_BUILD_TESTS "Build the unit tests" ON)
option(AV_BUILD_DEMOS "Build the demo applications" ON)
option(AV_STATIC_RUNTIME "Statically link the C/C++ runtime so release binaries run anywhere" ON)

if(MSVC AND AV_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Shared warning settings, linked privately by every target we own.
add_library(av_warnings INTERFACE)
if(MSVC)
    target_compile_options(av_warnings INTERFACE /W4 /permissive- /utf-8)
    # Portable standard functions (getenv, sscanf) are fine; don't nag for _s variants.
    target_compile_definitions(av_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(av_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
endif()

# Common settings for executables.
function(av_configure_executable target)
    target_link_libraries(${target} PRIVATE av_warnings)
    if(MINGW AND AV_STATIC_RUNTIME)
        target_link_options(${target} PRIVATE -static)
    elseif(UNIX AND NOT APPLE AND AV_STATIC_RUNTIME)
        target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endfunction()

add_subdirectory(engine)
add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(client)

if(AV_BUILD_DEMOS)
    add_subdirectory(demos)
endif()

if(AV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
