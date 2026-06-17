#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Properties editor — shows/edits the ACTIVE object: name, transform (position
//  / rotation / scale), and per-part fill & stroke. In Edit Mode it also shows
//  the active point's Bézier handle type. Chrome is design-system-tokened; the
//  shape colours are user document data (raw ColorEdit).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace DST = DesignSystem;

// ── Blender-style property-row layout helpers ────────────────────────────────
// A row's LABEL takes the left 2/5 of the editor width, right-justified; the
// CONTROL takes the rest. Keeps every input aligned to a common column.
namespace {
constexpr float kLabelFrac = 0.4f;     // 2/5 of the row width for the label

// Draw a right-justified label in the left column, then position the cursor for
// the control in the right column. Returns the control width.
float PropLabel(const char* label) {
    float full  = ImGui::GetContentRegionAvail().x;
    float lblW  = full * kLabelFrac;
    float pad   = ImGui::GetStyle().ItemInnerSpacing.x;
    ImVec2 start = ImGui::GetCursorScreenPos();
    float textW = ImGui::CalcTextSize(label).x;
    // Right-justify within the label column.
    ImGui::SetCursorScreenPos(ImVec2(start.x + std::max(0.0f, lblW - textW - pad),
                                     start.y + ImGui::GetStyle().FramePadding.y));
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0, 0);
    ImGui::SetCursorScreenPos(ImVec2(start.x + lblW + pad, start.y));
    float ctrlW = std::max(40.0f, full - lblW - pad);
    return ctrlW;
}

// A radio group rendered as a column of checkboxes (one ticked) to the right of a
// right-justified title — Blender's enum-as-toggles look. `labels` is a NUL-
// separated list ("Butt\0Round\0..."). Returns true (and sets *value) on change.
bool PropRadioChecks(const char* title, const char* labels, int* value) {
    bool changed = false;
    ImGui::PushID(title);
    float full = ImGui::GetContentRegionAvail().x;
    float lblW = full * kLabelFrac;
    float pad  = ImGui::GetStyle().ItemInnerSpacing.x;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    // Title: right-justified in the label column, vertically centred on the rows.
    int count = 0; for (const char* p = labels; *p; p += std::strlen(p) + 1) ++count;
    float rowH = ImGui::GetFrameHeight();
    float titleY = origin.y + (count * rowH + (count - 1) * ImGui::GetStyle().ItemSpacing.y
                               - ImGui::GetTextLineHeight()) * 0.5f;
    float titleW = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + std::max(0.0f, lblW - titleW - pad), titleY));
    ImGui::TextUnformatted(title);
    // Checkboxes stacked in the right column.
    int idx = 0;
    for (const char* p = labels; *p; p += std::strlen(p) + 1, ++idx) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + lblW + pad,
                                         origin.y + idx * (rowH + ImGui::GetStyle().ItemSpacing.y)));
        bool on = (*value == idx);
        ImGui::PushID(idx);
        if (ImGui::Checkbox("##c", &on) && on) { *value = idx; changed = true; }
        ImGui::SameLine(0, pad);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(p);
        ImGui::PopID();
    }
    // Leave the layout cursor BELOW the whole block (the manual positioning above
    // doesn't advance ImGui's flow), so the next row starts in the right place.
    float blockH = count * rowH + (count - 1) * ImGui::GetStyle().ItemSpacing.y;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y));
    ImGui::Dummy(ImVec2(full, blockH));
    ImGui::PopID();
    return changed;
}

// A DragFloat that DISPLAYS `displayDp` decimals but keeps FULL precision in the
// stored value (NoRoundToFormat): typing 265.1587 isn't lost behind "265.159", and
// clicking to text-edit shows the exact value ImGui holds. Label is right-justified
// in the left column; the drag fills the right column.
bool PropDragFloat(const char* label, float* v, float speed, float lo, float hi,
                   int displayDp = 3) {
    float ctrlW = PropLabel(label);
    ImGui::PushID(label);
    char fmt[16]; std::snprintf(fmt, sizeof fmt, "%%.%df", displayDp);
    ImGui::SetNextItemWidth(ctrlW);
    bool ch = ImGui::DragFloat("##d", v, speed, lo, hi, fmt,
                               ImGuiSliderFlags_NoRoundToFormat);
    ImGui::PopID();
    return ch;
}

