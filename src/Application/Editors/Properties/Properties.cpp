#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Renderer/Tessellation/Tessellator.h>
#include <UI/Widgets/Panel.h>        // UI::BeginPanel/EndPanel (grouped expandable)
#include <UI/Widgets/Checkbox.h>     // UI::Checkbox (token-styled coche)
#include <UI/Widgets/Dropdown.h>     // UI::Dropdown (token-styled property dropdown)
#include <UI/Widgets/DragValue.h>    // UI::DragValue (Blender-style numeric field)
#include <UI/Widgets/ButtonGroup.h>  // UI::ButtonGroup (linked single-toggle enums)
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
namespace DSn = DesignSystem;
constexpr float kLabelFrac = 0.4f;     // 2/5 of the row width for the label

ImVec4 PCol(DSn::Tok t) { return DSn::DesignSystem::Instance().GetColor(t); }
float  PFlt(DSn::Tok t) { return DSn::DesignSystem::Instance().GetFloat(t); }

float LabelColW() { return ImGui::GetContentRegionAvail().x * kLabelFrac; }
float CtrlColX0() {                       // control-column left, in cursor-X space
    return ImGui::GetCursorPosX() + LabelColW() + ImGui::GetStyle().ItemInnerSpacing.x;
}

// Extra vertical space BEFORE a group, so a group reads as distinct from the
// plain item rows around it (token-driven, larger than ItemSpacing).
void PropGroupGap() {
    ImGui::Dummy(ImVec2(0.0f, PFlt(DSn::Tok::C_PropertyGroup_Gap) *
                              DSn::DesignSystem::Instance().GetGlobalScale()));
}

// Build a DragValueConfig from a unit + display decimals (auto Shift/Ctrl/Alt).
UI::DragValueConfig DVCfg(const char* id, float speed, float lo, float hi,
                          int displayDp, const char* unit) {
    UI::DragValueConfig cfg;
    cfg.id = id; cfg.speed = speed; cfg.min = lo; cfg.max = hi;
    cfg.displayDecimals = displayDp; cfg.unit = unit ? unit : "";
    return cfg;
}

// Forward decl: PropVec2Group (below) builds rows out of PropDragFloat.
bool PropDragFloat(const char* label, float* v, float speed, float lo, float hi,
                   int displayDp, const char* unit);

// The height of one property row = one ui-unit (matches DragValue / Checkbox /
// the padlock), used to vertically centre labels on the control.
float RowH() {
    return PFlt(DSn::Tok::S_Size_ControlHeight) *
           DSn::DesignSystem::Instance().GetGlobalScale();
}

// Draw a right-justified label in the left column, vertically CENTRED on the
// ui-unit row, then position the cursor at the control column. Returns the
// control width.
float PropLabel(const char* label) {
    float full  = ImGui::GetContentRegionAvail().x;
    float lblW  = full * kLabelFrac;
    float pad   = ImGui::GetStyle().ItemInnerSpacing.x;
    ImVec2 start = ImGui::GetCursorScreenPos();
    float textW = ImGui::CalcTextSize(label).x;
    float cy    = start.y + std::max(0.0f, (RowH() - ImGui::GetTextLineHeight()) * 0.5f);
    // Right-justify within the label column, centred vertically on the row.
    ImGui::SetCursorScreenPos(ImVec2(start.x + std::max(0.0f, lblW - textW - pad), cy));
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0, 0);
    ImGui::SetCursorScreenPos(ImVec2(start.x + lblW + pad, start.y));
    float ctrlW = std::max(40.0f, full - lblW - pad);
    return ctrlW;
}

