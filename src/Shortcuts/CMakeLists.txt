cmake_minimum_required(VERSION 3.22)

add_library(Shortcuts STATIC
    src/ShortcutManager.cpp
)

target_include_directories(Shortcuts
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)