#include "Application.h"

#include "ViewportShapes.h"
#include <Ink/Geometry/Geometry.h>
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ToolManager.h>
#include <algorithm>
#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport editor overlays (docs/Ink/ROADMAP.md Lot 8) — 100% Vulkan via the
//  View's OverlayList (never ImGui inside the canvas):
//   • Object mode: selected-object CONTOUR (flattened outline), origin dots, and
//     dashed PARENT-relationship lines (Ink parentId, Lot 7).
//   • Edit mode: anchor points + handle lines/points, coloured by the anchor
//     kind (Corner/Smooth/Symmetric), and the edited object's contour.
//   • Modal transform: pivot cross, axis line, a line pivot→cursor, and a CUSTOM
//     cursor drawn in Vulkan (the OS cursor is hidden during the op).
//   • The 2D cursor, and the in-progress box-select / draw-shape gesture.
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok;

Ink::Color Tint(DS::DesignSystem& ds, Tok token, float alpha) {
    try {
        const ImVec4 c = ds.GetColor(token);
        return Ink::SrgbToLinearPremultiplied(c.x, c.y, c.z, alpha);
    } catch (...) {
        return Ink::SrgbToLinearPremultiplied(0.9f, 0.6f, 0.1f, alpha);
    }
}
Ink::Color Rgb(float r, float g, float b, float a) {
    return Ink::SrgbToLinearPremultiplied(r, g, b, a);
}

// Handle colour by anchor kind, from the design tokens the legacy edit mode
// used: Corner→Free (purple-ish), Smooth→Aligned (blue), Symmetric→Mirrored
// (green). Falls back to sensible hues if a token is missing.
Ink::Color HandleColor(DS::DesignSystem& ds, Ink::AnchorKind k, float a) {
    Tok t = k == Ink::AnchorKind::Smooth ? Tok::C_EditHandle_Aligned
          : k == Ink::AnchorKind::Symmetric ? Tok::C_EditHandle_Mirrored
                                            : Tok::C_EditHandle_Free;
    try { const ImVec4 c = ds.GetColor(t); return Ink::SrgbToLinearPremultiplied(c.x,c.y,c.z,a); }
    catch (...) {
        switch (k) {
            case Ink::AnchorKind::Smooth:    return Rgb(0.30f,0.62f,0.95f,a);
            case Ink::AnchorKind::Symmetric: return Rgb(0.45f,0.85f,0.40f,a);
            default:                         return Rgb(0.72f,0.55f,0.95f,a);
        }
    }
}
// Scale a colour's brightness (lighten selected lines/dots).
Ink::Color Shade(const Ink::Color& c, float f) {
    return { std::min(1.0f, c.r*f), std::min(1.0f, c.g*f), std::min(1.0f, c.b*f), c.a };
}

// A dashed segment (view px) on the overlay list.
void DashLine(Ink::OverlayList& ov, Ink::Vec2 a, Ink::Vec2 b, const Ink::Color& col,
              float th, float dash = 6.0f, float gap = 4.0f) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) return;
    const float ux = dx / len, uy = dy / len;
    for (float t = 0.0f; t < len; t += dash + gap) {
        const float t1 = std::min(t + dash, len);
        ov.AddLine({ a.x + ux * t, a.y + uy * t },
                   { a.x + ux * t1, a.y + uy * t1 }, col, th);
    }
}
} // namespace

