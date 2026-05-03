add_executable(display_p3 main.cpp ../common_egl.cpp ../common_windows.cpp)

set_target_properties(display_p3 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

target_link_libraries(display_p3 PRIVATE EGL opengl32)
