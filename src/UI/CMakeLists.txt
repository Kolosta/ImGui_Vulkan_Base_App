# cmake_minimum_required(VERSION 3.22)

# add_library(UI STATIC
#     src/IconManager.cpp
#     src/FontManager.cpp
#     src/TokenEditor.cpp
#     src/ShortcutEditor.cpp
# )

# target_include_directories(UI
#     PUBLIC
#         ${CMAKE_CURRENT_SOURCE_DIR}/include
#     PRIVATE
#         ${IMGUI_DIR}
#         ${IMGUI_DIR}/backends
#         ${nanosvg_SOURCE_DIR}/src
# )

# target_link_libraries(UI PUBLIC
#     DesignSystem
#     Shortcuts
#     Vulkan::Vulkan
# )




cmake_minimum_required(VERSION 3.22)

add_library(UI STATIC
    src/FontManager.cpp
    src/TokenEditor.cpp
    src/ShortcutEditor.cpp
)

target_include_directories(UI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_link_libraries(UI PUBLIC
    DesignSystem
    Shortcuts
    Vulkan::Vulkan
)