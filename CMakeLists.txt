cmake_minimum_required(VERSION 3.10)

project(EGL C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (UNIX AND NOT APPLE)
# Defines stub functions using typedefs for Wayland display server protocol
  option(EGL_UNIX_USE_WAYLAND "Define functions for Wayland platform" OFF)
endif()

if(WIN32)
  set(EGL_PLATFORM_SOURCES
      ${CMAKE_CURRENT_LIST_DIR}/src/egl_windows.cpp)
else()
  if(UNIX AND NOT APPLE)
    if (ANDROID OR EGL_UNIX_USE_WAYLAND)
      set(EGL_PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/egl_wayland_stub.cpp)
    else()
      set(EGL_PLATFORM_SOURCES
          ${CMAKE_CURRENT_LIST_DIR}/src/egl_x11.cpp)
    endif()
  endif()
endif()

set(EGL_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/egl.c
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_common.cpp
    ${EGL_PLATFORM_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_internal.h
    ${CMAKE_CURRENT_LIST_DIR}/include/EGL/egl.h
    ${CMAKE_CURRENT_LIST_DIR}/include/EGL/egl.h
    ${CMAKE_CURRENT_LIST_DIR}/include/EGL/eglext.h
    ${CMAKE_CURRENT_LIST_DIR}/include/EGL/eglplatform.h
    ${CMAKE_CURRENT_LIST_DIR}/include/KHR/khrplatform.h)

add_library(EGL STATIC
    ${EGL_SOURCES})

set_target_properties(EGL PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY         ${CMAKE_SOURCE_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/lib
    PREFIX                           "lib")

target_include_directories(EGL PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_definitions(EGL PUBLIC KHRONOS_STATIC) # PUBLIC - make it go down to every target linking EGL
add_definitions(-DEGLAPI=)
if(UNIX AND NOT APPLE AND EGL_UNIX_USE_WAYLAND)
  add_definitions(-DWL_EGL_PLATFORM)
endif()

add_subdirectory(examples)
