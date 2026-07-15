#include "Application.h"

#include "PropertiesRows.h"
#include <UI/Widgets/Panel.h>
#include <UI/Widgets/PopupMenu.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
//  Properties — the unified paint stack (docs/Ink/DOCUMENT_MODEL.md §4) on the
//  VIGNETTE layout: inside the Fills / Strokes sections, a rail of square
//  thumbnails on the LEFT (one per fill/stroke, each previewing that piece
//  alone — a swatch for a fill, a line sample for a stroke) with a "+" tile
//  under the last one, and the SELECTED item's full property rows on the
//  RIGHT. Hovering a stroke thumbnail shows a bigger sample in a tooltip.
//  The section grows with the rail (the panels auto-size), so a long stack
//  never clips. Structural edits commit immediately; drags fold into one undo
//  command committed on release.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace {
constexpr float kThumbBase = 40.0f;   // vignette side, × global scale
constexpr float kRailGap   = 3.0f;    // vertical gap between vignettes, × gs
}

// Real-pipeline render for the PATTERN vignettes: a per-(node, fill)
// off-screen Ink view showing ONLY that fill — the per-piece preview filter
// keeps just its drawables (motif copies, their strokes, the fill-clip
// stencil masks) — at 100 % ZOOM, centred on the shape, so the vignette
// previews the pattern as it tiles (actual motif, spacing, rotation, clip),
// never the whole object. Rendered at 128 px, minified into the 40 px tile;
// the hover tooltip shows it at native size.
ImTextureID Application::PaintPatternPreview(Ink::NodeId id, int fillIndex) {
    if (!ink_ || !project_.document || fillIndex < 0) return (ImTextureID)0;
    Ink::DRect bb;
    if (!ink_->NodeBounds(id, bb) || !bb.valid) return (ImTextureID)0;

    // Key space: bit1 set + bit0 clear marks the paint previews (Outliner
    // thumbnails are (id<<1)|1 = odd; viewport keys are aligned pointers);
    // the fill index folds in so each fill owns its view.
    const void* key = (const void*)(std::uintptr_t)(
        ((id * 41u + (std::uint64_t)fillIndex) << 2) | 2u);
    Ink::View* view = ink_->AcquireView(key);
    constexpr std::uint32_t px = 128;
    view->SetViewport(px, px);
    // 100 % zoom, centred on the shape: a 128×128 doc-unit window of the fill.
    const double cx = (bb.min.x + bb.max.x) * 0.5;
    const double cy = (bb.min.y + bb.max.y) * 0.5;
    view->SetCamera(cx - px * 0.5, cy - px * 0.5, 1.0);
    view->SetBackground(Ink::SrgbToLinearPremultiplied(1, 1, 1, 1));
    std::vector<std::uint64_t> owners;
    std::vector<Ink::NodeId> stack{ id };
    while (!stack.empty()) {
        const Ink::NodeId c = stack.back();
        stack.pop_back();
        owners.push_back(c);
        if (const Ink::Node* n = project_.document->Find(c))
            for (Ink::NodeId k : n->children) stack.push_back(k);
    }
    view->SetPreviewFilter(owners, fillIndex, /*pieceIsStroke=*/false);
    return (ImTextureID)view->Texture();
}

// ── Fills ─────────────────────────────────────────────────────────────────────

