#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  OverlayList — editor-side builder for the Compositor's full-GPU overlay pass
//  (Lot 12). The editor tessellates its canvas overlays (selection outlines,
//  handles, grid, page outlines, the 2D cursor, …) into COLOURED TRIANGLES here,
//  in SCREEN PIXELS, then `ToNDC` converts to the engine's NDC vertex list handed
//  to IViewRenderer::SubmitOverlay. The engine just rasterises — full GPU, no
//  per-frame ImGui draw list for the canvas overlays.
//
//  ANTI-ALIASING: filled edges and lines get a ~1px transparent FRINGE (the same
//  trick ImGui uses) so diagonals and circles stay smooth. The fringe is in screen
//  pixels, so it's a constant 1px on screen regardless of zoom.
//
//  Colours are ImGui-packed 0xAABBGGRR (IM_COL32) so token colours pass through.
// ─────────────────────────────────────────────────────────────────────────────

#include <Renderer/IViewRenderer.h>
#include <imgui.h>
#include <cmath>
#include <cstdint>
#include <vector>

namespace App {

class OverlayList {
public:
    using Vtx = Renderer::IViewRenderer::OverlayVertex;
    static constexpr float kFringe = 1.0f;   // AA fringe width, screen px

    void Clear() { px_.clear(); idx_.clear(); }
    bool Empty() const { return idx_.empty(); }

    // ── Raw triangle (no AA) ────────────────────────────────────────────────
    void AddTriangleFilled(ImVec2 a, ImVec2 b, ImVec2 c, ImU32 col) {
        uint32_t base = (uint32_t)px_.size();
        push(a, col); push(b, col); push(c, col);
        idx_.push_back(base); idx_.push_back(base + 1); idx_.push_back(base + 2);
    }
    // A quad a→b→c→d (two triangles), each vertex its own colour (for AA fringes).
    void AddQuad(ImVec2 a, ImU32 ca, ImVec2 b, ImU32 cb, ImVec2 c, ImU32 cc, ImVec2 d, ImU32 cd) {
        uint32_t base = (uint32_t)px_.size();
        push(a, ca); push(b, cb); push(c, cc); push(d, cd);
        idx_.push_back(base); idx_.push_back(base + 1); idx_.push_back(base + 2);
        idx_.push_back(base); idx_.push_back(base + 2); idx_.push_back(base + 3);
    }

    // ── Anti-aliased line (a→b), half-width `thick/2`, with 1px fringes ──────
    void AddLine(ImVec2 a, ImVec2 b, ImU32 col, float thick = 1.0f) {
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) return;
        float ux = dx / len, uy = dy / len;       // along
        float nx = -uy, ny = ux;                   // normal
        float hw = thick * 0.5f;
        ImU32 clear = col & 0x00FFFFFFu;            // same rgb, alpha 0
        // Inner (opaque) edges at ±hw, outer (transparent) fringe at ±(hw+fringe).
        ImVec2 i0{ a.x + nx * hw, a.y + ny * hw }, i1{ b.x + nx * hw, b.y + ny * hw };
        ImVec2 i2{ b.x - nx * hw, b.y - ny * hw }, i3{ a.x - nx * hw, a.y - ny * hw };
        ImVec2 o0{ a.x + nx * (hw + kFringe), a.y + ny * (hw + kFringe) };
        ImVec2 o1{ b.x + nx * (hw + kFringe), b.y + ny * (hw + kFringe) };
        ImVec2 o2{ b.x - nx * (hw + kFringe), b.y - ny * (hw + kFringe) };
        ImVec2 o3{ a.x - nx * (hw + kFringe), a.y - ny * (hw + kFringe) };
        AddQuad(i0, col, i1, col, i2, col, i3, col);       // opaque core
        AddQuad(o0, clear, o1, clear, i1, col, i0, col);   // +normal fringe
        AddQuad(i3, col, i2, col, o2, clear, o3, clear);   // −normal fringe
    }

    void AddRectFilled(ImVec2 mn, ImVec2 mx, ImU32 col) {
        // Solid fill (no fringe needed for axis-aligned rects — edges are crisp).
        AddTriangleFilled({ mn.x, mn.y }, { mx.x, mn.y }, { mx.x, mx.y }, col);
        AddTriangleFilled({ mn.x, mn.y }, { mx.x, mx.y }, { mn.x, mx.y }, col);
    }
    void AddRect(ImVec2 mn, ImVec2 mx, ImU32 col, float thick = 1.0f) {
        AddLine({ mn.x, mn.y }, { mx.x, mn.y }, col, thick);
        AddLine({ mx.x, mn.y }, { mx.x, mx.y }, col, thick);
        AddLine({ mx.x, mx.y }, { mn.x, mx.y }, col, thick);
        AddLine({ mn.x, mx.y }, { mn.x, mn.y }, col, thick);
    }

