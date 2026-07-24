cmake_minimum_required(VERSION 3.22)

add_library(UI STATIC
    src/Units.cpp
    src/Text/FontManager.cpp
    src/Widgets/IconWidgets.cpp
    src/Widgets/ButtonGroup.cpp
    src/Widgets/Dropdown.cpp
    src/Widgets/Checkbox.cpp
    src/Widgets/DragValue.cpp
    src/Widgets/ListRow.cpp
    src/Widgets/RowDrag.cpp
    src/Widgets/PopupMenu.cpp
    src/Widgets/Panel.cpp
    src/Widgets/NodeGraph.cpp
    src/Widgets/ScrollArea.cpp
    src/Widgets/SidePanel.cpp
    src/Widgets/ToolPalette.cpp
    src/Tokens/TokenEditor.cpp
    src/Tokens/TokenValueWidgets.cpp
    src/Tokens/TokenGraphWindow.cpp
    src/Tokens/TokenJsonExport.cpp
    src/Tokens/TokenInspector.cpp
    src/Tokens/TokenTreeView.cpp
    src/Tokens/TokensViewer.cpp
    src/Tokens/UserThemeEditor.cpp
    src/Shortcuts/ShortcutEditor.cpp
    src/Shortcuts/ShortcutCaptureField.cpp
    src/Shortcuts/KeyCap.cpp
    src/Chrome/StatusBar.cpp
    src/Chrome/BorderlessWindow.cpp
    src/Settings/SettingsWindow.cpp
    src/Settings/TokenPropertyRow.cpp
)

target_include_directories(UI
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${IMGUI_DIR}/misc/freetype
)

target_compile_definitions(UI PRIVATE
    IMGUI_ENABLE_DOCKING
    IMGUI_ENABLE_FREETYPE
)

target_link_libraries(UI PUBLIC
    DesignSystem
    Shortcuts
    VectorGraphics
    Vulkan::Vulkan
    Freetype::Freetype
    SDL3::SDL3-shared
)