bool PropDragFloat2(const char* label, float v[2], float speed, float lo, float hi,
                    int displayDp = 3) {
    float ctrlW = PropLabel(label);
    ImGui::PushID(label);
    char fmt[16]; std::snprintf(fmt, sizeof fmt, "%%.%df", displayDp);
    ImGui::SetNextItemWidth(ctrlW);
    bool ch = ImGui::DragFloat2("##d", v, speed, lo, hi, fmt,
                                ImGuiSliderFlags_NoRoundToFormat);
    ImGui::PopID();
    return ch;
}
}  // namespace

// Redistribute the auto pylons of a power-line part between its PINNED Pylon
// marks, respecting a minimum centre-to-centre spacing (feature 4). The pinned
// marks stay; intermediate pylons reflow evenly so a mapper can fix a few pylon
// positions and let the rest spread out. Works in arc-length t-space.
static void RelayoutPartPylons(Renderer::Part& part, float minSpacing) {
    if (minSpacing <= 1e-4f) return;
    Renderer::Shape tmp; tmp.parts.push_back(part);
    bool closed = false;
    std::vector<Renderer::Vec2> poly =
        Renderer::Tessellator::OutlinePart(tmp, tmp.parts.front(), 1.0f, closed);
    if (poly.size() < 2) return;
    float total = 0.0f;
    size_t n = poly.size(), segCount = closed ? n : n - 1;
    for (size_t i = 0; i < segCount; ++i)
        total += std::hypot(poly[(i + 1) % n].x - poly[i].x, poly[(i + 1) % n].y - poly[i].y);
    if (total < 1e-4f) return;

    std::vector<float> pins;
    std::vector<Renderer::LineMark> keep;
    for (const Renderer::LineMark& m : part.marks) {
        if (m.kind == Renderer::LineMarkKind::Pylon) pins.push_back(std::clamp(m.t, 0.0f, 1.0f));
        else keep.push_back(m);
    }
    std::sort(pins.begin(), pins.end());
    std::vector<float> anchors; anchors.push_back(0.0f);
    for (float p : pins) anchors.push_back(p);
    anchors.push_back(1.0f);

    float step = minSpacing / total;
    std::vector<Renderer::LineMark> out = keep;
    auto addPylon = [&](float t) {
        Renderer::LineMark m; m.kind = Renderer::LineMarkKind::Pylon;
        m.t = std::clamp(t, 0.0f, 1.0f); m.size = 0.6f; out.push_back(m);
    };
    for (float p : pins) addPylon(p);
    for (size_t i = 0; i + 1 < anchors.size(); ++i) {
        float a = anchors[i], b = anchors[i + 1];
        float span = b - a;
        if (span <= step * 1.5f) continue;
        int count = std::max(1, (int)std::floor(span / step) - 1);
        for (int k = 1; k <= count; ++k) addPylon(a + span * (float)k / (float)(count + 1));
    }
    part.marks = std::move(out);
}

