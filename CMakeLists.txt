cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

project(OneRSS VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT ANDROID)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LINUX TRUE)
endif()

if(ANDROID)
  set(ONERSS_BUILD_DESKTOP ON CACHE BOOL "" FORCE)
  set(ONERSS_BUILD_BACKEND OFF CACHE BOOL "" FORCE)
elseif (LINUX)
  option(ONERSS_BUILD_BACKEND "Build the backend application" ON)
  option(ONERSS_BUILD_DESKTOP "Build the desktop application" ON)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FetchContent)
include(OneRssFetch)

onerss_fetch(logfault
  GIT_REPOSITORY https://github.com/jgaa/logfault.git
  GIT_TAG        master
)

if (NOT ANDROID)
  find_package(OpenSSL REQUIRED)
endif()

if(ONERSS_BUILD_BACKEND OR ONERSS_BUILD_DESKTOP)
  add_subdirectory(proto)
endif()

if(ONERSS_BUILD_BACKEND)
  add_subdirectory(backend)
endif()

if(ONERSS_BUILD_DESKTOP)
  add_subdirectory(desktop)
endif()