// A square padlock toggle button drawn on the draw list (no SVG dependency):
// a body rectangle + a shackle arc. Locked = filled accent + closed shackle;
// unlocked = outline in the subtle text colour + open shackle. The slot is
// frame-height square. Returns true if toggled (and flips *locked).
bool PropPadlock(const char* id, bool* locked) {
    const float h = RowH();                 // ui-unit slot → matches the input row
    ImGui::PushID(id);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##lk", ImVec2(h, h));
    const bool hovered = ImGui::IsItemHovered();
    if (clicked) *locked = !*locked;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float gs = DSn::DesignSystem::Instance().GetGlobalScale();
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(
        *locked      ? PCol(DSn::Tok::S_Color_Accent_Default)
        : hovered    ? PCol(DSn::Tok::S_Color_Text_Default)
                     : PCol(DSn::Tok::S_Color_Text_Subtle));
    // Geometry centred in the square slot.
    const float bw = std::floor(h * 0.42f);          // body width
    const float bh = std::floor(h * 0.34f);          // body height
    const ImVec2 c(p0.x + h * 0.5f, p0.y + h * 0.5f);
    const ImVec2 bMin(c.x - bw * 0.5f, c.y - bh * 0.25f);
    const ImVec2 bMax(c.x + bw * 0.5f, c.y + bh * 0.75f);
    const float r = std::max(1.0f, 2.0f * gs);
    const float th = std::max(1.0f, std::floor(1.5f * gs));
    // Shackle: a half-ring above the body. When unlocked it tilts open (shorter
    // right leg) — a quick read of "open" without a second glyph.
    const float sr = bw * 0.34f;                       // shackle radius
    const ImVec2 sc(c.x, bMin.y);                      // shackle centre = body top
    constexpr float kPi = 3.14159265358979f;
    if (*locked) {
        dl->PathArcTo(sc, sr, kPi, kPi * 2.0f, 12);
        dl->PathStroke(col, ImDrawFlags_None, th);
        // Closed legs down to the body.
        dl->AddLine(ImVec2(sc.x - sr, sc.y), ImVec2(sc.x - sr, bMin.y), col, th);
        dl->AddLine(ImVec2(sc.x + sr, sc.y), ImVec2(sc.x + sr, bMin.y), col, th);
        dl->AddRectFilled(bMin, bMax, col, r);
    } else {
        dl->PathArcTo(sc, sr, kPi, kPi * 2.05f, 12); // slightly open
        dl->PathStroke(col, ImDrawFlags_None, th);
        dl->AddLine(ImVec2(sc.x - sr, sc.y), ImVec2(sc.x - sr, bMin.y), col, th);
        dl->AddRect(bMin, bMax, col, r, 0, th);
    }
    ImGui::PopID();
    return clicked;
}

// One axis row inside a Blender-style vector field: a small axis label ("X"/"Y")
// in the label column (right-justified, like a normal row label), then a drag
// input filling the control column MINUS a trailing padlock slot. Returns true
// if the value changed; *locked is toggled by the padlock (its own click never
// counts as a value change).
bool PropAxisRow(const char* axis, float* v, bool* locked, float speed,
                 float lo, float hi, int displayDp, const char* unit) {
    float ctrlW = PropLabel(axis);
    const float lockW = RowH() + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::PushID(axis);
    ImGui::BeginDisabled(*locked);
    UI::DragValueConfig cfg = DVCfg("##d", speed, lo, hi, displayDp, unit);
    cfg.width = std::max(40.0f, ctrlW - lockW);
    bool ch = UI::DragValue(cfg, v);
    ImGui::EndDisabled();
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    PropPadlock("##lk", locked);
    ImGui::PopID();
    return ch;
}

