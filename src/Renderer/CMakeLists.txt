cmake_minimum_required(VERSION 3.22)

# ── Renderer: the Vulkan-only vector engine ───────────────────────────────────
# Document model (pure data) + CPU tessellation + an offscreen Vulkan pipeline
# that renders each Viewport into a texture ImGui then blits. ImGui is NOT used
# to draw the vector document — only to display the finished offscreen image.

add_library(Renderer STATIC
    src/Document/Shape.cpp
    src/Tessellation/Tessellator.cpp
    src/Render/CanvasRenderer.cpp
)

target_include_directories(Renderer
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_link_libraries(Renderer PUBLIC
    Vulkan::Vulkan
)

# ── Shader compilation: GLSL → SPIR-V via glslc ───────────────────────────────
# glslc ships with the Vulkan SDK; hint its bin dir from $VULKAN_SDK. The .spv
# files are written next to the executable (resources are copied there too), so
# CanvasRenderer loads them from "<exe-dir>/shaders" at runtime.
find_program(GLSLC_EXECUTABLE
    NAMES glslc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED)

set(RENDERER_SHADER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shape.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shape.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pattern.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pattern_fill.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pattern_fill.frag
)

# Compile every shader into ${CMAKE_BINARY_DIR}/shaders/<name>.spv. The output
# dir matches where the app expects them relative to the binary tree; the
# executable's POST_BUILD step (root CMakeLists) mirrors this folder next to the
# .exe alongside the copied resources.
set(RENDERER_SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${RENDERER_SHADER_OUT_DIR})

set(RENDERER_SPV_FILES "")
foreach(SHADER ${RENDERER_SHADER_SRC})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV ${RENDERER_SHADER_OUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPV}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME} → ${SHADER_NAME}.spv"
        VERBATIM)
    list(APPEND RENDERER_SPV_FILES ${SPV})
endforeach()

add_custom_target(renderer_shaders DEPENDS ${RENDERER_SPV_FILES})
add_dependencies(Renderer renderer_shaders)

# Expose the compiled-shader directory so the root CMakeLists can copy it next
# to the executable in a POST_BUILD step.
set(RENDERER_SHADER_OUT_DIR ${RENDERER_SHADER_OUT_DIR} PARENT_SCOPE)
