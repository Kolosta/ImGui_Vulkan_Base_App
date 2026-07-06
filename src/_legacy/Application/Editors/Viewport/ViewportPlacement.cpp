#include "Application.h"
#include "ViewportToolsShared.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ToolManager.h>
#include <Shortcuts/ShortcutManager.h>
#include <VectorGraphics/IconManager.h>
#include <UI/Widgets/PopupMenu.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace App {

using Renderer::Vec2;

// Local-space bbox centre of a shape's combined outline (all parts).
static Renderer::Vec2 LocalCenter(Renderer::Shape& s) {
    Renderer::Vec2 mn{1e30f, 1e30f}, mx{-1e30f, -1e30f};
    bool any = false;
    for (const Renderer::Part& part : s.parts) {
        bool cl = false;
        std::vector<Renderer::Vec2> w =
            Renderer::Tessellator::OutlinePart(s, part, 1.0f, cl);
        for (auto& p : w) {
            Renderer::Vec2 l = Renderer::Tessellator::InverseTransform(s, p);
            mn.x = std::min(mn.x, l.x); mn.y = std::min(mn.y, l.y);
            mx.x = std::max(mx.x, l.x); mx.y = std::max(mx.y, l.y);
            any = true;
        }
    }
    return any ? Renderer::Vec2{ (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f } : s.origin;
}
// Move a shape's origin marker to a new WORLD point, keeping the geometry
// visually fixed. We solve for the local origin + translate so that:
//   (a) the geometry's world position is unchanged, and
//   (b) the origin marker lands exactly on `worldTarget`.
// Geometry world = origin + R·S·(local − origin) + translate. Requiring it
// constant for all `local` forces translate to absorb the change in
// (origin − R·S·origin); then translate is offset so World(origin) == target.
static void MoveOriginToWorld(Renderer::Shape& s, Renderer::Vec2 worldTarget,
                              Renderer::Vec2 pageOrigin = {0, 0}) {
    // Snapshot a reference geometry world point (origin's current world pos) so
    // we can keep the picture fixed across the change.
    auto rotS = [&](Renderer::Vec2 v) {
        v.x *= s.transform.scale.x; v.y *= s.transform.scale.y;
        float c = std::cos(s.transform.rotate), sn = std::sin(s.transform.rotate);
        return Renderer::Vec2{ v.x * c - v.y * sn, v.x * sn + v.y * c };
    };
    // World of an arbitrary local point BEFORE the change (to preserve geometry).
    Renderer::Vec2 newOriginLocal =
        Renderer::Tessellator::InverseTransform(s, worldTarget, pageOrigin);
    // Keep geometry fixed: world(local) must be unchanged. With origin o and
    // translate t: world = o + RS(local-o) + t = RS·local + (o - RS·o + t).
    // The constant term C = o - RS·o + t must stay the same. Solve t' for the
    // new origin o': t' = C - (o' - RS·o').
    Renderer::Vec2 oOld = s.origin, tOld = s.transform.translate;
    Renderer::Vec2 RSoOld = rotS(oOld);
    Renderer::Vec2 C{ oOld.x - RSoOld.x + tOld.x, oOld.y - RSoOld.y + tOld.y };
    Renderer::Vec2 RSoNew = rotS(newOriginLocal);
    s.origin = newOriginLocal;
    s.transform.translate = { C.x - (newOriginLocal.x - RSoNew.x),
                              C.y - (newOriginLocal.y - RSoNew.y) };
}


// Object context menu — uses the shared UI::ContextMenu (same look as the
// Dropdown menu), opened at the right-click position. Each row shows its bound
// shortcut. The menu captures input, so the tool handler is gated while it is
// open (see RenderViewport) to stop click-through.
void Application::RenderViewportContextMenu() {
    auto& sm  = Shortcuts::ShortcutManager::Instance();
    auto& doc = project_.document;
    const bool hasSel = doc.HasSelection();
    const bool multi  = doc.Selection().size() >= 2;

    // Pull label/shortcut/description for a registered action into a menu entry
    // (Lot 4: every menu row carries its action's description as a dwell tooltip
    // and its bound shortcut, kept in sync with the keymap automatically).
    auto fromAction = [&](const char* actionId, const char* fallbackLabel) {
        UI::MenuEntry e;
        const Shortcuts::Action* a = sm.GetAction(actionId);
        e.label    = (a && !a->name.empty()) ? a->name : fallbackLabel;
        e.shortcut = sm.GetShortcutString(actionId);
        if (a) e.tooltip = a->description;
        return e;
    };

    std::vector<UI::MenuEntry> entries;
    {
        UI::MenuEntry e = fromAction("edit.deleteSelection", "Delete");
        e.icon = "ink-eraser";
        e.enabled = hasSel; e.onClick = [this]{ Action_DeleteSelection(); };
        entries.push_back(std::move(e));
    }
    // Join is typed: two objects can merge only if they share a FAMILY
    // (Mesh↔Mesh, or any curve-like↔curve-like). A mixed selection greys Join
    // out with a tooltip explaining why, and offers "Convert all & Join"
    // shortcuts (Lot 6). Compute whether the selection is single-family.
    bool sameFamily = true; bool haveFamily = false; Renderer::PartType selFamily{};
    for (uint64_t id : doc.Selection()) {
        Renderer::Shape* s = doc.FindShape(id);
        if (!s) continue;
        Renderer::PartType f = s->Family();
        if (!haveFamily) { selFamily = f; haveFamily = true; }
        else if (f != selFamily) { sameFamily = false; break; }
    }
    {
        UI::MenuEntry e = fromAction("edit.joinSelection", "Join");
        const bool joinable = multi && sameFamily;
        e.enabled = joinable; e.onClick = [this]{ Action_JoinSelection(); };
        if (!multi)            e.tooltip = "Select at least two objects to join them";
        else if (!sameFamily)  e.tooltip = "Cannot join: the selection mixes Mesh and "
                                           "Curve types. Convert them to one type first "
                                           "(see \"Convert & Join\" below).";
        entries.push_back(std::move(e));
    }
    // When the selection mixes families, offer one-click "convert everything to
    // X, then Join" so the user doesn't have to do it in two steps.
    if (multi && !sameFamily) {
        UI::MenuEntry cj; cj.label = "Convert & Join"; cj.enabled = true;
        { UI::MenuEntry e; e.label = "All to Mesh & Join";
          e.tooltip = "Convert every selected object to Mesh, then join them";
          e.onClick = [this]{ Action_ConvertAllAndJoin(Renderer::PartType::Mesh); };
          cj.submenu.push_back(std::move(e)); }
        { UI::MenuEntry e; e.label = "All to Curve & Join";
          e.tooltip = "Convert every selected object to Curve, then join them";
          e.onClick = [this]{ Action_ConvertAllAndJoin(Renderer::PartType::Curve); };
          cj.submenu.push_back(std::move(e)); }
        entries.push_back(std::move(cj));
    }
    // Layer groups (Lot 11): Group the selection, Ungroup, or Select Group (select
    // the whole group of the object under the cursor / active object).
    {
        UI::MenuEntry e = fromAction("edit.group", "Group");
        e.enabled = hasSel; e.onClick = [this]{ Action_GroupSelection(); };
        entries.push_back(std::move(e));
    }
    {
        bool inGroup = false;
        for (uint64_t id : doc.Selection()) if (doc.GroupOfShape(id)) { inGroup = true; break; }
        UI::MenuEntry e = fromAction("edit.ungroup", "Ungroup");
        e.enabled = inGroup; e.onClick = [this]{ Action_UngroupSelection(); };
        entries.push_back(std::move(e));
    }
    {
        uint64_t g = doc.GroupOfShape(doc.ActiveId());
        UI::MenuEntry e; e.label = "Select Group"; e.enabled = (g != 0);
        e.tooltip = "Select the whole layer group of the active object";
        e.onClick = [this]{
            uint64_t gg = project_.document.GroupOfShape(project_.document.ActiveId());
            if (gg) { project_.document.SelectGroup(gg); MarkUndoLabel("Select Group"); }
        };
        entries.push_back(std::move(e));
    }
    // Convert To ▸ (Mesh / Curve) — Object-mode operation on the selection. This
    // switches the FAMILY (Mesh ⇄ Curve). The spline kind (Bézier/NURBS/Poly) is
    // chosen in Edit Mode via "Set Spline Type". The parent is just a grouping
    // label (no action) → no tooltip; the leaves carry the descriptions.
    {
        UI::MenuEntry c; c.label = "Convert To"; c.enabled = hasSel;
        auto leaf = [&](const char* label, Renderer::PartType t, const char* tip) {
            UI::MenuEntry e; e.label = label; e.tooltip = tip;
            e.onClick = [this, t]{ Action_ConvertSelectionTo(t); };
            c.submenu.push_back(std::move(e));
        };
        leaf("Mesh",  Renderer::PartType::Mesh,  "Straight-edge mesh: vertex / edge / face editing");
        leaf("Curve", Renderer::PartType::Curve, "Vector curve: point editing (set Bézier/NURBS/Poly in Edit Mode)");
        entries.push_back(std::move(c));
    }
    {
        UI::MenuEntry setOrigin; setOrigin.label = "Set Origin"; setOrigin.enabled = hasSel;
        {
            UI::MenuEntry e; e.label = "Origin to Geometry";
            e.tooltip = "Move the object's origin to the centre of its geometry (geometry stays put)";
            e.onClick = [this]{
                // Move the ORIGIN marker to the geometry's world centre; the
                // geometry stays put.
                for (uint64_t id : project_.document.Selection())
                    if (Renderer::Shape* s = project_.document.FindShape(id)) {
                        Renderer::Vec2 po = CurPageOriginOfShape(id);
                        Renderer::Vec2 mn, mx;
                        if (Renderer::Tessellator::WorldBounds(*s, 1.0f, mn, mx, po))
                            MoveOriginToWorld(*s, { (mn.x + mx.x) * 0.5f,
                                                    (mn.y + mx.y) * 0.5f }, po);
                        project_.dirty = true;
                    }
            };
            setOrigin.submenu.push_back(std::move(e));
        }
        {
            UI::MenuEntry e; e.label = "Geometry to Origin";
            e.tooltip = "Move the geometry so it is centred on the object's origin";
            e.onClick = [this]{
                for (uint64_t id : project_.document.Selection())
                    if (Renderer::Shape* s = project_.document.FindShape(id)) {
                        Renderer::Vec2 c = LocalCenter(*s);
                        float dx = s->origin.x - c.x, dy = s->origin.y - c.y;
                        for (Renderer::Part& part : s->parts) {
                            for (Renderer::Node& n : part.path.nodes) {
                                n.pos.x  += dx; n.pos.y  += dy;
                                n.hIn.x  += dx; n.hIn.y  += dy;
                                n.hOut.x += dx; n.hOut.y += dy;
                            }
                            part.pos.x += dx; part.pos.y += dy;
                        }
                        project_.dirty = true;
                    }
            };
            setOrigin.submenu.push_back(std::move(e));
        }
        {
            UI::MenuEntry e; e.label = "Origin to 2D Cursor";
            e.tooltip = "Move the object's origin onto the 2D cursor (geometry stays put)";
            e.onClick = [this]{
                // Move the ORIGIN marker onto the 2D cursor; geometry stays put.
                for (uint64_t id : project_.document.Selection())
                    if (Renderer::Shape* s = project_.document.FindShape(id)) {
                        MoveOriginToWorld(*s, project_.document.cursor,
                                          CurPageOriginOfShape(id));
                        project_.dirty = true;
                    }
            };
            setOrigin.submenu.push_back(std::move(e));
        }
        entries.push_back(std::move(setOrigin));
    }

    UI::ContextMenu("##viewportCtx", viewportMenuPos_, entries, "Object");
}

// ── Page (artboard) context menu ──────────────────────────────────────────────
// Right-clicking a page's name label opens this. Lets the user pick how the .acu
// thumbnail is framed (whole page, or an interactive crop zone) and resize the
// page. `pageCtxArtboard_` was captured when the menu was requested.
void Application::RenderPageContextMenu() {
    const int ab = pageCtxArtboard_;
    const bool valid = ab >= 0 && ab < (int)project_.artboards().size();

    std::vector<UI::MenuEntry> entries;
    {
        UI::MenuEntry def; def.label = "Define Thumbnail"; def.icon = "image";
        def.enabled = valid;
        {
            UI::MenuEntry e; e.label = "Whole Page";
            e.tooltip = "Use the whole page as the .acu thumbnail";
            e.onClick = [this, ab]{
                Action_UpdateThumbnail(ab, {0, 0}, {0, 0});   // whole artboard
            };
            def.submenu.push_back(std::move(e));
        }
        {
            UI::MenuEntry e; e.label = "Zone…";
            e.tooltip = "Draw a crop rectangle to frame the thumbnail";
            e.onClick = [this, ab]{ BeginThumbnailCrop(ab); };
            def.submenu.push_back(std::move(e));
        }
        entries.push_back(std::move(def));
    }
    {
        UI::MenuEntry e; e.label = "Rename Page…"; e.icon = "label";
        e.enabled = valid;
        e.tooltip = "Rename this page";
        e.onClick = [this, ab]{
            renamePageRequest_ = true; renamePageArtboard_ = ab;
            if (ab >= 0 && ab < (int)project_.artboards().size())
                std::snprintf(renamePageBuf_, sizeof(renamePageBuf_), "%s",
                              project_.artboards()[(size_t)ab].name.c_str());
        };
        entries.push_back(std::move(e));
    }
    {
        UI::MenuEntry e; e.label = "Resize Page…"; e.icon = "image-aspect-ratio";
        e.enabled = valid;
        e.tooltip = "Change this page's dimensions";
        e.onClick = [this, ab]{ resizePageRequest_ = true; resizePageArtboard_ = ab; };
        entries.push_back(std::move(e));
    }
    {
        // Toggle: clip this page's objects to its bounds (overflow into the void
        // is hidden — only the selection outline shows it). Per-page, off default.
        bool on = valid && project_.artboards()[(size_t)ab].clipContents;
        UI::MenuEntry e;
        e.label = std::string(on ? "[x] " : "[ ] ") + "Clip Contents to Page";
        e.tooltip = "Clip this page's objects to its bounds (hide overflow)";
        e.enabled = valid;
        e.onClick = [this, ab]{
            if (ab >= 0 && ab < (int)project_.artboards().size()) {
                auto& a = project_.artboards()[(size_t)ab];
                a.clipContents = !a.clipContents;
                MarkUndoLabel("Toggle page clip");
                project_.dirty = true;
            }
        };
        entries.push_back(std::move(e));
    }

    UI::ContextMenu("##pageCtx", pageCtxPos_, entries, "Page");
}

// Enter the interactive "Zone" thumbnail crop for `artboard`: seed a centred
// rectangle (60% of the page) the user can then move/resize, and arm a fresh
// drag so an immediate press-drag redefines it from scratch. Enter confirms.
void Application::BeginThumbnailCrop(int artboard) {
    if (artboard < 0 || artboard >= (int)project_.artboards().size()) return;
    const Renderer::Artboard& art = project_.artboards()[(size_t)artboard];
    cropArtboard_ = artboard;
    Renderer::Vec2 c{ art.pos.x + art.size.x * 0.5f,
                      art.pos.y + art.size.y * 0.5f };
    Renderer::Vec2 h{ art.size.x * 0.3f, art.size.y * 0.3f };
    cropMin_ = { c.x - h.x, c.y - h.y };
    cropMax_ = { c.x + h.x, c.y + h.y };
    cropDrag_ = -1;        // a press redefines/moves/resizes; nothing held yet
    cropOwner_ = nullptr;  // the first hovered Viewport leaf will claim it
}

// Drive the interactive crop overlay. Coordinates: the crop rect is stored in
// doc-units; `d2sDoc`/`s2dDoc` map doc-units ↔ screen (already unit-scaled by
// the caller, so we pass plain doc-units). Press-drag in empty space redefines
// the rect; dragging a corner/edge handle resizes; dragging inside moves it.
// Enter renders the thumbnail from the rect; Esc or RMB cancels.
void Application::HandleThumbnailCrop(const std::function<ImVec2(Renderer::Vec2)>& d2sDoc,
                                      const std::function<Renderer::Vec2(ImVec2)>& s2dDoc,
                                      float /*pxPer*/, bool hovered,
                                      App::OverlayDL& dl) {
    if (cropArtboard_ < 0) return;
    ImGuiIO& io = ImGui::GetIO();
    auto& ds = DesignSystem::DesignSystem::Instance();

    // Cancel (Esc / RMB) — leave the thumbnail untouched.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        cropArtboard_ = -1; cropDrag_ = -1; cropOwner_ = nullptr;
        rmbConsumedByTransform_ = true;   // swallow this RMB (no context menu)
        return;
    }
    // Confirm (Enter) — render the thumbnail from the normalised rect.
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
        Renderer::Vec2 mn{ std::min(cropMin_.x, cropMax_.x),
                           std::min(cropMin_.y, cropMax_.y) };
        Renderer::Vec2 mx{ std::max(cropMin_.x, cropMax_.x),
                           std::max(cropMin_.y, cropMax_.y) };
        int ab = cropArtboard_;
        cropArtboard_ = -1; cropDrag_ = -1; cropOwner_ = nullptr;
        Action_UpdateThumbnail(ab, mn, { mx.x - mn.x, mx.y - mn.y });
        return;
    }

    // Normalised rect (doc-units) → screen, recomputed each frame.
    auto screenRect = [&](ImVec2& r0, ImVec2& r1) {
        Renderer::Vec2 mn{ std::min(cropMin_.x, cropMax_.x),
                           std::min(cropMin_.y, cropMax_.y) };
        Renderer::Vec2 mx{ std::max(cropMin_.x, cropMax_.x),
                           std::max(cropMin_.y, cropMax_.y) };
        ImVec2 a = d2sDoc(mn), b = d2sDoc(mx);
        r0 = ImVec2(std::min(a.x, b.x), std::min(a.y, b.y));
        r1 = ImVec2(std::max(a.x, b.x), std::max(a.y, b.y));
    };
    ImVec2 r0, r1; screenRect(r0, r1);

    // Handle layout (screen px). hx/hy: which side each axis drives (-1 = min,
    // +1 = max, 0 = none). 0..3 corners, 4..7 edge midpoints.
    const float kHalf = 4.0f * ds.GetGlobalScale();   // handle half-size (small)
    const float kHit  = 7.0f * ds.GetGlobalScale();   // hit half-size (a bit larger)
    struct H { ImVec2 p; int hx; int hy; };
    ImVec2 mid((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f);
    H handles[8] = {
        {{r0.x, r0.y}, -1, -1}, {{r1.x, r0.y}, +1, -1},
        {{r0.x, r1.y}, -1, +1}, {{r1.x, r1.y}, +1, +1},
        {{mid.x, r0.y},  0, -1}, {{mid.x, r1.y},  0, +1},
        {{r0.x, mid.y}, -1,  0}, {{r1.x, mid.y}, +1,  0},
    };

    // ── Begin a drag: pick handle / body / empty, and anchor to the cursor ────
    if (hovered && cropDrag_ == -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        int pick = -1;
        for (int i = 0; i < 8; ++i)
            if (std::fabs(io.MousePos.x - handles[i].p.x) <= kHit &&
                std::fabs(io.MousePos.y - handles[i].p.y) <= kHit) { pick = i; break; }
        if (pick < 0) {
            bool inside = io.MousePos.x >= r0.x && io.MousePos.x <= r1.x &&
                          io.MousePos.y >= r0.y && io.MousePos.y <= r1.y;
            pick = inside ? -2 : -3;          // -2 move, -3 define a new rect
        }
        cropDrag_      = pick;
        cropDragRef_   = s2dDoc(io.MousePos);   // grab anchor (doc-units)
        cropRect0Min_  = cropMin_;
        cropRect0Max_  = cropMax_;
        cropEdgeX_ = cropEdgeY_ = 0;
        if (pick == -3) {                       // define: start a zero rect here
            cropMin_ = cropMax_ = cropDragRef_;
            cropRect0Min_ = cropRect0Max_ = cropDragRef_;
        } else if (pick >= 0) {
            // Map the visual handle side to the REAL component it grabs. The
            // visual "left" edge (hx<0) is the smaller screen-X = the smaller
            // doc-X component; capture which of min/max that is NOW, so the held
            // edge keeps following the cursor even if the rect later inverts.
            const H& h = handles[pick];
            if (h.hx < 0) cropEdgeX_ = (cropMin_.x <= cropMax_.x) ? -1 : +1;
            else if (h.hx > 0) cropEdgeX_ = (cropMin_.x <= cropMax_.x) ? +1 : -1;
            if (h.hy < 0) cropEdgeY_ = (cropMin_.y <= cropMax_.y) ? -1 : +1;
            else if (h.hy > 0) cropEdgeY_ = (cropMin_.y <= cropMax_.y) ? +1 : -1;
        }
    }

    // ── Continue the drag: apply (cursor − anchor) to the rect at grab time ───
    if (cropDrag_ != -1) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            Renderer::Vec2 cur = s2dDoc(io.MousePos);
            float dx = cur.x - cropDragRef_.x, dy = cur.y - cropDragRef_.y;
            if (cropDrag_ == -3) {              // define: opposite corner follows
                cropMin_ = cropRect0Min_;
                cropMax_ = cur;
            } else if (cropDrag_ == -2) {       // move the whole rect by the delta
                cropMin_ = { cropRect0Min_.x + dx, cropRect0Min_.y + dy };
                cropMax_ = { cropRect0Max_.x + dx, cropRect0Max_.y + dy };
            } else {                            // resize: move only the held REAL
                cropMin_ = cropRect0Min_; cropMax_ = cropRect0Max_;  // component
                if      (cropEdgeX_ < 0) cropMin_.x = cropRect0Min_.x + dx;
                else if (cropEdgeX_ > 0) cropMax_.x = cropRect0Max_.x + dx;
                if      (cropEdgeY_ < 0) cropMin_.y = cropRect0Min_.y + dy;
                else if (cropEdgeY_ > 0) cropMax_.y = cropRect0Max_.y + dy;
            }
        } else {
            cropDrag_ = -1;                     // released
        }
    }

    // ── Draw: dim outside the (normalised, clamped) rect, frame, handles, hint ─
    screenRect(r0, r1);   // refresh after edits this frame
    mid = ImVec2((r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f);
    handles[0].p = {r0.x, r0.y}; handles[1].p = {r1.x, r0.y};
    handles[2].p = {r0.x, r1.y}; handles[3].p = {r1.x, r1.y};
    handles[4].p = {mid.x, r0.y}; handles[5].p = {mid.x, r1.y};
    handles[6].p = {r0.x, mid.y}; handles[7].p = {r1.x, mid.y};

    ImU32 accent = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Accent_Default));
    ImVec2 cMin = gestureCanvasMin_, cMax = gestureCanvasMax_;
    ImU32 dim = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::C_ZoneOverlay_TransformDim));
    ImVec2 q0(std::max(r0.x, cMin.x), std::max(r0.y, cMin.y));
    ImVec2 q1(std::min(r1.x, cMax.x), std::min(r1.y, cMax.y));
    dl.AddRectFilled(cMin, ImVec2(cMax.x, q0.y), dim);               // top band
    dl.AddRectFilled(ImVec2(cMin.x, q1.y), cMax, dim);              // bottom band
    dl.AddRectFilled(ImVec2(cMin.x, q0.y), ImVec2(q0.x, q1.y), dim); // left
    dl.AddRectFilled(ImVec2(q1.x, q0.y), ImVec2(cMax.x, q1.y), dim); // right
    dl.AddRect(q0, q1, accent, 0.0f, 0, 1.5f);
    for (int i = 0; i < 8; ++i)
        dl.AddRectFilled(ImVec2(handles[i].p.x - kHalf, handles[i].p.y - kHalf),
                          ImVec2(handles[i].p.x + kHalf, handles[i].p.y + kHalf),
                          accent, 1.0f);
    const char* hint = "Thumbnail crop — drag to adjust, Enter to confirm, Esc to cancel";
    ImU32 txt = ImGui::GetColorU32(ds.GetColor(DesignSystem::Tok::S_Color_Text_Default));
    dl.AddText(ImVec2(cMin.x + 8.0f, cMin.y + 8.0f), txt, hint);
}