    // ── Anti-aliased filled circle (fan with a transparent fringe ring) ─────
    void AddCircleFilled(ImVec2 c, float r, ImU32 col, int seg = 0) {
        if (seg <= 0) seg = autoSeg(r);
        ImU32 clear = col & 0x00FFFFFFu;
        uint32_t centre = (uint32_t)px_.size();
        push(c, col);
        uint32_t inner = (uint32_t)px_.size();
        for (int i = 0; i <= seg; ++i) {
            float a = (float)i / (float)seg * 6.2831853f;
            push({ c.x + std::cos(a) * r, c.y + std::sin(a) * r }, col);
        }
        uint32_t outer = (uint32_t)px_.size();
        for (int i = 0; i <= seg; ++i) {
            float a = (float)i / (float)seg * 6.2831853f;
            push({ c.x + std::cos(a) * (r + kFringe), c.y + std::sin(a) * (r + kFringe) }, clear);
        }
        for (int i = 0; i < seg; ++i) {
            idx_.push_back(centre); idx_.push_back(inner + i); idx_.push_back(inner + i + 1);
            // fringe ring (inner opaque → outer transparent)
            idx_.push_back(inner + i);     idx_.push_back(outer + i);     idx_.push_back(outer + i + 1);
            idx_.push_back(inner + i);     idx_.push_back(outer + i + 1); idx_.push_back(inner + i + 1);
        }
    }
    void AddCircle(ImVec2 c, float r, ImU32 col, float thick = 1.0f, int seg = 0) {
        if (seg <= 0) seg = autoSeg(r);
        ImVec2 prev{ c.x + r, c.y };
        for (int i = 1; i <= seg; ++i) {
            float a = (float)i / (float)seg * 6.2831853f;
            ImVec2 cur{ c.x + std::cos(a) * r, c.y + std::sin(a) * r };
            AddLine(prev, cur, col, thick);
            prev = cur;
        }
    }

    // Convex polygon fill (fan). `pts` in screen pixels (no fringe — used for small
    // gizmos where crisp edges are fine; AA fringe could be added like the circle).
    void AddConvexPolyFilled(const ImVec2* pts, int n, ImU32 col) {
        for (int i = 2; i < n; ++i) AddTriangleFilled(pts[0], pts[i - 1], pts[i], col);
    }
    void AddPolyline(const ImVec2* pts, int n, ImU32 col, float thick = 1.0f, bool closed = false) {
        for (int i = 0; i + 1 < n; ++i) AddLine(pts[i], pts[i + 1], col, thick);
        if (closed && n > 2) AddLine(pts[n - 1], pts[0], col, thick);
    }

    // Convert the screen-pixel verts to the engine NDC list against the main
    // viewport (same projection ImGui uses). Call once per frame before submit.
    void ToNDC(std::vector<Vtx>& outVerts, std::vector<uint32_t>& outIdx) const {
        const ImGuiViewport* mv = ImGui::GetMainViewport();
        const float ox = mv->Pos.x, oy = mv->Pos.y, sw = mv->Size.x, sh = mv->Size.y;
        outVerts.resize(px_.size());
        for (size_t i = 0; i < px_.size(); ++i) {
            const Vtx& s = px_[i];
            outVerts[i].x = (sw > 0) ? ((s.x - ox) / sw * 2.0f - 1.0f) : 0.0f;
            outVerts[i].y = (sh > 0) ? ((s.y - oy) / sh * 2.0f - 1.0f) : 0.0f;
            outVerts[i].rgba = s.rgba;
        }
        outIdx = idx_;
    }

private:
    void push(ImVec2 p, ImU32 col) { px_.push_back({ p.x, p.y, col }); }
    static int autoSeg(float r) { int s = (int)(r * 0.6f) + 8; return s < 8 ? 8 : (s > 64 ? 64 : s); }

    std::vector<Vtx>      px_;    // verts in SCREEN pixels (converted to NDC at submit)
    std::vector<uint32_t> idx_;
};

