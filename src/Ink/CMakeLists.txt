cmake_minimum_required(VERSION 3.22)

# Ink — the 2D vector engine (docs/Ink/). Vulkan 1.3 only (dynamic rendering,
# synchronization2). Pure Vulkan + VMA + std: no ImGui, no SDL — the app hands
# in the shared device handles and texture-registration hooks (Renderer.h).
add_library(Ink STATIC
    # Document — the persistent model (pure data + invariants, GPU-less)
    src/Document/Document.cpp

    # Geometry — flatten / triangulate / stroke / boolean + the product cache
    src/Geometry/Flatten.cpp
    src/Geometry/Triangulate.cpp
    src/Geometry/Stroker.cpp
    src/Geometry/Boolean.cpp
    src/Geometry/GeometryCache.cpp

    # Scene — document → drawables (painter order, double transforms)
    src/Scene/Scene.cpp
    src/Scene/Picking.cpp

    # RHI — the only layer that touches raw Vulkan object creation
    src/RHI/Device.cpp
    src/RHI/Resources.cpp
    src/RHI/Pipeline.cpp
    src/RHI/VmaImpl.cpp

    # Graph — minimal frame graph (pass list + sync2 barrier derivation)
    src/Graph/RenderGraph.cpp

    # Render — engine root, GPU residency/tables, the passes
    src/Render/Renderer.cpp
    src/Render/GpuScene.cpp
    src/Render/DemoScene.cpp
    src/Render/Passes/ContentPass.cpp
    src/Render/Passes/OverlayPass.cpp
    src/Render/Passes/CompositePass.cpp
    src/Render/Passes/ScopePlayback.cpp
    src/Render/Passes/PresentPass.cpp

    # View — per-viewport camera/target/overlay façade
    src/View/View.cpp
    src/View/OverlayList.cpp
)

target_include_directories(Ink
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(Ink PUBLIC
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
)

# ── Shaders: GLSL → SPIR-V via glslc (Vulkan SDK) ────────────────────────────
find_program(INK_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)

set(INK_SHADER_SOURCES
    shaders/content/vector.vert
    shaders/content/vector.frag
    shaders/overlay/prim.vert
    shaders/overlay/prim.frag
    shaders/composite/iso.vert
    shaders/composite/iso.frag
    shaders/present/present.vert
    shaders/present/present.frag
)

set(INK_SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/spv)
file(MAKE_DIRECTORY ${INK_SHADER_OUT_DIR})

set(INK_SPV_FILES "")
foreach(src ${INK_SHADER_SOURCES})
    get_filename_component(name ${src} NAME)          # e.g. vector.vert
    set(out ${INK_SHADER_OUT_DIR}/${name}.spv)        # flat: vector.vert.spv
    add_custom_command(
        OUTPUT  ${out}
        COMMAND ${INK_GLSLC} --target-env=vulkan1.3 -O
                ${CMAKE_CURRENT_SOURCE_DIR}/${src} -o ${out}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src}
        COMMENT "glslc ${name}")
    list(APPEND INK_SPV_FILES ${out})
endforeach()

add_custom_target(ink_shaders DEPENDS ${INK_SPV_FILES})
add_dependencies(Ink ink_shaders)

# ── ink_tests: unit tests of the GPU-less layers (ctest) ────────────────────
option(INK_BUILD_TESTS "Build the Ink unit tests" ON)
if(INK_BUILD_TESTS)
    add_executable(ink_tests tests/unit/ink_tests.cpp)
    target_link_libraries(ink_tests PRIVATE Ink)
    add_test(NAME ink_unit COMMAND ink_tests)
endif()

# ── ink_bench: the headless performance harness (docs/Ink/PERF_TESTING.md) ──
option(INK_BUILD_BENCH "Build the ink_bench performance harness" ON)
if(INK_BUILD_BENCH)
    add_executable(ink_bench tests/perf/ink_bench.cpp)
    target_link_libraries(ink_bench PRIVATE Ink)
    # Default shader location baked in so a bare `ink_bench` run works from
    # anywhere; --shaders overrides it.
    target_compile_definitions(ink_bench PRIVATE
        INK_BENCH_SHADER_DIR="${INK_SHADER_OUT_DIR}")
    add_dependencies(ink_bench ink_shaders)
endif()

# Exported so the executable can mirror the SPIR-V next to itself
# ("<exe-dir>/shaders/ink") — see the root CMakeLists.
set(INK_SHADER_OUT_DIR ${INK_SHADER_OUT_DIR} PARENT_SCOPE)
