cmake_minimum_required(VERSION 3.24)

project(
  RingDownAnalysisCpp
  VERSION 0.1.0
  DESCRIPTION "Modern C++20 implementation of the RingDownAnalysis core pipeline"
  LANGUAGES CXX)

include(GNUInstallDirs)
include(cmake/ProjectOptions.cmake)

option(RINGDOWN_BUILD_TESTS "Build RingDownAnalysisCpp tests" ON)
option(RINGDOWN_BUILD_BENCHMARKS "Build RingDownAnalysisCpp benchmarks" ON)
option(RINGDOWN_BUILD_APPS "Build RingDownAnalysisCpp command-line tools" ON)

add_library(ringdownanalysis)
add_library(RingDownAnalysisCpp::ringdownanalysis ALIAS ringdownanalysis)

target_sources(
  ringdownanalysis
  PRIVATE src/version.cpp
  PUBLIC FILE_SET HEADERS
         BASE_DIRS include
         FILES include/ringdown/ringdown.hpp include/ringdown/version.hpp)

target_compile_features(ringdownanalysis PUBLIC cxx_std_20)
target_include_directories(
  ringdownanalysis
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
ringdown_apply_project_options(ringdownanalysis)

if(RINGDOWN_BUILD_APPS)
  add_subdirectory(apps)
endif()

if(RINGDOWN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(RINGDOWN_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
