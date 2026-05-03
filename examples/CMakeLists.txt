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
