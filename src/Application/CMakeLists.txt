cmake_minimum_required(VERSION 3.22)

add_library(Application STATIC
    # App: lifecycle, init (Vulkan/SDL), the detached secondary window
    App/Application.cpp
    App/ApplicationActions.cpp
    App/Clipboard.cpp
    App/ApplicationInit.cpp
    App/ShortcutDefaults.cpp
    App/SecondaryWindow.cpp

    # Chrome: window chrome (custom title bar, splash/about, main UI host)
    Chrome/MainUI.cpp
    Chrome/TitleBar.cpp
    Chrome/Splash.cpp

    # Editors: per-editor content, one folder per editor.
    # Viewport: canvas + drawing-tool state machine, split by tool for readability
    # (ViewportTools* share ViewportToolsShared.h for the common static helpers).
    Editors/Viewport/Viewport.cpp
    Editors/Viewport/ViewportToolbar.cpp
    Editors/Viewport/ViewportOverlays.cpp
    Editors/Viewport/ViewportTools.cpp
    Editors/Viewport/ViewportTransform.cpp
    Editors/Viewport/ViewportCurveTool.cpp
    Editors/Viewport/ViewportLineMarkTool.cpp
    Editors/Viewport/ViewportGesture.cpp
    Editors/Viewport/ViewportPlacement.cpp
    Editors/Viewport/EditMode.cpp
    Editors/Viewport/EditModeActions.cpp
    Editors/Outliner/Outliner.cpp
    Editors/Outliner/OutlinerMenus.cpp
    Editors/Properties/Properties.cpp
    Editors/Info/InfoEditors.cpp

    # Project: document model, .acu file format, undo/redo
    Project/ProjectFile.cpp
    Project/Undo.cpp

    # Layout: the Blender-style dynamic zone tree (split into clusters)
    Layout/EditorRegistry.cpp
    Layout/PageLayout.cpp
    Layout/ZoneLayoutCore.cpp
    Layout/ZoneLayoutTabs.cpp
    Layout/ZoneLayoutGestures.cpp
    Layout/ZoneLayoutDraw.cpp

    # Dev: debug/test panels and the design-system window
    Dev/DevPanels.cpp
    Dev/Windows.cpp

    # Modules: the module system + built-in modules (Typography, IOF Mapping).
    # Compiled into Application (internal modules live in the binary); ModuleAPI.h
    # is the stable contract external plugins build against.
    ../Modules/ModuleRegistry.cpp
    ../Modules/ModuleManager.cpp
    ../Modules/Typography/TypographyModule.cpp
    ../Modules/IofMapping/IofSpec.cpp
    ../Modules/IofMapping/IofGlyph.cpp
    ../Modules/IofMapping/IofSymbolDoc.cpp
    ../Modules/IofMapping/IofSymbolPlates.cpp
    ../Modules/IofMapping/IofSymbolText.cpp
    ../Modules/IofMapping/IofMappingModule.cpp
    ../Modules/IofMapping/IofMappingPanels.cpp
)

# Every sub-folder is on the include path so the flat relative includes
# (#include "Application.h" / "ZoneLayout.h" / "Project.h" / "ViewportTools.h")
# resolve from any source file regardless of which folder it lives in.
target_include_directories(Application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/App
        ${CMAKE_CURRENT_SOURCE_DIR}/Chrome
        ${CMAKE_CURRENT_SOURCE_DIR}/Editors/Viewport
        ${CMAKE_CURRENT_SOURCE_DIR}/Editors/Outliner
        ${CMAKE_CURRENT_SOURCE_DIR}/Editors/Properties
        ${CMAKE_CURRENT_SOURCE_DIR}/Editors/Info
        ${CMAKE_CURRENT_SOURCE_DIR}/Project
        ${CMAKE_CURRENT_SOURCE_DIR}/Util
        ${CMAKE_CURRENT_SOURCE_DIR}/Dev
        ${CMAKE_CURRENT_SOURCE_DIR}/Layout
        ${CMAKE_CURRENT_SOURCE_DIR}/../Modules
    PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/../
        ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/stb
)

target_link_libraries(Application PUBLIC
    DesignSystem
    UI
    Shortcuts
    VectorGraphics
    Renderer
    Compositor      # the second (modern-Vulkan) render engine, behind IViewRenderer
    Shell
    Vulkan::Vulkan
    SDL3::SDL3-shared
)