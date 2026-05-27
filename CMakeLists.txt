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
    set(EGL_BACKEND_SUFFIX "_GL_Windows_WGL_VK")

    # Optional: enable real OpenGL ES via Google ANGLE (delegated through libEGL).
    # ANGLE is provided via the vcpkg manifest (vcpkg.json at repo root).
    option(EGL_WIN_ENABLE_ANGLE "Enable OpenGL ES backend via ANGLE on Windows" ON)
    if(EGL_WIN_ENABLE_ANGLE)
        find_package(unofficial-angle CONFIG QUIET)
        if(unofficial-angle_FOUND)
            message(STATUS "EGL: ANGLE found via unofficial-angle CONFIG package")
            set(EGL_HAS_ANGLE ON)
            list(APPEND EGL_PLATFORM_SOURCES
                ${CMAKE_CURRENT_LIST_DIR}/src/egl_windows_angle.cpp)
            set(EGL_ES_BACKEND_SUFFIX "_ES_Windows_ANGLE")
        else()
            message(WARNING "EGL: EGL_WIN_ENABLE_ANGLE=ON but ANGLE package not found. "
                            "Run 'vcpkg install' or pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg.cmake>. "
                            "Disabling ANGLE for this build.")
            set(EGL_HAS_ANGLE OFF)
        endif()
    endif()

elseif(UNIX)
    # Linux / *BSD / Unix-like — select sub-platform via compile-time defines
    # Pass -DWL_EGL_PLATFORM=ON or -DGBM_PLATFORM=ON to select.
    # Default falls back to X11/GLX.  Add -DLINUX_VK=ON to enable Vulkan HDR on X11.
    if(WL_EGL_PLATFORM)
        message(STATUS "EGL: Wayland backend — GLX/XWayland + Vulkan")
        cmake_policy(SET CMP0072 NEW)
        find_package(Vulkan REQUIRED)
        find_package(OpenGL REQUIRED)
        find_package(X11 REQUIRED)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
        find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
        execute_process(
            COMMAND pkg-config --variable=pkgdatadir wayland-protocols
            OUTPUT_VARIABLE WAYLAND_PROTOCOLS_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
        set(XDG_SHELL_HDR "${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h")
        set(XDG_SHELL_SRC "${CMAKE_BINARY_DIR}/xdg-shell-protocol.c")
        add_custom_command(
            OUTPUT  ${XDG_SHELL_HDR}
            COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_XML} ${XDG_SHELL_HDR}
            DEPENDS ${XDG_SHELL_XML})
        add_custom_command(
            OUTPUT  ${XDG_SHELL_SRC}
            COMMAND ${WAYLAND_SCANNER} private-code ${XDG_SHELL_XML} ${XDG_SHELL_SRC}
            DEPENDS ${XDG_SHELL_XML})
        add_library(xdg_shell_protocol STATIC ${XDG_SHELL_SRC} ${XDG_SHELL_HDR})
        set_source_files_properties(${XDG_SHELL_SRC} ${XDG_SHELL_HDR} PROPERTIES GENERATED TRUE)
        target_include_directories(xdg_shell_protocol PUBLIC ${CMAKE_BINARY_DIR})
        add_compile_definitions(WL_EGL_PLATFORM)
        set(EGL_PLATFORM_SOURCES
            ${CMAKE_CURRENT_LIST_DIR}/src/egl_wayland.cpp
            ${CMAKE_CURRENT_LIST_DIR}/src/egl_linux_vk.cpp)
        set(EGL_BACKEND_SUFFIX "_GL_Linux_Wayland_VK")

        # Optional: enable real OpenGL ES via the system libEGL / libGLESv2
        # (Mesa / vendor driver). Libraries are loaded dynamically via dlopen.
        option(EGL_WAYLAND_ENABLE_GLES "Enable OpenGL ES backend via system libEGL/libGLESv2 on Wayland" ON)
        if(EGL_WAYLAND_ENABLE_GLES)
            message(STATUS "EGL: Wayland GLES backend enabled (system libEGL.so + libGLESv2.so)")
            set(EGL_HAS_GLES ON)
            list(APPEND EGL_PLATFORM_SOURCES
                ${CMAKE_CURRENT_LIST_DIR}/src/egl_linux_gles.cpp)
            set(EGL_ES_BACKEND_SUFFIX "_ES_Linux_Wayland_GLES")
        endif()
    elseif(GBM_PLATFORM)
        message(FATAL_ERROR "EGL: GBM/DRM backend has been removed. Use WL_EGL_PLATFORM for Wayland or the default X11/GLX backend.")
    else()
        message(STATUS "EGL: X11/GLX backend (default Unix)")
        cmake_policy(SET CMP0072 NEW)
        find_package(X11 REQUIRED)
        find_package(OpenGL REQUIRED)
        add_compile_definitions(USE_X11)
        option(LINUX_VK "Enable Vulkan HDR backend for X11/GLX" OFF)
        if(LINUX_VK)
            message(STATUS "EGL: X11/GLX + Vulkan HDR enabled (LINUX_VK)")
            find_package(Vulkan REQUIRED)
            add_compile_definitions(LINUX_VK)
            set(EGL_PLATFORM_SOURCES
                ${CMAKE_CURRENT_LIST_DIR}/src/egl_x11_glx.cpp
                ${CMAKE_CURRENT_LIST_DIR}/src/egl_linux_vk.cpp)
            set(EGL_BACKEND_SUFFIX "_GL_Linux_X11_GLX_VK")
        else()
            set(EGL_PLATFORM_SOURCES
                ${CMAKE_CURRENT_LIST_DIR}/src/egl_x11_glx.cpp)
            set(EGL_BACKEND_SUFFIX "_GL_Linux_X11_GLX")
        endif()

        # Optional: enable real OpenGL ES via the system libEGL / libGLESv2
        # (Mesa / vendor driver).  The libraries are loaded dynamically at
        # eglInitialize so no link-time dependency is added.
        option(EGL_LINUX_ENABLE_GLES "Enable OpenGL ES backend via system libEGL/libGLESv2 on Linux" ON)
        if(EGL_LINUX_ENABLE_GLES)
            message(STATUS "EGL: Linux GLES backend enabled (system libEGL.so + libGLESv2.so)")
            set(EGL_HAS_GLES ON)
            list(APPEND EGL_PLATFORM_SOURCES
                ${CMAKE_CURRENT_LIST_DIR}/src/egl_linux_gles.cpp)
            set(EGL_ES_BACKEND_SUFFIX "_ES_Linux_X11_GLES")
        endif()
    endif()