void Application::DrawEditOverlays(EditorState& st, const ViewCam& cam,
                                   Ink::OverlayList& ov, bool hovered) {
    (void)st;
    if (!ink_ || !project_.document) return;
    Ink::Document& doc = *project_.document;
    auto& ds = DS::DesignSystem::Instance();

    // The LEGACY selection palette: the ACTIVE object/element is the bright
    // orange state token, a SELECTED-not-active one the darker orange — never
    // the blue accent (that read as a different system).
    const Ink::Color selCol    = Tint(ds, Tok::S_State_Selected_OnPage, 1.0f);
    const Ink::Color activeCol = Tint(ds, Tok::S_State_Active_OnPage, 1.0f);
    const Ink::Color handleCol = Tint(ds, Tok::S_Color_Text_Default, 1.0f);
    const Ink::Color subtleCol = Tint(ds, Tok::S_Color_Text_Subtle, 0.9f);

    // Sub-pixel flatten tolerance so curved contours (circles, Béziers) stay
    // crisp and vector-smooth at ANY zoom. Flatten works in the path's LOCAL
    // space, so the local tolerance = 0.25 px / (zoom · worldScale): the segment
    // count then tracks the on-screen size. The overlay pass rasterises the
    // result in Vulkan above the content.
    const double kPxTol = 0.25;

    // Draw an object's flattened contour (world space → view) in `col`.
    auto drawContour = [&](Ink::NodeId id, const Ink::Color& col, float th) {
        const Ink::Node* n = doc.Find(id);
        if (!n || n->kind != Ink::NodeKind::Path || n->path.Empty()) return;
        const Ink::DMat23 w = doc.WorldTransform(id);
        const double ws = std::max(1e-6, std::sqrt(std::abs(w.m[0]*w.m[4] - w.m[1]*w.m[3])));
        const double localTol = std::max(1e-4, kPxTol / (std::max(1e-6, cam.zoom) * ws));
        auto polys = Ink::geom::Flatten(n->path, localTol);
        for (const auto& pl : polys) {
            const std::size_t m = pl.points.size();
            if (m < 2) continue;
            const std::size_t last = pl.closed ? m : m - 1;
            for (std::size_t i = 0; i < last; ++i) {
                const Ink::DVec2 p0 = w.Apply(pl.points[i]);
                const Ink::DVec2 p1 = w.Apply(pl.points[(i + 1) % m]);
                ov.AddLine(cam.DocToView(p0.x, p0.y), cam.DocToView(p1.x, p1.y), col, th);
            }
        }
    };
    auto originOf = [&](Ink::NodeId id) {
        const Ink::DMat23 w = doc.WorldTransform(id);
        return cam.DocToView(w.m[2], w.m[5]);
    };

    // ── Object mode ───────────────────────────────────────────────────────────
    if (edit_.mode == EditorMode::Object) {
        for (Ink::NodeId id : edit_.selection) {
            const bool active = (id == edit_.active);
            // Contour (paths) or bbox fallback (groups/instances).
            const Ink::Node* n = doc.Find(id);
            if (n && n->kind == Ink::NodeKind::Path)
                drawContour(id, active ? activeCol : selCol, active ? 2.0f : 1.5f);
            else {
                Ink::DRect nb;
                if (ink_->NodeBounds(id, nb)) {
                    const Ink::Vec2 a = cam.DocToView(nb.min.x, nb.min.y);
                    const Ink::Vec2 b = cam.DocToView(nb.max.x, nb.max.y);
                    ov.AddRect({ std::min(a.x,b.x), std::min(a.y,b.y) },
                               { std::max(a.x,b.x), std::max(a.y,b.y) },
                               active ? activeCol : selCol, active ? 2.0f : 1.5f);
                }
            }
            // Origin dot (colour by loose / on-page).
            const Ink::Vec2 o = originOf(id);
            const Ink::Color oc = Tint(ds, n && n->parent != Ink::kNullNode
                ? Tok::S_State_Active_OnPage : Tok::S_State_Active_Loose, 1.0f);
            ov.AddCircleFilled(o, 3.5f, oc);
            ov.AddCircle(o, 3.5f, Rgb(0, 0, 0, 0.6f), 1.0f);
            // Parent-relationship line (Ink parentId, Lot 7): dashed child→parent.
            if (n && n->parentId != Ink::kNullNode && doc.Find(n->parentId)) {
                const Ink::Vec2 po = originOf(n->parentId);
                DashLine(ov, o, po, subtleCol, 1.0f);
                ov.AddCircle(po, 2.5f, subtleCol, 1.0f);
            }
        }
    }

    // ── Edit mode: contour + anchors + handles (individually selectable) ──────
    // The LEGACY drawing rules, exactly: the contour/edges use the dim edge
    // token; anchor points are FILLED CIRCLES (r 3.5) with a white ring —
    // ACTIVE (last-picked) = bright orange, SELECTED = darker orange, plain =
    // the dim vertex token; handles stay in their per-TYPE hue (never orange),
    // lines lightened ×1.35 when the point or that handle is selected (else
    // ×0.75), dots lightened only when THAT handle is selected (else ×0.8).
    if (edit_.mode == EditorMode::Edit && edit_.active != Ink::kNullNode) {
        using ElemPart = EditContext::ElemPart;
        drawContour(edit_.active, Tint(ds, Tok::C_EditHandle_Edge, 1.0f), 1.5f);
        if (const Ink::Node* n = doc.Find(edit_.active)) {
            const Ink::DMat23 w = doc.WorldTransform(edit_.active);
            const Ink::Color vertCol   = Tint(ds, Tok::C_EditHandle_Vertex, 1.0f);
            const Ink::Color ringCol   = Tint(ds, Tok::C_EditHandle_VertexRing, 1.0f);
            // The ACTIVE element = the LAST-picked point in the selection.
            int actSp = -1, actA = -1;
            for (auto it = edit_.elemSel.rbegin(); it != edit_.elemSel.rend(); ++it)
                if (it->part == ElemPart::Point) { actSp = it->sp; actA = it->a; break; }
            for (int sp = 0; sp < (int)n->path.subpaths.size(); ++sp) {
                const auto& subp = n->path.subpaths[sp];
                // NURBS: the anchors are CONTROL POINTS off the curve — draw
                // the straight control polygon (the legacy dim "hull"), never
                // Bézier handles (the smooth curve itself is the content).
                if (subp.spline == Ink::SplineType::Nurbs &&
                    subp.anchors.size() >= 2) {
                    const Ink::Color hull =
                        Tint(ds, Tok::C_EditHandle_NurbsHull, 1.0f);
                    const std::size_t cnt = subp.anchors.size();
                    const std::size_t last = subp.closed ? cnt : cnt - 1;
                    for (std::size_t i = 0; i < last; ++i) {
                        const Ink::DVec2 p0 = w.Apply(subp.anchors[i].pos);
                        const Ink::DVec2 p1 =
                            w.Apply(subp.anchors[(i + 1) % cnt].pos);
                        ov.AddLine(cam.DocToView(p0.x, p0.y),
                                   cam.DocToView(p1.x, p1.y), hull, 1.0f);
                    }
                }
                for (int a = 0; a < (int)subp.anchors.size(); ++a) {
                    const Ink::Anchor& an = subp.anchors[a];
                    const bool ptSel  = edit_.ElemSelected(sp, a, ElemPart::Point);
                    const bool ptAct  = (sp == actSp && a == actA);
                    const bool inSel  = edit_.ElemSelected(sp, a, ElemPart::In);
                    const bool outSel = edit_.ElemSelected(sp, a, ElemPart::Out);
                    const bool touched = edit_.AnchorTouched(sp, a);
                    const Ink::DVec2 dp = w.Apply(an.pos);
                    const Ink::Vec2 v = cam.DocToView(dp.x, dp.y);
                    if (touched) {
                        const Ink::Color hc = HandleColor(ds, an.kind, 1.0f);
                        auto handle = [&](bool side, Ink::DVec2 rel, bool hsel) {
                            (void)side;
                            const Ink::DVec2 hp = w.Apply({ an.pos.x+rel.x, an.pos.y+rel.y });
                            const Ink::Vec2 hv = cam.DocToView(hp.x, hp.y);
                            const bool lineSel = ptSel || ptAct || hsel;
                            ov.AddLine(v, hv, lineSel ? Shade(hc,1.35f) : Shade(hc,0.75f), lineSel?1.5f:1.0f);
                            ov.AddCircleFilled(hv, hsel ? 4.0f : 3.0f, hsel ? Shade(hc,1.35f) : Shade(hc,0.8f));
                        };
                        if (an.hasIn)  handle(false, an.in,  inSel);
                        if (an.hasOut) handle(true,  an.out, outSel);
                    }
                    // Anchor point — the legacy circle: active bright orange,
                    // selected darker orange, plain dim, white ring on all.
                    const float vr = (ptSel || ptAct) ? 4.5f : 3.5f;
                    ov.AddCircleFilled(v, vr, ptAct ? activeCol
                                              : ptSel ? selCol : vertCol);
                    ov.AddCircle(v, vr, ringCol, 1.0f);
                }
            }
        }
    }

    // ── Pen (draw-on-create) preview: the anchors laid so far + a rubber
    // band from the last anchor to the cursor; the first anchor highlights
    // when closing is possible (a click there closes the path). ─────────────
    if (penActive_ && hovered) {
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const Ink::Vec2 mv{ mp.x - cam.canvasMin.x, mp.y - cam.canvasMin.y };
        // Follow-curve (Shift): the blue entry diamond, the traced preview and the
        // click that freezes a piece onto the pen. When it is following, skip the
        // normal rubber-band preview (the trace replaces it).
        bool followCommitted = false;
        const bool following =
            UpdatePenFollowCurve(cam, hovered, ov, followCommitted);
        (void)followCommitted;
        // The very first point (before its release) has no node yet — just show
        // its dot at the cursor while it is being placed.
        if (!following && penNode_ == Ink::kNullNode && penHasPending_)
            ov.AddCircleFilled(cam.DocToView(penPending_.pos.x, penPending_.pos.y),
                               3.5f, activeCol);
        if (!following && penNode_ != Ink::kNullNode) {
            // LIVE fill preview: the companion fill node re-follows the pen
            // path (frozen + pending, closed) each frame — real pipeline,
            // patterns included, curved closing edge.
            UpdatePenFillPreview(doc);
            const Ink::Node* pn = doc.Find(penNode_);
            if (pn && !pn->path.subpaths.empty()) {
              const auto& frozen = pn->path.subpaths.front().anchors;
              const std::size_t nFrozen = frozen.size();
              const Ink::DVec2 md = cam.ScreenToDoc(mp.x, mp.y);
              const double tol = std::max(1e-4, 0.25 / std::max(1e-6, cam.zoom));

              // Snap-to-close: cursor in the first anchor's close zone (≥3 frozen,
              // no pending), Ctrl/Shift suppress it. Matches HandlePenInput.
              bool closeHover = false;
              if (!penHasPending_ && nFrozen >= 3 &&
                  !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) {
                  const Ink::Vec2 fv = cam.DocToView(frozen.front().pos.x,
                                                     frozen.front().pos.y);
                  closeHover = std::hypot(mv.x - fv.x, mv.y - fv.y) <= 9.0f;
              }

              // Frozen anchor dots (the node renders their SOLID stroke already);
              // the closeable first anchor gets a ring.
              for (std::size_t i = 0; i < nFrozen; ++i) {
                  const Ink::Vec2 v = cam.DocToView(frozen[i].pos.x, frozen[i].pos.y);
                  const bool first = i == 0 && nFrozen >= 3;
                  ov.AddCircleFilled(v, first ? 4.5f : 3.0f,
                                     first ? activeCol : selCol);
                  if (first)
                      ov.AddCircle(v, closeHover ? 8.0f : 6.0f, activeCol,
                                   closeHover ? 2.5f : 1.5f, 16);
              }
              if (penHasPending_)
                  ov.AddCircleFilled(cam.DocToView(penPending_.pos.x,
                                                   penPending_.pos.y), 3.5f, activeCol);

              // TRUE single-curve preview. Build the FULL in-progress path
              // (frozen anchors + the pending/ghost/closing anchor) — one
              // continuous curve with proper joins throughout, real caps only at
              // the two TRUE ends. The pen node's own solid strokes are BLANKED
              // for this frame (frame-scoped, restored in Update) so the pipeline
              // never double-draws; the overlay renders the whole stroke. The
              // validated part is drawn OPAQUE and the in-progress section
              // TRANSLUCENT by tessellating the FROZEN curve and the FULL curve
              // and layering them in one dedup group (see below) — no cap at the
              // junction (it is mid-curve). FILLS render live via the fill node.
              Ink::PathData fullPath = pn->path;   // frozen (bezier, open)
              Ink::Subpath& fsp = fullPath.subpaths.front();
              bool haveExtra = false;
              if (penHasPending_) {
                  fsp.anchors.push_back(penPending_); haveExtra = true;
              } else if (closeHover) {
                  fsp.closed = true; haveExtra = true;
              } else if (nFrozen >= 1) {
                  Ink::Anchor g; g.pos = md;
                  fsp.anchors.push_back(g); haveExtra = true;
              }
              if (haveExtra && fsp.anchors.size() >= 2) {
                  // TWO flattenings of the SAME segments: the FULL in-progress
                  // curve (validated + in-progress, one continuous walk so the
                  // junction anchor keeps a proper JOIN) and the FROZEN-only
                  // curve (the validated part alone). Because both flatten the
                  // shared Bézier segments identically, the frozen mesh coincides
                  // pixel-for-pixel with the full mesh's validated portion — so
                  // we never have to CLASSIFY triangles (the old arc split was
                  // fooled wherever the in-progress end crossed back over the
                  // validated part, dropping/leaking triangles as it moved).
                  Ink::PathData frozenPath;
                  Ink::Subpath fz;
                  fz.spline = pn->path.subpaths.front().spline;
                  fz.anchors = frozen; fz.closed = false;
                  frozenPath.subpaths.push_back(std::move(fz));
                  const auto frozenFlat = Ink::geom::Flatten(frozenPath, tol);
                  const auto fullFlat   = Ink::geom::Flatten(fullPath, tol);
                  // Capture the strokes BEFORE blanking (blanking clears the
                  // node's own list, which we still iterate below).
                  const std::vector<Ink::Stroke> penStrokes = pn->style.strokes;
                  // Blank the pen node's solid strokes this frame (restored in
                  // Update) so only the overlay renders the stroke.
                  { Ink::Style blank = pn->style; blank.strokes.clear();
                    ApplyMarkPreviewStyle(penNode_, blank); }
                  bool anyStroke = false;
                  for (const Ink::Stroke& src : penStrokes) {
                      if (!src.enabled || src.width <= 0.0) continue;
                      anyStroke = true;
                      const Ink::Color k = src.paint.color;
                      const Ink::Color solid{ k.r*k.a, k.g*k.a, k.b*k.a, k.a };
                      const float ha = k.a * 0.5f;
                      const Ink::Color trans{ k.r*ha, k.g*ha, k.b*ha, ha };
                      auto emit = [&](const Ink::geom::Mesh& m,
                                      const Ink::Color& col) {
                          auto vp = [&](std::uint32_t idx) {
                              return cam.DocToView(m.positions[idx*2],
                                                   m.positions[idx*2+1]);
                          };
                          for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3)
                              ov.AddTriangle(vp(m.indices[i]), vp(m.indices[i+1]),
                                             vp(m.indices[i+2]), col);
                      };
                      const Ink::geom::Mesh mFrozen =
                          Ink::geom::TessellateStroke(frozenFlat, src, tol);
                      const Ink::geom::Mesh mFull =
                          Ink::geom::TessellateStroke(fullFlat, src, tol);
                      // ONE dedup group (shared stencil, first-wins per pixel).
                      // The VALIDATED mesh (solid) is emitted FIRST so it claims
                      // its pixels: its join self-overlaps blend exactly once,
                      // and it stays solid no matter where the in-progress end
                      // later crosses back over it. The FULL mesh (translucent)
                      // comes second — rejected on every already-solid pixel, so
                      // the validated part never shows the preview colour and no
                      // triangle drops out; it only paints the in-progress
                      // section (blending its own overlaps once too) and the
                      // junction keeps its true continuous JOIN. No spatial arc
                      // split, so nothing can be misclassified at a self-crossing.
                      ov.BeginDedup();
                      emit(mFrozen, solid);
                      emit(mFull, trans);
                      ov.EndDedup();
                  }
                  // Strokeless style: a thin construction line on the preview
                  // section so the in-progress curve still reads.
                  if (!anyStroke) {
                      for (const auto& pl : fullFlat)
                          for (std::size_t i = 0; i + 1 < pl.points.size(); ++i)
                              ov.AddLine(cam.DocToView(pl.points[i].x,
                                                       pl.points[i].y),
                                         cam.DocToView(pl.points[i+1].x,
                                                       pl.points[i+1].y),
                                         activeCol, 1.0f);
                  }
              }
            }
        }
    }
    // Draw-on-Create cursor: hide the OS cursor and draw a crosshair + a preview
    // glyph of the shape/curve being created (pen active OR a shape drag-box
    // armed). Foreground list, so it sits over everything. NOT over a floating
    // overlay (tool palette / N panel) and not while any popup is open — the
    // pointer is on UI there and must keep the plain OS cursor (a popup opened
    // from this zone still counts as "hovered" via the popup hierarchy).
    {
        const ImVec2 mp2 = ImGui::GetIO().MousePos;
        bool onOverlayUi = false;
        for (const ImVec4& r : st.overlayRects)
            if (mp2.x >= r.x && mp2.x <= r.z && mp2.y >= r.y && mp2.y <= r.w) {
                onOverlayUi = true;
                break;
            }
        const bool anyPopup = ImGui::IsPopupOpen(
            nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        if (hovered && !onOverlayUi && !anyPopup) {
            const std::string ck = CreateCursorKind();
            if (!ck.empty()) DrawCreateCursor(ck.c_str());
        }
    }

    // ── 2D cursor (legacy design): a red/white ringed crosshair. The four
    // ticks run from the inner radius to the outer length so the ring stays
    // concentric; −X/−Y are dark (readable on the page), +X red and +Y green
    // (the axes). All colours are the viewport-cursor tokens. ────────────────
    if (edit_.cursor2DValid && show2DCursor_) {
        const Ink::Vec2 c = cam.DocToView(edit_.cursor2D.x, edit_.cursor2D.y);
        const float rr = 9.0f, ri = 3.0f, ro = 14.0f;
        const Ink::Color ringW = Tint(ds, Tok::C_Viewport_CursorRing, 1.0f);
        const Ink::Color ringR = Tint(ds, Tok::C_Viewport_CursorRingAccent, 1.0f);
        const Ink::Color tickC = Tint(ds, Tok::C_Viewport_CursorTick, 1.0f);
        const Ink::Color axisX = Tint(ds, Tok::C_Viewport_CursorAxisX, 1.0f);
        const Ink::Color axisY = Tint(ds, Tok::C_Viewport_CursorAxisY, 1.0f);
        ov.AddCircle(c, rr, ringW, 3.0f, 24);
        ov.AddCircle(c, rr, ringR, 1.5f, 24);
        auto tick = [&](float dx, float dy, const Ink::Color& col) {
            ov.AddLine({ c.x + dx * ri, c.y + dy * ri },
                       { c.x + dx * ro, c.y + dy * ro }, col, 1.8f);
        };
        tick(-1, 0, tickC);   // −X
        tick(0, -1, tickC);   // −Y
        tick(+1, 0, axisX);   // +X
        tick(0, +1, axisY);   // +Y
    }

    // ── Modal transform feedback ──────────────────────────────────────────────
    if (transformOp_.Active()) {
        const Ink::Vec2 p = cam.DocToView(transformOp_.pivot.x, transformOp_.pivot.y);
        const float s = 10.0f;
        ov.AddLine({ p.x - s, p.y }, { p.x + s, p.y }, subtleCol, 1.0f);
        ov.AddLine({ p.x, p.y - s }, { p.x, p.y + s }, subtleCol, 1.0f);
        ov.AddCircle(p, 4.0f, activeCol, 1.5f);
        if (transformOp_.axis >= 0) {
            const Ink::DVec2 dir = transformOp_.axis == 0 ? transformOp_.basisX : transformOp_.basisY;
            const double L = 100000.0;
            const Ink::Vec2 a = cam.DocToView(transformOp_.pivot.x - dir.x*L, transformOp_.pivot.y - dir.y*L);
            const Ink::Vec2 b = cam.DocToView(transformOp_.pivot.x + dir.x*L, transformOp_.pivot.y + dir.y*L);
            const Ink::Color axisCol = transformOp_.axis == 0 ? Rgb(0.9f,0.25f,0.25f,0.8f)
                                                              : Rgb(0.35f,0.75f,0.3f,0.8f);
            ov.AddLine({ a.x, a.y }, { b.x, b.y }, axisCol, 1.0f);
        }
        // The pivot→cursor guide follows the VIRTUAL cursor (unbounded, real
        // speed — TransformOp.virtPx): it keeps pointing in the true transform
        // direction beyond the canvas edge, and coincides exactly with the
        // displayed cursor whenever that virtual point is inside the canvas
        // (both derive from the same accumulation). No line in Move; shown for
        // Rotate/Scale in BOTH Object and Edit mode. The cursor GLYPH itself
        // is the legacy icon, drawn by DrawTransformCursor (ViewportModal.cpp).
        if (transformOp_.kind != TransformOp::Kind::Move) {
            const Ink::Vec2 virt{ (float)(transformOp_.virtPx.x - cam.canvasMin.x),
                                  (float)(transformOp_.virtPx.y - cam.canvasMin.y) };
            DashLine(ov, p, virt, subtleCol, 1.0f, 5.0f, 4.0f);
        }
    }

    // ── In-progress canvas gesture ────────────────────────────────────────────
    if (canvasDrag_.kind == CanvasDrag::Kind::BoxSelect) {
        const Ink::Vec2 a = cam.DocToView(canvasDrag_.startDoc.x, canvasDrag_.startDoc.y);
        const Ink::Vec2 b = cam.DocToView(canvasDrag_.curDoc.x, canvasDrag_.curDoc.y);
        const Ink::Color fill = Tint(ds, Tok::S_Color_Accent_Default, 0.12f);
        ov.AddRectFilled({ std::min(a.x,b.x), std::min(a.y,b.y) },
                         { std::max(a.x,b.x), std::max(a.y,b.y) }, fill);
        ov.AddRect({ std::min(a.x,b.x), std::min(a.y,b.y) },
                   { std::max(a.x,b.x), std::max(a.y,b.y) }, selCol, 1.0f);
    } else if (canvasDrag_.kind == CanvasDrag::Kind::DrawShape) {
        // Shape-tool preview: a REAL translucent node (UpdateShapePreview) so
        // the pipeline renders the fills cut at the contour, patterns with the
        // right anchor, exactly like the final object. The overlay only adds
        // the construction contour (so a strokeless shape still reads) and the
        // subtle drag box.
        const Ink::DVec2 sa = canvasDrag_.startDoc, sb = canvasDrag_.curDoc;
        UpdateShapePreview(doc, canvasDrag_.shapeKind.c_str(), sa, sb);
        const double cx = (sa.x + sb.x) * 0.5, cy = (sa.y + sb.y) * 0.5;
        const double hw = std::max(1.0, std::abs(sb.x - sa.x) * 0.5);
        const double hh = std::max(1.0, std::abs(sb.y - sa.y) * 0.5);
        std::string nm;
        Ink::PathData gp = BuildShapeGeometry(canvasDrag_.shapeKind.c_str(),
                                              hw, hh, nm);
        const double localTol = std::max(1e-4, 0.25 / std::max(1e-6, cam.zoom));
        for (const auto& pl : Ink::geom::Flatten(gp, localTol)) {
            const std::size_t np = pl.points.size();
            if (np < 2) continue;
            const std::size_t last = pl.closed ? np : np - 1;
            for (std::size_t i = 0; i < last; ++i)
                ov.AddLine(cam.DocToView(cx + pl.points[i].x,
                                         cy + pl.points[i].y),
                           cam.DocToView(cx + pl.points[(i + 1) % np].x,
                                         cy + pl.points[(i + 1) % np].y),
                           activeCol, 1.0f);
        }
        const Ink::Vec2 a = cam.DocToView(sa.x, sa.y);
        const Ink::Vec2 b = cam.DocToView(sb.x, sb.y);
        ov.AddRect({ std::min(a.x,b.x), std::min(a.y,b.y) },
                   { std::max(a.x,b.x), std::max(a.y,b.y) }, subtleCol, 1.0f);
    }
    // ── Line-mark tool: handles + ghosts + its own mouse handling (it needs
    //    the overlay list, so it drives from the overlay phase). ─────────────
    // The object eyedropper (a Properties node picker) owns the click while
    // active — the line-mark tool must not preview or place anything then.
    if (MarkModeActive() && !ObjectPickActive())
        HandleMarkTool(st, cam, ov, hovered);

    (void)handleCol;
}

} // namespace App
