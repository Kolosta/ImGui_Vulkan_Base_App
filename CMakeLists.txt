cmake_minimum_required(VERSION 3.22)
project(Carto)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure Cargo is available
find_program(CARGO_EXECUTABLE cargo REQUIRED)
if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR "Cargo not found. Please install Rust and Cargo.")
endif()

# Vulkan
find_package(Vulkan REQUIRED)

# FreeType — replaces ImGui's default stb_truetype rasterizer so we can drive
# variable-font axes (weight/width/optical-size) and OpenType features at
# runtime. Provided by MSYS2: pacman -S mingw-w64-x86_64-freetype.
#
# The IDE-bundled CMake's FindFreetype module does not probe the MSYS2 mingw64
# prefix, so we hint it explicitly (override on the cache line if your MSYS2
# lives elsewhere). FREETYPE_DIR can also be set in CMakeUserPresets.json.
if(NOT DEFINED MSYS2_MINGW64_ROOT)
    set(MSYS2_MINGW64_ROOT "C:/msys64/mingw64" CACHE PATH "MSYS2 mingw64 prefix")
endif()
if(NOT FREETYPE_INCLUDE_DIRS AND EXISTS "${MSYS2_MINGW64_ROOT}/include/freetype2")
    set(FREETYPE_INCLUDE_DIRS
        "${MSYS2_MINGW64_ROOT}/include/freetype2"
        "${MSYS2_MINGW64_ROOT}/include")
endif()
if(NOT FREETYPE_LIBRARY AND EXISTS "${MSYS2_MINGW64_ROOT}/lib/libfreetype.dll.a")
    set(FREETYPE_LIBRARY "${MSYS2_MINGW64_ROOT}/lib/libfreetype.dll.a")
endif()
find_package(Freetype REQUIRED)

include(FetchContent)

# SDL3
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.0
)
FetchContent_MakeAvailable(SDL3)

# ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

# ImGui sources
set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# Subdirectories
add_subdirectory(src/DesignSystem)
add_subdirectory(src/UI)
add_subdirectory(src/Shortcuts)
add_subdirectory(src/VectorGraphics)
add_subdirectory(src/Application)

# Main executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${IMGUI_SOURCES}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    IMGUI_ENABLE_DOCKING
    IMGUI_ENABLE_FREETYPE
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUI_DIR}/misc/freetype
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3-shared
    Freetype::Freetype
    DesignSystem
    UI
    Shortcuts
    VectorGraphics
    Application
)

# ── Token integrity tests ────────────────────────────────────────────────────
# Structural correctness is already enforced at compile time (static_asserts
# in TokenSchema.cpp). This optional target adds the runtime value-level
# safety net. Enabled by default; build/run the `ds_token_tests` target.
option(DS_BUILD_TESTS "Build the design-system token integrity tests" ON)
if(DS_BUILD_TESTS)
    # DesignSystem references core ImGui symbols (ImGui::GetStyle, PushStyleVar
    # …) from ApplyGlobalStyle/PushAllStyles. The test never calls those, but
    # the linker still needs the definitions. Link the *core* ImGui TUs only —
    # the SDL/Vulkan backends are not needed and would drag heavy deps in.
    set(IMGUI_CORE_SOURCES
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
    )
    add_executable(ds_token_tests
        tests/TokenIntegrityTests.cpp
        ${IMGUI_CORE_SOURCES}
    )
    target_link_libraries(ds_token_tests PRIVATE DesignSystem)
    target_include_directories(ds_token_tests PRIVATE ${IMGUI_DIR})
    enable_testing()
    add_test(NAME token_integrity COMMAND ds_token_tests)
endif()

# Copy resources
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})


if(WIN32 AND MINGW)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE_DIR:VectorGraphics>/resvg_c.dll
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/resvg_c.dll
        COMMENT "Copying resvg DLL to executable directory"
    )

    # Récupère le répertoire du compilateur
    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

    # Sous la section où tu copies déjà les DLL MinGW
    if(TARGET SDL3::SDL3-shared)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3-shared>"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
            COMMENT "Copying SDL3 DLL to executable directory"
        )
    endif()
endif()