// ─────────────────────────────────────────────────────────────────────────────
//  OverlayDL — a drop-in for the editor's `ImDrawList* dl` overlay drawing. It mirrors
//  the ImDrawList methods the viewport uses, but routes GEOMETRY to the full-GPU
//  OverlayList when the Compositor is active (gpu = true) and to ImGui otherwise.
//  TEXT and IMAGES always go to ImGui (no glyph atlas yet — Lot 12-3). This lets the
//  overlay code stay identical at call sites (`dl.AddLine(...)`), engine-agnostic.
// ─────────────────────────────────────────────────────────────────────────────
class OverlayDL {
public:
    OverlayDL(ImDrawList* dl, OverlayList* ov, bool gpu) : dl_(dl), ov_(ov), gpu_(gpu && ov) {}

    // Geometry → GPU overlay when active, else ImGui. Thickness/segments honoured.
    void AddLine(ImVec2 a, ImVec2 b, ImU32 col, float thick = 1.0f) {
        if (gpu_) ov_->AddLine(a, b, col, thick); else dl_->AddLine(a, b, col, thick);
    }
    void AddRect(ImVec2 mn, ImVec2 mx, ImU32 col, float rounding = 0.0f,
                 ImDrawFlags flags = 0, float thick = 1.0f) {
        if (gpu_) ov_->AddRect(mn, mx, col, thick);    // rounding ignored on GPU (rare for overlays)
        else dl_->AddRect(mn, mx, col, rounding, flags, thick);
    }
    void AddRectFilled(ImVec2 mn, ImVec2 mx, ImU32 col, float rounding = 0.0f,
                       ImDrawFlags flags = 0) {
        if (gpu_) ov_->AddRectFilled(mn, mx, col);
        else dl_->AddRectFilled(mn, mx, col, rounding, flags);
    }
    void AddCircle(ImVec2 c, float r, ImU32 col, int seg = 0, float thick = 1.0f) {
        if (gpu_) ov_->AddCircle(c, r, col, thick, seg); else dl_->AddCircle(c, r, col, seg, thick);
    }
    void AddCircleFilled(ImVec2 c, float r, ImU32 col, int seg = 0) {
        if (gpu_) ov_->AddCircleFilled(c, r, col, seg); else dl_->AddCircleFilled(c, r, col, seg);
    }
    void AddConvexPolyFilled(const ImVec2* pts, int n, ImU32 col) {
        if (gpu_) ov_->AddConvexPolyFilled(pts, n, col); else dl_->AddConvexPolyFilled(pts, n, col);
    }
    void AddTriangleFilled(ImVec2 a, ImVec2 b, ImVec2 c, ImU32 col) {
        if (gpu_) ov_->AddTriangleFilled(a, b, c, col); else dl_->AddTriangleFilled(a, b, c, col);
    }
    void AddTriangle(ImVec2 a, ImVec2 b, ImVec2 c, ImU32 col, float thick = 1.0f) {
        if (gpu_) { ov_->AddLine(a, b, col, thick); ov_->AddLine(b, c, col, thick); ov_->AddLine(c, a, col, thick); }
        else dl_->AddTriangle(a, b, c, col, thick);
    }
    void AddQuad(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 col, float thick = 1.0f) {
        if (gpu_) { ov_->AddLine(a, b, col, thick); ov_->AddLine(b, c, col, thick);
                    ov_->AddLine(c, d, col, thick); ov_->AddLine(d, a, col, thick); }
        else dl_->AddQuad(a, b, c, d, col, thick);
    }
    void AddQuadFilled(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 col) {
        if (gpu_) { ov_->AddTriangleFilled(a, b, c, col); ov_->AddTriangleFilled(a, c, d, col); }
        else dl_->AddQuadFilled(a, b, c, d, col);
    }
    void AddPolyline(const ImVec2* pts, int n, ImU32 col, ImDrawFlags flags, float thick) {
        if (gpu_) ov_->AddPolyline(pts, n, col, thick, (flags & ImDrawFlags_Closed) != 0);
        else dl_->AddPolyline(pts, n, col, flags, thick);
    }
    // Text / images / anything not geometry → always ImGui (under the GPU overlay is
    // fine; text needs the glyph atlas which is Lot 12-3).
    template <class... A> void AddText(A&&... a) { dl_->AddText(std::forward<A>(a)...); }
    template <class... A> void AddImage(A&&... a) { dl_->AddImage(std::forward<A>(a)...); }
    template <class... A> void AddImageRounded(A&&... a) { dl_->AddImageRounded(std::forward<A>(a)...); }
    // Escape hatch for the rare ImGui-only call (clipping, etc.).
    ImDrawList* imgui() { return dl_; }

private:
    ImDrawList*  dl_;
    OverlayList* ov_;
    bool         gpu_;
};

} // namespace App