// Fill/stroke editor for one Part. `lineStyleLocked` greys out the stroke line
// styling (cap/join/align) — the IOF module manages those for fixed symbols.
// `capEditable` overrides the lock for the CAP control only (some ISOM symbols,
// e.g. cliffs, let the mapper still choose butt vs round ends).
static void RenderPartPaint(Renderer::Part& part, const char* tag, bool& dirty,
                            bool lineStyleLocked, bool capEditable) {
    ImGui::PushID(tag);
    bool fillOn = part.fill.enabled;
    if (ImGui::Checkbox("Fill", &fillOn)) { part.fill.enabled = fillOn; dirty = true; }
    if (part.fill.enabled) {
        float col[4] = { part.fill.color.r, part.fill.color.g,
                         part.fill.color.b, part.fill.color.a };
        ImGui::SameLine();
        if (ImGui::ColorEdit4("##fill", col,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            part.fill.color = { col[0], col[1], col[2], col[3] }; dirty = true;
        }
    }
    // How an OPEN curve's fill closes the gap between its two ends. Only meaningful
    // for a non-cyclic curve that is actually filled (plain fill or surface layers).
    if (part.IsCurveLike() && !part.path.closed &&
        (part.fill.enabled || !part.fillLayers.empty())) {
        static const char* kClose = "Follow curve\0Straight\0";
        int close = part.openFillStraight ? 1 : 0;
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("Fill close", &close, kClose)) {
            part.openFillStraight = (close == 1); dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open-curve fill: follow the curve (end handles / NURBS "
                              "weights) or close with a straight edge between the ends");
    }
    bool strokeOn = part.stroke.enabled;
    if (ImGui::Checkbox("Stroke", &strokeOn)) { part.stroke.enabled = strokeOn; dirty = true; }
    if (part.stroke.enabled) {
        float col[4] = { part.stroke.color.r, part.stroke.color.g,
                         part.stroke.color.b, part.stroke.color.a };
        ImGui::SameLine();
        if (ImGui::ColorEdit4("##stroke", col,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            part.stroke.color = { col[0], col[1], col[2], col[3] }; dirty = true;
        }
        float w = part.stroke.width;
        if (PropDragFloat("Width", &w, 0.25f, 0.1f, 200.0f, /*displayDp=*/3)) {
            part.stroke.width = w; dirty = true;
        }

        // ── Line style: cap / join / alignment (Blender-style toggle rows) ──
        // Editable for a classic project; read-only when a module (IOF) manages
        // its symbols' geometry.
        static const char* kCaps  = "Butt\0Round\0Square\0Taper\0";
        static const char* kJoins = "Miter\0Round\0Bevel\0";
        // Alignment labels depend on whether the path is CYCLIC: a closed contour
        // has an inside/outside (Center/Inner/Outer); an open curve has only two
        // sides of the line. Same enum underneath, so closing/opening the path maps
        // the chosen side to inner/outer and back.
        const bool cyclic = part.path.closed;
        static const char* kAlignsClosed = "Center\0Inner\0Outer\0";
        static const char* kAlignsOpen   = "Center\0Side A\0Side B\0";  // Inner→A, Outer→B
        const char* kAligns = cyclic ? kAlignsClosed : kAlignsOpen;
        int cap = (int)part.stroke.cap, join = (int)part.stroke.join,
            align = (int)part.stroke.align;
        // Cap stays editable when the symbol opts in (capEditable), even if the
        // rest of the line style is module-locked.
        ImGui::BeginDisabled(lineStyleLocked && !capEditable);
        if (PropRadioChecks("Cap", kCaps, &cap)) { part.stroke.cap = (Renderer::LineCap)cap; dirty = true; }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(lineStyleLocked);
        if (PropRadioChecks("Join", kJoins, &join)) { part.stroke.join = (Renderer::LineJoin)join; dirty = true; }
        if (PropRadioChecks("Align", kAligns, &align)) { part.stroke.align = (Renderer::StrokeAlign)align; dirty = true; }
        if (part.stroke.join == Renderer::LineJoin::Miter) {
            float ml = part.stroke.miterLimit;
            if (PropDragFloat("Miter limit", &ml, 0.1f, 1.0f, 20.0f)) {
                part.stroke.miterLimit = ml; dirty = true;
            }
        }

        // ── Decorator: GPU-instanced glyphs stamped along the curve (Core feature
        //    shared with the IOF module). Edge/side place the glyphs laterally.
        static const char* kDecor =
            "None\0Tags\0Tags both\0Dots\0Half dots\0Ties\0Pylons\0Slashes\0Vee\0"
            "Double line\0Railway\0Crosses\0Double slashes\0Double pylons\0"
            "Double ticks\0Edge lines\0Pair dots\0Pair slashes\0";
        int decor = (int)part.stroke.decor;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::Combo("Decorator", &decor, kDecor)) {
            part.stroke.decor = (Renderer::LineDecor)decor;
            part.stroke.decorSide = Renderer::DefaultSideForDecor(part.stroke.decor);
            dirty = true;
        }
        if (part.stroke.decor != Renderer::LineDecor::None) {
            if (PropDragFloat("Spacing",   &part.stroke.decorSpacing, 0.05f, 0.1f, 50.0f)) dirty = true;
            if (PropDragFloat("Size",      &part.stroke.decorSize,    0.02f, 0.02f, 20.0f)) dirty = true;
            if (PropDragFloat("Angle",     &part.stroke.decorAngleDeg, 1.0f, -180.0f, 180.0f)) dirty = true;
            if (PropDragFloat("Thickness", &part.stroke.decorThickness, 0.01f, 0.0f, 10.0f)) dirty = true;
            bool dc = part.stroke.decorCentered;
            if (ImGui::Checkbox("Phase centred", &dc)) { part.stroke.decorCentered = dc; dirty = true; }
            static const char* kEdge = "Construction\0Inner edge\0Outer edge\0";
            static const char* kSide = "One side\0Both sides\0Centred\0Alternating\0";
            int edge = (int)part.stroke.decorEdge, side = (int)part.stroke.decorSide;
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("Edge", &edge, kEdge)) { part.stroke.decorEdge = (Renderer::DecorEdge)edge; dirty = true; }
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("Side", &side, kSide)) { part.stroke.decorSide = (Renderer::DecorSide)side; dirty = true; }
        }

        ImGui::EndDisabled();
        if (lineStyleLocked && ImGui::IsItemHovered())
            ImGui::SetTooltip("Managed by the active module");
    }

    // ── Surface fill layers (pattern stack clipped to the contour) ──
    // A surface can overlay several patterns (ISOM combinable screens). Each layer
    // is editable; the offset is draggable to move the pattern within the surface.
    // Module-managed symbols keep these read-only (like the line style).
    ImGui::Spacing();
    ImGui::BeginDisabled(lineStyleLocked);
    if (ImGui::SmallButton("+ Fill layer")) {
        Renderer::FillLayer fl;                 // default: solid black 50%
        fl.pattern = Renderer::FillPattern::Dots; fl.opacity = 1.0f;
        fl.color = { 0,0,0,1 }; fl.spacing = 1.5f; fl.size = 0.3f;
        part.fillLayers.push_back(fl); dirty = true;
    }
    static const char* kPat = "Solid %\0Dots\0Lines\0Triangles\0Random dots\0Grid\0Cross-hatch\0";
    for (size_t i = 0; i < part.fillLayers.size(); ++i) {
        Renderer::FillLayer& fl = part.fillLayers[i];
        ImGui::PushID((int)(1000 + i));
        char hdr[48]; std::snprintf(hdr, sizeof hdr, "Layer %zu", i + 1);
        bool open = ImGui::CollapsingHeader(hdr, ImGuiTreeNodeFlags_DefaultOpen);
        if (open) {
            bool en = fl.enabled;
            if (ImGui::Checkbox("Enabled", &en)) { fl.enabled = en; dirty = true; }
            ImGui::SameLine();
            float fc[4] = { fl.color.r, fl.color.g, fl.color.b, fl.color.a };
            if (ImGui::ColorEdit4("##c", fc, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
                { fl.color = { fc[0],fc[1],fc[2],fc[3] }; dirty = true; }
            int pat = (int)fl.pattern;
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("Pattern", &pat, kPat)) { fl.pattern = (Renderer::FillPattern)pat; dirty = true; }
            if (fl.pattern == Renderer::FillPattern::Solid) {
                if (PropDragFloat("Opacity", &fl.opacity, 0.005f, 0.0f, 1.0f)) dirty = true;
            } else {
                if (PropDragFloat("Spacing", &fl.spacing, 0.05f, 0.1f, 50.0f)) dirty = true;
                if (PropDragFloat("Size",    &fl.size,    0.02f, 0.02f, 20.0f)) dirty = true;
                // Angle orients the whole motif (the dot/triangle lattice too).
                if (PropDragFloat("Angle", &fl.angleDeg, 1.0f, -180.0f, 180.0f)) dirty = true;
                // The Offset stays editable even for module-managed (locked) symbols:
                // a fixed IOF screen pattern still needs the user to nudge its phase to
                // align the motif on the map. Drop out of the section-wide BeginDisabled
                // for just this control, then restore it.
                if (lineStyleLocked) ImGui::EndDisabled();
                float off[2] = { fl.offset.x, fl.offset.y };
                if (PropDragFloat2("Offset", off, 0.05f, -100.0f, 100.0f))
                    { fl.offset = { off[0], off[1] }; dirty = true; }
                if (lineStyleLocked) ImGui::BeginDisabled(true);
                // Per-layer cut edge relative to the stroked contour.
                static const char* kClip = "Construction\0Inner side\0Outer side\0";
                int clip = (int)fl.clip;
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::Combo("Fill clip", &clip, kClip)) {
                    fl.clip = (Renderer::FillClip)clip; dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Cut this layer at the construction line, or the "
                                      "inner / outer edge of the stroke");
                // What the pattern lattice is pinned to. Object origin keeps the motif
                // glued to the shape (it follows a move); Document origin pins it to
                // 0,0 so moving the shape slides it over a static field. The Offset
                // above is applied relative to whichever anchor is chosen.
                static const char* kAnchor = "Object origin\0Document origin\0";
                int anchor = (int)fl.anchor;
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::Combo("Anchor", &anchor, kAnchor)) {
                    fl.anchor = (Renderer::FillAnchor)anchor; dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Pin the pattern lattice to the object (follows "
                                      "the shape) or to the document origin (0,0)");
            }
            if (ImGui::SmallButton("Up") && i > 0) {
                std::swap(part.fillLayers[i], part.fillLayers[i-1]); dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") && i + 1 < part.fillLayers.size()) {
                std::swap(part.fillLayers[i], part.fillLayers[i+1]); dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                part.fillLayers.erase(part.fillLayers.begin() + (long)i); dirty = true;
                ImGui::PopID(); break;
            }
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();

    // Line marks themselves are edited as SUB-OBJECTS (select one in the Viewport /
    // Outliner → its own Properties pane). The only mark control that belongs on the
    // OBJECT is the pylon relayout (it re-spreads the whole line's pylons).
    if (part.stroke.enabled && !part.IsParametric()) {
        bool hasPylon = false;
        for (const Renderer::LineMark& m : part.marks)
            if (m.kind == Renderer::LineMarkKind::Pylon) { hasPylon = true; break; }
        if (hasPylon) {
            ImGui::Spacing();
            static float minSpacing = 6.0f;
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Pylon min spacing", &minSpacing, 0.1f, 0.5f, 100.0f, "%.1f");
            if (ImGui::SmallButton("Relayout pylons")) {
                RelayoutPartPylons(part, minSpacing); dirty = true;
            }
        }
    }

    ImGui::PopID();
}

// Per-mark Properties pane: shown when a line mark is the active selection. Edits
// the mark's own fields (a quasi-object), NOT the host object's. Returns true if it
// handled the render (an active mark exists).
bool Application::RenderMarkProperties() {
    auto& ds  = DST::DesignSystem::Instance();
    auto& doc = project_.document;
    if (!doc.HasMarkSelection()) return false;
    const Renderer::MarkRef& ref = doc.ActiveMark();
    Renderer::Shape* s = doc.FindShape(ref.shape);
    if (!s || ref.part < 0 || ref.part >= (int)s->parts.size()) return false;
    Renderer::Part& part = s->parts[(size_t)ref.part];
    if (ref.index < 0 || ref.index >= (int)part.marks.size()) return false;
    Renderer::LineMark& m = part.marks[(size_t)ref.index];

    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DST::Tok::S_Color_Text_Default));
    bool dirty = false;
    const char* kindName =
        m.kind == Renderer::LineMarkKind::SlopeTick ? "Slope tick"
      : m.kind == Renderer::LineMarkKind::Crossing  ? "Crossing point"
      : m.kind == Renderer::LineMarkKind::Bridge    ? "Bridge"
      : m.kind == Renderer::LineMarkKind::DashAnchor? "Dash anchor"
      : "Pylon";
    ImGui::Text("%s", kindName);
    ImGui::TextDisabled("on %s", s->name.empty() ? "object" : s->name.c_str());
    ImGui::Separator();

    const int nSel = (int)doc.MarkSelection().size();
    if (nSel > 1) ImGui::TextDisabled("%d marks selected", nSel);

    // ── DashAnchor: a phase pin with no geometry — just a mode + pin status. ──
    if (m.kind == Renderer::LineMarkKind::DashAnchor) {
        if (m.nodeAnchor < 0 && PropDragFloat("Position", &m.t, 0.002f, 0.0f, 1.0f)) dirty = true;
        if (m.nodeAnchor >= 0) ImGui::TextDisabled("Pinned to control point %d", m.nodeAnchor);
        int mode = m.side >= 0 ? 0 : 1;
        static const char* kMode = "Centre a dash\0Centre a gap\0";
        if (PropRadioChecks("Force", kMode, &mode)) { m.side = mode == 0 ? +1 : -1; dirty = true; }
        if (dirty) project_.dirty = true;
        ImGui::PopStyleColor();
        return true;
    }

    if (PropDragFloat("Position", &m.t, 0.002f, 0.0f, 1.0f)) dirty = true;
    if (m.kind == Renderer::LineMarkKind::SlopeTick) {
        int side = m.side >= 0 ? 0 : 1;
        static const char* kSide = "Left\0Right\0";
        if (PropRadioChecks("Side", kSide, &side)) { m.side = side == 0 ? +1 : -1; dirty = true; }
    }
    if (m.kind == Renderer::LineMarkKind::Crossing ||
        m.kind == Renderer::LineMarkKind::Bridge) {
        if (PropDragFloat("Gap", &m.gap, 0.05f, 0.05f, 50.0f)) dirty = true;
    }
    if (PropDragFloat("Size", &m.size, 0.02f, 0.05f, 20.0f)) dirty = true;
    if (PropDragFloat("Thickness", &m.thickness, 0.01f, 0.0f, 10.0f)) dirty = true;
    if (m.kind == Renderer::LineMarkKind::SlopeTick) {
        bool om = m.outsideMeasure;
        if (ImGui::Checkbox("Outside measure", &om)) { m.outsideMeasure = om; dirty = true; }
    }
    if (m.kind == Renderer::LineMarkKind::Pylon) {
        bool sq = m.square;
        if (ImGui::Checkbox("Box (square pylon)", &sq)) {
            m.square = sq; if (sq && m.gap < 1e-3f) m.gap = 0.8f; dirty = true;
        }
        if (m.square && PropDragFloat("Box side", &m.gap, 0.02f, 0.1f, 10.0f)) dirty = true;
    }

    if (dirty) project_.dirty = true;
    ImGui::PopStyleColor();
    return true;
}

