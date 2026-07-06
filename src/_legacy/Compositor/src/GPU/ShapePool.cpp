#include "Compositor/GPU/ShapePool.h"

#include <algorithm>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor - GPU/ShapePool (impl): persistent per-shape slices with a free-list.
//
//  The buffer is host-visible + persistently mapped, so an "upload" is a memcpy
//  into the mapped region plus one ranged flush at end of frame. A shape's slice is
//  stable across frames (a free-list hands out blocks with rounded-up capacity), so
//  editing one shape rewrites only its own bytes — the other thousands are never
//  copied. A grown shape re-allocates (old block coalesced back into the free-list).
// ─────────────────────────────────────────────────────────────────────────────

namespace Comp {

uint32_t ShapePool::RoundCapacity(uint32_t count) {
    // Reserve a bit of slack so a small vertex-count wobble (a point drag adding a
    // few segments) reuses the block in place instead of re-allocating. Round up to
    // the next multiple of 64, min 64.
    if (count == 0) return 0;
    return ((count + 63u) / 64u) * 64u;
}

void ShapePool::Destroy() {
    if (buf_ && alloc_) alloc_->DestroyBuffer(buf_, mem_);
    buf_ = VK_NULL_HANDLE; mem_ = nullptr; mapped_ = nullptr;
    capacityElems_ = 0; tail_ = 0; highMark_ = 0;
    slots_.clear(); seen_.clear(); free_.clear(); dirtyFlush_ = false;
}

void ShapePool::Reserve(uint32_t elements) {
    if (elements <= capacityElems_) return;
    // Grow geometrically to amortise reallocation; preserve existing contents.
    uint32_t newCap = capacityElems_ ? capacityElems_ : 4096;
    while (newCap < elements) newCap *= 2;

    VkBuffer      nb = VK_NULL_HANDLE;
    VmaAllocation nm = nullptr;
    void*         np = nullptr;
    if (!alloc_ || !alloc_->CreateHostBuffer((VkDeviceSize)newCap * stride_, usage_,
                                             nb, nm, &np))
        return;   // best-effort: on failure keep the old buffer (writes clamp below)
    if (mapped_ && highMark_ > 0)
        std::memcpy(np, mapped_, (size_t)highMark_ * stride_);
    if (buf_) alloc_->DestroyBuffer(buf_, mem_);
    buf_ = nb; mem_ = nm; mapped_ = np; capacityElems_ = newCap;
    dirtyFlush_ = true;   // the whole live range moved to a new allocation
}

void ShapePool::FreeBlock_(uint32_t first, uint32_t capacity) {
    if (capacity == 0) return;
    // If this block is the current tail, just retract the tail (cheapest coalesce).
    if (first + capacity == tail_) { tail_ = first; return; }
    free_.push_back(FreeBlock{ first, capacity });
}

ShapePool::Slot ShapePool::Allocate(uint32_t count) {
    const uint32_t cap = RoundCapacity(count);
    // Best-fit over the free-list: the smallest block that fits (limits fragmentation).
    int best = -1;
    for (int i = 0; i < (int)free_.size(); ++i)
        if (free_[i].capacity >= cap &&
            (best < 0 || free_[i].capacity < free_[best].capacity))
            best = i;
    if (best >= 0) {
        FreeBlock b = free_[(size_t)best];
        free_.erase(free_.begin() + best);
        // Split: return the remainder to the free-list if it's worth keeping.
        if (b.capacity > cap) free_.push_back(FreeBlock{ b.first + cap, b.capacity - cap });
        return Slot{ b.first, count, cap };
    }
    // Bump the tail; grow the buffer if needed.
    if (tail_ + cap > capacityElems_) Reserve(tail_ + cap);
    Slot s{ tail_, count, cap };
    tail_ += cap;
    return s;
}

void ShapePool::BeginFrame() {
    for (auto& kv : slots_) seen_[kv.first] = false;
}

uint32_t ShapePool::Write(uint64_t shapeId, const void* src, uint32_t count) {
    Slot* slot = nullptr;
    auto it = slots_.find(shapeId);
    if (it != slots_.end()) {
        slot = &it->second;
        if (count > slot->capacity) {
            // Grew past its block → free it and take a new one.
            FreeBlock_(slot->first, slot->capacity);
            *slot = Allocate(count);
        } else {
            slot->count = count;
        }
    } else {
        Slot s = Allocate(count);
        slot = &(slots_[shapeId] = s);
    }
    seen_[shapeId] = true;
    if (count > 0 && mapped_ && src &&
        (uint64_t)slot->first + count <= capacityElems_) {
        std::memcpy(static_cast<uint8_t*>(mapped_) + (size_t)slot->first * stride_,
                    src, (size_t)count * stride_);
        dirtyFlush_ = true;
        highMark_ = std::max(highMark_, slot->first + count);
    }
    return slot->first;
}

const ShapePool::Slot* ShapePool::Touch(uint64_t shapeId) {
    auto it = slots_.find(shapeId);
    if (it == slots_.end()) return nullptr;
    seen_[shapeId] = true;
    highMark_ = std::max(highMark_, it->second.first + it->second.count);
    return &it->second;
}

void ShapePool::EndFrame() {
    // Free slots not seen this frame (removed shapes).
    for (auto it = slots_.begin(); it != slots_.end(); ) {
        auto s = seen_.find(it->first);
        if (s == seen_.end() || !s->second) {
            FreeBlock_(it->second.first, it->second.capacity);
            it = slots_.erase(it);
        } else {
            ++it;
        }
    }
    // Recompute the high-water mark from the live slots (so the flush range shrinks
    // when the tail objects are removed).
    highMark_ = 0;
    for (const auto& kv : slots_)
        highMark_ = std::max(highMark_, kv.second.first + kv.second.count);
}

void ShapePool::FlushWrites() {
    if (dirtyFlush_ && mem_ && alloc_ && highMark_ > 0)
        alloc_->Flush(mem_, 0, (VkDeviceSize)highMark_ * stride_);
    dirtyFlush_ = false;
}

} // namespace Comp
