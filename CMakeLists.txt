cmake_minimum_required(VERSION 3.10)

project(EGL C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR "This EGL implementation only supports Windows.")
endif()

find_package(Vulkan REQUIRED)
set(EGL_PLATFORM_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_windows.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_windows_vk.cpp)

set(EGL_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/egl.c
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_globals.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_config.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_display.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_context.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_surface.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_sync.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_image.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_api.cpp
    ${EGL_PLATFORM_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_internal.h
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

target_link_libraries(EGL PUBLIC Vulkan::Vulkan)
target_include_directories(EGL PRIVATE ${Vulkan_INCLUDE_DIRS})

target_include_directories(EGL PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_definitions(EGL PUBLIC KHRONOS_STATIC) # PUBLIC - make it go down to every target linking EGL
add_definitions(-DEGLAPI=)
add_subdirectory(examples)
