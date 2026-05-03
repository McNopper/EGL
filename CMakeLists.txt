cmake_minimum_required(VERSION 3.10)

project(EGL C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # Windows — WGL + Vulkan HDR (fully implemented)
    find_package(Vulkan REQUIRED)
    set(EGL_PLATFORM_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/egl_windows.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/egl_windows_vk.cpp)

elseif(EMSCRIPTEN)
    # WebAssembly / Emscripten — future egl_emscripten.cpp backend
    message(STATUS "EGL: Emscripten/WebGL backend not yet implemented")
    set(EGL_PLATFORM_SOURCES "")

elseif(CMAKE_SYSTEM_NAME STREQUAL "QNX")
    # QNX — Screen API — future egl_qnx.cpp backend
    message(STATUS "EGL: QNX Screen API backend not yet implemented")
    set(EGL_PLATFORM_SOURCES "")

elseif(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    # Haiku OS — future egl_haiku.cpp backend
    message(STATUS "EGL: Haiku backend not yet implemented")
    set(EGL_PLATFORM_SOURCES "")

elseif(CMAKE_SYSTEM_NAME STREQUAL "Fuchsia")
    # Fuchsia OS — future egl_fuchsia.cpp backend
    message(STATUS "EGL: Fuchsia backend not yet implemented")
    set(EGL_PLATFORM_SOURCES "")

elseif(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    # HarmonyOS (OpenHarmony) — future egl_harmonyos.cpp backend
    message(STATUS "EGL: HarmonyOS backend not yet implemented")
    set(EGL_PLATFORM_SOURCES "")

elseif(APPLE)
    # macOS / iOS — CGL / EAGL / Metal — future egl_apple.cpp backend
    message(STATUS "EGL: Apple (macOS/iOS) backend not yet implemented")
    set(EGL_PLATFORM_SOURCES "")

elseif(ANDROID)
    # Android — EGL passthrough — future egl_android.cpp backend
    message(STATUS "EGL: Android backend not yet implemented")
    set(EGL_PLATFORM_SOURCES "")

elseif(UNIX)
    # Linux / *BSD / Unix-like — select sub-platform via compile-time defines
    # Pass -DWL_EGL_PLATFORM=1, -D__GBM__=1, or -DUSE_OZONE=1 to select.
    # Default falls back to X11/GLX.
    if(WL_EGL_PLATFORM)
        message(STATUS "EGL: Wayland EGL backend not yet implemented")
        add_compile_definitions(WL_EGL_PLATFORM)
        set(EGL_PLATFORM_SOURCES "")
    elseif(GBM_PLATFORM)
        message(STATUS "EGL: GBM/DRM backend not yet implemented")
        add_compile_definitions(__GBM__)
        set(EGL_PLATFORM_SOURCES "")
    elseif(OZONE_PLATFORM)
        message(STATUS "EGL: Ozone backend not yet implemented")
        add_compile_definitions(USE_OZONE)
        set(EGL_PLATFORM_SOURCES "")
    else()
        message(STATUS "EGL: X11/GLX backend (default Unix)")
        cmake_policy(SET CMP0072 NEW)
        find_package(X11 REQUIRED)
        find_package(OpenGL REQUIRED)
        add_compile_definitions(USE_X11)
        set(EGL_PLATFORM_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/egl_x11_glx.cpp)
    endif()

else()
    message(FATAL_ERROR "EGL: Unsupported platform. Supported: WIN32, EMSCRIPTEN, QNX, Haiku, Fuchsia, OHOS, APPLE, ANDROID, UNIX")
endif()

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

if(WIN32)
    target_link_libraries(EGL PUBLIC Vulkan::Vulkan)
    target_include_directories(EGL PRIVATE ${Vulkan_INCLUDE_DIRS})
elseif(UNIX AND NOT WL_EGL_PLATFORM AND NOT GBM_PLATFORM AND NOT OZONE_PLATFORM)
    target_link_libraries(EGL PUBLIC ${CMAKE_DL_LIBS} X11::X11 OpenGL::GL)
    target_include_directories(EGL PRIVATE ${X11_INCLUDE_DIR})
endif()

target_include_directories(EGL PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_definitions(EGL PUBLIC KHRONOS_STATIC) # PUBLIC - make it go down to every target linking EGL
add_definitions(-DEGLAPI=)
add_subdirectory(examples)
