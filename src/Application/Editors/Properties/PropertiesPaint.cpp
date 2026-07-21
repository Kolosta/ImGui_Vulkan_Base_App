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

bool Application::PaintPatternSwatch(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                                     const Ink::Fill& f) {
    if (!project_.document || f.kind != Ink::FillKind::Pattern) return false;
    const Ink::Node* motif = project_.document->Find(f.pattern.motifRef);
    if (!motif || motif->kind != Ink::NodeKind::Path || motif->path.Empty())
        return false;
    const float wpx = mx.x - mn.x, hpx = mx.y - mn.y;
    if (wpx < 4.0f || hpx < 4.0f) return false;

    // Tessellate the MOTIF's geometry ONCE, in motif-local doc units (the
    // motif's own origin is the pattern anchor, exactly like the pipeline).
    // CPU triangles — so a HIDDEN motif still renders (patterns instance a
    // motif regardless of its visibility), and there are no texture / bounding
    // box artefacts. Same geometry functions the Vulkan pipeline uses, so the
    // swatch matches the on-canvas pattern.
    struct Tri { ImVec2 a, b, c; ImU32 col; };
    std::vector<Tri> tris;
    const double motifTol = 0.15;   // legible, cheap
    auto pushMesh = [&](const Ink::geom::Mesh& m, ImU32 col) {
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            auto V = [&](std::uint32_t idx) {
                return ImVec2((float)m.positions[idx * 2],
                              (float)m.positions[idx * 2 + 1]);
            };
            tris.push_back({ V(m.indices[i]), V(m.indices[i + 1]),
                             V(m.indices[i + 2]), col });
        }
    };
    const float fOpac = std::clamp(f.opacity, 0.0f, 1.0f);
    const auto motifFlat = Ink::geom::Flatten(motif->path, motifTol);
    for (const Ink::Fill& mf : motif->style.fills) {
        if (!mf.enabled || mf.kind != Ink::FillKind::Solid) continue;
        ImVec4 c = pr::ToSrgb(mf.paint.color);
        c.w *= mf.opacity * fOpac;
        pushMesh(Ink::geom::TriangulateFill(motifFlat, mf.rule),
                 ImGui::ColorConvertFloat4ToU32(c));
    }
    for (const Ink::Stroke& ms : motif->style.strokes) {
        if (!ms.enabled || ms.width <= 0.0) continue;
        ImVec4 c = pr::ToSrgb(ms.paint.color);
        c.w *= fOpac;
        Ink::Stroke bs = ms; bs.marks.clear(); bs.repeats.clear();
        pushMesh(Ink::geom::TessellateStroke(motifFlat, bs, motifTol),
                 ImGui::ColorConvertFloat4ToU32(c));
    }
    if (tris.empty()) return false;

    // Re-create the lattice INSIDE the vignette at a legible zoom: about 2.4
    // lattice periods across the tile, over a white plate, clipped to it —
    // the exact EmitPattern layout (lattice rotation, per-motif rotation +
    // scale, phase).
    const Ink::PatternFill& pat = f.pattern;
    const double sx = pat.spacingX > 1e-6 ? pat.spacingX : 40.0;
    const double sy = pat.spacingY > 1e-6 ? pat.spacingY : 40.0;
    const double unitPx = (double)std::min(wpx, hpx) / (2.4 * std::max(sx, sy));
    const ImVec2 ctr((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
    dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 255));
    dl->PushClipRect(mn, mx, true);
    const double lc = std::cos(pat.rotation), ls = std::sin(pat.rotation);
    const double mrot = pat.rotation + pat.motifRotation;
    const double sc = std::max(1e-3, pat.scale);
    const double mc = std::cos(mrot) * sc, msn = std::sin(mrot) * sc;
    // Motif reach in px (cull cells whose motif can't touch the tile).
    double motifR = 0.0;
    for (const Tri& t : tris) {
        motifR = std::max(motifR, (double)std::hypot(t.a.x, t.a.y));
        motifR = std::max(motifR, (double)std::hypot(t.b.x, t.b.y));
        motifR = std::max(motifR, (double)std::hypot(t.c.x, t.c.y));
    }
    const double reachPx = motifR * sc * unitPx;
    const double Rpx = std::hypot(wpx, hpx) * 0.5 + reachPx;
    const int gi1 = (int)std::ceil(Rpx / (sx * unitPx)) + 1;
    const int gj1 = (int)std::ceil(Rpx / (sy * unitPx)) + 1;
    int cells = 0;
    for (int gi = -gi1; gi <= gi1 && cells < 200; ++gi)
        for (int gj = -gj1; gj <= gj1 && cells < 200; ++gj) {
            const double lx = (gi * sx + pat.phaseX) * unitPx;
            const double ly = (gj * sy + pat.phaseY) * unitPx;
            const double cx = ctr.x + (lx * lc - ly * ls);
            const double cy = ctr.y + (lx * ls + ly * lc);
            if (std::abs(cx - ctr.x) > Rpx || std::abs(cy - ctr.y) > Rpx)
                continue;
            ++cells;
            auto place = [&](ImVec2 q) {
                // q is motif-local doc units → scaled/rotated → px, at the cell.
                const double qx = q.x, qy = q.y;
                return ImVec2((float)(cx + unitPx * (qx * mc - qy * msn)),
                              (float)(cy + unitPx * (qx * msn + qy * mc)));
            };
            for (const Tri& t : tris)
                dl->AddTriangleFilled(place(t.a), place(t.b), place(t.c),
                                      t.col);
        }
    dl->PopClipRect();
    return true;
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

        // Style lock (Node::propLocks): the whole paint stack renders
        // read-only (IOF: spec-fixed symbol colours/patterns).
        const bool styleLocked = (n->propLocks & Ink::PropLockStyle) != 0;
        if (pr::LockToggle("##lkFillStyle", styleLocked,
                           (n->propLocks & Ink::PropLockManaged) != 0))
            TogglePropLock(id, Ink::PropLockStyle);
        ImGui::BeginDisabled(styleLocked);
        DrawFillsStackBody(style, id, propFillSel_, liveApply,
                           structural, structLabel);
        ImGui::EndDisabled();
        if (styleLocked) structural = false;   // belt & braces: no writes

        if (structural) {
            const Ink::Style before = n->style;
            doc.SetStyle(id, style);
            CommitStyleEdit(id, before, structLabel);
        }
    }
    UI::EndPanel();
}

