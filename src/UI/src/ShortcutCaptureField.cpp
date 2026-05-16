#include <UI/ShortcutCaptureField.h>
#include <UI/KeyCap.h>
#include <Shortcuts/ShortcutManager.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <unordered_map>

namespace UI {

namespace {

ImVec4 SafeColor(const std::string& token, ImVec4 fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetColor(token); }
    catch (...) { return fallback; }
}

float SafeFloat(const std::string& token, float fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetFloat(token); }
    catch (...) { return fallback; }
}

ImVec2 SafeVec2(const std::string& token, ImVec2 fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetVec2(token); }
    catch (...) { return fallback; }
}

struct CaptureState {
    bool recording = false;
    Shortcuts::EventSignature original;     // restore on cancel
    Shortcuts::EventSignature working;      // live preview
    bool hasNonModifier = false;
};

std::unordered_map<ImGuiID, CaptureState>& States() {
    static std::unordered_map<ImGuiID, CaptureState> s;
    return s;
}

void DrawText(const char* text, ImVec4 col) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

} // namespace

float ShortcutCaptureField::Height() {
    auto& ds = DesignSystem::DesignSystem::Instance();
    float h  = SafeFloat("component.captureField.height", 28.0f);
    return h * ds.GetGlobalScale();
}

bool ShortcutCaptureField::Render(const char* id,
                                  Shortcuts::EventSignature& inout,
                                  Mode mode,
                                  bool /*withInputKindToggle*/,
                                  bool withDropdown,
                                  StatusOverride status) {
    using namespace Shortcuts;
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = ds.GetGlobalScale();

    ImGui::PushID(id);
    ImGuiID imguiId = ImGui::GetID("##cap");
    CaptureState& st = States()[imguiId];

    const float height   = Height();

    // The capture field is conceptually an input frame: derive its base
    // look (bg / hover / active / border / radius) from the SAME tokens
    // ImGui frames use so it stays visually consistent with combos &
    // inputs.  The recording/error overrides remain dedicated tokens.
    ImVec2 padding = SafeVec2("component.captureField.padding", ImVec2(8.0f, 4.0f));
    padding.x *= scale;
    padding.y *= scale;
    float radius = SafeFloat("component.frame.radius", 4.0f) * scale;

    ImVec4 bgIdle   = SafeColor("component.frame.background",
                                 ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
    ImVec4 bgHover  = SafeColor("component.frame.backgroundHover",
                                 ImVec4(0.18f, 0.18f, 0.20f, 1.0f));
    ImVec4 bgActive = SafeColor("component.frame.backgroundActive",
                                 ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImVec4 bgRec    = SafeColor("component.captureField.backgroundRecording",
                                 ImVec4(0.22f, 0.10f, 0.10f, 1.0f));
    ImVec4 bdIdle   = SafeColor("component.frame.border",
                                 ImVec4(0.40f, 0.40f, 0.45f, 1.0f));
    ImVec4 bdRec    = SafeColor("component.captureField.borderRecording",
                                 ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
    ImVec4 hint     = SafeColor("component.captureField.hintText",
                                 ImVec4(0.55f, 0.55f, 0.58f, 1.0f));
    float frameBorderPx = SafeFloat("component.frame.borderSize", 1.0f);

    bool committedThisFrame = false;

    // Width: respect PushItemWidth if any, else fall back to min width.
    ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
    float availW = ImGui::GetContentRegionAvail().x;
    float itemW  = ImGui::CalcItemWidth();
    float minWidth = SafeFloat("component.captureField.minWidth", 180.0f) * scale;
    float fieldW   = (itemW > 0.0f && itemW < availW) ? itemW : availW;
    if (fieldW < minWidth) fieldW = std::min(minWidth, availW);
    if (fieldW < 40.0f * scale) fieldW = 40.0f * scale;
    ImVec2 size(fieldW, height);

    // Split the field into a main record zone (left) and an optional
    // dropdown arrow zone (right).  The two hit-tests are distinct so
    // clicking the arrow opens the picker without arming a recording.
    float arrowW = withDropdown ? height : 0.0f;
    float mainW  = size.x - arrowW;
    if (mainW < 24.0f * scale) { mainW = size.x; arrowW = 0.0f; }

    bool clicked = ImGui::InvisibleButton("##cap", ImVec2(mainW, size.y));
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    bool arrowClicked = false;
    bool arrowHovered = false;
    bool arrowActive  = false;
    if (arrowW > 0.0f) {
        ImGui::SameLine(0.0f, 0.0f);
        arrowClicked = ImGui::InvisibleButton("##capdrop", ImVec2(arrowW, size.y));
        arrowHovered = ImGui::IsItemHovered();
        arrowActive  = ImGui::IsItemActive();
    }

    // Background colour selection (main area)
    ImVec4 bg;
    if (st.recording)      bg = bgRec;
    else if (active)       bg = bgActive;
    else if (hovered)      bg = bgHover;
    else                   bg = bgIdle;

    ImVec4 border = st.recording ? bdRec : bdIdle;
    float  borderThickness = st.recording ? (2.5f * scale)
                                          : (frameBorderPx * scale);
    if (!st.recording) {
        if (status == StatusOverride::Error) {
            border = SafeColor("component.shortcut.conflictHard",
                               ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
            borderThickness = 2.0f * scale;
        } else if (status == StatusOverride::Warning) {
            border = SafeColor("component.shortcut.conflict",
                               ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
            borderThickness = 1.5f * scale;
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 maxPt(cursorScreen.x + size.x, cursorScreen.y + size.y);
    dl->AddRectFilled(cursorScreen, maxPt, ImGui::ColorConvertFloat4ToU32(bg), radius);
    dl->AddRect(cursorScreen, maxPt, ImGui::ColorConvertFloat4ToU32(border),
                radius, 0, borderThickness);

    // ── Dropdown arrow zone (no fill; subtle grey on hover/active) ───────
    if (arrowW > 0.0f) {
        ImVec2 aMin(cursorScreen.x + mainW, cursorScreen.y);
        ImVec2 aMax(cursorScreen.x + size.x, cursorScreen.y + size.y);
        // Thin separator between record zone and arrow zone.
        dl->AddLine(ImVec2(aMin.x, aMin.y + 3.0f * scale),
                    ImVec2(aMin.x, aMax.y - 3.0f * scale),
                    ImGui::ColorConvertFloat4ToU32(bdIdle),
                    frameBorderPx * scale);
        if (arrowHovered || arrowActive) {
            ImVec4 hoverTint = SafeColor("component.frame.backgroundHover",
                                          ImVec4(0.22f,0.22f,0.25f,1.0f));
            if (arrowActive)
                hoverTint = SafeColor("component.frame.backgroundActive",
                                       ImVec4(0.10f,0.10f,0.12f,1.0f));
            dl->AddRectFilled(ImVec2(aMin.x + 1.0f, aMin.y + 1.0f),
                              ImVec2(aMax.x - 1.0f, aMax.y - 1.0f),
                              ImGui::ColorConvertFloat4ToU32(hoverTint),
                              radius, ImDrawFlags_RoundCornersRight);
        }
        // Caret glyph
        ImVec4 caretC = SafeColor("semantic.color.text.muted",
                                  ImVec4(0.6f,0.6f,0.6f,1.0f));
        float cx = (aMin.x + aMax.x) * 0.5f;
        float cy = (aMin.y + aMax.y) * 0.5f;
        float r  = 3.0f * scale;
        dl->AddTriangleFilled(ImVec2(cx - r, cy - r * 0.5f),
                              ImVec2(cx + r, cy - r * 0.5f),
                              ImVec2(cx, cy + r * 0.6f),
                              ImGui::ColorConvertFloat4ToU32(caretC));
        if (arrowHovered)
            ImGui::SetTooltip("Pick from a list");
        if (arrowClicked)
            ImGui::OpenPopup("##cap_picker");
    }

    // Click toggles recording / finishes recording
    if (clicked) {
        if (!st.recording) {
            st.recording      = true;
            st.original       = inout;
            st.working        = inout;
            st.hasNonModifier = false;
        } else {
            if (mode == Mode::SingleKey) {
                if (st.working.key != ImGuiKey_None) {
                    inout = st.working;
                    inout.modifiers = ModifierMask{};
                    inout.type = EventType::KeyPress;
                    committedThisFrame = true;
                }
                st.recording = false;
            } else {
                if (st.hasNonModifier) {
                    inout = st.working;
                    committedThisFrame = true;
                }
                st.recording = false;
            }
        }
    }

    // While recording: collect the live combo
    if (st.recording) {
        ModifierMask mods = ModifierMask::FromImGuiIO();

        if (mode == Mode::Combo) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                st.working   = st.original;
                st.hasNonModifier = false;
                st.recording = false;
            }
        }

        if (st.recording) {
            if (mode == Mode::Combo) {
                st.working.modifiers = mods;
                st.working.type      = EventType::KeyPress;
                st.working.mouseButton = MouseButton::None;

                for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                    ImGuiKey ik = (ImGuiKey)k;
                    if (IsModifierKey(ik))   continue;
                    if (ik == ImGuiKey_Escape) continue;
                    if (ImGui::IsKeyPressed(ik, false)) {
                        st.working.key = ik;
                        st.hasNonModifier = true;
                        if (st.working.IsValid()) {
                            inout = st.working;
                            committedThisFrame = true;
                            st.recording = false;
                            break;
                        }
                    }
                }

                if (st.recording) {
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.MouseWheel != 0.0f) {
                        st.working.type = io.MouseWheel > 0.0f
                            ? EventType::WheelUp : EventType::WheelDown;
                        st.working.key  = ImGuiKey_None;
                        st.hasNonModifier = true;
                        if (st.working.IsValid()) {
                            inout = st.working;
                            committedThisFrame = true;
                            st.recording = false;
                        }
                    }
                }
            } else {
                for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                    ImGuiKey ik = (ImGuiKey)k;
                    if (IsModifierKey(ik)) continue;
                    if (ImGui::IsKeyPressed(ik, false)) {
                        st.working.key = ik;
                        st.working.modifiers = ModifierMask{};
                        st.working.type = EventType::KeyPress;
                        st.working.mouseButton = MouseButton::None;
                        inout = st.working;
                        committedThisFrame = true;
                        st.recording = false;
                        break;
                    }
                }
            }
        }

        // Override global context so the keys we capture don't fire other
        // shortcuts in parallel. Reset is done at next BeginFrame.
        ShortcutContext octx;
        octx.window = "##captureField";
        ShortcutManager::Instance().SetCurrentContext(octx);
    }

    // ── Render content on top of the rect ───────────────────────────────
    ImGui::SetCursorScreenPos(ImVec2(cursorScreen.x + padding.x,
                                     cursorScreen.y + padding.y));
    if (st.recording) {
        if (st.hasNonModifier || mode == Mode::SingleKey) {
            KeyCap::DrawShortcutStyled(st.working, KeyCap::State::Recording, false);
        } else {
            ModifierMask m = st.working.modifiers;
            if (m.ctrl || m.shift || m.alt || m.super) {
                if (m.ctrl)  KeyCap::DrawKeyCap("Ctrl",  /*useSameLine=*/false);
                if (m.shift) KeyCap::DrawKeyCap("Shift", /*useSameLine=*/true);
                if (m.alt)   KeyCap::DrawKeyCap("Alt",   /*useSameLine=*/true);
                if (m.super) KeyCap::DrawKeyCap(Shortcuts::ModifierDisplayName("Super"),
                                                /*useSameLine=*/true);
                ImGui::SameLine(0.0f, 6.0f * scale);
                DrawText("+ ...", hint);
            } else {
                DrawText(mode == Mode::SingleKey
                             ? "Press a key"
                             : "Press a combination",
                         hint);
            }
        }
    } else {
        if (inout.IsValid()) {
            KeyCap::DrawShortcutStyled(inout, KeyCap::State::Normal, false);
        } else {
            DrawText("(unbound - click to record)", hint);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(cursorScreen.x, cursorScreen.y));
    ImGui::Dummy(size);

    if (hovered && !st.recording && !active) {
        ImGui::SetTooltip("Click to record a new %s\n"
                          "%s",
                          mode == Mode::SingleKey ? "key" : "combination",
                          mode == Mode::SingleKey
                              ? "(single key, no modifiers)"
                              : "(Esc cancels while recording)");
    }

    // ── Key picker popup (opened from the integrated arrow zone) ────────
    if (withDropdown && ImGui::BeginPopup("##cap_picker")) {
        ImGui::TextDisabled("Pick a key");
        ImGui::Separator();
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
            ImGuiKey ik = (ImGuiKey)k;
            if (IsModifierKey(ik)) continue;
            std::string name = KeyDisplayName(ik);
            if (name.empty() || name == "?") continue;
            if (ImGui::MenuItem(name.c_str())) {
                inout.key = ik;
                if (inout.type != EventType::KeyPress &&
                    inout.type != EventType::KeyRelease &&
                    inout.type != EventType::KeyClick &&
                    inout.type != EventType::KeyDoubleClick)
                    inout.type = EventType::KeyPress;
                inout.mouseButton = MouseButton::None;
                if (mode == Mode::SingleKey) inout.modifiers = ModifierMask{};
                committedThisFrame = true;
            }
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return committedThisFrame;
}

} // namespace UI
