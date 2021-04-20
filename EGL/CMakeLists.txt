cmake_minimum_required(VERSION 3.0.2)

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

add_library(egl STATIC
    ${EGL_SOURCES})

target_include_directories(egl PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include)
add_definitions(-DEGLAPI=)
option(EGL_NO_GLEW "Do not use GLEW" OFF)
if(EGL_NO_GLEW)
  add_definitions(-DEGL_NO_GLEW)
endif()
if(UNIX AND NOT APPLE AND EGL_UNIX_USE_WAYLAND)
  add_definitions(-DWL_EGL_PLATFORM)
endif()
