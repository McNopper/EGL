if(WIN32)
    set(PLATFORM_SOURCES ../common_windows.cpp)
    set(PLATFORM_LIBS opengl32)
elseif(UNIX)
    set(PLATFORM_SOURCES ../common_x11.cpp)
    set(PLATFORM_LIBS OpenGL::GL X11::X11)
endif()

add_executable(display_p3 main.cpp ../common_egl.cpp ${PLATFORM_SOURCES})

set_target_properties(display_p3 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

target_link_libraries(display_p3 PRIVATE EGL ${PLATFORM_LIBS})