// ── Modules::ModuleHost::CreateObjectSpec ─────────────────────────────────────
// Build a real Shape from a module's ObjectSpec (geometry + colour) and place it
// at the 2D cursor, exactly like Shift+A — so it is then selectable / movable /
// deletable. `loose` objects are page-less (the IOF overprint/course layer);
// others land on the active page. Returns the new shape id (0 on failure).
uint64_t Application::CreateObjectSpec(const Modules::ObjectSpec& spec) {
    // Preview placement: arm a cursor-following preview (the click commits). The
    // guard lets UpdatePlacement re-enter to actually create the object.
    if (!placementCommitting_ && PreviewPlacementEnabled()) {
        RequestPlacementSpec(spec);
        return 0;
    }
    using K = Renderer::ShapeKind;
    auto& doc = project_.document;
    const Renderer::Color col{ spec.r, spec.g, spec.b, spec.a };
    const float R = std::max(2.0f, spec.size) * 0.5f;

    Renderer::Shape s;
    switch (spec.geom) {
        case Modules::ObjectSpec::Geom::Area: {
            // Filled rectangle (e.g. lake, forest, open land).
            s = MakeShape(K::Rectangle);
            s.MainPart().pos = { -R, -R }; s.MainPart().size = { 2 * R, 2 * R };
            s.MainPart().fill.enabled = true;  s.MainPart().fill.color   = col;
            s.MainPart().stroke.enabled = false;
            break;
        }
        case Modules::ObjectSpec::Geom::Line: {
            // Short 2-point stroke (e.g. contour, path, fence).
            s = MakeShape(K::Curve, Renderer::PartType::Curve, Renderer::SplineType::Bezier);
            Renderer::Node a({ -R, 0 }); Renderer::Node b({ R, 0 });
            a.mode = b.mode = Renderer::HandleMode::Vector;
            s.MainPart().path.nodes = { a, b };
            s.MainPart().path.closed   = false;
            s.MainPart().fill.enabled  = false;
            s.MainPart().stroke.enabled = true;  s.MainPart().stroke.color = col;
            s.MainPart().stroke.width   = std::max(2.0f, R * 0.18f);
            break;
        }
        case Modules::ObjectSpec::Geom::Point:
        default: {
            // Small filled disc (most point symbols, and the course controls which
            // are an unfilled ring → stroke only).
            s = MakeShape(K::Ellipse);
            s.MainPart().pos = { -R, -R }; s.MainPart().size = { 2 * R, 2 * R };
            // A control/start/finish reads as a RING (stroke, no fill).
            const bool ring = spec.name.rfind("Control", 0) == 0 ||
                              spec.name == "Start" || spec.name == "Finish";
            s.MainPart().fill.enabled   = !ring; s.MainPart().fill.color   = col;
            s.MainPart().stroke.enabled = ring;  s.MainPart().stroke.color = col;
            s.MainPart().stroke.width   = std::max(2.0f, R * 0.22f);
            break;
        }
    }
    s.name = spec.name;
    s.SetLockScale(spec.lockScale);      // fixed-size symbol (both axes)
    s.lockRotation = spec.lockRotation;  // north-oriented symbol
    s.collectionId = spec.collectionId;  // IOF print-layer collection (0 = none)
    CenterOrigin(s);

    // Place at the 2D cursor (display space), like Shift+A. Loose → raw doc space;
    // otherwise relative to the active page's display origin in this viewport.
    int ab = -1;
    if (!spec.loose && doc.ActivePage()) ab = doc.ArtboardIndexById(doc.ActivePage());
    Renderer::Vec2 dispPo = (ab >= 0) ? CurPageOrigin(ab) : Renderer::Vec2{ 0, 0 };
    s.transform.translate = { doc.cursor.x - dispPo.x - s.origin.x,
                              doc.cursor.y - dispPo.y - s.origin.y };
    MarkUndoLabel("Add " + s.name);
    uint64_t id = AddShapeWorldDisplay(doc, ab, std::move(s));
    project_.dirty = true;
    return id;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Preview placement
// ─────────────────────────────────────────────────────────────────────────────

bool Application::PreviewPlacementEnabled() {
    if (activeCapabilities_.previewPlacement) return true;   // module forces it
    // Bool01 token (stored as Int), not a float — GetInt, else it throws.
    return DesignSystem::DesignSystem::Instance()
               .GetInt(DesignSystem::Tok::S_Config_PreviewPlacement) != 0;
}

void Application::RequestPlacementCore(const std::string& kind) {
    placement_          = {};             // reset previous arming
    placement_.armed    = true;
    placement_.source   = PlacementSource::Core;
    placement_.coreKind = kind;
}

void Application::RequestPlacementSpec(const Modules::ObjectSpec& spec) {
    placement_        = {};
    placement_.armed  = true;
    placement_.source = PlacementSource::ModuleSpec;
    placement_.spec   = spec;
}

void Application::RequestPlacementBaked(const Renderer::Shape& shape, bool loose,
                                       uint64_t coll, Modules::ModuleHost::PlaceMode mode) {
    const bool alreadyArmed = placement_.armed;
    // Switching symbols mid-placement: abandon any in-progress STYLED curve so the
    // draw branch re-seeds with the NEW symbol (otherwise the old template kept
    // drawing + the old mini-ghost stayed). Harmless when no gesture is active
    // (e.g. the re-arm after FinishCurveGesture already reset it).
    if (toolState_.styleActive) toolState_.Reset();
    placement_            = {};
    placement_.armed      = true;
    placement_.source     = PlacementSource::Baked;
    placement_.baked      = shape;
    placement_.bakedLoose = loose;
    placement_.bakedColl  = coll;
    placement_.mode       = mode;
    // Remember the tool to restore on cancel (only on the FIRST arm — re-arms of an
    // infinite placement keep the original previous tool). Then drop the active
    // tool: the placement preview owns the input until cancelled / committed.
    auto& tm = Shortcuts::Tools::ToolManager::Instance();
    if (!alreadyArmed) placementPrevTool_ = tm.GetActiveTool();
    tm.SetActiveTool("");
}

// Cancel an armed placement (and any in-progress styled-curve gesture) and restore
// the tool the user had before they picked the symbol.
void Application::EndPlacement() {
    placement_.armed = false;
    if (toolState_.styleActive) toolState_.Reset();
    Shortcuts::Tools::ToolManager::Instance().SetActiveTool(placementPrevTool_);
    placementPrevTool_.clear();
}

void Application::SetPlacementPreview(const Renderer::Shape& preview) {
    placement_.bakedPreview = preview;
    placement_.hasPreview   = true;
}

int Application::ArmedSymbolCode() const {
    // Armed via the placement preview, OR mid-draw of a styled curve (the gesture
    // owns the symbol then). Either way report the symbol's ISOM code.
    if (placement_.armed && placement_.source == PlacementSource::Baked)
        return placement_.baked.isomCode;
    if (toolState_.styleActive) return toolState_.styleTemplate.isomCode;
    return 0;
}


bool Application::UpdatePlacement(EditorState& st,
        const std::function<Renderer::Vec2(ImVec2)>& s2d,
        const std::function<ImVec2(Renderer::Vec2)>& d2s,
        float effZoom, bool hovered, App::OverlayDL& dl) {
    (void)st;
    ImGuiIO& io = ImGui::GetIO();

    // Cancel: Esc or right-click drops the arming and restores the previous tool.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        EndPlacement();
        rmbConsumedByTransform_ = true;
        return true;
    }
    if (!hovered) return true;   // owns the input but only draws over a canvas

    const ImVec2  mp   = io.MousePos;
    const Renderer::Vec2 mDoc = s2d(mp);

    // ── Line / area symbols: DRAW the geometry point-by-point ─────────────────
    // The baked shape is a STYLE TEMPLATE; we hand off to the core Curve tool but
    // tag the gesture so it carries the symbol's style and re-arms when finished.
    if (placement_.source == PlacementSource::Baked &&
        (placement_.mode == Modules::ModuleHost::PlaceMode::DrawLine ||
         placement_.mode == Modules::ModuleHost::PlaceMode::DrawArea)) {
        const bool closed = (placement_.mode == Modules::ModuleHost::PlaceMode::DrawArea);
        // Seed/continue the styled curve gesture on this leaf.
        if (!toolState_.Active()) {
            toolState_.Reset();
            toolState_.gesture       = ToolGesture::Bezier;
            toolState_.owner         = &st;
            toolState_.styleActive   = true;
            toolState_.styleClosed   = closed;
            toolState_.styleLoose    = placement_.bakedLoose;
            toolState_.styleColl     = placement_.bakedColl;
            toolState_.styleTemplate = placement_.baked;
            toolState_.stylePreview  = placement_.hasPreview ? placement_.bakedPreview
                                                             : placement_.baked;
            toolState_.styleHasPreview = true;
            toolState_.targetArtboard = project_.document.ActivePage()
                ? project_.document.ArtboardIndexById(project_.document.ActivePage()) : -1;
        }
        // The curve tool runs the whole authoring (it owns the gesture from here);
        // it draws its own preview. We keep `placement_.armed` so this branch is
        // re-entered each frame until the curve tool finishes (which re-arms us).
        HandleCurveTool(st, s2d, d2s, effZoom, hovered, dl);
        // A tiny ghost of the symbol bottom-right of the cursor (which symbol).
        DrawPlacementMiniGhost(toolState_.stylePreview, mp, effZoom);
        return true;
    }

    // ── Crossing point (519) onto a compatible curve ──────────────────────────
    // The crossing symbol is not a free object: dropping it on a wall/fence/
    // prominent-line inserts a Crossing mark that CUTS that line. Hold Ctrl to
    // place it freely anywhere instead (the normal baked-object path below).
    if (placement_.source == PlacementSource::Baked &&
        placement_.baked.isomCode == 5190 && !io.KeyCtrl) {
        struct CH { uint64_t sid = 0; int part = -1; int sub = 0; float t = 0.5f;
                    float dpx = 1e9f; Renderer::Vec2 p{0,0}, tan{1,0}; } best;
        const float zoom = std::max(0.0001f, effZoom);
        auto consider = [&](const Renderer::Shape& s) {
            if (!s.visible || !CrossingAllowedOn(s.isomCode)) return;
            Renderer::Vec2 po = CurPageOriginOfShape(s.id);
            for (int pi = 0; pi < (int)s.parts.size(); ++pi) {
                const Renderer::Part& part = s.parts[(size_t)pi];
                if (!part.stroke.enabled) continue;
                int subs = Renderer::Tessellator::SubpathCount(part);
                for (int subi = 0; subi < subs; ++subi) {
                    bool cl = false;
                    auto poly = Renderer::Tessellator::OutlinePartSub(s, part, subi, zoom, cl, po);
                    if (poly.size() < 2) continue;
                    float total = 0.0f; size_t n = poly.size(), sc = cl ? n : n - 1;
                    for (size_t i = 0; i < sc; ++i)
                        total += std::hypot(poly[(i+1)%n].x - poly[i].x, poly[(i+1)%n].y - poly[i].y);
                    float acc = 0.0f;
                    for (size_t i = 0; i < sc; ++i) {
                        Renderer::Vec2 a = poly[i], b = poly[(i+1)%n];
                        Renderer::Vec2 ab{ b.x - a.x, b.y - a.y };
                        float segLen = std::hypot(ab.x, ab.y); if (segLen < 1e-6f) continue;
                        float u = std::clamp(((mDoc.x-a.x)*ab.x + (mDoc.y-a.y)*ab.y)/(segLen*segLen), 0.0f, 1.0f);
                        Renderer::Vec2 proj{ a.x + ab.x*u, a.y + ab.y*u };
                        float dpx = std::hypot(mp.x - d2s(proj).x, mp.y - d2s(proj).y);
                        if (dpx < best.dpx)
                            best = { s.id, pi, subi, total > 1e-4f ? (acc+segLen*u)/total : 0.0f,
                                     dpx, proj, { ab.x/segLen, ab.y/segLen } };
                        acc += segLen;
                    }
                }
            }
        };
        for (const auto& ab : project_.document.artboards)
            for (const Renderer::Shape& s : ab.shapes) consider(s);
        for (const Renderer::Shape& s : project_.document.looseShapes) consider(s);

        if (best.part >= 0 && best.dpx <= 14.0f) {
            Renderer::Shape* sp = project_.document.FindShape(best.sid);
            if (sp && best.part < (int)sp->parts.size()) {
                const float scl = activeCapabilities_.symbolScale > 0.01f
                                      ? activeCapabilities_.symbolScale : 1.0f;
                Renderer::Part& part = sp->parts[(size_t)best.part];
                Renderer::LineMark m; m.kind = Renderer::LineMarkKind::Crossing;
                m.sub = best.sub; m.t = best.t;
                ApplyMarkPreset(m, sp->isomCode, scl);
                DrawLineMarkGhost(*sp, part, m, best.p, best.tan, d2s, zoom);
                ShowCrosshairCursor();
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    part.marks.push_back(m);
                    project_.document.SelectOnly(best.sid);
                    project_.document.SetActive(best.sid);
                    MarkUndoLabel("Add crossing point");
                    project_.dirty = true;
                    // Stay armed → place more crossings; Esc / right-click ends it.
                }
                return true;   // snapped to a curve → don't draw/commit a free object
            }
        }
        // Not over a compatible curve and Ctrl is up → a crossing can ONLY go on a
        // line here. Show the crosshair but DON'T place a free object; the user
        // must hover a wall/fence/prominent line (or hold Ctrl to place freely).
        ShowCrosshairCursor();
        return true;
    }

    // ── Ghost preview at the mouse (follows it like G/move) ───────────────────
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float alpha = ds.GetFloat(DesignSystem::Tok::S_Config_PlacementPreviewAlpha);

    if (placement_.source == PlacementSource::Baked) {
        // The exact baked glyph as a SMOOTH (SSAA) transparent Vulkan texture, so
        // the ghost has no triangle seams and matches the placed object. Sized to
        // the glyph's on-screen extent; blitted at the mouse, tinted to the preview
        // alpha. Falls back to a CPU triangle blit if texture rendering is absent.
        Renderer::Vec2 bmn, bmx;
        bool gotB = Renderer::Tessellator::WorldBounds(placement_.baked, 1.0f, bmn, bmx, {0,0});
        ImTextureID tex = 0; ImVec2 imgMin, imgMax;
        if (gotB) {
            float wpxF = (bmx.x - bmn.x) * effZoom, hpxF = (bmx.y - bmn.y) * effZoom;
            int wpx = std::max(4, (int)std::lround(wpxF));
            int hpx = std::max(4, (int)std::lround(hpxF));
            // padFrac 0 → the content fills the texture (1:1 with the on-screen
            // extent), so the ghost is the right size.
            uint64_t key = 0x6A05u;                 // single reused ghost slot
            // RenderGlyphTexture is now PROCEDURAL (GPU fills + instanced decor), so
            // the full symbol — patterns included — renders fast and shows in the ghost.
            uint64_t chash = Renderer::Tessellator::HashShape(placement_.baked, {0,0})
                           ^ ((uint64_t)wpx << 8) ^ ((uint64_t)hpx << 24);
            std::vector<Renderer::Shape> shapes = { placement_.baked };
            tex = RenderGlyphTexture(key, chash, shapes, wpx, hpx, 0.0f, /*transparent=*/true);
            // The texture is centred on the content centre; place it so the glyph's
            // local origin {0,0} lands on the mouse.
            Renderer::Vec2 c{ (bmn.x+bmx.x)*0.5f, (bmn.y+bmx.y)*0.5f };
            ImVec2 texCtr(mp.x + c.x * effZoom, mp.y + c.y * effZoom);
            imgMin = ImVec2(texCtr.x - wpx*0.5f, texCtr.y - hpx*0.5f);
            imgMax = ImVec2(texCtr.x + wpx*0.5f, texCtr.y + hpx*0.5f);
        }
        if (tex) {
            dl.AddImage(tex, imgMin, imgMax, ImVec2(0,0), ImVec2(1,1),
                         ImGui::GetColorU32(ImVec4(1,1,1, alpha)));
        } else {
            // CPU fallback (AA fill off to avoid seams). Strip fill PATTERNS — the
            // per-frame legacy bake of a patterned area is O(steps²) and would freeze
            // the placement ghost; the motif appears (GPU-fast) once placed.
            Renderer::Shape bakedGhost = placement_.baked;
            for (Renderer::Part& p : bakedGhost.parts) p.fillLayers.clear();
            Renderer::Mesh mesh;
            Renderer::Tessellator::AppendShape(bakedGhost, mesh, effZoom, {0, 0});
            const auto& vtx = mesh.vertices;
            const ImDrawListFlags savedAA = dl.imgui()->Flags;
            dl.imgui()->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            for (size_t i = 0; i + 3 <= vtx.size(); i += 3) {
                ImVec2 p[3]; const Renderer::Vertex& v0 = vtx[i];
                for (int k = 0; k < 3; ++k) {
                    const Renderer::Vertex& v = vtx[i + (size_t)k];
                    p[k] = ImVec2(mp.x + v.x * effZoom, mp.y + v.y * effZoom);
                }
                dl.AddTriangleFilled(p[0], p[1], p[2],
                                      ImGui::GetColorU32(ImVec4(v0.r, v0.g, v0.b, v0.a * alpha)));
            }
            dl.imgui()->Flags = savedAA;
        }
    } else if (placement_.source == PlacementSource::ModuleSpec) {
        const float R = std::max(2.0f, placement_.spec.size) * 0.5f;
        const float rpx = std::max(3.0f, R * effZoom);
        ImVec4 gc{ placement_.spec.r, placement_.spec.g, placement_.spec.b, alpha };
        const ImU32 col = ImGui::GetColorU32(gc);
        switch (placement_.spec.geom) {
            case Modules::ObjectSpec::Geom::Area:
                dl.AddRectFilled(ImVec2(mp.x - rpx, mp.y - rpx), ImVec2(mp.x + rpx, mp.y + rpx), col); break;
            case Modules::ObjectSpec::Geom::Line:
                dl.AddLine(ImVec2(mp.x - rpx, mp.y), ImVec2(mp.x + rpx, mp.y), col,
                            std::max(2.0f, placement_.spec.size * 0.18f * effZoom)); break;
            default:
                dl.AddCircleFilled(mp, rpx, col, 32); break;
        }
    } else {
        const float rpx = std::max(3.0f, 100.0f * effZoom);
        ImVec4 gc = ds.GetColor(DesignSystem::Tok::C_Viewport_Crosshair); gc.w = alpha;
        const ImU32 col = ImGui::GetColorU32(gc);
        if (placement_.coreKind == "bezier" || placement_.coreKind == "nurbs")
            dl.AddLine(ImVec2(mp.x - rpx, mp.y), ImVec2(mp.x + rpx, mp.y), col, 2.0f);
        else
            dl.AddRect(ImVec2(mp.x - rpx, mp.y - rpx), ImVec2(mp.x + rpx, mp.y + rpx), col, 0.0f, 0, 1.5f);
    }
    ShowCrosshairCursor();

    // ── Commit on left click ──────────────────────────────────────────────────
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Renderer::Vec2 saved = project_.document.cursor;
        project_.document.cursor = mDoc;
        placementCommitting_ = true;
        switch (placement_.source) {
            case PlacementSource::Baked:
                AddBakedShape(placement_.baked, placement_.bakedLoose, placement_.bakedColl); break;
            case PlacementSource::ModuleSpec:
                CreateObjectSpec(placement_.spec); break;
            case PlacementSource::Core:
                Action_AddShape(placement_.coreKind); break;
        }
        placementCommitting_ = false;
        project_.document.cursor = saved;
        // Baked symbols (the IOF catalogue) place INFINITELY — stay armed so the
        // user can drop another. Esc / right-click ends it. Core/ModuleSpec keep
        // the classic single-shot behaviour.
        if (placement_.source != PlacementSource::Baked) placement_.armed = false;
    }
    return true;
}

