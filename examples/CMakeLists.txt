# Platform sources and libs — set once, used by all examples via add_egl_example().
if(WIN32)
    set(PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/common_windows.cpp)
    set(PLATFORM_LIBS opengl32)
elseif(UNIX)
    if(WL_EGL_PLATFORM)
        set(PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/common_wayland.cpp)
        set(PLATFORM_LIBS xdg_shell_protocol ${WAYLAND_CLIENT_LIBRARIES} OpenGL::GL X11::X11)
    else()
        set(PLATFORM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/common_x11.cpp)
        set(PLATFORM_LIBS OpenGL::GL X11::X11)
    endif()
endif()

function(add_egl_example name)
    add_executable(${name}
        ${CMAKE_CURRENT_SOURCE_DIR}/${name}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/common_egl.cpp
        ${PLATFORM_SOURCES})
    set_target_properties(${name} PROPERTIES
        OUTPUT_NAME                      "${name}${EGL_BACKEND_SUFFIX}"
        RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)
    target_link_libraries(${name} PRIVATE EGL ${PLATFORM_LIBS})
endfunction()

add_egl_example(linear)
add_egl_example(srgb)
add_egl_example(scrgb_linear)
add_egl_example(scrgb)
add_egl_example(bt2020_pq)
add_egl_example(bt2020_linear)
add_egl_example(bt2020_hlg)
add_egl_example(display_p3)
add_egl_example(display_p3_linear)
add_egl_example(display_p3_passthrough)
add_egl_example(green_window)

# OpenGL ES variant of green_window (Windows + ANGLE only).
if(WIN32 AND EGL_HAS_ANGLE)
    set(es_name green_window)
    set(es_target ${es_name}_es)
    add_executable(${es_target}
        ${CMAKE_CURRENT_SOURCE_DIR}/${es_name}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/common_egl.cpp
        ${PLATFORM_SOURCES})
    set_target_properties(${es_target} PROPERTIES
        OUTPUT_NAME                      "${es_name}${EGL_ES_BACKEND_SUFFIX}"
        RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)
    target_compile_definitions(${es_target} PRIVATE
        GREEN_WINDOW_API=EGL_OPENGL_ES_API
        GREEN_WINDOW_RENDERABLE_BIT=EGL_OPENGL_ES2_BIT
        "GREEN_WINDOW_TITLE=\"EGL Green Window (ES)\"")
    # Link the example against ANGLE's libGLESv2 import lib for glClear*.
    target_link_libraries(${es_target} PRIVATE EGL unofficial::angle::libGLESv2)

    # Copy ANGLE runtime DLLs next to the .exe so it can run.
    add_custom_command(TARGET ${es_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:unofficial::angle::libEGL>
            $<TARGET_FILE:unofficial::angle::libGLESv2>
            ${CMAKE_SOURCE_DIR}/bin)
endif()

# OpenGL ES variant of green_window (Linux + system GLES only).
if(UNIX AND EGL_HAS_GLES)
    set(es_name green_window)
    set(es_target ${es_name}_es)
    add_executable(${es_target}
        ${CMAKE_CURRENT_SOURCE_DIR}/${es_name}/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/common_egl.cpp
        ${PLATFORM_SOURCES})
    set_target_properties(${es_target} PROPERTIES
        OUTPUT_NAME                      "${es_name}${EGL_ES_BACKEND_SUFFIX}"
        RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)
    target_compile_definitions(${es_target} PRIVATE
        GREEN_WINDOW_API=EGL_OPENGL_ES_API
        GREEN_WINDOW_RENDERABLE_BIT=EGL_OPENGL_ES2_BIT
        "GREEN_WINDOW_TITLE=\"EGL Green Window (ES)\"")
    # System GLES is loaded at runtime via dlopen; link libGLESv2 for glClear*.
    target_link_libraries(${es_target} PRIVATE EGL GLESv2)
endif()
