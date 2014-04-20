cmake_minimum_required(VERSION 2.6)
project (Comet)

set(CITYHASH_RELEASE "1.1.1")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/dep/cityhash/lib/libcityhash.la)
	message(STATUS "cityhash ${CITYHASH_RELEASE} already built")
else()
	set(CITYHASH_DIR ${CMAKE_BINARY_DIR}/build/cityhash-${CITYHASH_RELEASE})
	file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/build)
	file(DOWNLOAD http://cityhash.googlecode.com/files/cityhash-${CITYHASH_RELEASE}.tar.gz ${CMAKE_BINARY_DIR}/build/cityhash-${CITYHASH_RELEASE}.tar.gz SHOW_PROGRESS)
	execute_process(COMMAND tar xzf ${CMAKE_BINARY_DIR}/build/cityhash-${CITYHASH_RELEASE}.tar.gz WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/build/ )
	execute_process(COMMAND ${CITYHASH_DIR}/configure --prefix=${CMAKE_CURRENT_SOURCE_DIR}/dep/cityhash WORKING_DIRECTORY ${CITYHASH_DIR} )
	execute_process(COMMAND make install WORKING_DIRECTORY ${CITYHASH_DIR} )
endif()

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

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/dep/cityhash/lib)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/dep/cityhash/include)

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
target_link_libraries (Comet event cityhash)