// ── Modules::ModuleHost::AddBakedShape ────────────────────────────────────────
// Place a fully-baked shape (parts authored in local doc units, centred at the
// local origin) at the 2D cursor — like CreateObjectSpec, but the geometry is
// supplied by the caller (the IOF exact glyph builder). Honours preview placement.
uint64_t Application::AddBakedShape(const Renderer::Shape& shape,
                                    bool loose, uint64_t collectionId,
                                    Modules::ModuleHost::PlaceMode mode) {
    // Line/area symbols are always DRAWN point-by-point (even without the preview
    // pref) — they have no fixed geometry to stamp. Points honour the pref.
    const bool drawCurve = (mode == Modules::ModuleHost::PlaceMode::DrawLine ||
                            mode == Modules::ModuleHost::PlaceMode::DrawArea);
    if (!placementCommitting_ && (drawCurve || PreviewPlacementEnabled())) {
        RequestPlacementBaked(shape, loose, collectionId, mode);
        return 0;
    }
    auto& doc = project_.document;
    Renderer::Shape s = shape;
    s.collectionId = collectionId;
    int ab = -1;
    if (!loose && doc.ActivePage()) ab = doc.ArtboardIndexById(doc.ActivePage());
    Renderer::Vec2 dispPo = (ab >= 0) ? CurPageOrigin(ab) : Renderer::Vec2{ 0, 0 };
    // Geometry is centred at origin {0,0}; land that origin at the 2D cursor.
    s.transform.translate = { doc.cursor.x - dispPo.x - s.origin.x,
                              doc.cursor.y - dispPo.y - s.origin.y };
    MarkUndoLabel("Add " + s.name);
    uint64_t id = AddShapeWorldDisplay(doc, ab, std::move(s));
    project_.dirty = true;
    return id;
}


} // namespace App
