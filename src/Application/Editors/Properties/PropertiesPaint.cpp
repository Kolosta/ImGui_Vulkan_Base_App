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
        const float railGap = 4.0f * gs;

        // ── LEFT: the vignette rail (+ the "add" tile) ────────────────────────
        ImGui::BeginGroup();
        for (std::size_t i = 0; i < style.fills.size(); ++i) {
            ImGui::PushID((int)i);
            ImVec2 cmn, cmx;
            char tid[16];
            std::snprintf(tid, sizeof tid, "f%d", (int)i);
            if (pr::ThumbTile(tid, thumb, (int)i == propFillSel_, &cmn, &cmx))
                propFillSel_ = (int)i;
            pr::DrawFillSample(ImGui::GetWindowDrawList(), cmn, cmx,
                               style.fills[i]);
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, railGap * 0.5f));
        }
        if (pr::ThumbAddTile(thumb)) {
            Ink::Fill f; f.paint.color = pr::ToLinear(edit_.defaultFill);
            style.fills.push_back(f);
            propFillSel_ = (int)style.fills.size() - 1;
            structural = true; structLabel = "Add Fill";
        }
        ImGui::EndGroup();
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
        const float railGap = 4.0f * gs;

        // ── LEFT: the vignette rail (line samples + tooltip preview) ──────────
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
                pr::DrawStrokeSample(ImGui::GetWindowDrawList(), mn,
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
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, railGap * 0.5f));
        }
        if (pr::ThumbAddTile(thumb)) {
            Ink::Stroke s; s.paint.color = pr::ToLinear(edit_.defaultStroke);
            s.width = edit_.defaultStrokeWidth;
            style.strokes.push_back(s);
            propStrokeSel_ = (int)style.strokes.size() - 1;
            structural = true; structLabel = "Add Stroke";
        }
        ImGui::EndGroup();
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
