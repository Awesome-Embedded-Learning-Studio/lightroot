cmake_minimum_required(VERSION 3.20)

project(lightroot
  VERSION 0.1.0
  DESCRIPTION "A more usable Buildroot-style embedded rootfs builder"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LIGHTROOT_BUILD_TESTS "Build the GoogleTest suite" ON)
option(LIGHTROOT_BUILD_CLI "Build the lightroot CLI" ON)

# Sensible warnings for our own targets (third-party deps compile separately).
add_library(lightroot_warnings INTERFACE)
target_compile_options(lightroot_warnings INTERFACE
  $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:
      -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor>)

# CPM.cmake bootstrap + pinned dependencies.
include(${CMAKE_SOURCE_DIR}/cmake/cpm_bootstrap.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/dependencies.cmake)

add_subdirectory(src/core)

if(LIGHTROOT_BUILD_CLI)
  add_subdirectory(src/cli)
endif()

enable_testing()
if(LIGHTROOT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
