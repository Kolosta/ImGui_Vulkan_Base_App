cmake_minimum_required(VERSION 3.22)

add_library(Application STATIC
    Application.cpp
    ApplicationInit.cpp
    ApplicationUI.cpp
    ApplicationWindows.cpp
)

target_include_directories(Application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/../
)

target_link_libraries(Application PUBLIC
    DesignSystem
    UI
    Shortcuts
    VectorGraphics
    Vulkan::Vulkan
    SDL3::SDL3-shared
)