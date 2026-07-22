#include <UI/Widgets/DragValue.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace UI {

namespace {

namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;

ImVec4 Col(Tok t) { return DS::DesignSystem::Instance().GetColor(t); }
float  Flt(Tok t) { return DS::DesignSystem::Instance().GetFloat(t); }

// Resolve the OS window backing the current ImGui context (multi-viewport aware),
// so we can hide/warp the OS cursor exactly like the viewport gesture code does.
SDL_Window* HostWindow() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp) return nullptr;
    return SDL_GetWindowFromID((SDL_WindowID)(intptr_t)vp->PlatformHandle);
}

// Warp the OS cursor to an ImGui (global) position. SDL wants window-relative
// coordinates, so subtract the main viewport origin (≈0 for the single-window
// case, non-zero for a detached OS window such as Preferences).
void WarpTo(SDL_Window* w, ImVec2 imguiPos) {
    if (!w) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 o = vp ? vp->Pos : ImVec2(0, 0);
    SDL_WarpMouseInWindow(w, imguiPos.x - o.x, imguiPos.y - o.y);
}

// Forward decl (defined after DragState).
struct DragState;
void EndCursorGrab(DragState& st);

// One global interaction state — only ONE drag/edit can be live at a time.
struct DragState {
    ImGuiID id = 0;
    bool    pressed   = false;   // mouse went down on the field (drag-or-click pending)
    bool    dragging  = false;   // crossed the drag threshold
    bool    editing   = false;   // manual text-edit mode
    float   startValue = 0.0f;    // value at press (for Alt-steps and cancel)
    ImVec2  pressPos{};           // OS/ImGui mouse pos at press (cursor restored here)
    float   accumPx = 0.0f;       // accumulated horizontal drag pixels (mod-independent)
    bool    relMode = false;      // SDL relative-mouse (cursor GRABBED) during a drag
    char    buf[64] = {0};        // manual-edit text buffer
    bool    cursorHidden = false;
};
DragState& State() { static DragState s; return s; }

// End a drag's cursor GRAB: leave SDL relative-mouse mode (releases + reveals
// the OS cursor) and warp it back to the press point, then mark it shown. Safe
// to call whether or not a grab / hide was active.
void EndCursorGrab(DragState& st) {
    SDL_Window* w = HostWindow();
    if (st.relMode) {
        if (w) SDL_SetWindowRelativeMouseMode(w, false);
        st.relMode = false;
    }
    if (st.cursorHidden) {
        WarpTo(w, st.pressPos);
        ImGui::GetIO().MousePos = st.pressPos;
        st.cursorHidden = false;
    }
}

// Auto step heuristics from the field's speed/quantity/magnitude (Blender-like).
// `v` is a DISPLAY-unit value (the drag maths now runs in display units).
double AutoCtrlStep(const DragValueConfig& cfg, double v) {
    if (cfg.ctrlStep > 0.0f) return cfg.ctrlStep;
    // Angles snap to 1°; everything else to a power-of-ten near the magnitude.
    if (cfg.quantity == Units::Quantity::Angle ||
        std::strcmp(cfg.unit, "\xC2\xB0") == 0 || std::strcmp(cfg.unit, "deg") == 0)
        return 1.0;
    double mag = std::max(std::fabs(v), (double)cfg.speed * 100.0);
    if (mag <= 0.0) return 1.0;
    double e = std::floor(std::log10(mag));
    return std::pow(10.0, std::max(-3.0, e - 1.0));
}
double AutoAltStep(const DragValueConfig& cfg) {
    if (cfg.altStep > 0.0f) return cfg.altStep;
    return AutoCtrlStep(cfg, 1.0);   // unit-sized increments by default
}
double AutoButtonStep(const DragValueConfig& cfg, double v) {
    if (cfg.buttonStep > 0.0f) return cfg.buttonStep;
    return AutoCtrlStep(cfg, v);
}

void Clamp(const DragValueConfig& cfg, float* v) {
    if (cfg.min != cfg.max) *v = std::clamp(*v, cfg.min, cfg.max);
}

} // namespace

