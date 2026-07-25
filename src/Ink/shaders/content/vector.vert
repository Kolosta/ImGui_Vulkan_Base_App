#version 450
// Ink content pass — instanced vector geometry. Each drawn instance chases
// instance → item → paint through the GpuScene tables; gl_InstanceIndex is
// firstInstance-based in Vulkan, so one multi-draw indirect covers every
// batch (docs/Ink/RENDER_GRAPH.md §3).

layout(location = 0) in vec2 inPos;          // definition-local units
layout(location = 0) out vec4 vColor;        // linear premultiplied

struct InstanceRec {
    float m0, m1, m2, m3, m4, m5;            // row-major 2x3 local -> document
    uint  item;
    uint  pad_;
};
struct ItemRec {
    uint paint;
    uint flags;
    uint pad0_, pad1_;
};

layout(std430, set = 0, binding = 0) readonly buffer Instances { InstanceRec uInst[]; };
layout(std430, set = 0, binding = 1) readonly buffer Items     { ItemRec     uItems[]; };
layout(std430, set = 0, binding = 2) readonly buffer Paints    { vec4        uPaints[]; };

// World -> NDC affine, computed in double on the CPU per view.
layout(push_constant) uniform PC { vec2 scale; vec2 offset; } pc;

void main() {
    InstanceRec r = uInst[gl_InstanceIndex];
    vec2 world = vec2(r.m0 * inPos.x + r.m1 * inPos.y + r.m2,
                      r.m3 * inPos.x + r.m4 * inPos.y + r.m5);
    gl_Position = vec4(world * pc.scale + pc.offset, 0.0, 1.0);
    vColor = uPaints[uItems[r.item].paint];
}
