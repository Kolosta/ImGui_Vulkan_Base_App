#include <UI/Widgets/ScrollArea.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace UI {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }

// Per-scroll-region persistent state, keyed on the child window id. Holds the
// smoothed proximity factor (for the grow/brighten animation) and the active
// drag (so dragging keeps following the mouse outside the grab rect).
struct ScrollState {
    float prox    = 0.0f;     // 0 = rest, 1 = fully near/hovered (smoothed)
    bool  dragging = false;
    float dragGrabOffset = 0.0f;   // mouse-to-grab-top offset captured on press
};
std::unordered_map<ImGuiID, ScrollState>& States() {
    static std::unordered_map<ImGuiID, ScrollState> s;
    return s;
}

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
ImVec4 LerpCol(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t),
                  Lerp(a.z, b.z, t), Lerp(a.w, b.w, t));
}

} // namespace

bool BeginScroll(const char* id, const ImVec2& size,
                 ImGuiChildFlags childFlags, ImGuiWindowFlags extraFlags) {
    // The overlay grab lives in a `margin`-wide GUTTER reserved on the child's
    // right side. We narrow only the work/content rects (which back
    // GetContentRegionAvail and right-aligned sizing) — NOT the window padding,
    // so the LEFT inset is untouched and the content's left edge does not move.
    // The grab is centred in that reserved gutter (drawn in EndScroll), so it
    // sits to the right OF the content, never on top of it, and adds no width:
    // the child keeps its full allocated size, only the usable area is shrunk.
    bool vis = ImGui::BeginChild(id, size, childFlags,
                                 extraFlags | ImGuiWindowFlags_NoScrollbar);
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float gs = DS::DesignSystem::Instance().GetGlobalScale();
    const float gutter = Flt(Tok::C_Scrollbar_OverlayMargin) * gs;
    w->WorkRect.Max.x          = std::max(w->WorkRect.Min.x,          w->WorkRect.Max.x          - gutter);
    w->ContentRegionRect.Max.x = std::max(w->ContentRegionRect.Min.x, w->ContentRegionRect.Max.x - gutter);
    return vis;
}

void EndScroll() {
    ImGuiWindow* child = ImGui::GetCurrentWindow();
    const ImGuiID key   = child->ID;
    const float scrollMax = ImGui::GetScrollMaxY();
    const float scrollY   = ImGui::GetScrollY();

    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    const float gutter = Flt(Tok::C_Scrollbar_OverlayMargin) * gs;

    // The reserved gutter spans from the work-rect right edge (where content
    // stops) to the inner-rect right edge (child border). Centre the grab in it.
    const float trackX  = child->InnerRect.Max.x - gutter * 0.5f;
    // The true visible height drives the grab/content ratio; the TRACK is inset
    // top & bottom by the overlay padding so the grab clears the container edges
    // and never pokes past a rounded corner.
    const float pad      = Flt(Tok::C_Scrollbar_OverlayPadding) * gs;
    const float visibleH = child->InnerRect.GetHeight();
    const float trackTop = child->InnerRect.Min.y + pad;
    const float trackH   = std::max(1.0f, visibleH - pad * 2.0f);
    const float innerH   = visibleH;

    ScrollState& st = States()[key];

    // Only show + interact when the content actually overflows.
    const bool scrollable = (scrollMax > 0.5f) && (innerH > 1.0f);
    if (!scrollable) { st.prox = 0.0f; st.dragging = false; ImGui::EndChild(); return; }

    const float wRest  = Flt(Tok::C_Scrollbar_OverlayWidthRest)  * gs;
    const float wHover = Flt(Tok::C_Scrollbar_OverlayWidthHover) * gs;
    const float reach  = Flt(Tok::C_Scrollbar_OverlayProximity)  * gs;
    const float radius = Flt(Tok::C_Scrollbar_CornerRadius)      * gs;

    // Grab height/position from the visible/content ratio (native-scrollbar
    // maths): the grab covers `visibleH / contentH` of the (inset) track.
    const float contentH = innerH + scrollMax;
    const float grabH = std::max(wHover * 2.0f, trackH * (innerH / contentH));
    const float scrollFrac = (scrollMax > 0.0f) ? (scrollY / scrollMax) : 0.0f;
    const float grabTop = trackTop + (trackH - grabH) * scrollFrac;

    // ── Proximity / hover factor ────────────────────────────────────────────
    // Horizontal distance from the cursor to the gutter centre; 0 px → 1,
    // `reach` px → 0. Counts only within the track's vertical span.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool inRow = mouse.y >= trackTop && mouse.y <= trackTop + trackH;
    float target = 0.0f;
    if (inRow) {
        const float dist = std::abs(mouse.x - trackX);
        target = Clamp01(1.0f - dist / std::max(1.0f, reach));
    }
    if (st.dragging) target = 1.0f;
    const float dt = ImGui::GetIO().DeltaTime;
    st.prox = Lerp(st.prox, target, Clamp01(dt * 12.0f));
    if (std::abs(st.prox - target) < 0.005f) st.prox = target;

    const float curW = Lerp(wRest, wHover, st.prox);

    // ── Drag handling (geometric, no item — never disturbs the content) ──────
    const float halfHit = std::max(curW, gutter) * 0.5f + 2.0f * gs;
    const bool overGrab =
        mouse.x >= trackX - halfHit && mouse.x <= trackX + halfHit &&
        mouse.y >= grabTop && mouse.y <= grabTop + grabH;
    const bool overTrack =
        mouse.x >= trackX - halfHit && mouse.x <= trackX + halfHit && inRow;

    if (!st.dragging && overGrab && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        st.dragging = true;
        st.dragGrabOffset = mouse.y - grabTop;
    } else if (!st.dragging && overTrack && !overGrab &&
               ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        st.dragging = true;             // track click: grab jumps under cursor
        st.dragGrabOffset = grabH * 0.5f;
    }
    if (st.dragging) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            st.dragging = false;
        } else if (trackH - grabH > 0.5f) {
            const float newTop = mouse.y - st.dragGrabOffset;
            const float frac = (newTop - trackTop) / (trackH - grabH);
            ImGui::SetScrollY(child, Clamp01(frac) * scrollMax);
        }
    }

    // ── Draw the grab in the reserved gutter (on the child's draw list) ──────
    // Re-read the (possibly drag-updated) scroll straight off the child window.
    const float drawFrac =
        (scrollMax > 0.0f) ? (child->Scroll.y / scrollMax) : 0.0f;
    const float drawTop = trackTop + (trackH - grabH) * drawFrac;

    ImVec4 restCol = Col(Tok::C_Scrollbar_Grab);
    ImVec4 nearCol = Col(st.dragging ? Tok::C_Scrollbar_GrabDown
                                     : Tok::C_Scrollbar_GrabHover);
    ImVec4 col = LerpCol(restCol, nearCol, st.prox);

    // The gutter is empty (content was narrowed to its left), so drawing on the
    // child's own draw list keeps the grab above the background yet clear of any
    // panel — no z-order fight with the content.
    ImDrawList* dl = child->DrawList;
    ImVec2 gMin(trackX - curW * 0.5f, drawTop);
    ImVec2 gMax(trackX + curW * 0.5f, drawTop + grabH);
    // Rounded ends: clamp the corner-radius token to a half-width so a thin grab
    // reads as a full capsule (token may ask for more, never less than makes
    // geometric sense at the current width).
    const float r = std::min(radius, curW * 0.5f);
    dl->AddRectFilled(gMin, gMax, ImGui::ColorConvertFloat4ToU32(col), r);

    ImGui::EndChild();
}

} // namespace UI
