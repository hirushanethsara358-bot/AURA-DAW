cmake_minimum_required(VERSION 3.24)

project(
  AURA_DAW
  VERSION 1.0.0
  DESCRIPTION "AURA Digital Audio Workstation"
  LANGUAGES C CXX
)

# ---------------------------------------------------------------- options
option(AURA_ENABLE_QT6 "Build Qt6/QML user interface" OFF)
option(AURA_ENABLE_JUCE "Enable the experimental JUCE dependency (audio adapter is not implemented)" OFF)
option(AURA_BUILD_TESTS "Build unit tests" ON)
option(AURA_BUILD_INSTALLER "Build Windows installer target" OFF)
option(AURA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# ------------------------------------------------------------- language
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
  if(AURA_WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(AURA_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
endif()

# ------------------------------------------------------------- version
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuraVersion.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/Aura/Version.hpp"
  @ONLY
)

# ------------------------------------------------------------- deps
include(FetchContent)

# JSON (project serialization) — header-only, always needed.
FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

if(AURA_BUILD_TESTS)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  include(CTest)
  enable_testing()
endif()

if(AURA_ENABLE_QT6)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Concurrent)
  qt_standard_project_setup()
  if(QT_KNOWN_POLICY_QTP0001)
    qt_policy(SET QTP0001 NEW)
  endif()
  if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
  endif()
endif()

if(AURA_ENABLE_JUCE)
  # JUCE is an optional dependency for the experimental Windows integration.
  # The current audio engine does not yet contain a JUCE device adapter.
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    message(FATAL_ERROR
      "AURA_ENABLE_JUCE=ON requires third_party/JUCE/CMakeLists.txt. "
      "The repository does not currently include the JUCE submodule; see docs/BUILD.md.")
  endif()
  add_subdirectory(third_party/JUCE EXCLUDE_FROM_ALL)
endif()

# ------------------------------------------------------------- modules
add_subdirectory(Engine)
add_subdirectory(Plugins)
add_subdirectory(AI)
add_subdirectory(Instruments)
add_subdirectory(Project)
add_subdirectory(Session)

if(AURA_ENABLE_QT6)
  add_subdirectory(UI)
endif()

if(AURA_BUILD_TESTS)
  add_subdirectory(Tests)
endif()

if(AURA_BUILD_INSTALLER)
  add_subdirectory(Installer)
endif()

# ------------------------------------------------------------- summary
message(STATUS "")
message(STATUS "AURA DAW ${PROJECT_VERSION} configuration:")
message(STATUS "  Qt6 UI ............ ${AURA_ENABLE_QT6}")
message(STATUS "  JUCE dependency ... ${AURA_ENABLE_JUCE}")
message(STATUS "  Tests ............. ${AURA_BUILD_TESTS}")
message(STATUS "  Installer ......... ${AURA_BUILD_INSTALLER}")
message(STATUS "")