void Application::RenderProperties() {
    // A selected line mark is a quasi-object — show ITS properties, not the host's.
    if (RenderMarkProperties()) return;

    auto& ds  = DST::DesignSystem::Instance();
    auto& doc = project_.document;
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DST::Tok::S_Color_Text_Default));

    Renderer::Shape* s = doc.ActiveShape();
    if (!s) {
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DST::Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("No active object.");
        ImGui::TextUnformatted("(select an object in the Viewport or Outliner)");
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        return;
    }
    bool dirty = false;

    // ── Object name ──────────────────────────────────────────────────────────
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", s->name.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##objname", buf, sizeof(buf))) { s->name = buf; dirty = true; }
    }

    // ── Object type (Mesh / Curve) ───────────────────────────────────────────
    // Read-only here: family is set by Add / "Convert To"; the spline kind by
    // "Set Spline Type" (Edit Mode). Shown so the user knows why a Join may be
    // greyed out (only same-family objects can join). "(mixed)" if a joined
    // object holds parts of several families.
    {
        bool mixed = false;
        Renderer::PartType t0 = s->parts.empty() ? Renderer::PartType::Mesh
                                                  : s->parts.front().type;
        for (const Renderer::Part& p : s->parts) if (p.type != t0) { mixed = true; break; }
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DST::Tok::S_Color_Text_Subtle));
        if (t0 == Renderer::PartType::Curve && !mixed && !s->parts.empty())
            ImGui::Text("Type: Curve (%s)", Renderer::SplineTypeName(s->parts.front().spline));
        else
            ImGui::Text("Type: %s%s", Renderer::PartTypeName(t0), mixed ? " (mixed)" : "");
        ImGui::PopStyleColor();
    }

    // ── Page (artboard) the object belongs to ────────────────────────────────
    // Changing it re-parents the object to that page, keeping its visual
    // position (page-relative coords). Pick the active object's id BEFORE the
    // move so we can re-fetch it after (the move invalidates `s`).
    {
        const uint64_t sid = s->id;
        int cur = doc.ArtboardOfShape(sid);
        const bool loose = doc.IsLooseShape(sid);
        if (loose) {
            // Page-less object (in a collection with no page ancestor). It lives
            // in document space, not bound to any page. Show "(none)" — read-only;
            // re-binding happens by moving it back under a page in the Outliner.
            ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(DST::Tok::S_Color_Text_Subtle));
            ImGui::Text("Page: (none — in a page-less collection)");
            ImGui::PopStyleColor();
        } else if (cur >= 0 && doc.artboards.size() > 1) {
            std::string preview = doc.artboards[(size_t)cur].name;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("Page", preview.c_str())) {
                for (int i = 0; i < (int)doc.artboards.size(); ++i) {
                    bool sel = (i == cur);
                    if (ImGui::Selectable(doc.artboards[(size_t)i].name.c_str(), sel) &&
                        i != cur) {
                        doc.MoveShapeToArtboard(sid, i, /*keepWorldPos=*/true);
                        // Belongs to the new page → reset to that page's root.
                        if (Renderer::Shape* m = doc.FindShape(sid)) m->collectionId = 0;
                        MarkUndoLabel("Move to page");
                        dirty = true;
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        // `s` may now point into a different vector; re-fetch defensively.
        s = doc.FindShape(sid);
        if (!s) { ImGui::PopStyleColor(); return; }
    }

    // ── Transform ────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        float tr[2] = { s->transform.translate.x, s->transform.translate.y };
        if (PropDragFloat2("Position", tr, 0.5f, 0.0f, 0.0f, 3)) {
            s->transform.translate = { tr[0], tr[1] }; dirty = true;
        }
        float deg = s->transform.rotate * 57.2957795f;
        if (PropDragFloat("Rotation", &deg, 0.5f, -3600.0f, 3600.0f, 3)) {
            s->transform.rotate = deg * 0.01745329f; dirty = true;
        }
        float sc[2] = { s->transform.scale.x, s->transform.scale.y };
        if (PropDragFloat2("Scale", sc, 0.01f, -1000.0f, 1000.0f, 3)) {
            s->transform.scale = { sc[0], sc[1] }; dirty = true;
        }
        // Per-object transform locks. A module that manages them itself (IOF:
        // fixed-size / north-oriented symbols) shows these read-only.
        const bool forced = activeCapabilities_.lockTransformsForced;
        ImGui::BeginDisabled(forced);
        bool ls = s->lockScale, lr = s->lockRotation;
        if (ImGui::Checkbox("Lock scale", &ls))    { s->lockScale = ls; dirty = true; }
        ImGui::SameLine();
        if (ImGui::Checkbox("Lock rotation", &lr)) { s->lockRotation = lr; dirty = true; }
        ImGui::EndDisabled();
        if (forced && ImGui::IsItemHovered())
            ImGui::SetTooltip("Managed by the active module");
    }

    // ── Edit-mode: active point handle type ──────────────────────────────────
    if (editorMode_ == EditorMode::Edit && doc.HasVertSelection()) {
        if (ImGui::CollapsingHeader("Active Point", ImGuiTreeNodeFlags_DefaultOpen)) {
            const Renderer::VertRef& v = doc.ActiveVert();
            Renderer::Shape* vs = doc.FindShape(v.shape);
            if (vs && v.part < (int)vs->parts.size() &&
                v.node < (int)vs->parts[(size_t)v.part].path.nodes.size()) {
                Renderer::Part& vp = vs->parts[(size_t)v.part];
                Renderer::Node& n = vp.path.nodes[(size_t)v.node];
                // Order MUST match the HandleMode enum values (0..4).
                static const char* kModes[] = { "Free", "Aligned", "Mirrored",
                                                "Vector", "Aligned + Mirrored" };
                int mode = (int)n.mode;
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::Combo("Handle Type", &mode, kModes, 5)) {
                    Action_SetHandleType((Renderer::HandleMode)mode);  // applies to selection
                    dirty = true;
                }
                // NURBS control-point weight (rational): >1 pulls the curve toward
                // the point; the exact-circle hull uses √2/2 on the corner controls.
                if (vp.spline == Renderer::SplineType::Nurbs) {
                    float wgt = n.weight;
                    ImGui::SetNextItemWidth(140.0f);
                    if (PropDragFloat("Weight", &wgt, 0.01f, 0.01f, 100.0f)) {
                        n.weight = std::max(0.01f, wgt); dirty = true;
                    }
                }
            }
        }
    }

    // ── Curve options (cyclic, NURBS order) ──────────────────────────────────
    // Operate on the active object's curve part(s). Single-part is the common
    // case; with several parts we drive them together off the first curve part.
    {
        Renderer::Part* cp = nullptr;
        for (Renderer::Part& p : s->parts) if (p.IsCurveLike()) { cp = &p; break; }
        if (cp && ImGui::CollapsingHeader("Curve", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool cyclic = cp->path.closed;
            if (ImGui::Checkbox("Cyclic", &cyclic)) {
                for (Renderer::Part& p : s->parts)
                    if (p.IsCurveLike()) p.path.closed = cyclic;
                MarkUndoLabel("Toggle cyclic");
                dirty = true;
            }
            if (cp->spline == Renderer::SplineType::Nurbs) {
                // Order U capped at the control-point count (a B-spline needs
                // order ≤ control points). Min 2 (straight control polygon).
                int maxOrder = std::max(2, (int)cp->path.nodes.size());
                int order = std::clamp(cp->orderU, 2, maxOrder);
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::SliderInt("Order U", &order, 2, maxOrder)) {
                    for (Renderer::Part& p : s->parts)
                        if (p.IsCurveLike() && p.spline == Renderer::SplineType::Nurbs)
                            p.orderU = std::clamp(order, 2,
                                std::max(2, (int)p.path.nodes.size()));
                    MarkUndoLabel("Set NURBS order");
                    dirty = true;
                }
                // Knot-vector options. Bezier U matters in BOTH modes (it makes the
                // polygon rational-Bézier segments — exact circles/arcs even cyclic).
                // Endpoint U only applies to OPEN curves (a cyclic NURBS is periodic).
                bool endp = cp->nurbsEndpoint, bez = cp->nurbsBezier;
                ImGui::BeginDisabled(cp->path.closed);
                if (ImGui::Checkbox("Endpoint U", &endp)) {
                    for (Renderer::Part& p : s->parts)
                        if (p.IsCurveLike() && p.spline == Renderer::SplineType::Nurbs)
                            p.nurbsEndpoint = endp;
                    MarkUndoLabel("NURBS endpoint U"); dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Make the curve meet its first/last control point\n(clamped knots — needed for arcs / half-circles)");
                ImGui::EndDisabled();
                if (ImGui::Checkbox("Bezier U", &bez)) {
                    for (Renderer::Part& p : s->parts)
                        if (p.IsCurveLike() && p.spline == Renderer::SplineType::Nurbs)
                            p.nurbsBezier = bez;
                    MarkUndoLabel("NURBS bezier U"); dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Treat the control polygon as rational Bezier segments\n(exact circles / arcs from a weighted square / triangle hull)");
            }
        }
    }

    // ── Fill & Stroke (per part) ─────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Paint", ImGuiTreeNodeFlags_DefaultOpen)) {
        // A module-managed symbol (IOF: fixed-size ISOM glyph) keeps its line
        // style read-only — the geometry, like the symbol itself, is not editable.
        const bool lineStyleLocked =
            activeCapabilities_.lockTransformsForced && s->isomCode != 0;
        const bool capEditable = s->allowCapEdit;
        if (s->parts.size() <= 1) {
            if (!s->parts.empty()) RenderPartPaint(s->parts[0], "p0", dirty, lineStyleLocked, capEditable);
        } else {
            for (size_t i = 0; i < s->parts.size(); ++i) {
                char hdr[32]; std::snprintf(hdr, sizeof(hdr), "Part %zu", i + 1);
                char id[8];   std::snprintf(id, sizeof(id), "p%zu", i);
                if (ImGui::CollapsingHeader(hdr, ImGuiTreeNodeFlags_DefaultOpen))
                    RenderPartPaint(s->parts[i], id, dirty, lineStyleLocked, capEditable);
            }
        }
    }

    if (dirty) project_.dirty = true;
    ImGui::PopStyleColor();
}

} // namespace App
