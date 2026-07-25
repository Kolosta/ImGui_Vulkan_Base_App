#include "Render/RendererInternal.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Node UI font atlas (docs/Ink/NODE_UI.md) — a single FreeType-rasterized
//  bitmap covering ASCII 32..126 at one reference pixel size, uploaded once
//  at Renderer::Initialize and never rebuilt. Shelf-packed (simplest layout
//  that fits a small, fixed glyph set with no repacking logic needed).
//
//  Stored PREMULTIPLIED WHITE (rgb=coverage, a=coverage) so the ONE Node UI
//  fragment shader (`outColor = texture(tex,uv) * tint`) handles both this
//  atlas AND a live preview vignette's full-color image uniformly (ImGui's
//  own "white-pixel + atlas" trick, reimplemented without ImGui) — see
//  NodeUIList.h's header comment.
// ─────────────────────────────────────────────────────────────────────────────

namespace Ink::detail {

namespace {

constexpr std::uint32_t kFirstChar = 32;    // ' '
constexpr std::uint32_t kLastChar  = 126;   // '~'
constexpr std::uint32_t kAtlasW    = 512;
constexpr std::uint32_t kAtlasH    = 256;
constexpr std::uint32_t kPad       = 1;     // px between glyphs (no bilinear bleed)

} // namespace

bool BuildFontAtlas(RendererImpl& r, FontAtlasData& atlas,
                   const std::string& fontPath, VkDescriptorSetLayout setLayout) {
    atlas = FontAtlasData{};
    if (fontPath.empty()) return false;

    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft)) return false;
    FT_Face face = nullptr;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face)) {
        FT_Done_FreeType(ft);
        return false;
    }
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)atlas.referencePx)) {
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
        return false;
    }
    atlas.lineHeight = (float)(face->size->metrics.height >> 6);

    std::vector<std::uint8_t> pixels(kAtlasW * kAtlasH * 4, 0);
    std::uint32_t penX = kPad, penY = kPad, rowH = 0;

    for (std::uint32_t c = kFirstChar; c <= kLastChar; ++c) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) continue;
        const FT_GlyphSlot g = face->glyph;
        const std::uint32_t gw = g->bitmap.width, gh = g->bitmap.rows;

        if (penX + gw + kPad > kAtlasW) { penX = kPad; penY += rowH + kPad; rowH = 0; }
        if (penY + gh + kPad > kAtlasH) break;   // atlas exhausted — rest silently missing

        for (std::uint32_t row = 0; row < gh; ++row) {
            const std::uint8_t* src = g->bitmap.buffer + row * g->bitmap.pitch;
            std::uint8_t* dst = pixels.data() + ((penY + row) * kAtlasW + penX) * 4;
            for (std::uint32_t col = 0; col < gw; ++col) {
                const std::uint8_t v = src[col];
                dst[col * 4 + 0] = v; dst[col * 4 + 1] = v;
                dst[col * 4 + 2] = v; dst[col * 4 + 3] = v;
            }
        }

        GlyphInfo info;
        info.u0 = (float)penX / (float)kAtlasW;
        info.v0 = (float)penY / (float)kAtlasH;
        info.u1 = (float)(penX + gw) / (float)kAtlasW;
        info.v1 = (float)(penY + gh) / (float)kAtlasH;
        info.width    = (float)gw;
        info.height   = (float)gh;
        info.bearingX = (float)g->bitmap_left;
        info.bearingY = (float)g->bitmap_top;
        info.advance  = (float)(g->advance.x >> 6);
        atlas.glyphs[c] = info;

        penX += gw + kPad;
        rowH = std::max(rowH, gh);
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    atlas.image = rhi::CreateImage2D(r.device, kAtlasW, kAtlasH,
                                     VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT);
    if (!atlas.image) return false;

    rhi::Buffer staging = rhi::CreateHostBuffer(
        r.device, pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!staging || !staging.mapped) {
        rhi::DestroyBuffer(r.device, staging);
        rhi::DestroyImage(r.device, atlas.image);
        return false;
    }
    std::memcpy(staging.mapped, pixels.data(), pixels.size());

    rhi::Image& img = atlas.image;
    auto transition = [&](VkCommandBuffer cmd, VkImageLayout layout,
                          VkPipelineStageFlags2 stage, VkAccessFlags2 access) {
        VkImageMemoryBarrier2 b{};
        b.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask  = img.stage == VK_PIPELINE_STAGE_2_NONE
                              ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : img.stage;
        b.srcAccessMask = img.access;
        b.dstStageMask  = stage;
        b.dstAccessMask = access;
        b.oldLayout     = img.layout;
        b.newLayout     = layout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image            = img.image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkDependencyInfo dep{};
        dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers    = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
        img.layout = layout; img.stage = stage; img.access = access;
    };

    const bool uploaded = rhi::ImmediateSubmit(r.device, [&](VkCommandBuffer cmd) {
        transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent      = { kAtlasW, kAtlasH, 1 };
        vkCmdCopyBufferToImage(cmd, staging.buffer, img.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        transition(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    });
    rhi::DestroyBuffer(r.device, staging);
    if (!uploaded) { rhi::DestroyImage(r.device, atlas.image); return false; }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = r.descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &setLayout;
    if (vkAllocateDescriptorSets(r.device.vk(), &ai, &atlas.descriptorSet) != VK_SUCCESS) {
        rhi::DestroyImage(r.device, atlas.image);
        return false;
    }
    VkDescriptorImageInfo imgInfo{ r.device.linearSampler(), atlas.image.view,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = atlas.descriptorSet;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &imgInfo;
    vkUpdateDescriptorSets(r.device.vk(), 1, &w, 0, nullptr);

    atlas.ready = true;
    return true;
}

void DestroyFontAtlas(RendererImpl& r, FontAtlasData& atlas) {
    if (atlas.descriptorSet)
        vkFreeDescriptorSets(r.device.vk(), r.descriptorPool, 1, &atlas.descriptorSet);
    rhi::DestroyImage(r.device, atlas.image);
    atlas = FontAtlasData{};
}

} // namespace Ink::detail
