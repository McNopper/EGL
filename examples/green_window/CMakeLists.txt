add_executable(green_window main.cpp)

set_target_properties(green_window PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   ${CMAKE_SOURCE_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

target_link_libraries(green_window PRIVATE EGL)

if(WIN32)
    target_link_libraries(green_window PRIVATE opengl32)
elseif(UNIX)
    target_link_libraries(green_window PRIVATE GL X11)
endif()
