#include "Compositor/Engine.h"
#include "../Internal.h"

#include <algorithm>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
//  Compositor - GPU/Targets: per-view target lifecycle on the VMA allocator.
//
//  A ViewTarget is the offscreen surface a view renders into: the SSAA colour
//  image (+ view + ImGui descriptor), the stencil, and - allocated lazily, only
//  when an object needs them - the isolation layer (P4) and the blend backdrop
//  copy (Lot 4b). Plus the per-view vertex buffers (geometry + coverage) and the
//  submit-slot ring (command buffer + binary semaphore + fence). Pipelines that
//  consume these live in Pipelines/Pipelines.cpp.
// ─────────────────────────────────────────────────────────────────────────────

namespace Comp {

void Engine::CreateTargetImages(ViewTarget& t, uint32_t w, uint32_t h) {
    t.w = w; t.h = h;
    // TRANSFER_SRC so the canvas can be copied to the backdrop for blend modes.
    allocator_.CreateImage(w, h, kColorFormat,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           t.image, t.alloc);

    VkImageViewCreateInfo vci{};
    vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image    = t.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format   = kColorFormat;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    Check(vkCreateImageView(device_, &vci, nullptr, &t.view), "vkCreateImageView");

    VkDescriptorSetAllocateInfo dai{};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = blitDescPool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &blitSetLayout_;
    Check(vkAllocateDescriptorSets(device_, &dai, &t.desc), "vkAllocateDescriptorSets");

    VkDescriptorImageInfo dii{};
    dii.sampler     = sampler_;
    dii.imageView   = t.view;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet w0{};
    w0.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w0.dstSet          = t.desc;
    w0.dstBinding      = 0;
    w0.descriptorCount = 1;
    w0.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w0.pImageInfo      = &dii;
    vkUpdateDescriptorSets(device_, 1, &w0, 0, nullptr);

    // Stencil attachment for the P2 Mask/Coverage stage (Lot 3b).
    allocator_.CreateImage(w, h, stencilFormat_,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           t.stencil, t.stencilAlloc);
    VkImageViewCreateInfo sv{};
    sv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    sv.image    = t.stencil;
    sv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    sv.format   = stencilFormat_;
    // STENCIL-only view (used only as the stencil attachment); the barrier still
    // transitions both aspects of a combined format.
    sv.subresourceRange = { VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
    Check(vkCreateImageView(device_, &sv, nullptr, &t.stencilView), "vkCreateImageView (stencil)");
    // Isolation levels are created LAZILY (EnsureIsoLevel) — only views that
    // actually isolate an object/group pay their (full-view) VRAM, per depth.
}

Engine::ViewTarget::IsoLevel& Engine::EnsureIsoLevel(ViewTarget& t, int depth, bool withBackdrop) {
    // depth is 1-based (level 0 = the canvas). The pool is RESERVED once to its max so
    // a deeper level's resize never REALLOCATES — otherwise an IsoLevel& held by an
    // outer recursion frame would dangle (use-after-free → driver SIGSEGV). Grow the
    // size up to depth (within the reserved capacity, so addresses stay stable).
    if (t.isoLevels.capacity() < (size_t)kMaxIsoDepth) t.isoLevels.reserve(kMaxIsoDepth);
    if (depth > kMaxIsoDepth) depth = kMaxIsoDepth;                 // clamp pathological nesting
    if ((int)t.isoLevels.size() < depth) t.isoLevels.resize(depth);
    ViewTarget::IsoLevel& L = t.isoLevels[(size_t)depth - 1];
    const uint32_t w = t.w, h = t.h;

    if (L.color == VK_NULL_HANDLE) {
        // Isolation colour (sampled by the composite) + its own stencil.
        allocator_.CreateImage(w, h, kColorFormat,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT,   // a nested level copies it to a backdrop
                               L.color, L.colorAlloc);
        VkImageViewCreateInfo iv{};
        iv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image    = L.color; iv.viewType = VK_IMAGE_VIEW_TYPE_2D; iv.format = kColorFormat;
        iv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        Check(vkCreateImageView(device_, &iv, nullptr, &L.colorView), "vkCreateImageView (iso level)");

        VkDescriptorSetAllocateInfo idai{};
        idai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        idai.descriptorPool = blitDescPool_; idai.descriptorSetCount = 1; idai.pSetLayouts = &blitSetLayout_;
        Check(vkAllocateDescriptorSets(device_, &idai, &L.desc), "vkAllocateDescriptorSets (iso level)");
        VkDescriptorImageInfo idii{};
        idii.sampler = sampler_; idii.imageView = L.colorView; idii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet iw{};
        iw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; iw.dstSet = L.desc;
        iw.dstBinding = 0; iw.descriptorCount = 1;
        iw.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; iw.pImageInfo = &idii;
        vkUpdateDescriptorSets(device_, 1, &iw, 0, nullptr);

        allocator_.CreateImage(w, h, stencilFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                               L.stencil, L.stencilAlloc);
        VkImageViewCreateInfo isv{};
        isv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        isv.image = L.stencil; isv.viewType = VK_IMAGE_VIEW_TYPE_2D; isv.format = stencilFormat_;
        isv.subresourceRange = { VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1 };
        Check(vkCreateImageView(device_, &isv, nullptr, &L.stencilView), "vkCreateImageView (iso level stencil)");
    }

    if (withBackdrop && L.backdrop == VK_NULL_HANDLE) {
        allocator_.CreateImage(w, h, kColorFormat,
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               L.backdrop, L.backdropAlloc);
        VkImageViewCreateInfo bv{};
        bv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        bv.image = L.backdrop; bv.viewType = VK_IMAGE_VIEW_TYPE_2D; bv.format = kColorFormat;
        bv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        Check(vkCreateImageView(device_, &bv, nullptr, &L.backdropView), "vkCreateImageView (level backdrop)");

        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = blitDescPool_; dai.descriptorSetCount = 1; dai.pSetLayouts = &blendSetLayout_;
        Check(vkAllocateDescriptorSets(device_, &dai, &L.blendDesc), "vkAllocateDescriptorSets (level blend)");
        VkDescriptorImageInfo dii[2]{};
        dii[0].sampler = sampler_; dii[0].imageView = L.colorView;    dii[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dii[1].sampler = sampler_; dii[1].imageView = L.backdropView; dii[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet wr[2]{};
        wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[0].dstSet = L.blendDesc;
        wr[0].dstBinding = 0; wr[0].descriptorCount = 1;
        wr[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr[0].pImageInfo = &dii[0];
        wr[1] = wr[0]; wr[1].dstBinding = 1; wr[1].pImageInfo = &dii[1];
        vkUpdateDescriptorSets(device_, 2, wr, 0, nullptr);
    }
    return L;
}

void Engine::EnsurePickTarget(ViewTarget& t) {
    if (t.pickImg != VK_NULL_HANDLE) return;
    const uint32_t w = t.w, h = t.h;
    // R32UI id image: a colour attachment for the id-pass, TRANSFER_SRC so the one
    // requested pixel can be copied out to a host buffer for async readback.
    allocator_.CreateImage(w, h, kPickFormat,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           t.pickImg, t.pickAlloc);
    VkImageViewCreateInfo pv{};
    pv.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    pv.image    = t.pickImg;
    pv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    pv.format   = kPickFormat;
    pv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    Check(vkCreateImageView(device_, &pv, nullptr, &t.pickView), "vkCreateImageView (pick)");

    // 1-pixel host-visible readback buffer (a single R32UI) + a fence so the next
    // frame can poll last frame's copy without stalling the GPU.
    allocator_.CreateHostBuffer(sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                t.pickReadBuf, t.pickReadAlloc, &t.pickReadMapped);
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    Check(vkCreateFence(device_, &fci, nullptr, &t.pickFence), "vkCreateFence (pick)");
}

void Engine::EnsureTargetVbo(ViewTarget& t, VkDeviceSize bytes) {
    if (t.vbo != VK_NULL_HANDLE && t.vboCap >= bytes) return;
    if (t.vbo) { allocator_.DestroyBuffer(t.vbo, t.vboAlloc); t.vbo = VK_NULL_HANDLE; t.vboAlloc = nullptr; t.vboMapped = nullptr; }
    VkDeviceSize cap = 4096;
    while (cap < bytes) cap *= 2;
    allocator_.CreateHostBuffer(cap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                t.vbo, t.vboAlloc, &t.vboMapped);
    t.vboCap = cap;
}

void Engine::EnsureTargetMaskVbo(ViewTarget& t, VkDeviceSize bytes) {
    if (t.maskVbo != VK_NULL_HANDLE && t.maskCap >= bytes) return;
    if (t.maskVbo) { allocator_.DestroyBuffer(t.maskVbo, t.maskAlloc); t.maskVbo = VK_NULL_HANDLE; t.maskAlloc = nullptr; t.maskMapped = nullptr; }
    VkDeviceSize cap = 4096;
    while (cap < bytes) cap *= 2;
    allocator_.CreateHostBuffer(cap, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                t.maskVbo, t.maskAlloc, &t.maskMapped);
    t.maskCap = cap;
}

void Engine::DestroyTarget(ViewTarget& t) {
    if (t.desc)  { vkFreeDescriptorSets(device_, blitDescPool_, 1, &t.desc); t.desc = VK_NULL_HANDLE; }
    if (t.view)  { vkDestroyImageView(device_, t.view, nullptr); t.view = VK_NULL_HANDLE; }
    if (t.image) { allocator_.DestroyImage(t.image, t.alloc); t.image = VK_NULL_HANDLE; t.alloc = nullptr; }
    if (t.stencilView) { vkDestroyImageView(device_, t.stencilView, nullptr); t.stencilView = VK_NULL_HANDLE; }
    if (t.stencil) { allocator_.DestroyImage(t.stencil, t.stencilAlloc); t.stencil = VK_NULL_HANDLE; t.stencilAlloc = nullptr; }
    // Isolation levels (Lot 11-4e). Free every allocated level's resources.
    for (ViewTarget::IsoLevel& L : t.isoLevels) {
        if (L.desc) vkFreeDescriptorSets(device_, blitDescPool_, 1, &L.desc);
        if (L.colorView) vkDestroyImageView(device_, L.colorView, nullptr);
        if (L.color) allocator_.DestroyImage(L.color, L.colorAlloc);
        if (L.stencilView) vkDestroyImageView(device_, L.stencilView, nullptr);
        if (L.stencil) allocator_.DestroyImage(L.stencil, L.stencilAlloc);
        if (L.blendDesc) vkFreeDescriptorSets(device_, blitDescPool_, 1, &L.blendDesc);
        if (L.backdropView) vkDestroyImageView(device_, L.backdropView, nullptr);
        if (L.backdrop) allocator_.DestroyImage(L.backdrop, L.backdropAlloc);
    }
    t.isoLevels.clear();
    if (t.vbo)   { allocator_.DestroyBuffer(t.vbo, t.vboAlloc); t.vbo = VK_NULL_HANDLE; t.vboAlloc = nullptr; t.vboMapped = nullptr; t.vboCap = 0; }
    if (t.maskVbo) { allocator_.DestroyBuffer(t.maskVbo, t.maskAlloc); t.maskVbo = VK_NULL_HANDLE; t.maskAlloc = nullptr; t.maskMapped = nullptr; t.maskCap = 0; }
    t.fillPool.Destroy();   // Lot 13-1b: persistent per-shape fill pool
    // Picking id-pass resources (Lot 8). DestroyTarget runs after vkDeviceWaitIdle,
    // so any in-flight readback has completed.
    if (t.pickView) { vkDestroyImageView(device_, t.pickView, nullptr); t.pickView = VK_NULL_HANDLE; }
    if (t.pickImg) { allocator_.DestroyImage(t.pickImg, t.pickAlloc); t.pickImg = VK_NULL_HANDLE; t.pickAlloc = nullptr; }
    if (t.pickReadBuf) { allocator_.DestroyBuffer(t.pickReadBuf, t.pickReadAlloc); t.pickReadBuf = VK_NULL_HANDLE; t.pickReadAlloc = nullptr; t.pickReadMapped = nullptr; }
    if (t.pickFence) { vkDestroyFence(device_, t.pickFence, nullptr); t.pickFence = VK_NULL_HANDLE; }
    t.pickPending = false; t.pickRequested = false; t.pickIds.clear(); t.pickIdsInFlight.clear();
    t.vertexCount = 0; t.maskVertexCount = 0;
    t.fillPages.clear(); t.hasGeom = false; t.buildSig = 0;
}

Engine::ViewTarget& Engine::AcquireTarget(const void* key, uint32_t w, uint32_t h) {
    ViewTarget& t = targets_[key];
    if (t.image == VK_NULL_HANDLE) {
        CreateTargetImages(t, w, h);
    } else if (t.w != w || t.h != h) {
        vkDeviceWaitIdle(device_);   // resize is rare; keep it simple
        DestroyTarget(t);
        CreateTargetImages(t, w, h);
    }
    return t;
}

Engine::SubmitSlot& Engine::AcquireSlot() {
    if ((size_t)viewsThisFrame_ >= slots_.size()) {
        SubmitSlot s{};
        VkCommandBufferAllocateInfo cai{};
        cai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool        = cmdPool_;
        cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        vkAllocateCommandBuffers(device_, &cai, &s.cmd);
        VkFenceCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device_, &fci, nullptr, &s.fence);
        VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore(device_, &sci, nullptr, &s.sem);
        if (timestampPeriodNs_ > 0.0f) {   // GPU timing supported (Lot 13-0)
            VkQueryPoolCreateInfo qci{};
            qci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            qci.queryType = VK_QUERY_TYPE_TIMESTAMP; qci.queryCount = 2;
            vkCreateQueryPool(device_, &qci, nullptr, &s.tsPool);
        }
        slots_.push_back(s);
    }
    return slots_[viewsThisFrame_];
}

// ── Frame ──────────────────────────────────────────────────────────────────────

} // namespace Comp
