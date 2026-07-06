cmake_minimum_required(VERSION 3.22)

# ── Compositor: the second (from-scratch, modern-Vulkan) render engine ─────────
# Runs in parallel to Renderer::CanvasRenderer behind the Renderer::IViewRenderer
# contract; the Application keeps exactly one engine alive at a time and can
# clean-swap between them. Shares the Renderer's Document model (pure data).
# See docs/Vulkan/COMPOSITOR_PIPELINE.md for the architecture + lot roadmap.
#
# Lot 0: skeleton Engine only (implements the contract, no GPU work yet). The GPU
# layer (VMA, dynamic rendering, sync2), render graph and passes land in Lot 1+.

add_library(Compositor STATIC
    # The Engine class, split by concern across several TUs (one class, many .cpp;
    # shared helpers + push layouts in src/Internal.h). See CLAUDE.md "File Org.".
    src/Engine.cpp                  # lifecycle + per-frame spine + swapchain composite
    src/Pipelines/Pipelines.cpp     # all VkPipelines + shader loading + stencil probe
    src/GPU/Targets.cpp             # per-view target / iso / backdrop / vbo / slot
    src/Frame/Signature.cpp         # per-view content signature (rebuild-or-reuse)
    src/Passes/RenderView.cpp       # the P1->P2->P4 render orchestrator
    # Geometry (Lot 13): GPU-oriented fill geometry, ear-clip-free (stencil-then-cover).
    src/Geometry/FillGeometry.cpp
    # GPU layer (modern Vulkan): VMA allocator + (later) buffers/images/sync.
    src/GPU/Allocator.cpp
    src/GPU/ShapePool.cpp   # Lot 13-1b: persistent per-shape vertex pool (free-list)
    src/GPU/VmaImpl.cpp     # the single VMA_IMPLEMENTATION translation unit
)

target_include_directories(Compositor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_link_libraries(Compositor PUBLIC
    Vulkan::Vulkan
    Renderer        # shared Document model + IViewRenderer contract
)

# VMA is an implementation detail of the Compositor (header included only in
# Allocator.cpp / VmaImpl.cpp), so link it PRIVATE.
target_link_libraries(Compositor PRIVATE
    GPUOpen::VulkanMemoryAllocator
)

# ── Shader compilation: GLSL → SPIR-V via glslc ───────────────────────────────
# Outputs into ${CMAKE_BINARY_DIR}/shaders/compositor, a subfolder of the dir the
# root CMakeLists already mirrors next to the executable (copy_directory of
# ${CMAKE_BINARY_DIR}/shaders). At runtime the engine loads them from
# "<exe-dir>/shaders/compositor/<name>.spv".
find_program(GLSLC_EXECUTABLE
    NAMES glslc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED)

set(COMPOSITOR_SHADER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/blit.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/blit.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shape.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shape.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fill_stencil.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fill_cover.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fill_cover.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/flatten_prim.comp   # Lot 13-4c: GPU flatten scaffold
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pattern_fill.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pattern_fill.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/iso_composite.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/iso_composite.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/iso_blend.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/picking.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/picking.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/overlay.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/overlay.frag
)

set(COMPOSITOR_SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders/compositor)
file(MAKE_DIRECTORY ${COMPOSITOR_SHADER_OUT_DIR})

set(COMPOSITOR_SPV_FILES "")
foreach(SHADER ${COMPOSITOR_SHADER_SRC})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV ${COMPOSITOR_SHADER_OUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPV}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPV}
        DEPENDS ${SHADER}
        COMMENT "Compiling compositor shader ${SHADER_NAME} → ${SHADER_NAME}.spv"
        VERBATIM)
    list(APPEND COMPOSITOR_SPV_FILES ${SPV})
endforeach()

add_custom_target(compositor_shaders DEPENDS ${COMPOSITOR_SPV_FILES})
add_dependencies(Compositor compositor_shaders)
