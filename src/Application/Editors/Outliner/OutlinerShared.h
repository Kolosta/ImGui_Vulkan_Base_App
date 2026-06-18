#pragma once
// Internal Outliner helpers shared by the tree view and the context menus:
// the inline-rename scratch state and the collection-icon colour palette.
// These were file-static in Outliner.cpp; `inline` here gives one shared
// instance across the two Outliner*.cpp translation units.
#include <DesignSystem/DesignSystem.h>
#include <Renderer/Document/Document.h>
#include <imgui.h>
#include <cstdint>

namespace App {

namespace DST = DesignSystem;

// Per-frame scratch for inline renaming (one rename at a time across the tree).
// The id is the target: a shape id (no tag bit), a collection id | kCollBit, or
// an artboard id | kPageBit.
inline uint64_t s_renameId   = 0;
inline char     s_renameBuf[128] = {0};
constexpr uint64_t kCollBit = 1ull << 63;   // tag a rename target as a collection
constexpr uint64_t kPageBit = 1ull << 62;   // tag a rename target as a page

// Collection icon colour palette. Index 0 = default (theme text); 1..N map to a
// design-system primitive hue (resolved at draw time so it follows the theme).
struct Hue { const char* name; DST::Tok tok; };
inline constexpr Hue kCollHues[] = {
    { "Cyan",       DST::Tok::P_Color_Cyan_500       },
    { "Indigo",     DST::Tok::P_Color_Indigo_500     },
    { "Cinnamon",   DST::Tok::P_Color_Cinnamon_500   },
    { "Green",      DST::Tok::P_Color_Green_500       },
    { "Yellow",     DST::Tok::P_Color_Yellow_500     },
    { "Orange",     DST::Tok::P_Color_Orange_500     },
    { "Red",        DST::Tok::P_Color_Red_500         },
    { "Magenta",    DST::Tok::P_Color_Magenta_500    },
    { "Purple",     DST::Tok::P_Color_Purple_500     },
    { "Turquoise",  DST::Tok::P_Color_Turquoise_500  },
};
constexpr int kNumCollHues = (int)(sizeof(kCollHues) / sizeof(kCollHues[0]));

// Resolve a collection's icon colour to an ImU32. colorIndex 0 = theme text,
// 1..N = palette hue, −1 = its stored customColor.
inline ImU32 CollectionIconColor(const Renderer::Collection& c) {
    auto& ds = DST::DesignSystem::Instance();
    if (c.colorIndex < 0)
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(c.customColor.r, c.customColor.g, c.customColor.b, c.customColor.a));
    if (c.colorIndex == 0)
        return ImGui::ColorConvertFloat4ToU32(ds.GetColor(DST::Tok::S_Color_Text_Subtle));
    int i = (c.colorIndex - 1) % kNumCollHues;
    return ImGui::ColorConvertFloat4ToU32(ds.GetColor(kCollHues[(size_t)i].tok));
}

} // namespace App