// ── Shared fills stack body (Properties + the Fill editor) ───────────────────
// The vignette rail + the selected fill's properties, editing `style` in place.
// `node` = the pattern-preview / eyedropper target (kNullNode when editing the
// default style); `sel` = the caller's rail selection.
void Application::DrawFillsStackBody(
    Ink::Style& style, Ink::NodeId node, int& sel,
    const std::function<void(const char*, bool)>& liveApply,
    bool& structural, const char*& structLabel) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const float gs = pr::Gs();
    {
        const float thumb = kThumbBase * gs;

        // ── LEFT: the vignette rail (dynamic drag-reorder + the "add" tile) ───
        // A pattern fill's vignette is the REAL pipeline render of the node; a
        // solid stays a plain swatch. Dragging a vignette floats it above the
        // others (which slide aside) — the modifier-panel behaviour.
        const float cellH = thumb + kRailGap * gs;
        const ImVec2 railOrigin = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        {
            const int nF = (int)style.fills.size();
            pr::VReorder rr("##fillRail", nF, cellH);
            const int grabbed = rr.Grabbed();
            for (int i = 0; i < nF; ++i) {
                ImGui::PushID(i);
                const Ink::Fill& f = style.fills[(std::size_t)i];
                const bool isGrab = (i == grabbed);
                float posY = railOrigin.y + (float)i * cellH + rr.CellOffset(i);
                if (isGrab) posY = rr.GrabbedScreenY(railOrigin.y);
                const ImVec2 pos(railOrigin.x, posY);
                ImVec2 cmn, cmx;
                char tid[16];
                std::snprintf(tid, sizeof tid, "f%d", i);
                const bool clicked =
                    pr::ThumbTile(tid, thumb, i == sel, &cmn, &cmx, &pos);
                rr.HandleCell(i, ImGui::IsItemActivated(), ImGui::IsItemActive(),
                              railOrigin.y + (float)i * cellH, railOrigin.y);
                if (clicked && rr.Grabbed() < 0) sel = i;
                if (!isGrab) {
                    // Pattern vignettes RE-CREATE the fill in the tile (motif
                    // lattice at a legible zoom, selection-independent).
                    if (f.kind != Ink::FillKind::Pattern ||
                        !PaintPatternSwatch(ImGui::GetWindowDrawList(),
                                            cmn, cmx, f))
                        pr::DrawFillSample(ImGui::GetWindowDrawList(),
                                           cmn, cmx, f);
                }
                // Hover-dwell preview (only when nothing is being dragged).
                if (grabbed < 0 &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
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
                    if (f.kind != Ink::FillKind::Pattern ||
                        !PaintPatternSwatch(tdl, mn,
                                            ImVec2(mn.x + big, mn.y + big), f))
                        pr::DrawFillSample(tdl, mn,
                                           ImVec2(mn.x + big, mn.y + big), f);
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
                ImGui::PopID();
            }
            // The grabbed tile's sample on top (foreground draw list).
            if (grabbed >= 0 && grabbed < nF) {
                const Ink::Fill& gf = style.fills[(std::size_t)grabbed];
                const float posY = rr.GrabbedScreenY(railOrigin.y);
                const float ins = 3.0f * gs;
                const ImVec2 gmn(railOrigin.x + ins, posY + ins);
                const ImVec2 gmx(railOrigin.x + thumb - ins, posY + thumb - ins);
                ImDrawList* fdl = ImGui::GetForegroundDrawList();
                if (gf.kind != Ink::FillKind::Pattern ||
                    !PaintPatternSwatch(fdl, gmn, gmx, gf))
                    pr::DrawFillSample(fdl, gmn, gmx, gf);
            }
            ImGui::SetCursorScreenPos(
                ImVec2(railOrigin.x, railOrigin.y + (float)nF * cellH));
            if (pr::ThumbAddTile(thumb)) {
                // Seed the new fill from the default stack's first entry.
                Ink::Fill f = edit_.defaultFills.empty() ? Ink::Fill{}
                                                         : edit_.defaultFills.front();
                style.fills.push_back(f);
                sel = (int)style.fills.size() - 1;
                structural = true; structLabel = "Add Fill";
            }
            pr::VReorder::Move mv = rr.Commit();
            if (mv.from >= 0 && mv.to >= 0 && mv.from != mv.to &&
                mv.from < (int)style.fills.size() &&
                mv.to < (int)style.fills.size()) {
                Ink::Fill moved = style.fills[(std::size_t)mv.from];
                style.fills.erase(style.fills.begin() + mv.from);
                style.fills.insert(style.fills.begin() + mv.to, moved);
                if (sel == mv.from) sel = mv.to;
                else if (mv.from < mv.to && sel > mv.from &&
                         sel <= mv.to) --sel;
                else if (mv.to < mv.from && sel >= mv.to &&
                         sel < mv.from) ++sel;
                liveApply("Reorder Fills", true);
            }
        }
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 8.0f * gs);

        // ── RIGHT: the selected fill's properties ─────────────────────────────
        sel = std::clamp(sel, 0, std::max(0, (int)style.fills.size() - 1));
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
            Ink::Fill& f = style.fills[(std::size_t)sel];

            bool enabled = f.enabled;
            if (pr::CheckRow("Enabled", &enabled)) {
                f.enabled = enabled; structural = true;
            }
            static const char* kKind[] = { "Solid", "Pattern", "Instanced" };
            int kind = (int)f.kind;
            if (pr::DropdownRow("Type", kKind, 3, &kind)) {
                f.kind = (Ink::FillKind)kind; structural = true;
                // A fresh pattern defaults to the exact contour clip.
                if (f.kind == Ink::FillKind::Pattern &&
                    f.pattern.motifRef == Ink::kNullNode)
                    f.pattern.clip = Ink::PatternClip::Contour;
                // A fresh instanced fill seeds one circle so it shows at once.
                if (f.kind == Ink::FillKind::Instanced &&
                    f.instanced.elements.empty() && f.instanced.lines.empty())
                    f.instanced.elements.push_back(Ink::InstElement{});
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
            } else if (f.kind == Ink::FillKind::Pattern) {
                // ── Pattern fill (legacy fill-layer feature set) ──
                bool pickReq = false;
                if (pr::NodePickerRow("Motif", doc, &f.pattern.motifRef, node,
                                      /*allowNone=*/true, /*pathsOnly=*/false,
                                      &pickReq)) {
                    structural = true; structLabel = "Pattern Motif";
                }
                if (pickReq) {
                    // Eyedropper: pick a node in the viewport/outliner; the
                    // commit re-fetches the style so the write survives. When
                    // there is NO host node (the Fill editor on the default
                    // style), the pick writes into the defaults instead.
                    const std::size_t fi = (std::size_t)sel;
                    const Ink::NodeId nid = node;
                    BeginObjectPick(nullptr, [this, nid, fi](Ink::NodeId picked) {
                        if (!project_.document) return;
                        if (nid != Ink::kNullNode) {
                            const Ink::Node* nn = project_.document->Find(nid);
                            if (!nn || fi >= nn->style.fills.size()) return;
                            Ink::Style before = nn->style, after = before;
                            after.fills[fi].pattern.motifRef = picked;
                            project_.document->SetStyle(nid, after);
                            CommitStyleEdit(nid, before, "Pattern Motif");
                        } else if (fi < edit_.defaultFills.size()) {
                            edit_.defaultFills[fi].pattern.motifRef = picked;
                            ApplyDefaultFillsEdit("Pattern Motif", true);
                        }
                    });
                }
                float sp[2] = { (float)f.pattern.spacingX,
                                (float)f.pattern.spacingY };
                bool dx = false, dy = false;
                unsigned ch = pr::Vec2Group("Spacing", sp, 0.2f, 0.1f,
                                            10000.0f, 2, "", &dx, &dy, true,
                                            pr::Quantity::Length);
                if (ch) {
                    f.pattern.spacingX = sp[0]; f.pattern.spacingY = sp[1];
                    liveApply("Pattern Spacing", false);
                }
                if (dx || dy) liveApply("Pattern Spacing", true);

                float off[2] = { (float)f.pattern.phaseX,
                                 (float)f.pattern.phaseY };
                ch = pr::Vec2Group("Offset", off, 0.2f, -10000.0f,
                                   10000.0f, 2, "", &dx, &dy, true,
                                   pr::Quantity::Length);
                if (ch) {
                    f.pattern.phaseX = off[0]; f.pattern.phaseY = off[1];
                    liveApply("Pattern Offset", false);
                }
                if (dx || dy) liveApply("Pattern Offset", true);

                pr::GroupGap();
                float angleDeg =
                    (float)(f.pattern.rotation * 180.0 / 3.14159265358979);
                if (pr::DragFloat("Angle", &angleDeg, 0.5f, -360.0f, 360.0f,
                                  1, "", pr::Quantity::Angle)) {
                    f.pattern.rotation = angleDeg * 3.14159265358979 / 180.0;
                    liveApply("Pattern Angle", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Pattern Angle", true);

                float motifDeg =
                    (float)(f.pattern.motifRotation * 180.0 / 3.14159265358979);
                if (pr::DragFloat("Motif angle", &motifDeg, 0.5f, -360.0f,
                                  360.0f, 1, "", pr::Quantity::Angle)) {
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
            } else {
                // ── Instanced fill: generated shapes + line-sets on a grid or
                // scatter layout (Scene::EmitInstancedFill) ──
                Ink::InstancedFill& in = f.instanced;
                constexpr double kD2R = 3.14159265358979 / 180.0;
                constexpr double kR2D = 180.0 / 3.14159265358979;
                // A unit-aware drag over a DOUBLE store: render + live-apply +
                // release-commit, all correctly sequenced. `s2d`/`d2s` convert
                // store↔display (radians↔degrees for an angle; identity else).
                auto dragD = [&](const char* label, double* dst, float speed,
                                 double mn, double mx, int dec, pr::Quantity q,
                                 const char* lbl, double s2d = 1.0,
                                 double d2s = 1.0) {
                    float v = (float)((*dst) * s2d);
                    const bool ch = pr::DragFloat(label, &v, speed, (float)mn,
                                                  (float)mx, dec, "", q);
                    if (ch) { *dst = (double)v * d2s; liveApply(lbl, false); }
                    if (ImGui::IsItemDeactivatedAfterEdit()) liveApply(lbl, true);
                };
                static const char* kMode[] = { "Add", "Blend", "Cut" };

                // The fill's base colour — shapes/lines with "Fill colour" on
                // inherit it (a single knob to recolour the whole field).
                {
                    bool crel = false;
                    if (pr::ColorRow("Fill colour", &f.paint.color, true, &crel))
                        liveApply("Fill Colour", false);
                    if (crel) liveApply("Fill Colour", true);
                }

                static const char* kLayout[] = { "Grid", "Scatter" };
                int layout = (int)in.layout;
                if (pr::ButtonGroupRow("Layout", kLayout, 2, &layout)) {
                    in.layout = (Ink::InstLayout)layout;
                    structural = true; structLabel = "Instanced Layout";
                }

                if (in.layout == Ink::InstLayout::Grid) {
                    static const char* kAxes[] = { "2 axes", "3 axes" };
                    int axes = in.gridAxes >= 3 ? 1 : 0;
                    if (pr::ButtonGroupRow("Grid", kAxes, 2, &axes)) {
                        in.gridAxes = axes == 1 ? 3 : 2;
                        structural = true; structLabel = "Grid Axes";
                    }
                    dragD("Spacing 1", &in.spacing[0], 0.2f, 0.1, 100000.0, 2,
                          pr::Quantity::Length, "Grid Spacing");
                    dragD("Angle 1", &in.axisAngle[0], 0.5f, -360.0, 360.0, 1,
                          pr::Quantity::Angle, "Grid Angle", kR2D, kD2R);
                    if (in.gridAxes < 3) {
                        dragD("Spacing 2", &in.spacing[1], 0.2f, 0.1, 100000.0, 2,
                              pr::Quantity::Length, "Grid Spacing");
                        dragD("Angle 2", &in.axisAngle[1], 0.5f, -360.0, 360.0, 1,
                              pr::Quantity::Angle, "Grid Angle", kR2D, kD2R);
                    }
                } else {
                    // Density is driven by a COUNT (N spread over the area) OR a
                    // DISTANCE band (fills the whole area at that spacing) — never
                    // both. The area is always filled either way.
                    static const char* kSMode[] = { "Count", "Distance" };
                    int smode = (int)in.scatterMode;
                    if (pr::ButtonGroupRow("Density", kSMode, 2, &smode)) {
                        in.scatterMode = (Ink::InstScatterMode)smode;
                        structural = true; structLabel = "Scatter Density";
                    }
                    if (in.scatterMode == Ink::InstScatterMode::Count) {
                        int count = in.scatterCount;
                        if (pr::DragInt("Count", &count, 1.0f, 0, 100000)) {
                            in.scatterCount = count;
                            liveApply("Scatter Count", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Scatter Count", true);
                    } else {
                        dragD("Min distance", &in.scatterMinDist, 0.2f, 0.0,
                              100000.0, 2, pr::Quantity::Length, "Scatter Min");
                        dragD("Max distance", &in.scatterMaxDist, 0.2f, 0.0,
                              100000.0, 2, pr::Quantity::Length, "Scatter Max");
                    }
                    bool avoid = in.avoidCollisions;
                    if (pr::CheckRow("No overlap", &avoid)) {
                        in.avoidCollisions = avoid;
                        structural = true; structLabel = "Scatter Collisions";
                    }
                    if (ImGui::IsItemHovered())
                        UI::DrawTooltipTranslucent(
                            "Keep whole shapes from overlapping — centres stay at "
                            "least the two shapes' radii apart",
                            ImGui::GetIO().MousePos, 1.0f);
                }

                pr::GroupGap();
                // Position jitter only applies to the regular grid — a scatter
                // is already random and its spacing/overlap is controlled above.
                if (in.layout == Ink::InstLayout::Grid)
                    dragD("Pos jitter", &in.posJitter, 0.2f, 0.0, 100000.0, 2,
                          pr::Quantity::Length, "Position Jitter");
                dragD("Rot jitter", &in.rotJitter, 0.5f, 0.0, 360.0, 1,
                      pr::Quantity::Angle, "Rotation Jitter", kR2D, kD2R);
                int seed = (int)in.seed;
                if (pr::DragInt("Seed", &seed, 1.0f, 0, 1000000)) {
                    in.seed = (std::uint32_t)std::max(0, seed);
                    liveApply("Seed", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) liveApply("Seed", true);
                dragD("Rotation", &in.rotation, 0.5f, -360.0, 360.0, 1,
                      pr::Quantity::Angle, "Layout Rotation", kR2D, kD2R);

                // ── Shapes ──
                pr::GroupGap();
                ImGui::TextUnformatted("Shapes");
                static const char* kShape[] = { "Circle", "Rectangle",
                                                "Triangle", "Diamond",
                                                "Half-circle" };
                int removeElem = -1;
                for (int ei = 0; ei < (int)in.elements.size(); ++ei) {
                    ImGui::PushID(2000 + ei);
                    Ink::InstElement& e = in.elements[(std::size_t)ei];
                    int shape = (int)e.shape;
                    if (pr::DropdownRow("Shape", kShape, 5, &shape)) {
                        e.shape = (Ink::InstShape)shape;
                        structural = true; structLabel = "Shape";
                    }
                    if (e.shape == Ink::InstShape::Triangle) {
                        dragD("Side A", &e.sizeA, 0.2f, 0.1, 100000.0, 2,
                              pr::Quantity::Length, "Shape Size");
                        dragD("Side B", &e.sizeB, 0.2f, 0.1, 100000.0, 2,
                              pr::Quantity::Length, "Shape Size");
                        dragD("Side C", &e.sizeC, 0.2f, 0.1, 100000.0, 2,
                              pr::Quantity::Length, "Shape Size");
                    } else if (e.shape == Ink::InstShape::Circle ||
                               e.shape == Ink::InstShape::HalfCircle) {
                        dragD("Radius", &e.sizeA, 0.2f, 0.1, 100000.0, 2,
                              pr::Quantity::Length, "Shape Size");
                    } else {   // Rectangle / Diamond — half extents
                        dragD("Width", &e.sizeA, 0.2f, 0.1, 100000.0, 2,
                              pr::Quantity::Length, "Shape Size");
                        dragD("Height", &e.sizeB, 0.2f, 0.1, 100000.0, 2,
                              pr::Quantity::Length, "Shape Size");
                    }
                    dragD("Angle", &e.rotation, 0.5f, -360.0, 360.0, 1,
                          pr::Quantity::Angle, "Shape Angle", kR2D, kD2R);
                    int mode = (int)e.mode;
                    if (pr::ButtonGroupRow("Mode", kMode, 3, &mode)) {
                        e.mode = (Ink::MarkObjectMode)mode;
                        structural = true; structLabel = "Shape Mode";
                    }
                    if (e.mode != Ink::MarkObjectMode::Subtract) {
                        bool ufc = e.useFillColor;
                        if (pr::CheckRow("Fill colour", &ufc)) {
                            e.useFillColor = ufc;
                            structural = true; structLabel = "Shape Colour";
                        }
                        if (!e.useFillColor) {
                            bool crel = false;
                            if (pr::ColorRow("Colour", &e.color, true, &crel))
                                liveApply("Shape Colour", false);
                            if (crel) liveApply("Shape Colour", true);
                        }
                    }
                    float eo = e.opacity;
                    if (pr::DragFloat(
                            e.mode == Ink::MarkObjectMode::Subtract
                                ? "Erase strength" : "Opacity",
                            &eo, 0.5f, 0.0f, 1.0f, 0, "", pr::Quantity::Percent)) {
                        e.opacity = eo; liveApply("Shape Opacity", false);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        liveApply("Shape Opacity", true);
                    if (ImGui::SmallButton("Remove shape")) removeElem = ei;
                    ImGui::PopID();
                    pr::GroupGap();
                }
                if (removeElem >= 0) {
                    in.elements.erase(in.elements.begin() + removeElem);
                    structural = true; structLabel = "Remove Shape";
                }
                if (ImGui::SmallButton("Add shape")) {
                    in.elements.push_back(Ink::InstElement{});
                    structural = true; structLabel = "Add Shape";
                }

                // ── Line-sets ──
                pr::GroupGap();
                ImGui::TextUnformatted("Line sets");
                static const char* kCap[] = { "Butt", "Round", "Square" };
                int removeLine = -1;
                for (int li = 0; li < (int)in.lines.size(); ++li) {
                    ImGui::PushID(3000 + li);
                    Ink::InstLineSet& l = in.lines[(std::size_t)li];
                    dragD("Angle", &l.angle, 0.5f, -360.0, 360.0, 1,
                          pr::Quantity::Angle, "Line Angle", kR2D, kD2R);
                    dragD("Spacing", &l.spacing, 0.2f, 0.1, 100000.0, 2,
                          pr::Quantity::Length, "Line Spacing");
                    // How that spacing is MEASURED: axis to axis (the pitch is
                    // the spacing — thicker lines close the gap up) or edge to
                    // edge (the visible gap is the spacing — thicker lines
                    // spread the set out instead).
                    {
                        static const char* kSpMode[] = { "Center to center",
                                                         "Border to border" };
                        int spm = (int)l.spacingMode;
                        if (pr::DropdownRow("Measured", kSpMode, 2, &spm)) {
                            l.spacingMode = (Ink::InstLineSpacing)spm;
                            structural = true; structLabel = "Line Spacing Mode";
                        }
                    }
                    dragD("Offset", &l.phase, 0.2f, -100000.0, 100000.0, 2,
                          pr::Quantity::Length, "Line Offset");
                    dragD("Width", &l.line.width, 0.1f, 0.01, 100000.0, 2,
                          pr::Quantity::Length, "Line Width");
                    int cap = (int)l.line.cap;
                    if (pr::DropdownRow("Cap", kCap, 3, &cap)) {
                        l.line.cap = (Ink::CapStyle)cap;
                        structural = true; structLabel = "Line Cap";
                    }
                    bool dashed = !l.line.dashPattern.empty();
                    if (pr::CheckRow("Dashed", &dashed)) {
                        if (dashed) l.line.dashPattern = { l.line.width * 4.0,
                                                           l.line.width * 3.0 };
                        else l.line.dashPattern.clear();
                        structural = true; structLabel = "Line Dash";
                    }
                    if (dashed && l.line.dashPattern.size() >= 2) {
                        dragD("Dash", &l.line.dashPattern[0], 0.2f, 0.1, 100000.0,
                              2, pr::Quantity::Length, "Line Dash");
                        dragD("Gap", &l.line.dashPattern[1], 0.2f, 0.1, 100000.0,
                              2, pr::Quantity::Length, "Line Dash");
                        // Stagger: shift each successive LINE along itself by a
                        // % of the dash period, so neighbours fall out of step.
                        dragD("Stagger", &l.stagger, 0.5f, 0.0, 1.0, 0,
                              pr::Quantity::Percent, "Line Stagger");
                        if (ImGui::IsItemHovered())
                            UI::DrawTooltipTranslucent(
                                "Offset each next line along its own direction "
                                "by this share of the dash period (50 % = every "
                                "other line half a period out of step)",
                                ImGui::GetIO().MousePos, 1.0f);
                    }
                    int lmode = (int)l.mode;
                    if (pr::ButtonGroupRow("Mode", kMode, 3, &lmode)) {
                        l.mode = (Ink::MarkObjectMode)lmode;
                        structural = true; structLabel = "Line Mode";
                    }
                    if (l.mode != Ink::MarkObjectMode::Subtract) {
                        bool ufc = l.useFillColor;
                        if (pr::CheckRow("Fill colour", &ufc)) {
                            l.useFillColor = ufc;
                            structural = true; structLabel = "Line Colour";
                        }
                        if (!l.useFillColor) {
                            bool crel = false;
                            if (pr::ColorRow("Colour", &l.color, true, &crel))
                                liveApply("Line Colour", false);
                            if (crel) liveApply("Line Colour", true);
                        }
                    }
                    if (ImGui::SmallButton("Remove line set")) removeLine = li;
                    ImGui::PopID();
                    pr::GroupGap();
                }
                if (removeLine >= 0) {
                    in.lines.erase(in.lines.begin() + removeLine);
                    structural = true; structLabel = "Remove Line Set";
                }
                if (ImGui::SmallButton("Add line set")) {
                    Ink::InstLineSet l;
                    l.line.width = 2.0;
                    in.lines.push_back(l);
                    structural = true; structLabel = "Add Line Set";
                }

                // ── Clip + anchor (shared pattern vocabulary) ──
                pr::GroupGap();
                static const char* kClip[] = { "Box", "Shape", "Inner", "Outer" };
                int clip = (int)in.clip;
                if (pr::ButtonGroupRow("Fill clip", kClip, 4, &clip)) {
                    in.clip = (Ink::PatternClip)clip;
                    structural = true; structLabel = "Instanced Clip";
                }
                static const char* kAnchor[] = { "Object", "Document" };
                int anchor = (int)in.anchor;
                if (pr::ButtonGroupRow("Anchor", kAnchor, 2, &anchor)) {
                    in.anchor = (Ink::PatternAnchor)anchor;
                    structural = true; structLabel = "Instanced Anchor";
                }
            }

            float op = f.opacity;
            if (pr::DragFloat("Opacity", &op, 0.5f, 0.0f, 1.0f, 0, "",
                              pr::Quantity::Percent)) {
                f.opacity = op;
                liveApply("Fill Opacity", false);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                liveApply("Fill Opacity", true);

            pr::GroupGap();
            pr::ControlColumn();
            if (ImGui::SmallButton("Remove")) {
                style.fills.erase(style.fills.begin() + sel);
                sel = std::max(0, sel - 1);
                structural = true; structLabel = "Remove Fill";
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
}

// ── Compact single-object editor (gap start/end markers) ────────────────────
// The full palette of an object EXCEPT the Gap shape and the per-mark position
// (each marker object carries its OWN side/offset). Edits apply immediately
// (one style command each), consistent with the structural edits around it.
bool Application::PropMarkObjectCompact(Ink::MarkObject& o, double strokeWidth,
                                        Ink::Document& doc, Ink::NodeId hostId,
                                        bool& structural, const char*& structLabel) {
    static const char* kShape[] = { "Circle", "Rectangle", "Diamond",
                                    "Instance", "Triangle", "Half Circle" };
    static const Ink::MarkShape kShapeVal[] = {
        Ink::MarkShape::Circle, Ink::MarkShape::Rectangle,
        Ink::MarkShape::Diamond, Ink::MarkShape::Instance,
        Ink::MarkShape::Triangle, Ink::MarkShape::HalfCircle };
    static const char* kMode[]  = { "Fusion", "Blend", "Cut" };
    static const char* kBend[]  = { "Hard", "Bend", "Follow", "Chord" };
    static const char* kSide[]  = { "Center", "Left", "Right" };
    // The absolute mode is a DOCUMENT-unit length: the switch's second cell and
    // the fields' suffix follow the document unit (mm / pt / px …), not a fixed
    // "px". `docUn` = the doc unit's short name; `lenQ` tags a field Length while
    // absolute (converts + labels), Scalar while percent (a plain "%").
    const char* docUn = pr::un::Name(
        pr::un::Resolve(pr::un::DocumentSystem(), pr::un::LengthScale::Normal));
    const char* kUnit[2] = { "%", docUn };
    static const char* kBlend[] = {
        "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
        "Color Dodge", "Color Burn", "Hard Light", "Soft Light", "Difference",
        "Exclusion", "Erase" };
    const double sw = strokeWidth > 1e-9 ? strokeWidth : 1.0;
    bool changed = false;
    auto S = [&](const char* l) { structural = true; structLabel = l; changed = true; };
    // Length while absolute, plain Scalar while percent (with the "%" suffix).
    const pr::Quantity lenQ = o.sizePercent ? pr::Quantity::Scalar
                                            : pr::Quantity::Length;

    int shp = 0;
    for (int i = 0; i < 6; ++i) if (o.shape == kShapeVal[i]) shp = i;
    if (pr::DropdownRow("Object", kShape, 6, &shp)) {
        const Ink::MarkShape ns = kShapeVal[shp];
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
        // Mark `structural` (the caller commits the edited style COPY). A direct
        // doc.SetStyle here would write the document's OLD, unedited style back
        // over the change — that is what reset the value.
        if (pr::DragFloat("Distance", &off, o.sizePercent ? 1.0f : 0.1f,
                          -100000.0f, 100000.0f, 2, o.sizePercent ? "%" : "",
                          lenQ)) {
            o.sideOffset = off; S("Marker Distance"); }
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
                              1000000.0f, 2, o.sizePercent ? "%" : "", lenQ)) {
                *v = f; S("Marker Size"); }
        };
        if (o.shape == Ink::MarkShape::Circle ||
            o.shape == Ink::MarkShape::HalfCircle) sizeField("Radius", &o.size);
        else if (o.shape == Ink::MarkShape::Rectangle ||
                 o.shape == Ink::MarkShape::Triangle) {
            sizeField("Length", &o.size); sizeField("Width", &o.width);
        } else sizeField("Diagonal", &o.size);
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
        if (pr::DragFloat("Rotation", &rot, 0.5f, -360.0f, 360.0f, 1, "", pr::Quantity::Angle)) {
            o.rotation = rot * 3.14159265358979 / 180.0; S("Marker Rotation"); }
    }
    {   // Along the line (before/after the gap end).
        float ao = (float)o.alongOffset;
        if (pr::DragFloat("Along", &ao, o.sizePercent ? 1.0f : 0.1f,
                          -100000.0f, 100000.0f, 2, o.sizePercent ? "%" : "",
                          lenQ)) {
            o.alongOffset = ao; S("Marker Along"); }
    }
    int bd = (int)o.bend;
    if (pr::DropdownRow("Bend", kBend, 4, &bd)) { o.bend = (Ink::MarkBend)bd; S("Marker Bend"); }
    if (o.mode == Ink::MarkObjectMode::Blend) {
        int bl = std::min((int)o.blend, 12);
        if (pr::DropdownRow("Blend mode", kBlend, 13, &bl)) { o.blend = (Ink::BlendMode)bl; S("Marker Blend"); }
        int fr = o.front ? 1 : 0;
        static const char* kOrder[] = { "Behind", "Front" };
        if (pr::ButtonGroupRow("Order", kOrder, 2, &fr)) { o.front = (fr == 1); S("Marker Order"); }
    }
    // Colour: only Blend exposes a custom fill. A Fusion object is baked into the
    // stroke mesh, so it is ALWAYS the stroke colour; Cut ignores colour.
    if (o.shape != Ink::MarkShape::Instance && o.mode == Ink::MarkObjectMode::Blend) {
        bool inh = o.useStrokeColor;
        if (pr::CheckRow("Stroke colour", &inh)) { o.useStrokeColor = inh; S("Marker Colour"); }
        if (!o.useStrokeColor) {
            bool rel = false;
            // Edit the style COPY and mark it structural; the caller commits it.
            // (A doc.SetStyle here re-applied the OLD style, resetting to black.)
            if (pr::ColorRow("Colour", &o.color, true, &rel)) S("Marker Colour");
            if (rel) S("Marker Colour");
        }
    } else if (o.mode == Ink::MarkObjectMode::Fusion && !o.useStrokeColor) {
        o.useStrokeColor = true;   // Fusion is always stroke-coloured
        S("Marker Colour");
    }
    return changed;
}

// ── Strokes ───────────────────────────────────────────────────────────────────

void Application::PropStrokesSection(Ink::NodeId id) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n || n->kind != Ink::NodeKind::Path) return;

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

        // Style lock — shared with the Fills section (one bit for the whole
        // paint stack).
        const bool styleLocked = (n->propLocks & Ink::PropLockStyle) != 0;
        if (pr::LockToggle("##lkStrokeStyle", styleLocked,
                           (n->propLocks & Ink::PropLockManaged) != 0))
            TogglePropLock(id, Ink::PropLockStyle);
        ImGui::BeginDisabled(styleLocked);
        DrawStrokesStackBody(style, propStrokeSel_, liveApply,
                             structural, structLabel);
        ImGui::EndDisabled();
        if (styleLocked) structural = false;

        if (structural) {
            const Ink::Style before = n->style;
            doc.SetStyle(id, style);
            CommitStyleEdit(id, before, structLabel);
        }
    }
    UI::EndPanel();
}

// ── Shared strokes stack body (Properties + the Stroke editor) ───────────────
// The vignette rail + the selected stroke's properties, editing `style` in
// place. `sel` = the caller's rail selection.
void Application::DrawStrokesStackBody(
    Ink::Style& style, int& sel,
    const std::function<void(const char*, bool)>& liveApply,
    bool& structural, const char*& structLabel) {
    const float gs = pr::Gs();
    // Same vocabulary as a repeat's Side (Ink::StrokeAlign), in the enum's
    // frozen order: Center, Inside, Outside, Left, Right.
    static const char* kAlign[] = { "Center", "Inner", "Outer", "Left", "Right" };
    static const char* kCap[]   = { "Butt", "Round", "Square", "Taper" };
    static const char* kJoin[]  = { "Miter", "Round", "Bevel" };
    static const char* kSpace[] = { "Document", "Viewport px" };
    {
        const float thumb = kThumbBase * gs;

        // ── LEFT: the vignette rail (line samples, dynamic drag-reorder) ──────
        // Fixed-height cells stacked vertically; dragging a vignette floats it
        // with the cursor above the others, which slide out of its way (the
        // modifier-panel behaviour). Placement is absolute so the grabbed tile
        // can move independently of the flow.
        const float cellH = thumb + kRailGap * gs;
        const ImVec2 railOrigin = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        {
            const int nS = (int)style.strokes.size();
            pr::VReorder rr("##strokeRail", nS, cellH);
            const int grabbed = rr.Grabbed();
            for (int i = 0; i < nS; ++i) {
                ImGui::PushID(100 + i);
                const bool isGrab = (i == grabbed);
                float posY = railOrigin.y + (float)i * cellH + rr.CellOffset(i);
                if (isGrab) posY = rr.GrabbedScreenY(railOrigin.y);
                const ImVec2 pos(railOrigin.x, posY);
                ImVec2 cmn, cmx;
                char tid[16];
                std::snprintf(tid, sizeof tid, "s%d", i);
                const bool clicked =
                    pr::ThumbTile(tid, thumb, i == sel, &cmn, &cmx, &pos);
                rr.HandleCell(i, ImGui::IsItemActivated(), ImGui::IsItemActive(),
                              railOrigin.y + (float)i * cellH, railOrigin.y);
                // A plain click (no drag this hold) selects.
                if (clicked && rr.Grabbed() < 0) sel = i;
                // The grabbed tile's sample is drawn LAST (foreground) so it sits
                // over its neighbours; the rest draw inline.
                if (!isGrab)
                    pr::DrawStrokeSample(ImGui::GetWindowDrawList(), cmn, cmx,
                                         style.strokes[(std::size_t)i]);
                // Hover-dwell preview (only when nothing is being dragged).
                if (grabbed < 0 &&
                    ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
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
                                         style.strokes[(std::size_t)i]);
                    ImGui::Text("%.2f %s%s", style.strokes[(std::size_t)i].width,
                                style.strokes[(std::size_t)i].widthSpace ==
                                        Ink::WidthSpace::Viewport
                                    ? "px" : "doc",
                                style.strokes[(std::size_t)i].dashPattern.empty()
                                    ? "" : " \xC2\xB7 dashed");
                    ImGui::EndTooltip();
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }
            // Draw the grabbed tile's sample on top (foreground draw list).
            if (grabbed >= 0 && grabbed < nS) {
                const float posY = rr.GrabbedScreenY(railOrigin.y);
                const float ins = 3.0f * gs;
                const ImVec2 gmn(railOrigin.x + ins, posY + ins);
                const ImVec2 gmx(railOrigin.x + thumb - ins, posY + thumb - ins);
                pr::DrawStrokeSample(ImGui::GetForegroundDrawList(), gmn, gmx,
                                     style.strokes[(std::size_t)grabbed]);
            }
            // Reserve the rail's flow footprint (nS cells), then the "+" tile.
            ImGui::SetCursorScreenPos(
                ImVec2(railOrigin.x, railOrigin.y + (float)nS * cellH));
            if (pr::ThumbAddTile(thumb)) {
                // Seed the new stroke from the default stack's first entry.
                Ink::Stroke s;
                if (!edit_.defaultStrokes.empty()) s = edit_.defaultStrokes.front();
                else s.width = 2.0;
                style.strokes.push_back(s);
                sel = (int)style.strokes.size() - 1;
                structural = true; structLabel = "Add Stroke";
            }
            // Apply a committed drag move (reorder the stack + follow selection).
            pr::VReorder::Move mv = rr.Commit();
            if (mv.from >= 0 && mv.to >= 0 && mv.from != mv.to &&
                mv.from < (int)style.strokes.size() &&
                mv.to < (int)style.strokes.size()) {
                Ink::Stroke moved = style.strokes[(std::size_t)mv.from];
                style.strokes.erase(style.strokes.begin() + mv.from);
                style.strokes.insert(style.strokes.begin() + mv.to, moved);
                if (sel == mv.from) sel = mv.to;
                else if (mv.from < mv.to && sel > mv.from &&
                         sel <= mv.to) --sel;
                else if (mv.to < mv.from && sel >= mv.to &&
                         sel < mv.from) ++sel;
                liveApply("Reorder Strokes", true);
            }
        }
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, 8.0f * gs);

        // ── RIGHT: the selected stroke's properties ───────────────────────────
        sel = std::clamp(sel, 0, std::max(0, (int)style.strokes.size() - 1));
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
            Ink::Stroke& s = style.strokes[(std::size_t)sel];

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
            // A hairline width is literal SCREEN px (non-scaling) — a plain
            // "px" scalar, not a document length to convert; a Document width is
            // a real length that follows the document unit.
            const bool vpWidth = s.widthSpace == Ink::WidthSpace::Viewport;
            if (pr::DragFloat("Width", &w, 0.1f, 0.0f, 10000.0f, 2,
                              vpWidth ? "px" : "",
                              vpWidth ? pr::Quantity::Scalar
                                      : pr::Quantity::Length)) {
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
            if (pr::DropdownRow("Align", kAlign, 5, &align)) {
                s.align = (Ink::StrokeAlign)align; structural = true;
            }
            if (ImGui::IsItemHovered())
                UI::DrawTooltipTranslucent(
                    "Left/Right stay on one side of the path for its whole "
                    "length; Inner/Outer follow the shape — on an open path "
                    "they swap sides wherever the curvature does",
                    ImGui::GetIO().MousePos, 1.0f);
            if (s.align != Ink::StrokeAlign::Center) {
                // How far off the path. 50 % of the width puts one edge on the
                // path (the classic placement); negative crosses to the other
                // side. Percent or doc units, like a repeat's own offset.
                float ao = (float)s.alignOffset;
                if (pr::DragFloat(s.alignOffsetPercent ? "Offset %" : "Offset",
                                  &ao, 0.5f, -10000.0f, 10000.0f, 1,
                                  s.alignOffsetPercent ? "%" : "",
                                  s.alignOffsetPercent ? pr::Quantity::Scalar
                                                       : pr::Quantity::Length)) {
                    s.alignOffset = ao; liveApply("Stroke Offset", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Stroke Offset", true);
                bool aop = s.alignOffsetPercent;
                if (pr::CheckRow("Offset in %", &aop)) {
                    // Keep the same distance when the unit changes.
                    if (aop && !s.alignOffsetPercent && s.width > 1e-9)
                        s.alignOffset = s.alignOffset / s.width * 100.0;
                    else if (!aop && s.alignOffsetPercent)
                        s.alignOffset = s.alignOffset * 0.01 * s.width;
                    s.alignOffsetPercent = aop;
                    structural = true; structLabel = "Stroke Offset Unit";
                }
            }
            if (pr::DropdownRow("Cap", kCap, 4, &cap)) {
                s.cap = (Ink::CapStyle)cap; structural = true;
            }
            if (s.cap == Ink::CapStyle::Taper) {
                // 0 = auto (2× width); a positive value is the triangle length.
                float tl = (float)s.taperLength;
                if (pr::DragFloat("Taper length", &tl, 0.2f, 0.0f, 100000.0f, 2,
                                  "", pr::Quantity::Length)) {
                    s.taperLength = tl; liveApply("Taper Length", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Taper Length", true);
            }
            if (s.cap == Ink::CapStyle::Butt) {
                // TILT the flat end: the cap pivots on one rim so the stroke is
                // cut on a slant. An open V whose arms must both finish
                // horizontal takes a single angle (their frames mirror it).
                float ca = (float)(s.capAngle * 180.0 / 3.14159265358979);
                if (pr::DragFloat("Cap angle", &ca, 0.5f, -45.0f, 45.0f, 1,
                                  "", pr::Quantity::Angle)) {
                    s.capAngle = ca * 3.14159265358979 / 180.0;
                    liveApply("Cap Angle", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Cap Angle", true);
                if (ImGui::IsItemHovered())
                    UI::DrawTooltipTranslucent(
                        "Slant the butt cap: 0 cuts the end square, ±45 tilts "
                        "it as far as the end can lean",
                        ImGui::GetIO().MousePos, 1.0f);
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
                if (pr::DragFloat("Dash", &dash, 0.1f, 0.05f, 10000.0f, 2, "",
                                  pr::Quantity::Length)) {
                    if (s.dashPattern.size() < 2) s.dashPattern.resize(2, 8.0);
                    s.dashPattern[0] = dash;
                    liveApply("Stroke Dashes", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Stroke Dashes", true);
                if (pr::DragFloat("Gap", &gap, 0.1f, 0.05f, 10000.0f, 2, "",
                                  pr::Quantity::Length)) {
                    if (s.dashPattern.size() < 2) s.dashPattern.resize(2, 8.0);
                    s.dashPattern[1] = gap;
                    liveApply("Stroke Dashes", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Stroke Dashes", true);
                float dOff = (float)s.dashOffset;
                if (pr::DragFloat("Dash offset", &dOff, 0.1f, -10000.0f,
                                  10000.0f, 2, "", pr::Quantity::Length)) {
                    s.dashOffset = dOff;
                    liveApply("Dash Offset", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Dash Offset", true);
                // With SEVERAL Dash/Gap phase marks the pattern stretches
                // piecewise between them — this picks what absorbs the fit.
                static const char* kFit[] = { "Scale Both", "Adapt Dashes",
                                              "Adapt Gaps" };
                int fit = (int)s.dashFit;
                if (pr::DropdownRow("Anchor fit", kFit, 3, &fit)) {
                    s.dashFit = (Ink::DashFit)fit;
                    structural = true; structLabel = "Dash Fit";
                }
            }

            // ── REPEATS: object runs along the stroke (the IOF fence-tick
            // family) — groups of primitives at a regular pitch, on a side,
            // inclined, added / blended / cut. Part of the stroke STYLE, so
            // new objects inherit them and preview them while being drawn;
            // marks can re-phase a run (repeat anchor, Marks side panel).
            pr::GroupGap();
            {
                ImGui::PushStyleColor(ImGuiCol_Text,
                    pr::SafeColor(pr::Tok::S_Color_Text_Subtle,
                                  ImVec4(0.6f, 0.6f, 0.6f, 1)));
                ImGui::Text("Repeats (%d)", (int)s.repeats.size());
                ImGui::PopStyleColor();
                int removeRep = -1;
                for (std::size_t ri = 0; ri < s.repeats.size(); ++ri) {
                    Ink::StrokeRepeat& rp = s.repeats[ri];
                    ImGui::PushID((int)(3000 + ri));
                    bool en = rp.enabled;
                    if (pr::CheckRow("Enabled", &en)) {
                        rp.enabled = en;
                        structural = true; structLabel = "Repeat Enabled";
                    }
                    static const char* kRShape[] = {
                        "Circle", "Rectangle", "Diamond", "Triangle",
                        "Half Circle", "Line" };
                    static const Ink::MarkShape kRShapeVal[] = {
                        Ink::MarkShape::Circle, Ink::MarkShape::Rectangle,
                        Ink::MarkShape::Diamond, Ink::MarkShape::Triangle,
                        Ink::MarkShape::HalfCircle, Ink::MarkShape::Line };
                    int shIdx = 0;
                    for (int i = 0; i < 6; ++i)
                        if (rp.shape == kRShapeVal[i]) shIdx = i;
                    if (pr::DropdownRow("Shape", kRShape, 6, &shIdx)) {
                        rp.shape = kRShapeVal[shIdx];
                        // Line defaults to a 0 % side offset (its START at the
                        // stroke, reaching out).
                        if (rp.shape == Ink::MarkShape::Line &&
                            rp.side != Ink::RepeatSide::Center)
                            rp.sideOffset = 0.0;
                        structural = true; structLabel = "Repeat Shape";
                    }
                    static const char* kRMode[] = { "Add", "Blend", "Cut" };
                    int mode = (int)rp.mode;
                    if (pr::ButtonGroupRow("Mode", kRMode, 3, &mode)) {
                        rp.mode = (Ink::MarkObjectMode)mode;
                        structural = true; structLabel = "Repeat Mode";
                    }
                    const bool addMode = rp.mode == Ink::MarkObjectMode::Fusion;
                    bool pct = rp.sizePercent;
                    if (pr::CheckRow("Size in %", &pct)) {
                        rp.sizePercent = pct;
                        structural = true; structLabel = "Repeat Units";
                    }
                    // Length while absolute (doc-unit), plain "%" while percent.
                    const char* rpU = rp.sizePercent ? "%" : "";
                    const pr::Quantity rpQ = rp.sizePercent ? pr::Quantity::Scalar
                                                            : pr::Quantity::Length;
                    float sz = (float)rp.size;
                    if (pr::DragFloat("Length", &sz, 0.5f, 0.01f, 10000.0f, 2,
                                      rpU, rpQ)) {
                        rp.size = sz; liveApply("Repeat Size", false);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        liveApply("Repeat Size", true);
                    if (rp.shape == Ink::MarkShape::Rectangle ||
                        rp.shape == Ink::MarkShape::Triangle ||
                        rp.shape == Ink::MarkShape::Line) {
                        float wd = (float)rp.width;
                        if (pr::DragFloat(rp.shape == Ink::MarkShape::Line
                                              ? "Thickness" : "Width",
                                          &wd, 0.5f, 0.01f, 10000.0f, 2, rpU, rpQ)) {
                            rp.width = wd; liveApply("Repeat Width", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Repeat Width", true);
                    }
                    float rot =
                        (float)(rp.rotation * 180.0 / 3.14159265358979);
                    if (pr::DragFloat("Incline", &rot, 0.5f, -360.0f, 360.0f,
                                      1, "", pr::Quantity::Angle)) {
                        rp.rotation = rot * 3.14159265358979 / 180.0;
                        liveApply("Repeat Incline", false);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        liveApply("Repeat Incline", true);
                    {
                        // How the object reads the line's direction. Segment
                        // keeps it square to the edge right up to a corner;
                        // Smoothed blends the vertex tangents, which leans
                        // objects over as they approach a hard corner.
                        static const char* kOrient[] = { "Perpendicular",
                                                         "Smoothed" };
                        int ori = (int)rp.orient;
                        if (pr::DropdownRow("Orient", kOrient, 2, &ori)) {
                            rp.orient = (Ink::MarkOrient)ori;
                            structural = true;
                            structLabel = "Repeat Orientation";
                        }
                        if (ImGui::IsItemHovered())
                            UI::DrawTooltipTranslucent(
                                "Perpendicular squares each object to the "
                                "segment it sits on, all the way into a hard "
                                "corner; Smoothed blends the vertex tangents",
                                ImGui::GetIO().MousePos, 1.0f);
                    }
                    static const char* kRSide[] = { "Center", "Left", "Right",
                                                    "Inside", "Outside" };
                    int side = (int)rp.side;
                    if (pr::DropdownRow("Side", kRSide, 5, &side)) {
                        const bool wasCenter =
                            rp.side == Ink::RepeatSide::Center;
                        rp.side = (Ink::RepeatSide)side;
                        // First time OFF Center → the natural default (Line
                        // starts at the stroke = 0 %; other shapes 50 %).
                        if (wasCenter && rp.side != Ink::RepeatSide::Center)
                            rp.sideOffset =
                                rp.shape == Ink::MarkShape::Line ? 0.0 : 50.0;
                        structural = true; structLabel = "Repeat Side";
                    }
                    if (rp.side != Ink::RepeatSide::Center) {
                        float so = (float)rp.sideOffset;
                        // Negative offsets allowed (the other side / reversed).
                        if (pr::DragFloat(rp.offsetPercent ? "Offset %"
                                                           : "Offset",
                                          &so, 0.5f, -10000.0f, 10000.0f, 1,
                                          rp.offsetPercent ? "%" : "",
                                          rp.offsetPercent ? pr::Quantity::Scalar
                                                           : pr::Quantity::Length)) {
                            rp.sideOffset = so;
                            liveApply("Repeat Offset", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Repeat Offset", true);
                        bool opct = rp.offsetPercent;
                        if (pr::CheckRow("Offset in %", &opct)) {
                            rp.offsetPercent = opct;
                            structural = true; structLabel = "Repeat Offset";
                        }
                    }
                    if (rp.shape == Ink::MarkShape::Line &&
                        rp.side != Ink::RepeatSide::Center) {
                        bool jn = rp.lineJoin;
                        if (pr::CheckRow("Join to path", &jn)) {
                            rp.lineJoin = jn;
                            structural = true; structLabel = "Line Join";
                        }
                        bool cl = rp.lineClip;
                        if (pr::CheckRow("Clip at path", &cl)) {
                            rp.lineClip = cl;
                            structural = true; structLabel = "Line Clip";
                        }
                    }
                    static const char* kRDist[] = { "Pitch", "Gap", "Count",
                                                    "Density" };
                    int dist = (int)rp.distribute;
                    if (pr::DropdownRow("Distribute", kRDist, 4, &dist)) {
                        rp.distribute = (Ink::RepeatDistribute)dist;
                        structural = true; structLabel = "Repeat Distribution";
                    }
                    if (rp.distribute == Ink::RepeatDistribute::Pitch) {
                        float pv = (float)rp.pitch;
                        if (pr::DragFloat("Spacing c-c", &pv, 0.2f, 0.01f,
                                          10000.0f, 2, "", pr::Quantity::Length)) {
                            rp.pitch = pv; liveApply("Repeat Pitch", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Repeat Pitch", true);
                    } else if (rp.distribute == Ink::RepeatDistribute::Gap) {
                        float gv = (float)rp.gap;
                        if (pr::DragFloat("Gap edge-edge", &gv, 0.2f, 0.0f,
                                          10000.0f, 2, "", pr::Quantity::Length)) {
                            rp.gap = gv; liveApply("Repeat Gap", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Repeat Gap", true);
                    } else if (rp.distribute == Ink::RepeatDistribute::Count) {
                        int cv = rp.count;
                        if (pr::DragInt("Count", &cv, 0.2f, 1, 10000)) {
                            rp.count = cv; liveApply("Repeat Count", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Repeat Count", true);
                    } else {
                        float dv = (float)rp.density;
                        if (pr::DragFloat("Per 100 units", &dv, 0.2f, 0.01f,
                                          1000.0f, 2)) {
                            rp.density = dv; liveApply("Repeat Density", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Repeat Density", true);
                    }
                    float ph = (float)rp.phase;
                    if (pr::DragFloat("Phase", &ph, 0.2f, -10000.0f,
                                      10000.0f, 2, "", pr::Quantity::Length)) {
                        rp.phase = ph; liveApply("Repeat Phase", false);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        liveApply("Repeat Phase", true);
                    int gc = rp.groupCount;
                    if (pr::DragInt("Group size", &gc, 0.1f, 1, 64)) {
                        rp.groupCount = gc; liveApply("Repeat Group", false);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        liveApply("Repeat Group", true);
                    if (rp.groupCount > 1) {
                        float gp = (float)rp.groupPitch;
                        if (pr::DragFloat("Group c-c", &gp, 0.1f, 0.01f,
                                          10000.0f, 2, "", pr::Quantity::Length)) {
                            rp.groupPitch = gp;
                            liveApply("Repeat Group Pitch", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Repeat Group Pitch", true);
                    }
                    // Trims.
                    float ts0 = (float)rp.startTrim, ts1 = (float)rp.endTrim;
                    if (pr::DragFloat("Trim start", &ts0, 0.5f, 0.0f,
                                      1000000.0f, 1, "", pr::Quantity::Length)) {
                        rp.startTrim = ts0; liveApply("Repeat Trim", false);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        liveApply("Repeat Trim", true);
                    if (pr::DragFloat("Trim end", &ts1, 0.5f, 0.0f,
                                      1000000.0f, 1, "", pr::Quantity::Length)) {
                        rp.endTrim = ts1; liveApply("Repeat Trim", false);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                        liveApply("Repeat Trim", true);
                    {
                        // What the trim is measured TO.
                        static const char* kTrimM[] = { "To centre",
                                                        "To outer edge" };
                        int tm = (int)rp.trimMeasure;
                        if (pr::DropdownRow("Trim from", kTrimM, 2, &tm)) {
                            rp.trimMeasure = (Ink::RepeatTrimMeasure)tm;
                            structural = true;
                            structLabel = "Repeat Trim Measure";
                        }
                        if (ImGui::IsItemHovered())
                            UI::DrawTooltipTranslucent(
                                "Whether the trim reaches the object's centre "
                                "or the outer edge of the whole group — the "
                                "second is the clear gap you actually see",
                                ImGui::GetIO().MousePos, 1.0f);
                    }
                    // Anchor fit — how the pitch stretches between repeat
                    // anchors (marks). ScaleBoth stretches the pitch; the
                    // others keep it exact.
                    static const char* kRFit[] = { "Stretch pitch",
                                                   "Fixed pitch" };
                    int fit = rp.fit == Ink::DashFit::ScaleBoth ? 0 : 1;
                    if (pr::DropdownRow("Anchor fit", kRFit, 2, &fit)) {
                        rp.fit = fit == 0 ? Ink::DashFit::ScaleBoth
                                          : Ink::DashFit::ScaleDash;
                        structural = true; structLabel = "Repeat Fit";
                    }
                    // Colour options: only BLEND exposes a colour choice. ADD
                    // (Fusion) fuses in the stroke colour; CUT erases (no
                    // colour). Opacity is the erase strength for Cut, the paint
                    // alpha for Blend-with-stroke-colour; a Blend with a CUSTOM
                    // colour already carries alpha, so opacity is hidden there.
                    const bool cutMode = rp.mode == Ink::MarkObjectMode::Subtract;
                    if (rp.mode == Ink::MarkObjectMode::Blend) {
                        bool useSC = rp.useStrokeColor;
                        if (pr::CheckRow("Stroke colour", &useSC)) {
                            rp.useStrokeColor = useSC;
                            structural = true; structLabel = "Repeat Colour";
                        }
                        if (!rp.useStrokeColor) {
                            bool crel = false;
                            if (pr::ColorRow("Colour", &rp.color, true, &crel))
                                liveApply("Repeat Colour", false);
                            if (crel) liveApply("Repeat Colour", true);
                        }
                    }
                    const bool customAlpha =
                        rp.mode == Ink::MarkObjectMode::Blend &&
                        !rp.useStrokeColor;
                    if (!addMode && !customAlpha) {
                        float op = rp.opacity;
                        if (pr::DragFloat(cutMode ? "Erase strength" : "Opacity",
                                          &op, 0.5f, 0.0f, 1.0f, 0, "",
                                          pr::Quantity::Percent)) {
                            rp.opacity = op;
                            liveApply("Repeat Opacity", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Repeat Opacity", true);
                    }
                    if (ImGui::SmallButton("Remove repeat"))
                        removeRep = (int)ri;
                    ImGui::PopID();
                    pr::GroupGap();
                }
                if (removeRep >= 0) {
                    s.repeats.erase(s.repeats.begin() + removeRep);
                    structural = true; structLabel = "Remove Repeat";
                }
                if (ImGui::SmallButton("Add repeat")) {
                    Ink::StrokeRepeat rp;
                    rp.shape = Ink::MarkShape::Rectangle;
                    // A fence tick: SHORT along the line, TALL across it.
                    rp.size = 8.0;    // half-length along the tangent
                    rp.width = 40.0;  // half-height across
                    s.repeats.push_back(rp);
                    structural = true; structLabel = "Add Repeat";
                }
            }

            // Line MARKS are no longer edited here — they moved to the viewport
            // "Marks" side-panel tab (visible in Line-Mark mode). Select one or
            // more marks on the canvas and edit the active one there.
#if 0
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
                                          m.offsetPercent ? "%" : "",
                                          m.offsetPercent ? pr::Quantity::Scalar
                                                          : pr::Quantity::Length)) {
                            m.offset = off; liveApply("Mark Distance", false);
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                            liveApply("Mark Distance", true);
                        const char* kUnit[2] = { "%", pr::un::Name(pr::un::Resolve(
                            pr::un::DocumentSystem(), pr::un::LengthScale::Normal)) };
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
                            // Switching TO a rectangle or a gap applies the
                            // default length (200 %) when the size still holds
                            // the shared 100 % default.
                            if ((ns == Ink::MarkShape::Rectangle ||
                                 ns == Ink::MarkShape::Gap) &&
                                o.shape != ns && o.sizePercent &&
                                std::abs(o.size - 100.0) < 1e-6)
                                o.size = 200.0;
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
                                              360.0f, 1, "", pr::Quantity::Angle)) {
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
                        // Colour (primitives only). A FUSION object is baked into
                        // the stroke mesh, so it is ALWAYS the stroke colour — no
                        // colour choice. Cut ignores colour. Only Blend exposes a
                        // custom fill.
                        if (o.shape != Ink::MarkShape::Instance &&
                            o.mode == Ink::MarkObjectMode::Blend) {
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
                        } else if (o.mode == Ink::MarkObjectMode::Fusion &&
                                   !o.useStrokeColor) {
                            o.useStrokeColor = true;   // Fusion is stroke-coloured
                            structural = true; structLabel = "Mark Colour";
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
#endif

            pr::GroupGap();
            pr::ControlColumn();
            if (ImGui::SmallButton("Remove")) {
                style.strokes.erase(style.strokes.begin() + sel);
                sel = std::max(0, sel - 1);
                structural = true; structLabel = "Remove Stroke";
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
}

// ── Single-mark editor (viewport "Marks" side-panel tab) ────────────────────
// The full editor for ONE mark: position / side / phase and its object list
// (shapes, gap + start/end markers, modes, colours). Edits go through a style
// COPY committed as one undo command, mirroring PropStrokesSection.
void Application::DrawMarkEditor(Ink::NodeId node, int strokeIdx, int markIdx) {
    if (!project_.document) return;
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(node);
    if (!n || strokeIdx < 0 || strokeIdx >= (int)n->style.strokes.size()) return;
    const auto& marks0 = n->style.strokes[(std::size_t)strokeIdx].marks;
    if (markIdx < 0 || markIdx >= (int)marks0.size()) return;

    const Ink::NodeId id = node;
    Ink::Style style = n->style;
    Ink::Stroke& s = style.strokes[(std::size_t)strokeIdx];
    Ink::StrokeMark& m = s.marks[(std::size_t)markIdx];
    bool structural = false;
    const char* structLabel = "Edit Mark";

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

    static const char* kBlend[] = {
        "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
        "Color Dodge", "Color Burn", "Hard Light", "Soft Light", "Difference",
        "Exclusion", "Erase" };
    static const char* kShape[] = {
        "Circle", "Rectangle", "Diamond", "Instance", "Gap", "Triangle",
        "Half Circle" };
    constexpr int kNShape = 7;   // dropdown order == MarkShape enum order

    // Position along the line.
    float t = (float)m.t;
    if (pr::DragFloat("Position", &t, 0.002f, 0.0f, 1.0f, 3)) {
        m.t = t; liveApply("Mark Position", false);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) liveApply("Mark Position", true);

    // Side + distance.
    static const char* kSide[] = { "Center", "Left", "Right" };
    int sd = (int)m.side;
    if (pr::ButtonGroupRow("Side", kSide, 3, &sd)) {
        m.side = (Ink::MarkSide)sd; structural = true; structLabel = "Mark Side";
    }
    if (m.side != Ink::MarkSide::Center) {
        float off = (float)m.offset;
        if (pr::DragFloat("Distance", &off, m.offsetPercent ? 0.5f : 0.1f,
                          -100000.0f, 100000.0f, 2, m.offsetPercent ? "%" : "")) {
            m.offset = off; liveApply("Mark Distance", false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) liveApply("Mark Distance", true);
        static const char* kUnit[] = { "%", "px" };
        int un = m.offsetPercent ? 0 : 1;
        if (pr::ButtonGroupRow("Unit", kUnit, 2, &un)) {
            const bool toPercent = (un == 0);
            if (toPercent != m.offsetPercent) {
                const double w = s.width > 1e-9 ? s.width : 1.0;
                if (toPercent) m.offset = m.offset / w * 100.0;
                else           m.offset = m.offset * 0.01 * w;
                m.offsetPercent = toPercent;
                structural = true; structLabel = "Mark Unit";
            }
        }
    }

    // Dash phase.
    static const char* kPhase[] = { "Neutral", "Dash", "Gap" };
    int ph = (int)m.phase;
    if (pr::ButtonGroupRow("Dash phase", kPhase, 3, &ph)) {
        m.phase = (Ink::MarkPhase)ph; structural = true; structLabel = "Mark Phase";
    }
    if (m.phase != Ink::MarkPhase::Neutral) {
        // Forced size of the centred dash/gap (0 = the pattern's own length).
        float asz = (float)m.anchorSize;
        if (pr::DragFloat("Anchor size", &asz, 0.1f, 0.0f, 100000.0f, 2, "",
                          pr::Quantity::Length)) {
            m.anchorSize = asz; liveApply("Mark Anchor Size", false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            liveApply("Mark Anchor Size", true);
    }
    // Repeat-run anchoring: centre an object/group of every repeat here, or
    // the space BETWEEN two groups.
    static const char* kRAnchor[] = { "None", "Object", "Between" };
    int ra = (int)m.repeatAnchor;
    if (pr::ButtonGroupRow("Repeats", kRAnchor, 3, &ra)) {
        m.repeatAnchor = (Ink::MarkRepeatAnchor)ra;
        structural = true; structLabel = "Mark Repeat Anchor";
    }
    if (m.repeatAnchor != Ink::MarkRepeatAnchor::None) {
        // The forced pitch (doc units) of the repeat segment starting here —
        // the "Anchor Size" for repeats (0 = the run's own pitch).
        float rg = (float)m.repeatGap;
        if (pr::DragFloat("Repeat pitch", &rg, 0.1f, 0.0f, 100000.0f, 2, "",
                          pr::Quantity::Length)) {
            m.repeatGap = rg; liveApply("Mark Repeat Pitch", false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            liveApply("Mark Repeat Pitch", true);
    }

    // ── Objects on this mark ──────────────────────────────────────────────────
    int removeObj = -1;
    for (std::size_t oi = 0; oi < m.objects.size(); ++oi) {
        Ink::MarkObject& o = m.objects[oi];
        ImGui::PushID((int)(50 + oi));
        int shp = (int)o.shape;
        if (pr::DropdownRow("Object", kShape, kNShape, &shp)) {
            const Ink::MarkShape ns = (Ink::MarkShape)shp;
            if ((ns == Ink::MarkShape::Rectangle || ns == Ink::MarkShape::Gap) &&
                o.shape != ns && o.sizePercent && std::abs(o.size - 100.0) < 1e-6)
                o.size = 200.0;
            o.bend = Ink::DefaultBendFor(ns);
            o.shape = ns;
            structural = true; structLabel = "Mark Object";
        }
        const bool isGap = o.shape == Ink::MarkShape::Gap;
        if (isGap) {
            float len = (float)o.size;
            if (pr::DragFloat("Length", &len, o.sizePercent ? 1.0f : 0.1f, 0.0f,
                              1000000.0f, 2, o.sizePercent ? "%" : "")) {
                o.size = len; structural = true; structLabel = "Gap Length";
            }
            static const char* kUnitG[] = { "%", "px" };
            const double sw = s.width > 1e-9 ? s.width : 1.0;
            int un = o.sizePercent ? 0 : 1;
            if (pr::ButtonGroupRow("Unit", kUnitG, 2, &un)) {
                const bool toPct = (un == 0);
                if (toPct != o.sizePercent) {
                    o.size = toPct ? o.size / sw * 100.0 : o.size * 0.01 * sw;
                    o.sizePercent = toPct;
                    structural = true; structLabel = "Gap Unit";
                }
            }
            static const char* kCap[] = { "Butt", "Round", "Square" };
            int cs = (int)o.gapStart;
            if (pr::DropdownRow("Start cap", kCap, 3, &cs)) {
                o.gapStart = (Ink::GapCap)cs; structural = true; structLabel = "Gap Cap";
            }
            int ce = (int)o.gapEnd;
            if (pr::DropdownRow("End cap", kCap, 3, &ce)) {
                o.gapEnd = (Ink::GapCap)ce; structural = true; structLabel = "Gap Cap";
            }
            bool cut = o.gapCutsObjects;
            if (pr::CheckRow("Cut objects", &cut)) {
                o.gapCutsObjects = cut; structural = true;
                structLabel = "Gap Cut Objects";
            }
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
                    PropMarkObjectCompact(lst[k], s.width, doc, id, structural,
                                          structLabel);
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
                    lst.push_back(Ink::MarkObject{});
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
        static const char* kMode[] = { "Fusion", "Blend", "Cut" };
        int md = (int)o.mode;
        if (pr::ButtonGroupRow("Mode", kMode, 3, &md)) {
            o.mode = (Ink::MarkObjectMode)md; structural = true;
            structLabel = "Mark Object Mode";
        }
        if (o.shape == Ink::MarkShape::Instance) {
            bool pickReq = false;
            if (pr::NodePickerRow("Node", doc, &o.nodeRef, id, true, false, &pickReq)) {
                structural = true; structLabel = "Mark Instance";
            }
            if (pickReq) {
                const std::size_t ooi = oi; const int sIdx = strokeIdx;
                const int mmi = markIdx;
                BeginObjectPick(nullptr,
                    [this, id, sIdx, mmi, ooi](Ink::NodeId picked) {
                        if (!project_.document) return;
                        const Ink::Node* nn = project_.document->Find(id);
                        if (!nn || sIdx < 0 ||
                            sIdx >= (int)nn->style.strokes.size()) return;
                        Ink::Style before = nn->style, after = before;
                        auto& mks = after.strokes[(std::size_t)sIdx].marks;
                        if ((std::size_t)mmi >= mks.size() ||
                            ooi >= mks[(std::size_t)mmi].objects.size()) return;
                        mks[(std::size_t)mmi].objects[ooi].nodeRef = picked;
                        project_.document->SetStyle(id, after);
                        CommitStyleEdit(id, before, "Mark Instance");
                    });
            }
            float sc = (float)o.size;
            if (pr::DragFloat("Scale", &sc, o.sizePercent ? 1.0f : 0.01f, 0.0f,
                              1000000.0f, 2, o.sizePercent ? "%" : "\xC3\x97")) {
                o.size = sc; liveApply("Mark Scale", false);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) liveApply("Mark Scale", true);
            static const char* kUnit2[] = { "%", "\xC3\x97" };
            int un2 = o.sizePercent ? 0 : 1;
            if (pr::ButtonGroupRow("Unit", kUnit2, 2, &un2)) {
                const bool toPct = (un2 == 0);
                if (toPct != o.sizePercent) {
                    o.size = toPct ? o.size * 100.0 : o.size * 0.01;
                    o.sizePercent = toPct; structural = true; structLabel = "Mark Unit";
                }
            }
        } else {
            const double sw = s.width > 1e-9 ? s.width : 1.0;
            auto sizeField = [&](const char* label, double* v) {
                float f = (float)*v;
                if (pr::DragFloat(label, &f, o.sizePercent ? 1.0f : 0.1f, 0.0f,
                                  1000000.0f, 2, o.sizePercent ? "%" : "")) {
                    *v = f; liveApply("Mark Object Size", false);
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                    liveApply("Mark Object Size", true);
            };
            if (o.shape == Ink::MarkShape::Circle ||
                o.shape == Ink::MarkShape::HalfCircle) sizeField("Radius", &o.size);
            else if (o.shape == Ink::MarkShape::Rectangle ||
                     o.shape == Ink::MarkShape::Triangle) {
                sizeField("Length", &o.size); sizeField("Width", &o.width);
            } else sizeField("Diagonal", &o.size);
            static const char* kUnit[] = { "%", "px" };
            int un = o.sizePercent ? 0 : 1;
            if (pr::ButtonGroupRow("Unit", kUnit, 2, &un)) {
                const bool toPct = (un == 0);
                if (toPct != o.sizePercent) {
                    if (toPct) { o.size = o.size / sw * 100.0; o.width = o.width / sw * 100.0; }
                    else       { o.size = o.size * 0.01 * sw; o.width = o.width * 0.01 * sw; }
                    o.sizePercent = toPct; structural = true; structLabel = "Mark Unit";
                }
            }
        }
        if (o.shape != Ink::MarkShape::Circle) {
            float rot = (float)(o.rotation * 180.0 / 3.14159265358979);
            if (pr::DragFloat("Rotation", &rot, 0.5f, -360.0f, 360.0f, 1, "", pr::Quantity::Angle)) {
                o.rotation = rot * 3.14159265358979 / 180.0;
                liveApply("Mark Object Rotation", false);
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
                liveApply("Mark Object Rotation", true);
        }
        {
            float ao = (float)o.alongOffset;
            if (pr::DragFloat("Along", &ao, o.sizePercent ? 1.0f : 0.1f,
                              -100000.0f, 100000.0f, 2, o.sizePercent ? "%" : "")) {
                o.alongOffset = ao; liveApply("Mark Along", false);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) liveApply("Mark Along", true);
        }
        {
            static const char* kBend[] = { "Hard", "Bend", "Follow", "Chord" };
            int bd = (int)o.bend;
            if (pr::DropdownRow("Bend", kBend, 4, &bd)) {
                o.bend = (Ink::MarkBend)bd; structural = true; structLabel = "Mark Bend";
            }
        }
        if (o.mode == Ink::MarkObjectMode::Blend) {
            int bl = std::min((int)o.blend, 12);
            if (pr::DropdownRow("Blend mode", kBlend, 13, &bl)) {
                o.blend = (Ink::BlendMode)bl; structural = true; structLabel = "Mark Blend";
            }
            static const char* kOrder[] = { "Behind", "Front" };
            int fr = o.front ? 1 : 0;
            if (pr::ButtonGroupRow("Order", kOrder, 2, &fr)) {
                o.front = (fr == 1); structural = true; structLabel = "Mark Order";
            }
        }
        if (o.shape != Ink::MarkShape::Instance &&
            o.mode == Ink::MarkObjectMode::Blend) {
            bool inh = o.useStrokeColor;
            if (pr::CheckRow("Stroke colour", &inh)) {
                o.useStrokeColor = inh; structural = true; structLabel = "Mark Colour";
            }
            if (!o.useStrokeColor) {
                bool rel = false;
                if (pr::ColorRow("Colour", &o.color, true, &rel))
                    liveApply("Mark Colour", false);
                if (rel) liveApply("Mark Colour", true);
            }
        } else if (o.mode == Ink::MarkObjectMode::Fusion && !o.useStrokeColor) {
            o.useStrokeColor = true; structural = true; structLabel = "Mark Colour";
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
        m.objects.push_back(Ink::MarkObject{});
        structural = true; structLabel = "Add Mark Object";
    }

    if (structural) {
        const Ink::Style before = n->style;
        doc.SetStyle(id, style);
        CommitStyleEdit(id, before, structLabel);
    }
}

} // namespace App
