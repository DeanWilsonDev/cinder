cmake_minimum_required(VERSION 3.30)

project(cinder VERSION 0.1.0 LANGUAGES CXX)

if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
   OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18))
  message(FATAL_ERROR "Cinder needs GCC 14 or later or Clang 18 or later; found ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

if(NOT APPLE AND NOT (UNIX AND NOT APPLE))
  message(FATAL_ERROR "Cinder only supports macOS and Linux.")
endif()

# ---------------------------------------------------------------------------
# Dependencies (Firefly, Cimmerian, Amanuensis — DeanWilsonDev org)
# ---------------------------------------------------------------------------

include(FetchContent)

FetchContent_Declare(
  cimmerian
  GIT_REPOSITORY https://github.com/DeanWilsonDev/cimmerian.git
  GIT_TAG main)

FetchContent_Declare(
  firefly
  GIT_REPOSITORY https://github.com/DeanWilsonDev/firefly.git
  GIT_TAG main)

FetchContent_Declare(
  amanuensis
  GIT_REPOSITORY https://github.com/DeanWilsonDev/amanuensis.git
  GIT_TAG main)

set(CIMMERIAN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(AMANUENSIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# Cimmerian first: Firefly's own CMakeLists.txt vendors Amanuensis via git
# submodule (external/amanuensis) and add_subdirectory()'s it unconditionally
# with no `if(NOT TARGET amanuensis)` guard, so declaring our own top-level
# Amanuensis dependency *after* Firefly and only populating it conditionally
# is the only way to avoid a duplicate `add_library(amanuensis ...)` CMake
# error. See docs/firefly.md item 1 for the upstream fix this works around.
FetchContent_MakeAvailable(cimmerian firefly)

if(NOT TARGET amanuensis)
  FetchContent_MakeAvailable(amanuensis)
endif()

# Firefly's Sinks::ISink has a non-virtual destructor despite being destroyed
# polymorphically via unique_ptr<ISink> (firefly/include/firefly/sinks/i-sink.hpp),
# which trips -Wdelete-abstract-non-virtual-dtor in any translation unit that
# includes <firefly/log.hpp>. Narrowly suppressed here pending the upstream fix
# requested in docs/firefly.md item 2 — not a blanket warning suppression.
set(CINDER_WARNING_FLAGS -Wall -Wextra)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND CINDER_WARNING_FLAGS
       -Wno-delete-abstract-non-virtual-dtor
       -Wno-delete-non-abstract-non-virtual-dtor)
endif()

# ---------------------------------------------------------------------------
# Platform audio backend selection
# ---------------------------------------------------------------------------

if(APPLE)
  set(CINDER_PLATFORM_LIBRARIES "-framework AudioToolbox" "-framework CoreFoundation")
elseif(UNIX)
  find_package(ALSA REQUIRED)
  set(CINDER_PLATFORM_LIBRARIES ALSA::ALSA)
endif()

# ---------------------------------------------------------------------------
# cinder_core library
# ---------------------------------------------------------------------------

add_library(
  cinder_core STATIC
  src/backend/audio-backend.cpp
  src/backend/audio-decoder.cpp
  src/mixer/cinder-mixer.cpp
  src/loader/audio-state-loader.cpp)

target_include_directories(
  cinder_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                     $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

target_link_libraries(cinder_core PUBLIC firefly amanuensis PRIVATE ${CINDER_PLATFORM_LIBRARIES})

target_compile_options(cinder_core PRIVATE ${CINDER_WARNING_FLAGS})

# ---------------------------------------------------------------------------
# Subprojects
# ---------------------------------------------------------------------------

enable_testing()
add_subdirectory(tests)
add_subdirectory(driver)
