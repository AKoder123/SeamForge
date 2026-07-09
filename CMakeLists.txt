cmake_minimum_required(VERSION 3.22)
project(SeamForgeReverse VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(SEAMFORGE_BUILD_GUI   "Build the Qt6 GUI application" ON)
option(SEAMFORGE_BUILD_TESTS "Build unit tests"              ON)

find_package(Eigen3 3.3 REQUIRED)
find_package(assimp REQUIRED)
find_package(nlohmann_json 3.10 REQUIRED)

add_subdirectory(src/core)
add_subdirectory(src/tools)

if(SEAMFORGE_BUILD_GUI)
  find_package(Qt6 QUIET COMPONENTS Widgets OpenGLWidgets)
  if(Qt6_FOUND)
    add_subdirectory(src/app)
  else()
    message(WARNING "Qt6 not found - GUI will not be built. Core, CLI and tests are unaffected.")
  endif()
endif()

if(SEAMFORGE_BUILD_TESTS)
  find_package(Catch2 3 QUIET)
  if(Catch2_FOUND)
    enable_testing()
    add_subdirectory(tests)
  else()
    message(WARNING "Catch2 v3 not found - tests will not be built.")
  endif()
endif()
