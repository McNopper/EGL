add_subdirectory(linear)
add_subdirectory(srgb)

# HDR examples require the Windows Vulkan/WGL HDR backend
if(WIN32)
    add_subdirectory(scrgb_linear)
    add_subdirectory(scrgb)
    add_subdirectory(bt2020_pq)
    add_subdirectory(bt2020_linear)
    add_subdirectory(bt2020_hlg)
    add_subdirectory(display_p3)
    add_subdirectory(display_p3_linear)
    add_subdirectory(display_p3_passthrough)
endif()