void Application::PropFillsSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Path) return;
    const float gs = pr::Gs();

    // Vignette selection is per-node (both rails reset on a node switch).
    if (propPaintNode_ != id) {
        propPaintNode_ = id;
        propFillSel_   = 0;
        propStrokeSel_ = 0;
    }

    UI::PanelConfig pc; pc.id = "##fills"; pc.label = "Fills"; pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open) {
        Ink::Style style = n->style;
        bool structural = false;            // committed immediately at the end
        const char* structLabel = "Edit Fills";

        auto liveApply = [&](const char* releaseLabel, bool released) {
            if (!propEditActive_) {
                propEditActive_ = true; propEditNode_ = id;
                propEditBefore_ = n->style;
            }
            doc.SetStyle(id, style);
            if (released && propEditActive_ && propEditNode_ == id) {
                CommitStyleEdit(id, propEditBefore_, releaseLabel);
                propEditActive_ = false;
            }
        };

        const float thumb = kThumbBase * gs;

        // ── LEFT: the vignette rail (+ the "add" tile) ────────────────────────
        // A pattern fill's vignette is the REAL pipeline render of the node
        // (its actual motif/spacing/rotation), minified into the tile; solids
        // stay plain swatches. Both show a bigger preview on hover-dwell.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(ImGui::GetStyle().ItemSpacing.x,
                                   kRailGap * gs));
        ImGui::BeginGroup();
        for (std::size_t i = 0; i < style.fills.size(); ++i) {
            ImGui::PushID((int)i);
            const Ink::Fill& f = style.fills[i];
            ImVec2 cmn, cmx;
            char tid[16];
            std::snprintf(tid, sizeof tid, "f%d", (int)i);
            if (pr::ThumbTile(tid, thumb, (int)i == propFillSel_, &cmn, &cmx))
                propFillSel_ = (int)i;
            ImTextureID patTex = (ImTextureID)0;
            if (f.kind == Ink::FillKind::Pattern)
                patTex = PaintPatternPreview(id, (int)i);
            if (patTex)
                ImGui::GetWindowDrawList()->AddImage(patTex, cmn, cmx);
            else
                pr::DrawFillSample(ImGui::GetWindowDrawList(), cmn, cmx, f);
            // Hover-dwell: a bigger sample in a tooltip (like the strokes).
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::PushStyleColor(ImGuiCol_PopupBg,
                    pr::SafeColor(pr::Tok::S_Color_Background_Layer1,
                                  ImVec4(0.13f, 0.13f, 0.15f, 1)));
                ImGui::BeginTooltip();
                const float big = thumb * 3.2f;
                const ImVec2 mn = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(big, big));
                ImDrawList* tdl = ImGui::GetWindowDrawList();
                tdl->AddRectFilled(mn, ImVec2(mn.x + big, mn.y + big),
                                   IM_COL32(255, 255, 255, 255));
                if (patTex)
                    tdl->AddImage(patTex, mn, ImVec2(mn.x + big, mn.y + big));
                else
                    pr::DrawFillSample(tdl, mn, ImVec2(mn.x + big, mn.y + big), f);
                if (f.kind == Ink::FillKind::Solid) {
                    const ImVec4 c = pr::ToSrgb(f.paint.color);
                    ImGui::Text("RGBA %.2f %.2f %.2f %.2f \xC2\xB7 opacity %.2f",
                                c.x, c.y, c.z, c.w, f.opacity);
                } else {
                    ImGui::TextUnformatted("Pattern fill");
                }
                ImGui::EndTooltip();
                ImGui::PopStyleColor();
            }
            // Drag the vignette up/down to reorder the DRAW ORDER of the
            // stack (a plain click still selects — the swap needs a real
            // vertical drag past the neighbouring tile). Selection follows
            // the moved fill; the whole drag folds into ONE undo command.
            if (ImGui::IsItemActive() &&
                std::fabs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y) >
                    (thumb + kRailGap * gs) * 0.6f) {
                const float dy =
                    ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y;
                const int nxt = (int)i + (dy < 0.0f ? -1 : 1);
                if (nxt >= 0 && nxt < (int)style.fills.size()) {
                    std::swap(style.fills[i], style.fills[(std::size_t)nxt]);
                    if (propFillSel_ == (int)i) propFillSel_ = nxt;
                    else if (propFillSel_ == nxt) propFillSel_ = (int)i;
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                    liveApply("Reorder Fills", false);
                }
            }
            if (ImGui::IsItemDeactivated())
                liveApply("Reorder Fills", true);
            ImGui::PopID();
        }
        if (pr::ThumbAddTile(thumb)) {
            Ink::Fill f; f.paint.color = pr::ToLinear(edit_.defaultFill);
            style.fills.push_back(f);
            propFillSel_ = (int)style.fills.size() - 1;
            structural = true; structLabel = "Add Fill";
        }
        ImGui::EndGroup();
        ImGui::PopStyleVar();
        ImGui::SameLine(0.0f, 8.0f * gs);

        // ── RIGHT: the selected fill's properties ─────────────────────────────
        propFillSel_ = std::clamp(propFillSel_, 0,
                                  std::max(0, (int)style.fills.size() - 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##fillProps", ImVec2(ImGui::GetContentRegionAvail().x, 0),
                          ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
        if (style.fills.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                pr::SafeColor(pr::Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1)));
            ImGui::TextUnformatted("No fills.");
            ImGui::TextUnformatted("Click + to add one.");
            ImGui::PopStyleColor();
        } else {
            Ink::Fill& f = style.fills[(std::size_t)propFillSel_];

            bool enabled = f.enabled;
            if (pr::CheckRow("Enabled", &enabled)) {
                f.enabled = enabled; structural = true;
            }
            static const char* kKind[] = { "Solid", "Pattern" };
            int kind = (int)f.kind;
            if (pr::DropdownRow("Type", kKind, 2, &kind)) {
                f.kind = (Ink::FillKind)kind; structural = true;
                // A fresh pattern defaults to the exact contour clip.
                if (f.kind == Ink::FillKind::Pattern &&
                    f.pattern.motifRef == Ink::kNullNode)
                    f.pattern.clip = Ink::PatternClip::Contour;
            }

            if (f.kind == Ink::FillKind::Solid) {
                bool released = false;
                if (pr::ColorRow("Color", &f.paint.color, true, &released))
                    liveApply("Fill Colour", false);
                if (released) liveApply("Fill Colour", true);

                static const char* kRule[] = { "Non-zero", "Even-odd" };
                int rule = (int)f.rule;
                if (pr::DropdownRow("Rule", kRule, 2, &rule)) {
                    f.rule = (Ink::FillRule)rule; structural = true;
                }
            } else {
                // ── Pattern fill (legacy fill-layer feature set) ──
                bool pickReq = false;
                if (pr::NodePickerRow("Motif", doc, &f.pattern.motifRef, id,
                                      /*allowNone=*/true, /*pathsOnly=*/false,
                                      &pickReq)) {
                    structural = true; structLabel = "Pattern Motif";
                }
                if (pickReq) {
                    // Eyedropper: pick a node in the viewport/outliner; the
                    // commit re-fetches the style so the write survives.
                    const std::size_t fi = (std::size_t)propFillSel_;
                    BeginObjectPick(nullptr, [this, id, fi](Ink::NodeId picked) {
                        if (!project_.document) return;
                        const Ink::Node* nn = project_.document->Find(id);
                        if (!nn || fi >= nn->style.fills.size()) return;
                        Ink::Style before = nn->style, after = before;
                        after.fills[fi].pattern.motifRef = picked;
                        project_.document->SetStyle(id, after);
                        CommitStyleEdit(id, before, "Pattern Motif");
                    });
                }
                float sp[2] = { (float)f.pattern.spacingX,
                                (float)f.pattern.spacingY };
                bool dx = false, dy = false;
                unsigned ch = pr::Vec2Group("Spacing", sp, 0.2f, 0.1f,
                                            10000.0f, 2, "", &dx, &dy);
                if (ch) {
                    f.pattern.spacingX = sp[0]; f.pattern.spacingY = sp[1];
                    liveApply("Pattern Spacing", false);
                }
                if (dx || dy) liveApply("Pattern Spacing", true);

                float off[2] = { (float)f.pattern.phaseX,
                                 (float)f.pattern.phaseY };
                ch = pr::Vec2Group("Offset", off, 0.2f, -10000.0f,
                                   10000.0f, 2, "", &dx, &dy);
                if (ch) {
                    f.pattern.phaseX = off[0]; f.pattern.phaseY = off[1];
                    liveApply("Pattern Offset", false);
                }
                if (dx || dy) liveApply("Pattern Offset", true);

                pr::GroupGap();
                float angleDeg =
                    (float)(f.pattern.rotation * 180.0 / 3.14159265358979);
                if (pr::DragFloat("Angle", &angleDeg, 0.5f, -360.0f, 360.0f,
                                  1, "\xC2\xB0")) {
                    f.pattern.rotation = angleDeg * 3.14159265358979 / 180.0;
                    liveApply("Pattern Angle", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Pattern Angle", true);

                float motifDeg =
                    (float)(f.pattern.motifRotation * 180.0 / 3.14159265358979);
                if (pr::DragFloat("Motif angle", &motifDeg, 0.5f, -360.0f,
                                  360.0f, 1, "\xC2\xB0")) {
                    f.pattern.motifRotation = motifDeg * 3.14159265358979 / 180.0;
                    liveApply("Motif Angle", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Motif Angle", true);

                float sc = (float)f.pattern.scale;
                if (pr::DragFloat("Scale", &sc, 0.01f, 0.01f, 100.0f, 2)) {
                    f.pattern.scale = sc;
                    liveApply("Pattern Scale", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Pattern Scale", true);

                // The legacy fill clip: where the pattern is cut.
                static const char* kClip[] = { "Box", "Shape", "Inner",
                                               "Outer" };
                int clip = (int)f.pattern.clip;
                if (pr::ButtonGroupRow("Fill clip", kClip, 4, &clip)) {
                    f.pattern.clip = (Ink::PatternClip)clip;
                    structural = true; structLabel = "Pattern Clip";
                }
                if (ImGui::IsItemHovered())
                    UI::DrawTooltipTranslucent(
                        "Cut the pattern at the bounding box, the exact "
                        "contour, or the inner / outer edge of the stroke",
                        ImGui::GetIO().MousePos, 1.0f);

                static const char* kAnchor[] = { "Object", "Document" };
                int anchor = (int)f.pattern.anchor;
                if (pr::ButtonGroupRow("Anchor", kAnchor, 2, &anchor)) {
                    f.pattern.anchor = (Ink::PatternAnchor)anchor;
                    structural = true; structLabel = "Pattern Anchor";
                }
                if (ImGui::IsItemHovered())
                    UI::DrawTooltipTranslucent(
                        "Pin the lattice to the object (follows the shape) "
                        "or to the document origin (the shape slides over "
                        "a static field)",
                        ImGui::GetIO().MousePos, 1.0f);
            }

            float op = f.opacity;
            if (pr::DragFloat("Opacity", &op, 0.005f, 0.0f, 1.0f, 3)) {
                f.opacity = op;
                liveApply("Fill Opacity", false);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                liveApply("Fill Opacity", true);

            pr::GroupGap();
            pr::ControlColumn();
            if (ImGui::SmallButton("Remove")) {
                style.fills.erase(style.fills.begin() + propFillSel_);
                propFillSel_ = std::max(0, propFillSel_ - 1);
                structural = true; structLabel = "Remove Fill";
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        if (structural) {
            const Ink::Style before = n->style;
            doc.SetStyle(id, style);
            CommitStyleEdit(id, before, structLabel);
        }
    }
    UI::EndPanel();
}

// ── Compact single-object editor (gap start/end markers) ────────────────────
// The full palette of an object EXCEPT the Gap shape and the per-mark position
// (each marker object carries its OWN side/offset). Edits apply immediately
// (one style command each), consistent with the structural edits around it.
bool Application::PropMarkObjectCompact(Ink::MarkObject& o, double strokeWidth,
                                        Ink::Document& doc, Ink::NodeId hostId,
                                        bool& structural, const char*& structLabel) {
    static const char* kShape[] = { "Circle", "Rectangle", "Diamond", "Instance" };
    static const char* kMode[]  = { "Fusion", "Blend", "Cut" };
    static const char* kBend[]  = { "Hard", "Bend", "Follow" };
    static const char* kSide[]  = { "Center", "Left", "Right" };
    static const char* kUnit[]  = { "%", "px" };
    static const char* kBlend[] = {
        "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
        "Color Dodge", "Color Burn", "Hard Light", "Soft Light", "Difference",
        "Exclusion", "Erase" };
    const double sw = strokeWidth > 1e-9 ? strokeWidth : 1.0;
    bool changed = false;
    auto S = [&](const char* l) { structural = true; structLabel = l; changed = true; };

    int shp = std::min((int)o.shape, 3);
    if (pr::DropdownRow("Object", kShape, 4, &shp)) {
        const Ink::MarkShape ns = (Ink::MarkShape)shp;
        if (ns == Ink::MarkShape::Rectangle && o.shape != Ink::MarkShape::Rectangle
            && o.sizePercent && std::abs(o.size - 100.0) < 1e-6) o.size = 200.0;
        o.bend = Ink::DefaultBendFor(ns); o.shape = ns; S("Marker Object");
    }
    int md = (int)o.mode;
    if (pr::ButtonGroupRow("Mode", kMode, 3, &md)) { o.mode = (Ink::MarkObjectMode)md; S("Marker Mode"); }

    // Its own side + offset (a marker never inherits — it sits at the gap end).
    o.sideInherit = false;
    int sd = (int)o.side;
    if (pr::ButtonGroupRow("Side", kSide, 3, &sd)) { o.side = (Ink::MarkSide)sd; S("Marker Side"); }
    if (o.side != Ink::MarkSide::Center) {
        float off = (float)o.sideOffset;
        if (pr::DragFloat("Distance", &off, o.sizePercent ? 1.0f : 0.1f,
                          -100000.0f, 100000.0f, 2, o.sizePercent ? "%" : "")) {
            o.sideOffset = off; doc.SetStyle(hostId, doc.Find(hostId)->style); }
        if (ImGui::IsItemDeactivatedAfterEdit()) S("Marker Distance");
    }

    if (o.shape == Ink::MarkShape::Instance) {
        bool pickReq = false;
        if (pr::NodePickerRow("Node", doc, &o.nodeRef, hostId, true, false, &pickReq))
            S("Marker Node");
        float sc = (float)o.size;
        if (pr::DragFloat("Scale", &sc, o.sizePercent ? 1.0f : 0.01f, 0.0f,
                          1000000.0f, 2, o.sizePercent ? "%" : "\xC3\x97")) {
            o.size = sc; S("Marker Scale"); }
    } else {
        auto sizeField = [&](const char* l, double* v) {
            float f = (float)*v;
            if (pr::DragFloat(l, &f, o.sizePercent ? 1.0f : 0.1f, 0.0f,
                              1000000.0f, 2, o.sizePercent ? "%" : "")) { *v = f; S("Marker Size"); }
        };
        if (o.shape == Ink::MarkShape::Circle) sizeField("Radius", &o.size);
        else if (o.shape == Ink::MarkShape::Rectangle) { sizeField("Length", &o.size); sizeField("Width", &o.width); }
        else sizeField("Diagonal", &o.size);
    }
    int un = o.sizePercent ? 0 : 1;
    if (pr::ButtonGroupRow("Unit", kUnit, 2, &un)) {
        const bool toPct = (un == 0);
        if (toPct != o.sizePercent) {
            if (toPct) { o.size = o.size / sw * 100.0; o.width = o.width / sw * 100.0;
                         o.sideOffset = o.sideOffset / sw * 100.0; }
            else       { o.size = o.size * 0.01 * sw; o.width = o.width * 0.01 * sw;
                         o.sideOffset = o.sideOffset * 0.01 * sw; }
            o.sizePercent = toPct; S("Marker Unit");
        }
    }
    if (o.shape != Ink::MarkShape::Circle) {
        float rot = (float)(o.rotation * 180.0 / 3.14159265358979);
        if (pr::DragFloat("Rotation", &rot, 0.5f, -360.0f, 360.0f, 1, "\xC2\xB0")) {
            o.rotation = rot * 3.14159265358979 / 180.0; S("Marker Rotation"); }
    }
    int bd = (int)o.bend;
    if (pr::ButtonGroupRow("Shape", kBend, 3, &bd)) { o.bend = (Ink::MarkBend)bd; S("Marker Bend"); }
    if (o.mode == Ink::MarkObjectMode::Blend) {
        int bl = std::min((int)o.blend, 12);
        if (pr::DropdownRow("Blend mode", kBlend, 13, &bl)) { o.blend = (Ink::BlendMode)bl; S("Marker Blend"); }
        int fr = o.front ? 1 : 0;
        static const char* kOrder[] = { "Behind", "Front" };
        if (pr::ButtonGroupRow("Order", kOrder, 2, &fr)) { o.front = (fr == 1); S("Marker Order"); }
    }
    if (o.shape != Ink::MarkShape::Instance && o.mode != Ink::MarkObjectMode::Subtract) {
        bool inh = o.useStrokeColor;
        if (pr::CheckRow("Stroke colour", &inh)) { o.useStrokeColor = inh; S("Marker Colour"); }
        if (!o.useStrokeColor) {
            bool rel = false;
            if (pr::ColorRow("Colour", &o.color, true, &rel)) doc.SetStyle(hostId, doc.Find(hostId)->style);
            if (rel) S("Marker Colour");
        }
    }
    return changed;
}

// ── Strokes ───────────────────────────────────────────────────────────────────

void Application::PropStrokesSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Path) return;
    const float gs = pr::Gs();
    static const char* kAlign[] = { "Center", "Inner", "Outer" };
    static const char* kCap[]   = { "Butt", "Round", "Square" };
    static const char* kJoin[]  = { "Miter", "Round", "Bevel" };
    static const char* kSpace[] = { "Document", "Viewport px" };

    UI::PanelConfig pc; pc.id = "##strokes"; pc.label = "Strokes";
    pc.defaultOpen = true;
    if (UI::BeginPanel(pc).open) {
        Ink::Style style = n->style;
        bool structural = false;
        const char* structLabel = "Edit Strokes";

        auto liveApply = [&](const char* releaseLabel, bool released) {
            if (!propEditActive_) {
                propEditActive_ = true; propEditNode_ = id;
                propEditBefore_ = n->style;
            }
            doc.SetStyle(id, style);
            if (released && propEditActive_ && propEditNode_ == id) {
                CommitStyleEdit(id, propEditBefore_, releaseLabel);
                propEditActive_ = false;
            }
        };

        const float thumb = kThumbBase * gs;

        // ── LEFT: the vignette rail (line samples + tooltip preview) ──────────
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                            ImVec2(ImGui::GetStyle().ItemSpacing.x,
                                   kRailGap * gs));
        ImGui::BeginGroup();
        for (std::size_t i = 0; i < style.strokes.size(); ++i) {
            ImGui::PushID((int)(100 + i));
            ImVec2 cmn, cmx;
            char tid[16];
            std::snprintf(tid, sizeof tid, "s%d", (int)i);
            if (pr::ThumbTile(tid, thumb, (int)i == propStrokeSel_, &cmn, &cmx))
                propStrokeSel_ = (int)i;
            pr::DrawStrokeSample(ImGui::GetWindowDrawList(), cmn, cmx,
                                 style.strokes[i]);
            // Hover-dwell: a BIGGER sample in a tooltip (Blender-style).
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::PushStyleColor(ImGuiCol_PopupBg,
                    pr::SafeColor(pr::Tok::S_Color_Background_Layer1,
                                  ImVec4(0.13f, 0.13f, 0.15f, 1)));
                ImGui::BeginTooltip();
                const ImVec2 sz(thumb * 4.0f, thumb * 1.6f);
                const ImVec2 mn = ImGui::GetCursorScreenPos();
                ImGui::Dummy(sz);
                ImDrawList* tdl = ImGui::GetWindowDrawList();
                tdl->AddRectFilled(mn, ImVec2(mn.x + sz.x, mn.y + sz.y),
                                   IM_COL32(255, 255, 255, 255));
                pr::DrawStrokeSample(tdl, mn,
                                     ImVec2(mn.x + sz.x, mn.y + sz.y),
                                     style.strokes[i]);
                ImGui::Text("%.2f %s%s", style.strokes[i].width,
                            style.strokes[i].widthSpace ==
                                    Ink::WidthSpace::Viewport
                                ? "px" : "doc",
                            style.strokes[i].dashPattern.empty() ? ""
                                                                 : " \xC2\xB7 dashed");
                ImGui::EndTooltip();
                ImGui::PopStyleColor();
            }
            // Drag the vignette up/down to reorder the stack's draw order
            // (click still selects; the swap needs a real vertical drag).
            if (ImGui::IsItemActive() &&
                std::fabs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y) >
                    (thumb + kRailGap * gs) * 0.6f) {
                const float dy =
                    ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y;
                const int nxt = (int)i + (dy < 0.0f ? -1 : 1);
                if (nxt >= 0 && nxt < (int)style.strokes.size()) {
                    std::swap(style.strokes[i], style.strokes[(std::size_t)nxt]);
                    if (propStrokeSel_ == (int)i) propStrokeSel_ = nxt;
                    else if (propStrokeSel_ == nxt) propStrokeSel_ = (int)i;
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                    liveApply("Reorder Strokes", false);
                }
            }
            if (ImGui::IsItemDeactivated())
                liveApply("Reorder Strokes", true);
            ImGui::PopID();
        }
        if (pr::ThumbAddTile(thumb)) {
            Ink::Stroke s; s.paint.color = pr::ToLinear(edit_.defaultStroke);
            s.width = edit_.defaultStrokeWidth;
            style.strokes.push_back(s);
            propStrokeSel_ = (int)style.strokes.size() - 1;
            structural = true; structLabel = "Add Stroke";
        }
        ImGui::EndGroup();
        ImGui::PopStyleVar();
        ImGui::SameLine(0.0f, 8.0f * gs);

        // ── RIGHT: the selected stroke's properties ───────────────────────────
        propStrokeSel_ = std::clamp(propStrokeSel_, 0,
                                    std::max(0, (int)style.strokes.size() - 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##strokeProps",
                          ImVec2(ImGui::GetContentRegionAvail().x, 0),
                          ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
        if (style.strokes.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                pr::SafeColor(pr::Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1)));
            ImGui::TextUnformatted("No strokes.");
            ImGui::TextUnformatted("Click + to add one.");
            ImGui::PopStyleColor();
        } else {
            Ink::Stroke& s = style.strokes[(std::size_t)propStrokeSel_];

            bool enabled = s.enabled;
            if (pr::CheckRow("Enabled", &enabled)) {
                s.enabled = enabled; structural = true;
            }
            bool released = false;
            if (pr::ColorRow("Color", &s.paint.color, true, &released))
                liveApply("Stroke Colour", false);
            if (released) liveApply("Stroke Colour", true);

            // Width + its space. "Viewport px" = the non-scaling HAIRLINE
            // (constant on-screen width at any zoom).
            float w = (float)s.width;
            if (pr::DragFloat("Width", &w, 0.1f, 0.0f, 10000.0f, 2,
                              s.widthSpace == Ink::WidthSpace::Viewport
                                  ? "px" : "")) {
                s.width = w;
                liveApply("Stroke Width", false);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                liveApply("Stroke Width", true);
            int space = (int)s.widthSpace;
            if (pr::ButtonGroupRow("Width space", kSpace, 2, &space)) {
                s.widthSpace = (Ink::WidthSpace)space;
                // A sensible default when flipping into hairline mode.
                if (s.widthSpace == Ink::WidthSpace::Viewport && s.width > 32.0)
                    s.width = 1.5;
                structural = true; structLabel = "Stroke Width Space";
            }
            if (ImGui::IsItemHovered())
                UI::DrawTooltipTranslucent(
                    "Document: the width scales with the object. "
                    "Viewport px: a hairline with constant on-screen width",
                    ImGui::GetIO().MousePos, 1.0f);

            int align = (int)s.align, cap = (int)s.cap, join = (int)s.join;
            if (pr::DropdownRow("Align", kAlign, 3, &align)) {
                s.align = (Ink::StrokeAlign)align; structural = true;
            }
            if (pr::DropdownRow("Cap", kCap, 3, &cap)) {
                s.cap = (Ink::CapStyle)cap; structural = true;
            }
            if (pr::DropdownRow("Join", kJoin, 3, &join)) {
                s.join = (Ink::JoinStyle)join; structural = true;
            }
            if (s.join == Ink::JoinStyle::Miter) {
                float ml = (float)s.miterLimit;
                if (pr::DragFloat("Miter limit", &ml, 0.1f, 1.0f, 100.0f, 1)) {
                    s.miterLimit = ml;
                    liveApply("Miter Limit", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Miter Limit", true);
            }

            // Dashes (a single on/off pair covers the common cases; the
            // full multi-run pattern editor rides the styles lot).
            pr::GroupGap();
            bool dashed = !s.dashPattern.empty();
            if (pr::CheckRow("Dashed", &dashed)) {
                if (dashed) s.dashPattern = { 12.0, 8.0 };
                else        s.dashPattern.clear();
                structural = true; structLabel = "Stroke Dashes";
            }
            if (!s.dashPattern.empty()) {
                float dash = (float)s.dashPattern[0];
                float gap  = (float)(s.dashPattern.size() > 1
                                         ? s.dashPattern[1]
                                         : s.dashPattern[0]);
                if (pr::DragFloat("Dash", &dash, 0.1f, 0.05f, 10000.0f, 2)) {
                    if (s.dashPattern.size() < 2) s.dashPattern.resize(2, 8.0);
                    s.dashPattern[0] = dash;
                    liveApply("Stroke Dashes", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Stroke Dashes", true);
                if (pr::DragFloat("Gap", &gap, 0.1f, 0.05f, 10000.0f, 2)) {
                    if (s.dashPattern.size() < 2) s.dashPattern.resize(2, 8.0);
                    s.dashPattern[1] = gap;
                    liveApply("Stroke Dashes", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Stroke Dashes", true);
                float dOff = (float)s.dashOffset;
                if (pr::DragFloat("Dash offset", &dOff, 0.1f, -10000.0f,
                                  10000.0f, 2)) {
                    s.dashOffset = dOff;
                    liveApply("Dash Offset", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Dash Offset", true);
            }

            // ── Marks (docs/Ink/IOF_CORE_PLAN.md Phase A — the generic model):
            //    a mark is a point on the line that re-phases the dash run
            //    (Neutral/Dash/Gap) and carries a list of OBJECTS (SVG-marker
            //    shapes or node instances, added or subtracted). The line-mark
            //    tool drops marks on the canvas; this panel edits them. ───────
            pr::GroupGap();
            {
                const int strokeIdx = propStrokeSel_;
                static const char* kBlend[] = {
                    "Normal", "Multiply", "Screen", "Overlay", "Darken",
                    "Lighten", "Color Dodge", "Color Burn", "Hard Light",
                    "Soft Light", "Difference", "Exclusion", "Erase" };
                static const char* kShape[] = {
                    "Circle", "Rectangle", "Diamond", "Instance", "Gap" };
                ImGui::PushStyleColor(ImGuiCol_Text,
                    pr::SafeColor(pr::Tok::S_Color_Text_Subtle,
                                  ImVec4(0.6f, 0.6f, 0.6f, 1)));
                ImGui::Text("Marks (%d)", (int)s.marks.size());
                ImGui::PopStyleColor();

                int removeMark = -1;
                for (std::size_t mi = 0; mi < s.marks.size(); ++mi) {
                    Ink::StrokeMark& m = s.marks[mi];
                    ImGui::PushID((int)(2000 + mi));
                    const EditContext::MarkRef ref{ id, strokeIdx, (int)mi };
                    const bool sel = edit_.MarkSelected(ref);

                    // Position along the line (selecting the row syncs canvas).
                    float t = (float)m.t;
                    if (pr::DragFloat(sel ? "\xE2\x97\x86 Position" : "Position",
                                      &t, 0.002f, 0.0f, 1.0f, 3)) {
                        m.t = t; liveApply("Mark Position", false);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        liveApply("Mark Position", true);
                    if (ImGui::IsItemClicked()) edit_.MarkSelectOnly(ref);

                    // Side (Center / Left / Right) + a signed offset for the
                    // off-line sides, in % of the stroke width (default) or
                    // doc-units. Switching the unit CONVERTS the value so the
                    // object doesn't jump.
                    static const char* kSide[] = { "Center", "Left", "Right" };
                    int sd = (int)m.side;
                    if (pr::ButtonGroupRow("Side", kSide, 3, &sd)) {
                        m.side = (Ink::MarkSide)sd;
                        structural = true; structLabel = "Mark Side";
                    }
                    if (m.side != Ink::MarkSide::Center) {
                        float off = (float)m.offset;
                        if (pr::DragFloat("Distance", &off, m.offsetPercent ? 0.5f : 0.1f,
                                          -100000.0f, 100000.0f, 2,
                                          m.offsetPercent ? "%" : "")) {
                            m.offset = off; liveApply("Mark Distance", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Mark Distance", true);
                        static const char* kUnit[] = { "%", "px" };
                        int un = m.offsetPercent ? 0 : 1;
                        if (pr::ButtonGroupRow("Unit", kUnit, 2, &un)) {
                            const bool toPercent = (un == 0);
                            if (toPercent != m.offsetPercent) {
                                // Convert so the resolved distance is unchanged.
                                const double w = s.width > 1e-9 ? s.width : 1.0;
                                if (toPercent) m.offset = m.offset / w * 100.0;
                                else           m.offset = m.offset * 0.01 * w;
                                m.offsetPercent = toPercent;
                                structural = true; structLabel = "Mark Unit";
                            }
                        }
                        if (ImGui::IsItemHovered())
                            UI::DrawTooltipTranslucent(
                                "100 % = one full stroke width (50 % reaches the "
                                "stroke edge)", ImGui::GetIO().MousePos, 1.0f);
                    }

                    // Dash phase (Neutral / Dash / Gap).
                    static const char* kPhase[] = { "Neutral", "Dash", "Gap" };
                    int ph = (int)m.phase;
                    if (pr::ButtonGroupRow("Dash phase", kPhase, 3, &ph)) {
                        m.phase = (Ink::MarkPhase)ph;
                        structural = true; structLabel = "Mark Phase";
                    }
                    if (ImGui::IsItemHovered())
                        UI::DrawTooltipTranslucent(
                            "Force a dash element or a gap to land on this mark "
                            "(Neutral = no re-phasing)",
                            ImGui::GetIO().MousePos, 1.0f);

                    // ── Objects on this mark (may be none) ────────────────────
                    int removeObj = -1;
                    for (std::size_t oi = 0; oi < m.objects.size(); ++oi) {
                        Ink::MarkObject& o = m.objects[oi];
                        ImGui::PushID((int)(50 + oi));
                        int shp = (int)o.shape;
                        if (pr::DropdownRow("Object", kShape, 5, &shp)) {
                            const Ink::MarkShape ns = (Ink::MarkShape)shp;
                            // Switching TO a rectangle applies its default
                            // proportions (length 200 %) when the dimensions
                            // still hold the shared 100 % default.
                            if (ns == Ink::MarkShape::Rectangle &&
                                o.shape != Ink::MarkShape::Rectangle &&
                                o.sizePercent && std::abs(o.size - 100.0) < 1e-6)
                                o.size = 200.0;
                            // Give the new shape its default bend.
                            o.bend = Ink::DefaultBendFor(ns);
                            o.shape = ns;
                            structural = true; structLabel = "Mark Object";
                        }
                        const bool isGap = o.shape == Ink::MarkShape::Gap;
                        // ── Gap: opens the line over a length with end caps ──
                        if (isGap) {
                            float len = (float)o.size;
                            if (pr::DragFloat("Length", &len,
                                    o.sizePercent ? 1.0f : 0.1f, 0.0f, 1000000.0f,
                                    2, o.sizePercent ? "%" : "")) {
                                o.size = len; structural = true;
                                structLabel = "Gap Length";
                            }
                            static const char* kUnitG[] = { "%", "px" };
                            const double sw = s.width > 1e-9 ? s.width : 1.0;
                            int un = o.sizePercent ? 0 : 1;
                            if (pr::ButtonGroupRow("Unit", kUnitG, 2, &un)) {
                                const bool toPct = (un == 0);
                                if (toPct != o.sizePercent) {
                                    o.size = toPct ? o.size / sw * 100.0
                                                   : o.size * 0.01 * sw;
                                    o.sizePercent = toPct;
                                    structural = true; structLabel = "Gap Unit";
                                }
                            }
                            static const char* kCap[] = { "Butt", "Round", "Square" };
                            int cs = (int)o.gapStart;
                            if (pr::DropdownRow("Start cap", kCap, 3, &cs)) {
                                o.gapStart = (Ink::GapCap)cs;
                                structural = true; structLabel = "Gap Cap";
                            }
                            int ce = (int)o.gapEnd;
                            if (pr::DropdownRow("End cap", kCap, 3, &ce)) {
                                o.gapEnd = (Ink::GapCap)ce;
                                structural = true; structLabel = "Gap Cap";
                            }
                            bool cut = o.gapCutsObjects;
                            if (pr::CheckRow("Cut objects", &cut)) {
                                o.gapCutsObjects = cut;
                                structural = true; structLabel = "Gap Cut Objects";
                            }
                            if (ImGui::IsItemHovered())
                                UI::DrawTooltipTranslucent(
                                    "Also remove the other mark objects that "
                                    "fall inside the gap (not just the line).",
                                    ImGui::GetIO().MousePos, 1.0f);
                            // Start / End marker OBJECT lists — each is a full
                            // sub-object (shape, mode, bend, size, side, blend,
                            // colour), edited by the shared compact editor.
                            auto markerList = [&](const char* label,
                                                  std::vector<Ink::MarkObject>& lst) {
                                ImGui::PushStyleColor(ImGuiCol_Text,
                                    pr::SafeColor(pr::Tok::S_Color_Text_Subtle,
                                                  ImVec4(0.6f, 0.6f, 0.6f, 1)));
                                ImGui::Text("%s (%d)", label, (int)lst.size());
                                ImGui::PopStyleColor();
                                int rm = -1;
                                for (std::size_t k = 0; k < lst.size(); ++k) {
                                    ImGui::PushID((int)(400 + k));
                                    if (PropMarkObjectCompact(lst[k], s.width, doc,
                                                              id, structural,
                                                              structLabel))
                                        {}
                                    pr::ControlColumn();
                                    if (ImGui::SmallButton("Remove")) rm = (int)k;
                                    ImGui::Separator();
                                    ImGui::PopID();
                                }
                                if (rm >= 0) {
                                    lst.erase(lst.begin() + rm);
                                    structural = true; structLabel = "Gap Marker";
                                }
                                pr::ControlColumn();
                                if (ImGui::SmallButton("Add")) {
                                    Ink::MarkObject mo;   // fused circle default
                                    lst.push_back(mo);
                                    structural = true; structLabel = "Gap Marker";
                                }
                            };
                            pr::GroupGap();
                            ImGui::PushID("gapStart");
                            markerList("Start markers", o.gapStartObjects);
                            ImGui::PopID();
                            pr::GroupGap();
                            ImGui::PushID("gapEnd");
                            markerList("End markers", o.gapEndObjects);
                            ImGui::PopID();

                            pr::GroupGap();
                            pr::ControlColumn();
                            if (ImGui::SmallButton("Remove object")) removeObj = (int)oi;
                            ImGui::Separator();
                            ImGui::PopID();
                            continue;
                        }
                        // Mode: Fusion (part of the stroke) / Blend / Subtract.
                        static const char* kMode[] = { "Fusion", "Blend", "Cut" };
                        int md = (int)o.mode;
                        if (pr::ButtonGroupRow("Mode", kMode, 3, &md)) {
                            o.mode = (Ink::MarkObjectMode)md;
                            structural = true; structLabel = "Mark Object Mode";
                        }
                        if (ImGui::IsItemHovered())
                            UI::DrawTooltipTranslucent(
                                "Fusion: the shape becomes part of the stroke "
                                "(one drawing). Blend: composited over the "
                                "stroke with a blend mode. Cut: erases the "
                                "stroke.", ImGui::GetIO().MousePos, 1.0f);
                        if (o.shape == Ink::MarkShape::Instance) {
                            bool pickReq = false;
                            if (pr::NodePickerRow("Node", doc, &o.nodeRef, id,
                                                  true, false, &pickReq)) {
                                structural = true; structLabel = "Mark Instance";
                            }
                            if (pickReq) {
                                const std::size_t mmi = mi, ooi = oi;
                                const int sIdx = strokeIdx;
                                BeginObjectPick(nullptr,
                                    [this, id, sIdx, mmi, ooi](Ink::NodeId picked) {
                                        if (!project_.document) return;
                                        const Ink::Node* nn = project_.document->Find(id);
                                        if (!nn || sIdx < 0 ||
                                            sIdx >= (int)nn->style.strokes.size())
                                            return;
                                        Ink::Style before = nn->style, after = before;
                                        auto& mks = after.strokes[(std::size_t)sIdx].marks;
                                        if (mmi >= mks.size() ||
                                            ooi >= mks[mmi].objects.size()) return;
                                        mks[mmi].objects[ooi].nodeRef = picked;
                                        project_.document->SetStyle(id, after);
                                        CommitStyleEdit(id, before, "Mark Instance");
                                    });
                            }
                            // Scale: `size` is the instance factor (100 % = ×1
                            // when in percent, or a raw multiplier in units).
                            float sc = (float)o.size;
                            if (pr::DragFloat("Scale", &sc, o.sizePercent ? 1.0f : 0.01f,
                                              0.0f, 1000000.0f, 2,
                                              o.sizePercent ? "%" : "\xC3\x97")) {
                                o.size = sc; liveApply("Mark Scale", false);
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit())
                                liveApply("Mark Scale", true);
                            static const char* kUnit2[] = { "%", "\xC3\x97" };
                            int un2 = o.sizePercent ? 0 : 1;
                            if (pr::ButtonGroupRow("Unit", kUnit2, 2, &un2)) {
                                const bool toPct = (un2 == 0);
                                if (toPct != o.sizePercent) {
                                    // %→× : 100 %% = ×1.
                                    o.size = toPct ? o.size * 100.0 : o.size * 0.01;
                                    o.sizePercent = toPct;
                                    structural = true; structLabel = "Mark Unit";
                                }
                            }
                        } else {
                            // A size field (in % of stroke width or doc-units,
                            // per the object's sizePercent), with a shared unit
                            // toggle that CONVERTS both size + width.
                            const double sw = s.width > 1e-9 ? s.width : 1.0;
                            auto sizeField = [&](const char* label, double* v) {
                                float f = (float)*v;
                                if (pr::DragFloat(label, &f,
                                        o.sizePercent ? 1.0f : 0.1f, 0.0f,
                                        1000000.0f, 2, o.sizePercent ? "%" : "")) {
                                    *v = f; liveApply("Mark Object Size", false);
                                }
                                if (ImGui::IsItemDeactivatedAfterEdit())
                                    liveApply("Mark Object Size", true);
                            };
                            if (o.shape == Ink::MarkShape::Circle) {
                                sizeField("Radius", &o.size);
                            } else if (o.shape == Ink::MarkShape::Rectangle) {
                                sizeField("Length", &o.size);
                                sizeField("Width", &o.width);
                            } else {   // Diamond
                                sizeField("Diagonal", &o.size);
                            }
                            static const char* kUnit[] = { "%", "px" };
                            int un = o.sizePercent ? 0 : 1;
                            if (pr::ButtonGroupRow("Unit", kUnit, 2, &un)) {
                                const bool toPct = (un == 0);
                                if (toPct != o.sizePercent) {
                                    if (toPct) { o.size = o.size / sw * 100.0;
                                                 o.width = o.width / sw * 100.0; }
                                    else       { o.size = o.size * 0.01 * sw;
                                                 o.width = o.width * 0.01 * sw; }
                                    o.sizePercent = toPct;
                                    structural = true; structLabel = "Mark Unit";
                                }
                            }
                            if (ImGui::IsItemHovered())
                                UI::DrawTooltipTranslucent(
                                    "100 % = one full stroke width",
                                    ImGui::GetIO().MousePos, 1.0f);
                        }
                        // Rotation — a circle has no orientation, so skip it.
                        if (o.shape != Ink::MarkShape::Circle) {
                            float rot = (float)(o.rotation * 180.0 / 3.14159265358979);
                            if (pr::DragFloat("Rotation", &rot, 0.5f, -360.0f,
                                              360.0f, 1, "\xC2\xB0")) {
                                o.rotation = rot * 3.14159265358979 / 180.0;
                                liveApply("Mark Object Rotation", false);
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit())
                                liveApply("Mark Object Rotation", true);
                        }
                        // Along-offset: shift the object before/after the mark
                        // along the line (percent of stroke width or doc-units).
                        {
                            float ao = (float)o.alongOffset;
                            if (pr::DragFloat("Along", &ao,
                                    o.sizePercent ? 1.0f : 0.1f, -100000.0f,
                                    100000.0f, 2, o.sizePercent ? "%" : "")) {
                                o.alongOffset = ao; liveApply("Mark Along", false);
                            }
                            if (ImGui::IsItemDeactivatedAfterEdit())
                                liveApply("Mark Along", true);
                            if (ImGui::IsItemHovered())
                                UI::DrawTooltipTranslucent(
                                    "Nudge the object along the line, before (−) "
                                    "or after (+) the mark point.",
                                    ImGui::GetIO().MousePos, 1.0f);
                        }
                        // Hard shape / Bend / Follow — all bend along the curve
                        // except Hard (Bend and Follow curve the outline).
                        {
                            static const char* kBend[] = { "Hard", "Bend",
                                                           "Follow" };
                            int bd = (int)o.bend;
                            if (pr::ButtonGroupRow("Shape", kBend, 3, &bd)) {
                                o.bend = (Ink::MarkBend)bd;
                                structural = true; structLabel = "Mark Bend";
                            }
                            if (ImGui::IsItemHovered())
                                UI::DrawTooltipTranslucent(
                                    "Hard: a rigid shape. Bend / Follow: the "
                                    "outline curves along the line.",
                                    ImGui::GetIO().MousePos, 1.0f);
                        }
                        // Blend mode + front/behind ordering (Blend mode only).
                        if (o.mode == Ink::MarkObjectMode::Blend) {
                            int bl = std::min((int)o.blend, 12);
                            if (pr::DropdownRow("Blend mode", kBlend, 13, &bl)) {
                                o.blend = (Ink::BlendMode)bl;
                                structural = true; structLabel = "Mark Blend";
                            }
                            static const char* kOrder[] = { "Behind", "Front" };
                            int fr = o.front ? 1 : 0;
                            if (pr::ButtonGroupRow("Order", kOrder, 2, &fr)) {
                                o.front = (fr == 1);
                                structural = true; structLabel = "Mark Order";
                            }
                            if (ImGui::IsItemHovered())
                                UI::DrawTooltipTranslucent(
                                    "Front: the mark blends over the stroke. "
                                    "Behind: the stroke blends over the mark "
                                    "(the reverse blend order).",
                                    ImGui::GetIO().MousePos, 1.0f);
                        }
                        // Colour (primitives; Cut ignores colour). Available in
                        // every non-Cut mode, Fusion included.
                        if (o.shape != Ink::MarkShape::Instance &&
                            o.mode != Ink::MarkObjectMode::Subtract) {
                            bool inh = o.useStrokeColor;
                            if (pr::CheckRow("Stroke colour", &inh)) {
                                o.useStrokeColor = inh;
                                structural = true; structLabel = "Mark Colour";
                            }
                            if (!o.useStrokeColor) {
                                bool rel = false;
                                if (pr::ColorRow("Colour", &o.color, true, &rel))
                                    liveApply("Mark Colour", false);
                                if (rel) liveApply("Mark Colour", true);
                            }
                        }
                        pr::ControlColumn();
                        if (ImGui::SmallButton("Remove object")) removeObj = (int)oi;
                        ImGui::Separator();
                        ImGui::PopID();
                    }
                    if (removeObj >= 0) {
                        m.objects.erase(m.objects.begin() + removeObj);
                        structural = true; structLabel = "Remove Mark Object";
                    }
                    pr::ControlColumn();
                    if (ImGui::SmallButton("Add object")) {
                        m.objects.push_back(Ink::MarkObject{});   // circle 100 %
                        structural = true; structLabel = "Add Mark Object";
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove mark")) removeMark = (int)mi;
                    if ((int)mi + 1 < (int)s.marks.size()) ImGui::Separator();
                    ImGui::PopID();
                }
                if (removeMark >= 0) {
                    s.marks.erase(s.marks.begin() + removeMark);
                    edit_.markSel.clear();
                    structural = true; structLabel = "Remove Mark";
                }
                pr::ControlColumn();
                if (ImGui::SmallButton("Add mark")) {
                    Ink::StrokeMark m;
                    m.t = 0.5;
                    m.objects.push_back(Ink::MarkObject{});   // fused circle 100 %
                    s.marks.push_back(m);
                    structural = true; structLabel = "Add Mark";
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Add dash tick")) {
                    Ink::StrokeMark m;   // objectless dash re-phaser
                    m.t = 0.5; m.phase = Ink::MarkPhase::Dash;
                    s.marks.push_back(m);
                    structural = true; structLabel = "Add Dash Tick";
                }
            }

            pr::GroupGap();
            pr::ControlColumn();
            if (ImGui::SmallButton("Remove")) {
                style.strokes.erase(style.strokes.begin() + propStrokeSel_);
                propStrokeSel_ = std::max(0, propStrokeSel_ - 1);
                structural = true; structLabel = "Remove Stroke";
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        if (structural) {
            const Ink::Style before = n->style;
            doc.SetStyle(id, style);
            CommitStyleEdit(id, before, structLabel);
        }
    }
    UI::EndPanel();
}

} // namespace App
