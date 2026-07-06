#pragma once

#include "Compositor/GPU/Allocator.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor - GPU/ShapePool (Lot 13-1b): a persistent per-shape vertex pool.
//
//  Today a view re-uploads its WHOLE vertex buffer whenever anything changes, even
//  if only one shape moved among thousands (the static-masses wall — see 13-1a's
//  `dirty` HUD counter). This pool fixes that: each shape owns a STABLE slice in a
//  host-visible, persistently-mapped GPU buffer, tracked by a free-list. On a
//  frame, only CHANGED shapes rewrite their slice (a plain memcpy into the mapped
//  region + a ranged flush); unchanged shapes are never touched — no re-upload.
//  A shape that grew past its slice capacity is re-allocated (its old block goes
//  back to the free-list, coalesced); a removed shape frees its block.
//
//  Generic over the element type (vertex or fan-vertex), so the base / cover / fill
//  streams each get their own pool. It only manages BYTES + offsets; the caller
//  keeps the per-shape draw metadata (counts, page shift, colours). Offsets/counts
//  are in ELEMENTS of `stride` bytes, matching a vkCmdDraw firstVertex/vertexCount.
//
//  Not thread-safe; one pool per stream per view, driven from RenderView.
// ─────────────────────────────────────────────────────────────────────────────

namespace Comp {

class ShapePool {
public:
    // A shape's slice in the pool: [firstElement, firstElement+count) live; the
    // block reserves `capacity` elements (count ≤ capacity) so small edits reuse it.
    struct Slot {
        uint32_t first    = 0;   // element offset (→ vkCmdDraw firstVertex)
        uint32_t count    = 0;   // live element count (→ vkCmdDraw vertexCount)
        uint32_t capacity = 0;   // reserved elements (grow triggers re-alloc)
    };

    // `stride` = bytes per element (sizeof the vertex). `usage` is the buffer usage
    // (VERTEX_BUFFER_BIT). The pool grows its buffer as needed. Call once.
    void Init(Allocator* alloc, uint32_t stride, VkBufferUsageFlags usage) {
        alloc_ = alloc; stride_ = stride; usage_ = usage;
    }
    void Destroy();

    // The GPU buffer + its live element high-water mark (for metrics / bounds).
    VkBuffer Buffer()  const { return buf_; }
    uint32_t HighMark() const { return highMark_; }
    bool     Valid()   const { return buf_ != VK_NULL_HANDLE; }

    // Begin a build pass: mark every existing slot as unseen. Shapes fed this pass
    // survive; the rest are freed in EndFrame(). (Lets the pool drop removed shapes.)
    void BeginFrame();

    // Look up a shape's slot (nullptr if it has none yet). Const — for the draw walk.
    const Slot* Find(uint64_t shapeId) const {
        auto it = slots_.find(shapeId);
        return it == slots_.end() ? nullptr : &it->second;
    }

    // Ensure `shapeId` has a slot holding `count` elements copied from `src`
    // (host memory, `count`×stride bytes), and mark it seen this frame. Re-allocates
    // (frees old block, takes a new one) only if `count` exceeds the current
    // capacity; otherwise reuses the block in place. Uploads (memcpy + flush) ONLY
    // here — an unchanged shape whose caller doesn't call Write keeps its data. The
    // caller decides whether to Write (dirty) or just Touch (unchanged). Returns the
    // slot's element offset (for the caller's draw metadata).
    uint32_t Write(uint64_t shapeId, const void* src, uint32_t count);

    // Mark an unchanged shape's slot as still alive this frame WITHOUT re-uploading.
    // Returns its element offset, or keeps it dropped if it has no slot (the caller
    // then must Write it). Use for shapes the dirty tracker says are unchanged.
    const Slot* Touch(uint64_t shapeId);

    // End the build pass: free every slot not seen this frame (removed shapes),
    // returning their blocks to the free-list (coalesced).
    void EndFrame();

    // Flush all writes accumulated this frame in one ranged flush (0..highMark).
    void FlushWrites();

private:
    struct FreeBlock { uint32_t first; uint32_t capacity; };
    // Grow the mapped buffer to at least `elements` capacity, preserving contents.
    void Reserve(uint32_t elements);
    // Take a block of ≥`count` elements from the free-list or bump the tail.
    Slot Allocate(uint32_t count);
    void FreeBlock_(uint32_t first, uint32_t capacity);
    static uint32_t RoundCapacity(uint32_t count);

    Allocator*         alloc_  = nullptr;
    uint32_t           stride_ = 0;
    VkBufferUsageFlags usage_  = 0;

    VkBuffer      buf_    = VK_NULL_HANDLE;
    VmaAllocation mem_    = nullptr;
    void*         mapped_ = nullptr;
    uint32_t      capacityElems_ = 0;   // total elements the buffer can hold
    uint32_t      tail_     = 0;        // first never-allocated element
    uint32_t      highMark_ = 0;        // highest live element (for flush + draw bounds)

    std::unordered_map<uint64_t, Slot> slots_;   // shapeId → slice
    std::unordered_map<uint64_t, bool> seen_;     // fed this frame?
    std::vector<FreeBlock>             free_;      // recyclable holes
    bool          dirtyFlush_ = false;   // any Write happened this frame
};

} // namespace Comp