else()
    message(FATAL_ERROR "EGL: Unsupported platform. Supported: WIN32, UNIX")
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
    if(EGL_HAS_ANGLE)
        target_compile_definitions(EGL PUBLIC EGL_WIN_ENABLE_ANGLE)
        # No link-time dependency on ANGLE — egl_windows_angle.cpp loads it
        # dynamically at eglInitialize via LoadLibrary.
    endif()
elseif(UNIX AND WL_EGL_PLATFORM)
    target_link_libraries(EGL PUBLIC
        ${CMAKE_DL_LIBS} X11::X11 OpenGL::GL
        ${WAYLAND_CLIENT_LIBRARIES} Vulkan::Vulkan)
    target_include_directories(EGL PRIVATE
        ${X11_INCLUDE_DIR}
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${CMAKE_BINARY_DIR})
    if(EGL_HAS_GLES)
        target_compile_definitions(EGL PUBLIC EGL_WAYLAND_ENABLE_GLES)
    endif()
elseif(UNIX AND NOT GBM_PLATFORM)
    target_link_libraries(EGL PUBLIC ${CMAKE_DL_LIBS} X11::X11 OpenGL::GL)
    if(LINUX_VK)
        target_link_libraries(EGL PUBLIC Vulkan::Vulkan)
    endif()
    if(EGL_HAS_GLES)
        target_compile_definitions(EGL PUBLIC EGL_LINUX_ENABLE_GLES)
    endif()
    target_include_directories(EGL PRIVATE ${X11_INCLUDE_DIR})
endif()

target_include_directories(EGL PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_definitions(EGL PUBLIC KHRONOS_STATIC) # PUBLIC - make it go down to every target linking EGL
add_definitions(-DEGLAPI=)
add_subdirectory(examples)
