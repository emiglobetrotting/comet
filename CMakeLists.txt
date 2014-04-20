cmake_minimum_required(VERSION 2.6)
project (Comet)

# https://github.com/facebook/hhvm/blob/master/CMake/FindLibEvent.cmake
set(LibEvent_EXTRA_PREFIXES /usr/local /opt/local /usr)
foreach(prefix ${LibEvent_EXTRA_PREFIXES})
	list(APPEND LibEvent_INCLUDE_PATHS "${prefix}/include")
	list(APPEND LibEvent_LIB_PATHS "${prefix}/lib")
endforeach()

find_path(LIBEVENT_INCLUDE_DIR event.h PATHS ${LibEvent_INCLUDE_PATHS})
find_library(LIBEVENT_LIB NAMES event PATHS ${LibEvent_LIB_PATHS})

link_directories(${LibEvent_LIB_PATHS})
include_directories(${LibEvent_INCLUDE_PATHS})

set(GCC_COVERAGE_COMPILE_FLAGS "-Wdeprecated-writable-strings")
set(CLANG_COVERAGE_COMPILE_FLAGS "-Wdeprecated-writable-strings")

set(dir ${CMAKE_CURRENT_SOURCE_DIR}/build)
set(EXECUTABLE_OUTPUT_PATH ${dir} CACHE PATH "Build directory" FORCE)
set(LIBRARY_OUTPUT_PATH ${dir} CACHE PATH "Build directory" FORCE)

set (COMET_VERSION_MAJOR 0)
set (COMET_VERSION_MINOR 1)

configure_file (
  "${PROJECT_SOURCE_DIR}/src/config.h.in"
  "${PROJECT_BINARY_DIR}/src/config.h"
  )

add_executable(Comet src/comet.cpp)
target_link_libraries (Comet event)
