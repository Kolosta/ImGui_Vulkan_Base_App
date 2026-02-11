cmake_minimum_required(VERSION 3.22)

# ================================================================
# Unified resvg-bindings (library + icon compiler)
# ================================================================

set(RESVG_BINDINGS_DIR
    "${CMAKE_SOURCE_DIR}/src/tools/resvg-bindings"
)

set(RESVG_LIB_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/resvg-lib"
)

set(ICON_COMPILER_OUTPUT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/IconData.h"
)

set(ICONS_DIR
    "${CMAKE_SOURCE_DIR}/resources/icons"
)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${RESVG_LIB_DIR}")

# ================================================================
# Build resvg-bindings (library + compiler) with static CRT
# ================================================================

if(WIN32)
    set(RESVG_RUST_TARGET "x86_64-pc-windows-gnu")
    set(RESVG_CARGO_RELEASE_DIR
        "${RESVG_LIB_DIR}/cargo/${RESVG_RUST_TARGET}/release"
    )
    set(RESVG_DLL "${RESVG_CARGO_RELEASE_DIR}/resvg_c.dll")
    set(RESVG_IMPLIB "${RESVG_CARGO_RELEASE_DIR}/libresvg_c.dll.a")
    set(ICON_COMPILER_EXE "${RESVG_CARGO_RELEASE_DIR}/icon_compiler.exe")

    add_custom_command(
        OUTPUT
            ${RESVG_DLL}
            ${RESVG_IMPLIB}
            ${ICON_COMPILER_EXE}

        COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${RESVG_LIB_DIR}/cargo
            CARGO_BUILD_TARGET=${RESVG_RUST_TARGET}
            "RUSTFLAGS=-C target-feature=+crt-static -C link-args=-static"
            cargo build --release --manifest-path ${RESVG_BINDINGS_DIR}/Cargo.toml

        WORKING_DIRECTORY ${RESVG_BINDINGS_DIR}
        COMMENT "Building resvg-bindings (library + compiler)"
        VERBATIM
    )

    add_custom_target(build_resvg_bindings
        DEPENDS
            ${RESVG_DLL}
            ${RESVG_IMPLIB}
            ${ICON_COMPILER_EXE}
    )

    add_library(resvg_c SHARED IMPORTED GLOBAL)
    set_target_properties(resvg_c PROPERTIES
        IMPORTED_LOCATION "${RESVG_DLL}"
        IMPORTED_IMPLIB "${RESVG_IMPLIB}"
    )

else()
    set(RESVG_CARGO_RELEASE_DIR
        "${RESVG_LIB_DIR}/cargo/release"
    )
    set(RESVG_SO "${RESVG_CARGO_RELEASE_DIR}/libresvg_c.so")
    set(ICON_COMPILER_EXE "${RESVG_CARGO_RELEASE_DIR}/icon_compiler")

    add_custom_command(
        OUTPUT
            ${RESVG_SO}
            ${ICON_COMPILER_EXE}

        COMMAND ${CMAKE_COMMAND} -E env
            CARGO_TARGET_DIR=${RESVG_LIB_DIR}/cargo
            cargo build --release --manifest-path ${RESVG_BINDINGS_DIR}/Cargo.toml

        WORKING_DIRECTORY ${RESVG_BINDINGS_DIR}
        COMMENT "Building resvg-bindings (library + compiler)"
        VERBATIM
    )

    add_custom_target(build_resvg_bindings
        DEPENDS
            ${RESVG_SO}
            ${ICON_COMPILER_EXE}
    )

    add_library(resvg_c SHARED IMPORTED GLOBAL)
    set_target_properties(resvg_c PROPERTIES
        IMPORTED_LOCATION "${RESVG_SO}"
    )
endif()

add_dependencies(resvg_c build_resvg_bindings)

# ================================================================
# Icon compiler execution (generates IconData.h)
# ================================================================

add_custom_command(
    OUTPUT ${ICON_COMPILER_OUTPUT}

    COMMAND ${ICON_COMPILER_EXE}
        ${ICONS_DIR}
        ${ICON_COMPILER_OUTPUT}

    DEPENDS
        ${ICON_COMPILER_EXE}

    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling icons to IconData.h"
    VERBATIM
)

add_custom_target(generate_icon_data ALL
    DEPENDS ${ICON_COMPILER_OUTPUT}
)

# ================================================================
# VectorGraphics static library
# ================================================================

add_library(VectorGraphics STATIC
    src/IconManager.cpp
    editors/IconEditorWindow.cpp
    ${ICON_COMPILER_OUTPUT}
)

add_dependencies(VectorGraphics
    generate_icon_data
    build_resvg_bindings
)

target_include_directories(VectorGraphics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}
        ${RESVG_BINDINGS_DIR}/include
    PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_link_libraries(VectorGraphics PUBLIC
    DesignSystem
    Vulkan::Vulkan
    resvg_c
)

# ================================================================
# Runtime DLL copy (Windows only)
# ================================================================

if(WIN32)
    add_custom_command(TARGET VectorGraphics POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_PROPERTY:resvg_c,IMPORTED_LOCATION>"
            "$<TARGET_FILE_DIR:VectorGraphics>/resvg_c.dll"
        COMMENT "Copying resvg_c.dll to VectorGraphics output"
    )
endif()