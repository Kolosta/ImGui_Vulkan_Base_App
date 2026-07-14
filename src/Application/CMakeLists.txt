cmake_minimum_required(VERSION 3.22)

# NOTE (Ink rework, Lot 0): the legacy engines (Renderer, Compositor) and every
# application file coupled to them / to the old document live untracked-by-the-
# build under src/_legacy/ (see src/_legacy/README.md and docs/Ink/). The
# Viewport / Outliner / Properties editors below are minimal placeholders until
# the Ink engine lands (docs/Ink/ROADMAP.md).
add_library(Application STATIC
    # App: lifecycle, init (Vulkan/SDL), generic actions, prefs undo, the
    # detached secondary window
    App/Application.cpp
    App/Actions.cpp
    App/PrefsUndo.cpp
    App/ApplicationInit.cpp
    App/ProjectIO.cpp
    App/ShortcutDefaults.cpp
    App/SecondaryWindow.cpp

    # Chrome: window chrome (custom title bar, splash/about, main UI host)
    Chrome/MainUI.cpp
    Chrome/TitleBar.cpp
    Chrome/Splash.cpp

    # Editors: the Ink-backed canvas + the editing loop (Lot 8), plus the
    # Outliner/Properties placeholders (rebuilt in Lot 9).
    Editors/Viewport/Viewport.cpp
    Editors/Viewport/ViewportInput.cpp
    Editors/Viewport/ViewportModal.cpp
    Editors/Viewport/ViewportTools.cpp
    Editors/Viewport/ViewportMarkTool.cpp
    Editors/Viewport/ViewportOverlays.cpp
    Editors/Viewport/ViewportToolbar.cpp
    Editors/Outliner/Outliner.cpp
    Editors/Outliner/OutlinerMenus.cpp
    Editors/Outliner/OutlinerDragDrop.cpp
    Editors/Properties/Properties.cpp
    Editors/Properties/PropertiesPaint.cpp
    Editors/Properties/PropertiesModifiers.cpp
    Editors/Info/InfoEditors.cpp

    # Project: the .acu v2 persistence codec (docs/acu-format.md, Lot 10)
    Project/AcuFile.cpp

    # Layout: the Blender-style dynamic zone tree (split into clusters)
    Layout/EditorRegistry.cpp
    Layout/ZoneLayoutCore.cpp
    Layout/ZoneLayoutTabs.cpp
    Layout/ZoneLayoutGestures.cpp
    Layout/ZoneLayoutDraw.cpp

    # Dev: debug/test panels and the design-system window
    Dev/DevPanels.cpp
    Dev/Windows.cpp

    # Modules: the module system + built-in modules (Typography).
    # IofMapping is quarantined under src/_legacy/Modules/ until it is rebuilt
    # on Ink (ROADMAP Lot 11). ModuleAPI.h is the stable contract.
    ../Modules/ModuleRegistry.cpp
    ../Modules/ModuleManager.cpp
    ../Modules/Typography/TypographyModule.cpp
)

# Every sub-folder is on the include path so the flat relative includes
# (#include "Application.h" / "ZoneLayout.h" / "Project.h") resolve from any
# source file regardless of which folder it lives in.
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
    Ink
    Shell
    Vulkan::Vulkan
    SDL3::SDL3-shared
)
