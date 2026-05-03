add_executable(display_p3_passthrough main.cpp ../common_egl.cpp ../common_windows.cpp)

set_target_properties(display_p3_passthrough PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

target_link_libraries(display_p3_passthrough PRIVATE EGL opengl32)
