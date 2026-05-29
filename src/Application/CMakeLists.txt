cmake_minimum_required(VERSION 3.22)

add_library(Application STATIC
    # Core: lifecycle, init, app chrome (menu/status/toolbar), windows, dev panels
    Core/Application.cpp
    Core/ApplicationInit.cpp
    Core/ApplicationUI.cpp
    Core/DevPanels.cpp
    Core/Windows.cpp

    # Editors: per-editor content
    Editors/Viewport.cpp
    Editors/Outliner.cpp

    # Layout: the Blender-style dynamic zone tree (split into clusters)
    Layout/ZoneLayoutCore.cpp
    Layout/ZoneLayoutTabs.cpp
    Layout/ZoneLayoutGestures.cpp
    Layout/ZoneLayoutDraw.cpp
)

# Core/ and Layout/ are on the include path so the existing relative includes
# (#include "Application.h" / "ZoneLayout.h" / "Project.h") resolve unchanged
# from any sub-folder, with no edits to the source #include lines.
target_include_directories(Application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Core
        ${CMAKE_CURRENT_SOURCE_DIR}/Layout
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