bool DragValue(const DragValueConfig& cfg, float* v) {
    DS::DesignSystem::ComponentScope _cs("DragValue");
    auto& ds = DS::DesignSystem::Instance();
    const float gs = ds.GetGlobalScale();
    ImGuiIO& io = ImGui::GetIO();

    const float h        = Flt(Tok::S_Size_ControlHeight) * gs;   // ui-unit tall
    const float radius   = Flt(Tok::C_DragValue_CornerRadius) * gs;
    const bool  bordersOn = ds.BordersEnabled();
    const float borderW  = bordersOn ? Flt(Tok::C_DragValue_BorderWidth) * gs : 0.0f;
    const float padX     = ImGui::GetStyle().FramePadding.x;

    float width = cfg.width > 0.0f ? cfg.width : ImGui::GetContentRegionAvail().x;
    width = std::max(width, h * 2.0f);

    ImGui::PushID(cfg.id);
    const ImGuiID id = ImGui::GetID("##dv");
    DragState& st = State();

    // Safety: never leave the OS cursor GRABBED past a drag. If the button is up
    // while a grab is still live (e.g. the dragged field vanished mid-gesture),
    // release it here — idempotent, so the normal release path below is
    // unaffected (its EndCursorGrab then no-ops). The first field rendered this
    // frame catches a stale grab even when the drag owner is gone.
    if (st.relMode && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        EndCursorGrab(st);

    // Unit awareness: for Length/Angle/Percent, *v is the BASE value and the
    // field's maths/display run in the resolved DISPLAY unit.
    const bool unitAware = cfg.quantity != Units::Quantity::Scalar;
    const Units::UnitSystem sys =
        cfg.useDocSystem ? Units::DocumentSystem() : cfg.system;
    auto toDisp = [&](double base) {
        return unitAware ? Units::DisplayValue(base, cfg.quantity, sys, cfg.scale)
                         : base;
    };
    auto toBase = [&](double disp) {
        return unitAware ? Units::DisplayToBase(disp, cfg.quantity, sys, cfg.scale)
                         : disp;
    };

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + h);

    bool changed = false;

    // ── Manual edit mode: an InputText over the field, full precision ─────────
    if (st.editing && st.id == id) {
        ImGui::SetCursorScreenPos(p0);
        ImGui::SetNextItemWidth(width);
        // Match the field's resting look exactly: same fill, rounding, NO border
        // and NO keyboard-focus ring, and a FramePadding that yields ui-unit height
        // (so the field doesn't grow when it enters edit mode).
        const float padY = std::max(0.0f, (h - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Col(Tok::C_DragValue_BackgroundDrag));
        ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));   // no focus ring
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, radius);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(ImGui::GetStyle().FramePadding.x, padY));
        if (ImGui::IsWindowAppearing() || ImGui::GetActiveID() != ImGui::GetID("##edit"))
            ImGui::SetKeyboardFocusHere();
        // No char filter: the field accepts a full expression — units (mm, pt,
        // °, %), parentheses, operators (+ − * /), constants (pi, tau) and both
        // decimal separators. UI::Units::Parse interprets it on commit.
        bool enter = ImGui::InputText("##edit", st.buf, sizeof(st.buf),
                                      ImGuiInputTextFlags_EnterReturnsTrue |
                                      ImGuiInputTextFlags_AutoSelectAll);
        const bool esc      = ImGui::IsKeyPressed(ImGuiKey_Escape);
        const bool deact    = ImGui::IsItemDeactivated();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
        if (enter || (deact && !esc)) {
            // Interpret the text as a unit-aware expression → BASE value. Any
            // compatible unit tag converts; a bare number uses the display unit;
            // Scalar accepts a plain arithmetic expression. Invalid → no change.
            if (auto r = Units::Parse(st.buf, cfg.quantity, sys, cfg.scale)) {
                *v = (float)*r; Clamp(cfg, v); changed = true;
            }
            st.editing = false; st.id = 0;
        } else if (esc) {
            st.editing = false; st.id = 0;   // cancel: keep the existing value
        }
        ImGui::PopID();
        return changed;
    }

    // ── Interaction (drag / click / step buttons) ────────────────────────────
    ImGui::InvisibleButton("##dv", ImVec2(width, h),
                           ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();

    // Step buttons inset at each end, only meaningful on hover (not while
    // dragging) — and only when the field is wide enough to spare the room.
    // Below the threshold they would take two ui-units out of a field barely
    // wider than that, leaving no space for the number itself; the drag and the
    // double-click edit still work, and the buttons return by themselves as
    // soon as the panel is widened.
    const bool hasSteps = width >= cfg.minButtonsUiUnits * h;
    const float btnW = hasSteps ? h : 0.0f;                // square slots
    const ImVec2 lMin = p0, lMax(p0.x + btnW, p1.y);
    const ImVec2 rMin(p1.x - btnW, p0.y), rMax = p1;
    const bool overLeft  = hasSteps && hovered && io.MousePos.x < lMax.x;
    const bool overRight = hasSteps && hovered && io.MousePos.x > rMin.x;
    const bool overButton = overLeft || overRight;

    // Cursor: hidden from the moment of a press on the drag area until release (we
    // move it ourselves while dragging); a normal arrow over the +/- step buttons;
    // the horizontal-resize cursor over the central drag area at rest.
    if (st.id == id && st.cursorHidden)
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    else if (hovered && overButton)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    else if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    // ── Press: decide between step-button click, drag, or pending manual edit ─
    if (ImGui::IsItemActivated()) {
        st.id = id; st.pressed = true; st.dragging = false; st.editing = false;
        st.startValue = *v; st.pressPos = io.MousePos;
        st.accumPx = 0.0f; st.relMode = false;
        // Hide the cursor immediately on a press in the central drag area (not on a
        // step button). It reappears at the press point on release (whether the
        // press became a drag, a no-op click, or manual edit).
        st.cursorHidden = !overButton;
    }

    const bool active = (st.id == id);

    // Cancel a live drag with right-click or Esc → restore the PRECISE start value
    // (written back to the caller) and the cursor to the press point.
    if (active && (st.pressed || st.dragging)) {
        const bool cancel = ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                            ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (cancel) {
            // The drag committed intermediate values into *v; restore the exact
            // starting value and report it as a change so the caller writes it back.
            const bool restored = (*v != st.startValue);
            *v = st.startValue;
            EndCursorGrab(st);
            st.pressed = st.dragging = false; st.id = 0;
            ImGui::ClearActiveID();
            ImGui::PopID();
            return restored;
        }
    }

    if (active && held && st.pressed) {
        // Detect drag once the pointer moves past the threshold. When it starts,
        // GRAB the OS cursor via SDL relative-mouse mode: the cursor is frozen,
        // hidden and confined to the window, so it can NEVER hover / resize /
        // recolour any other zone while dragging (io.MousePos stops moving at the
        // OS level, not just for ImGui). Motion then comes as RAW relative deltas.
        if (!st.dragging) {
            float moved = std::fabs(io.MousePos.x - st.pressPos.x) +
                          std::fabs(io.MousePos.y - st.pressPos.y);
            if (moved > io.MouseDragThreshold) {
                st.dragging = true;
                if (SDL_Window* w = HostWindow()) {
                    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "1");
                    if (SDL_SetWindowRelativeMouseMode(w, true)) {
                        SDL_GetRelativeMouseState(nullptr, nullptr);  // flush
                        st.relMode = true;
                    }
                }
            }
        }
        if (st.dragging) {
            // Per-frame motion: RAW relative delta while grabbed (smooth at any
            // speed — no OS-cursor warp to fight); io.MouseDelta as a fallback if
            // the grab could not be entered.
            float rx = 0.0f, ry = 0.0f;
            if (st.relMode) SDL_GetRelativeMouseState(&rx, &ry);
            else            rx = io.MouseDelta.x;
            (void)ry;
            st.accumPx += rx;
            // Belt-and-suspenders for any custom hover-test that reads io.MousePos
            // directly: keep it pinned to the press point for the rest of the frame.
            io.MousePos = st.pressPos;

            // Effective speed with modifiers. The maths runs in DISPLAY units
            // (speed = display-units per pixel); the base value is stored back.
            double speed = cfg.speed;
            if (io.KeyShift) speed *= (cfg.shiftPrecision > 0.0f ? cfg.shiftPrecision : 0.1f);
            const double dispStart = toDisp((double)st.startValue);
            double disp = dispStart + (double)st.accumPx * speed;

            if (io.KeyCtrl) {                 // snap to round DISPLAY values
                double step = AutoCtrlStep(cfg, disp);
                if (step > 0.0) disp = std::round(disp / step) * step;
            } else if (io.KeyAlt) {           // stepped increments from the start
                double step = AutoAltStep(cfg);
                if (step > 0.0) {
                    double k = std::round((disp - dispStart) / step);
                    disp = dispStart + k * step;
                }
            }
            float val = (float)toBase(disp);
            float clamped = val;
            Clamp(cfg, &clamped);
            // PEG the accumulator AT the limit when clamped, so a reverse drag
            // responds instantly instead of first undoing the run-past distance
            // the background drag piled up beyond min / max.
            if (clamped != val && cfg.min != cfg.max && std::fabs(speed) > 1e-12)
                st.accumPx = (float)((toDisp((double)clamped) - dispStart) / speed);
            if (clamped != *v) { *v = clamped; changed = true; }
        }
    }

    // ── Release ──────────────────────────────────────────────────────────────
    if (active && ImGui::IsItemDeactivated()) {
        if (st.dragging) {
            // End drag: release the grab + restore the cursor at the press point.
            EndCursorGrab(st);
            st.pressed = st.dragging = false; st.id = 0;
        } else if (st.pressed) {
            // A clean click (no drag): the cursor was hidden on press and didn't
            // move, so just show it again here (no warp needed).
            st.cursorHidden = false;
            if (overLeft || overRight) {
                const double d0 = toDisp((double)*v);
                const double step = AutoButtonStep(cfg, d0);
                *v = (float)toBase(d0 + (overRight ? step : -step));
                Clamp(cfg, v); changed = true;
                st.pressed = false; st.id = 0;
            } else {
                st.editing = true; st.pressed = false;
                // Seed the edit with the DISPLAY-unit number only (no suffix).
                const std::string seed = unitAware
                    ? Units::FormatNumber(*v, cfg.quantity, sys, cfg.scale)
                    : Units::FormatNumber(*v, Units::Quantity::Scalar, sys, cfg.scale);
                std::snprintf(st.buf, sizeof(st.buf), "%s", seed.c_str());
            }
        }
    }

    // ── Draw ─────────────────────────────────────────────────────────────────
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool pressedLook = active && (st.dragging || (st.pressed && held));
    ImVec4 fillV = (active && st.dragging) ? Col(Tok::C_DragValue_BackgroundDrag)
                 : pressedLook              ? Col(Tok::C_DragValue_BackgroundPressed)
                 : hovered                  ? Col(Tok::C_DragValue_BackgroundHover)
                                            : Col(Tok::C_DragValue_Background);
    dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(fillV), radius);
    if (borderW > 0.01f)
        dl->AddRect(p0, p1, ImGui::ColorConvertFloat4ToU32(Col(Tok::C_DragValue_Border)),
                    radius, 0, borderW);

    // Hover step buttons (drawn inside the field, not while dragging).
    const bool showSteps = hasSteps && hovered && !(active && st.dragging);
    if (showSteps) {
        const ImU32 hov = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_DragValue_StepButtonHover));
        if (overLeft)  dl->AddRectFilled(lMin, lMax, hov, radius, ImDrawFlags_RoundCornersLeft);
        if (overRight) dl->AddRectFilled(rMin, rMax, hov, radius, ImDrawFlags_RoundCornersRight);
        const ImU32 g = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_DragValue_StepButton));
        const float gw = std::max(1.0f, std::floor(1.5f * gs));
        const float s = h * 0.16f;
        ImVec2 lc(p0.x + btnW * 0.5f, p0.y + h * 0.5f);
        ImVec2 rc(p1.x - btnW * 0.5f, p0.y + h * 0.5f);
        dl->AddLine(ImVec2(lc.x - s, lc.y), ImVec2(lc.x + s, lc.y), g, gw);      // −
        dl->AddLine(ImVec2(rc.x - s, rc.y), ImVec2(rc.x + s, rc.y), g, gw);      // +
        dl->AddLine(ImVec2(rc.x, rc.y - s), ImVec2(rc.x, rc.y + s), g, gw);
    }

    // Value text (centred), display-rounded in the DISPLAY unit; unit label in
    // the subtle colour. Length/Angle/Percent derive the label from the active
    // system; a Scalar field keeps its fixed `unit` suffix.
    char num[48];
    char fmt[16]; std::snprintf(fmt, sizeof fmt, "%%.%df", std::max(0, cfg.displayDecimals));
    std::snprintf(num, sizeof num, fmt, toDisp((double)*v));
    const std::string label = unitAware
        ? Units::UnitLabel(cfg.quantity, sys, cfg.scale)
        : std::string(cfg.unit ? cfg.unit : "");
    const bool hasUnit = !label.empty();
    ImVec2 numSz = ImGui::CalcTextSize(num);
    ImVec2 unitSz = hasUnit ? ImGui::CalcTextSize(label.c_str()) : ImVec2(0, 0);
    const float gap = hasUnit ? 2.0f * gs : 0.0f;
    float total = numSz.x + gap + unitSz.x;
    float tx = p0.x + (width - total) * 0.5f;
    float ty = p0.y + (h - numSz.y) * 0.5f;
    const ImU32 numCol  = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_DragValue_Text));
    const ImU32 unitCol = ImGui::ColorConvertFloat4ToU32(Col(Tok::C_DragValue_Unit));
    if (hasUnit && cfg.unitBeforeValue) {
        dl->AddText(ImVec2(tx, ty), unitCol, label.c_str());
        dl->AddText(ImVec2(tx + unitSz.x + gap, ty), numCol, num);
    } else {
        dl->AddText(ImVec2(tx, ty), numCol, num);
        if (hasUnit)
            dl->AddText(ImVec2(tx + numSz.x + gap, ty), unitCol, label.c_str());
    }

    (void)padX;
    ImGui::PopID();
    return changed;
}

} // namespace UI
