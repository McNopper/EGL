add_executable(scrgb_linear main.cpp ../common_egl.cpp ../common_windows.cpp)

set_target_properties(scrgb_linear PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

target_link_libraries(scrgb_linear PRIVATE EGL opengl32)