// Draw the group title on the SAME line as the first axis row, RIGHT-justified so
// it ends just to the left of the first axis label (X) with a small gap, and
// vertically centred on the row. Does NOT advance the line — the axis row that
// follows lays out from the same origin. `firstAxis` is the label of the first
// row ("X") so we can stop just before it.
void PropGroupTitleInline(const char* title, const char* firstAxis) {
    ImVec2 start = ImGui::GetCursorScreenPos();
    float lblW = LabelColW();
    float pad  = ImGui::GetStyle().ItemInnerSpacing.x;
    // The axis label is right-justified in [0, lblW]; its left edge:
    float axisW = ImGui::CalcTextSize(firstAxis).x;
    float axisX = start.x + std::max(0.0f, lblW - axisW - pad);
    // Title ends a small gap before the axis label, right-justified.
    float titleW = ImGui::CalcTextSize(title).x;
    float titleX = axisX - pad - titleW;
    float cy = start.y + std::max(0.0f, (RowH() - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(std::max(start.x, titleX), cy));
    ImGui::TextUnformatted(title);
    ImGui::SameLine(0, 0);
    ImGui::SetCursorScreenPos(start);   // restore: the axis row lays out from here
}

// Blender-style vector field: a group gap, then row 1 = "title · X · [input]" and
// row 2 = "Y · [input]" (inputs aligned, independent padlocks). Returns true if
// any axis value changed (padlock toggles don't count).
bool PropVec2Blender(const char* title, float v[2], bool* lockX, bool* lockY,
                     float speed, float lo, float hi, int displayDp,
                     const char* unit = "") {
    ImGui::PushID(title);
    PropGroupGap();
    PropGroupTitleInline(title, "X");             // on the same line as X below
    bool ch = false;
    ch |= PropAxisRow("X", &v[0], lockX, speed, lo, hi, displayDp, unit);
    ch |= PropAxisRow("Y", &v[1], lockY, speed, lo, hi, displayDp, unit);
    ImGui::PopID();
    return ch;
}

// Vector field WITHOUT per-axis padlocks (e.g. a pattern Offset): same inline-title
// + X/Y rows layout, but the input fills the whole control column.
bool PropVec2Group(const char* title, float v[2], float speed, float lo, float hi,
                   int displayDp, const char* unit = "") {
    ImGui::PushID(title);
    PropGroupGap();
    PropGroupTitleInline(title, "X");
    bool ch = false;
    ch |= PropDragFloat("X", &v[0], speed, lo, hi, displayDp, unit);
    ch |= PropDragFloat("Y", &v[1], speed, lo, hi, displayDp, unit);
    ImGui::PopID();
    return ch;
}

// A single DragValue WITH a trailing padlock (e.g. Rotation).
bool PropDragFloatLocked(const char* label, float* v, bool* locked, float speed,
                         float lo, float hi, int displayDp, const char* unit = "") {
    float ctrlW = PropLabel(label);
    const float lockW = RowH() + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::PushID(label);
    ImGui::BeginDisabled(*locked);
    UI::DragValueConfig cfg = DVCfg("##d", speed, lo, hi, displayDp, unit);
    cfg.width = std::max(40.0f, ctrlW - lockW);
    bool ch = UI::DragValue(cfg, v);
    ImGui::EndDisabled();
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    PropPadlock("##lk", locked);
    ImGui::PopID();
    return ch;
}

// A checkbox aligned in the CONTROL column (label to the right of the box).
bool PropCheckRow(const char* id, const char* label, bool* v) {
    ImGui::SetCursorPosX(CtrlColX0());
    return UI::Checkbox(id, label, v);
}

// Push the cursor to the start of the control column (for label-less buttons).
void PropControlColumn() { ImGui::SetCursorPosX(CtrlColX0()); }

// A colour swatch row: a right-justified label in the left column and a FULL-WIDTH
// swatch button filling the control column at one ui-unit tall. Clicking opens a
// colour picker popup. (A ColorEdit4 with NoInputs collapses to a frame-height
// square, which is why we draw an explicitly-sized ColorButton instead.)
bool PropColorRow(const char* id, const char* label, Renderer::Color* col,
                  ImGuiColorEditFlags flags) {
    float ctrlW = PropLabel(label);
    ImGui::PushID(id);
    const float gs = DSn::DesignSystem::Instance().GetGlobalScale();
    const float rowH = PFlt(DSn::Tok::S_Size_ControlHeight) * gs;
    float c[4] = { col->r, col->g, col->b, col->a };
    bool ch = false;

    // Full-width swatch button (alpha shown as a checkerboard). Opens the picker.
    ImGuiColorEditFlags btnFlags = ImGuiColorEditFlags_AlphaPreviewHalf |
                                   ImGuiColorEditFlags_NoTooltip;
    if (ImGui::ColorButton("##sw", ImVec4(c[0], c[1], c[2], c[3]), btnFlags,
                           ImVec2(ctrlW, rowH)))
        ImGui::OpenPopup("##pick");
    if (ImGui::BeginPopup("##pick")) {
        ImGuiColorEditFlags pickFlags = ImGuiColorEditFlags_AlphaBar |
                                        (flags & ImGuiColorEditFlags_NoAlpha);
        if (ImGui::ColorPicker4("##p", c, pickFlags)) ch = true;
        ImGui::EndPopup();
    }
    if (ch) *col = { c[0], c[1], c[2], c[3] };
    ImGui::PopID();
    return ch;
}

// Blender-style enum dropdown in a property row: a right-justified label in the
// left column and a token-styled UI::Dropdown filling the right column. `labels`
// is a NUL-separated list ("Follow curve\0Straight\0") so call sites read like
// the ImGui::Combo they replace. Returns true (and writes *value) on a pick.
bool PropDropdown(const char* label, const char* labels, int* value) {
    std::vector<std::string> opts;
    for (const char* p = labels; *p; p += std::strlen(p) + 1) opts.emplace_back(p);
    int cur = std::clamp(*value, 0, (int)opts.size() - 1);

    PropLabel(label);                                // positions the control cursor
    UI::DropdownConfig cfg;
    cfg.id = "##dd";                                 // unique via PushID(label)
    cfg.triggerLabel = opts.empty() ? "" : opts[(size_t)cur];
    cfg.selectedIndex = cur;
    for (const std::string& o : opts) {
        UI::DropdownItem it; it.label = o; cfg.items.push_back(it);
    }
    // The dropdown sizes its trigger to its content and draws at the cursor the
    // label column left; the menu left-aligns under the trigger.
    ImGui::PushID(label);
    UI::DropdownResult r = UI::Dropdown(cfg);
    ImGui::PopID();
    if (r.changed) { *value = r.selected; return true; }
    return false;
}

// A horizontal linked single-toggle button group in a property row (replaces a
// small enum dropdown): label + a UI::ButtonGroup of one cell per option, the
// selected one highlighted. `labels` is a NUL-separated list. Returns true (and
// writes *value) on a pick.
bool PropButtonGroup(const char* label, const char* labels, int* value) {
    std::vector<std::string> opts;
    for (const char* p = labels; *p; p += std::strlen(p) + 1) opts.emplace_back(p);
    if (opts.empty()) return false;
    int cur = std::clamp(*value, 0, (int)opts.size() - 1);

    float ctrlW = PropLabel(label);
    const float gs = DSn::DesignSystem::Instance().GetGlobalScale();
    const float rowH = PFlt(DSn::Tok::S_Size_ControlHeight) * gs;
    const float cellW = std::max(24.0f, ctrlW / (float)opts.size());

    ImGui::PushID(label);
    UI::ButtonGroup bg("##bg");
    std::vector<float> cols((size_t)opts.size(), cellW);
    bg.SetGrid(cols, { rowH });
    for (int i = 0; i < (int)opts.size(); ++i) {
        UI::ButtonGroup::Cell c{};
        c.label = opts[(size_t)i]; c.col = i; c.row = 0;
        c.selected = (i == cur);
        bg.AddCell(c);
    }
    UI::ButtonGroup::Result r = bg.Render();
    ImGui::PopID();
    if (r.clickedIndex >= 0 && r.clickedIndex != cur) {
        *value = r.clickedIndex; return true;
    }
    return false;
}

// A radio group rendered as a column of checkboxes (one ticked) — Blender's
// enum-as-toggles look. The group title sits at the TOP, left of the FIRST
// checkbox (not vertically centred), preceded by the larger inter-group gap.
// `labels` is a NUL-separated list. Returns true on change.
bool PropRadioChecks(const char* title, const char* labels, int* value) {
    bool changed = false;
    PropGroupGap();
    ImGui::PushID(title);
    float full = ImGui::GetContentRegionAvail().x;
    float lblW = full * kLabelFrac;
    float pad  = ImGui::GetStyle().ItemInnerSpacing.x;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    int count = 0; for (const char* p = labels; *p; p += std::strlen(p) + 1) ++count;
    const float rowH = RowH();                            // ui-unit rows
    const float stride = rowH + ImGui::GetStyle().ItemSpacing.y;
    const float textDY = std::max(0.0f, (rowH - ImGui::GetTextLineHeight()) * 0.5f);
    // Title: vertically centred on the FIRST row, right-justified in the label col.
    float titleW = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + std::max(0.0f, lblW - titleW - pad),
                                     origin.y + textDY));
    ImGui::TextUnformatted(title);
    // Checkboxes stacked in the right column; each label centred on its row.
    int idx = 0;
    for (const char* p = labels; *p; p += std::strlen(p) + 1, ++idx) {
        float rowY = origin.y + idx * stride;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + lblW + pad, rowY));
        bool on = (*value == idx);
        ImGui::PushID(idx);
        if (UI::CheckboxBox("##c", &on) && on) { *value = idx; changed = true; }
        // Label centred vertically on the ui-unit row, just right of the box.
        ImGui::SameLine(0, pad);
        float lx = ImGui::GetCursorScreenPos().x;
        ImGui::SetCursorScreenPos(ImVec2(lx, rowY + textDY));
        ImGui::TextUnformatted(p);
        ImGui::PopID();
    }
    // Leave the layout cursor BELOW the whole block (the manual positioning above
    // doesn't advance ImGui's flow), so the next row starts in the right place.
    float blockH = count * rowH + (count - 1) * ImGui::GetStyle().ItemSpacing.y;
    (void)stride;
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
                   int displayDp = 3, const char* unit = "") {
    float ctrlW = PropLabel(label);
    ImGui::PushID(label);
    UI::DragValueConfig cfg = DVCfg("##d", speed, lo, hi, displayDp, unit);
    cfg.width = ctrlW;
    bool ch = UI::DragValue(cfg, v);
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
    const ImGuiColorEditFlags kColFlags =
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar;
    // Fill: the enable checkbox on its own row, then (when on) a "Color" row with
    // the swatch aligned in the control column.
    bool fillOn = part.fill.enabled;
    if (PropCheckRow("##fillEn", "Fill", &fillOn)) { part.fill.enabled = fillOn; dirty = true; }
    if (part.fill.enabled) {
        if (PropColorRow("##fill", "Color", &part.fill.color, kColFlags)) dirty = true;
    }
    // How an OPEN curve's fill closes the gap between its two ends. Only meaningful
    // for a non-cyclic curve that is actually filled (plain fill or surface layers).
    if (part.IsCurveLike() && !part.path.closed &&
        (part.fill.enabled || !part.fillLayers.empty())) {
        static const char* kClose = "Follow curve\0Straight\0";
        int close = part.openFillStraight ? 1 : 0;
        if (PropDropdown("Fill close", kClose, &close)) {
            part.openFillStraight = (close == 1); dirty = true;
        }
    }
    // Stroke: same two-row treatment (enable, then "Color").
    bool strokeOn = part.stroke.enabled;
    if (PropCheckRow("##strokeEn", "Stroke", &strokeOn)) { part.stroke.enabled = strokeOn; dirty = true; }
    if (part.stroke.enabled) {
        if (PropColorRow("##stroke", "Color", &part.stroke.color, kColFlags)) dirty = true;
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
        if (PropDropdown("Decorator", kDecor, &decor)) {
            part.stroke.decor = (Renderer::LineDecor)decor;
            part.stroke.decorSide = Renderer::DefaultSideForDecor(part.stroke.decor);
            dirty = true;
        }
        if (part.stroke.decor != Renderer::LineDecor::None) {
            if (PropDragFloat("Spacing",   &part.stroke.decorSpacing, 0.05f, 0.1f, 50.0f)) dirty = true;
            if (PropDragFloat("Size",      &part.stroke.decorSize,    0.02f, 0.02f, 20.0f)) dirty = true;
            if (PropDragFloat("Angle",     &part.stroke.decorAngleDeg, 1.0f, -180.0f, 180.0f, 3, "\xC2\xB0")) dirty = true;
            if (PropDragFloat("Thickness", &part.stroke.decorThickness, 0.01f, 0.0f, 10.0f)) dirty = true;
            bool dc = part.stroke.decorCentered;
            if (PropCheckRow("##phaseCentred", "Phase centred", &dc)) { part.stroke.decorCentered = dc; dirty = true; }
            static const char* kEdge = "Construction\0Inner edge\0Outer edge\0";
            static const char* kSide = "One side\0Both sides\0Centred\0Alternating\0";
            int edge = (int)part.stroke.decorEdge, side = (int)part.stroke.decorSide;
            if (PropDropdown("Edge", kEdge, &edge)) { part.stroke.decorEdge = (Renderer::DecorEdge)edge; dirty = true; }
            if (PropDropdown("Side", kSide, &side)) { part.stroke.decorSide = (Renderer::DecorSide)side; dirty = true; }
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
    PropControlColumn();
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
        UI::PanelConfig pc; pc.id = "##layer"; pc.label = hdr; pc.defaultOpen = true;
        UI::PanelResult pr = UI::BeginPanel(pc);
        if (pr.open) {
            bool en = fl.enabled;
            if (PropCheckRow("##layerEn", "Enabled", &en)) { fl.enabled = en; dirty = true; }
            if (PropColorRow("##c", "Color", &fl.color, kColFlags)) dirty = true;
            int pat = (int)fl.pattern;
            if (PropDropdown("Pattern", kPat, &pat)) { fl.pattern = (Renderer::FillPattern)pat; dirty = true; }
            if (fl.pattern == Renderer::FillPattern::Solid) {
                if (PropDragFloat("Opacity", &fl.opacity, 0.005f, 0.0f, 1.0f)) dirty = true;
            } else {
                if (PropDragFloat("Spacing", &fl.spacing, 0.05f, 0.1f, 50.0f)) dirty = true;
                if (PropDragFloat("Size",    &fl.size,    0.02f, 0.02f, 20.0f)) dirty = true;
                // Angle orients the whole motif (the dot/triangle lattice too).
                if (PropDragFloat("Angle", &fl.angleDeg, 1.0f, -180.0f, 180.0f, 3, "\xC2\xB0")) dirty = true;
                // The Offset stays editable even for module-managed (locked) symbols:
                // a fixed IOF screen pattern still needs the user to nudge its phase to
                // align the motif on the map. Drop out of the section-wide BeginDisabled
                // for just this control, then restore it.
                if (lineStyleLocked) ImGui::EndDisabled();
                float off[2] = { fl.offset.x, fl.offset.y };
                if (PropVec2Group("Offset", off, 0.05f, -100.0f, 100.0f, 3))
                    { fl.offset = { off[0], off[1] }; dirty = true; }
                if (lineStyleLocked) ImGui::BeginDisabled(true);
                // Per-layer cut edge relative to the stroked contour (linked toggles).
                static const char* kClip = "Construction\0Inner side\0Outer side\0";
                int clip = (int)fl.clip;
                if (PropButtonGroup("Fill clip", kClip, &clip)) {
                    fl.clip = (Renderer::FillClip)clip; dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Cut this layer at the construction line, or the "
                                      "inner / outer edge of the stroke");
                // What the pattern lattice is pinned to. Object origin keeps the motif
                // glued to the shape (it follows a move); Document origin pins it to
                // 0,0 so moving the shape slides it over a static field.
                static const char* kAnchor = "Object origin\0Document origin\0";
                int anchor = (int)fl.anchor;
                if (PropButtonGroup("Anchor", kAnchor, &anchor)) {
                    fl.anchor = (Renderer::FillAnchor)anchor; dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Pin the pattern lattice to the object (follows "
                                      "the shape) or to the document origin (0,0)");
            }
            PropControlColumn();
            if (ImGui::SmallButton("Up") && i > 0) {
                std::swap(part.fillLayers[i], part.fillLayers[i-1]); dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Down") && i + 1 < part.fillLayers.size()) {
                std::swap(part.fillLayers[i], part.fillLayers[i+1]); dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                part.fillLayers.erase(part.fillLayers.begin() + (long)i); dirty = true;
                UI::EndPanel(); ImGui::PopID(); break;
            }
        }
        UI::EndPanel();
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
            PropDragFloat("Pylon min spacing", &minSpacing, 0.1f, 0.5f, 100.0f, 1);
            PropControlColumn();
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
        if (PropCheckRow("##outsideMeasure", "Outside measure", &om)) { m.outsideMeasure = om; dirty = true; }
    }
    if (m.kind == Renderer::LineMarkKind::Pylon) {
        bool sq = m.square;
        if (PropCheckRow("##squarePylon", "Box (square pylon)", &sq)) {
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
            PropLabel("Page");                       // positions the control cursor
            UI::DropdownConfig cfg;
            cfg.id = "##pageDD";
            cfg.triggerLabel = doc.artboards[(size_t)cur].name;
            cfg.selectedIndex = cur;
            for (const auto& ab : doc.artboards) {
                UI::DropdownItem it; it.label = ab.name; cfg.items.push_back(it);
            }
            UI::DropdownResult r = UI::Dropdown(cfg);
            if (r.changed && r.selected != cur) {
                doc.MoveShapeToArtboard(sid, r.selected, /*keepWorldPos=*/true);
                // Belongs to the new page → reset to that page's root.
                if (Renderer::Shape* m = doc.FindShape(sid)) m->collectionId = 0;
                MarkUndoLabel("Move to page");
                dirty = true;
            }
        }
        // `s` may now point into a different vector; re-fetch defensively.
        s = doc.FindShape(sid);
        if (!s) { ImGui::PopStyleColor(); return; }
    }

    // ── Transform ────────────────────────────────────────────────────────────
    {
        UI::PanelConfig pc; pc.id = "##transform"; pc.label = "Transform"; pc.defaultOpen = true;
        UI::PanelResult pr = UI::BeginPanel(pc);
        if (pr.open) {
            // A module that manages transforms itself (IOF: fixed-size / north-
            // oriented symbols) shows the whole block — values AND padlocks — read-only.
            const bool forced = activeCapabilities_.lockTransformsForced;
            ImGui::BeginDisabled(forced);

            // Location: title row, then per-axis X/Y rows with independent padlocks.
            float tr[2] = { s->transform.translate.x, s->transform.translate.y };
            bool lpx = s->lockPosX, lpy = s->lockPosY;
            if (PropVec2Blender("Location", tr, &lpx, &lpy, 0.5f, 0.0f, 0.0f, 3)) {
                s->transform.translate = { tr[0], tr[1] }; dirty = true;
            }
            if (lpx != s->lockPosX || lpy != s->lockPosY) {
                s->lockPosX = lpx; s->lockPosY = lpy; dirty = true;
            }

            // Rotation: single value + padlock (whole-rotation lock).
            float deg = s->transform.rotate * 57.2957795f;
            bool lr = s->lockRotation;
            if (PropDragFloatLocked("Rotation", &deg, &lr, 0.5f, -3600.0f, 3600.0f, 3, "\xC2\xB0")) {
                s->transform.rotate = deg * 0.01745329f; dirty = true;
            }
            if (lr != s->lockRotation) { s->lockRotation = lr; dirty = true; }

            // Scale: title row, then per-axis X/Y rows with independent padlocks.
            float sc[2] = { s->transform.scale.x, s->transform.scale.y };
            bool lsx = s->lockScaleX, lsy = s->lockScaleY;
            if (PropVec2Blender("Scale", sc, &lsx, &lsy, 0.01f, -1000.0f, 1000.0f, 3)) {
                s->transform.scale = { sc[0], sc[1] }; dirty = true;
            }
            if (lsx != s->lockScaleX || lsy != s->lockScaleY) {
                s->lockScaleX = lsx; s->lockScaleY = lsy; dirty = true;
            }

            ImGui::EndDisabled();
            if (forced && ImGui::IsItemHovered())
                ImGui::SetTooltip("Managed by the active module");
        }
        UI::EndPanel();
    }

    // ── Edit-mode: active point handle type ──────────────────────────────────
    if (editorMode_ == EditorMode::Edit && doc.HasVertSelection()) {
        UI::PanelConfig pc; pc.id = "##activePoint"; pc.label = "Active Point"; pc.defaultOpen = true;
        UI::PanelResult pr = UI::BeginPanel(pc);
        if (pr.open) {
            const Renderer::VertRef& v = doc.ActiveVert();
            Renderer::Shape* vs = doc.FindShape(v.shape);
            if (vs && v.part < (int)vs->parts.size() &&
                v.node < (int)vs->parts[(size_t)v.part].path.nodes.size()) {
                Renderer::Part& vp = vs->parts[(size_t)v.part];
                Renderer::Node& n = vp.path.nodes[(size_t)v.node];
                // Order MUST match the HandleMode enum values (0..4).
                static const char* kModes =
                    "Free\0Aligned\0Mirrored\0Vector\0Aligned + Mirrored\0";
                int mode = (int)n.mode;
                if (PropDropdown("Handle Type", kModes, &mode)) {
                    Action_SetHandleType((Renderer::HandleMode)mode);  // applies to selection
                    dirty = true;
                }
                // NURBS control-point weight (rational): >1 pulls the curve toward
                // the point; the exact-circle hull uses √2/2 on the corner controls.
                if (vp.spline == Renderer::SplineType::Nurbs) {
                    float wgt = n.weight;
                    if (PropDragFloat("Weight", &wgt, 0.01f, 0.01f, 100.0f)) {
                        n.weight = std::max(0.01f, wgt); dirty = true;
                    }
                }
            }
        }
        UI::EndPanel();
    }

    // ── Curve options (cyclic, NURBS order) ──────────────────────────────────
    // Operate on the active object's curve part(s). Single-part is the common
    // case; with several parts we drive them together off the first curve part.
    {
        Renderer::Part* cp = nullptr;
        for (Renderer::Part& p : s->parts) if (p.IsCurveLike()) { cp = &p; break; }
        if (cp) {
            UI::PanelConfig pc; pc.id = "##curve"; pc.label = "Curve"; pc.defaultOpen = true;
            UI::PanelResult pr = UI::BeginPanel(pc);
            if (pr.open) {
            bool cyclic = cp->path.closed;
            if (PropCheckRow("##cyclic", "Cyclic", &cyclic)) {
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
                float ctrlW = PropLabel("Order U");
                ImGui::SetNextItemWidth(ctrlW);
                if (ImGui::SliderInt("##orderU", &order, 2, maxOrder)) {
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
                if (PropCheckRow("##endpointU", "Endpoint U", &endp)) {
                    for (Renderer::Part& p : s->parts)
                        if (p.IsCurveLike() && p.spline == Renderer::SplineType::Nurbs)
                            p.nurbsEndpoint = endp;
                    MarkUndoLabel("NURBS endpoint U"); dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Make the curve meet its first/last control point\n(clamped knots — needed for arcs / half-circles)");
                ImGui::EndDisabled();
                if (PropCheckRow("##bezierU", "Bezier U", &bez)) {
                    for (Renderer::Part& p : s->parts)
                        if (p.IsCurveLike() && p.spline == Renderer::SplineType::Nurbs)
                            p.nurbsBezier = bez;
                    MarkUndoLabel("NURBS bezier U"); dirty = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Treat the control polygon as rational Bezier segments\n(exact circles / arcs from a weighted square / triangle hull)");
            }
            }
            UI::EndPanel();
        }
    }

    // ── Fill & Stroke (per part) ─────────────────────────────────────────────
    {
        UI::PanelConfig pc; pc.id = "##paint"; pc.label = "Paint"; pc.defaultOpen = true;
        UI::PanelResult pr = UI::BeginPanel(pc);
        if (pr.open) {
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
                    UI::PanelConfig ppc; ppc.id = id; ppc.label = hdr; ppc.defaultOpen = true;
                    UI::PanelResult ppr = UI::BeginPanel(ppc);
                    if (ppr.open)
                        RenderPartPaint(s->parts[i], id, dirty, lineStyleLocked, capEditable);
                    UI::EndPanel();
                }
            }
        }
        UI::EndPanel();
    }

    if (dirty) project_.dirty = true;
    ImGui::PopStyleColor();
}

} // namespace App
