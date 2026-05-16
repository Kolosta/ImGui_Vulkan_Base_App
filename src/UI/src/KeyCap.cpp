#include <UI/KeyCap.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <vector>
#include <string>

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

void DrawSingleChip(const char* label, ImVec4 bg, ImVec4 border, ImVec4 text) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale  = ds.GetGlobalScale();
    ImVec2 padding     = SafeVec2("component.keycap.padding", ImVec2(6.0f, 2.0f));
    padding.x *= scale;
    padding.y *= scale;
    float radius       = SafeFloat("component.keycap.radius", 4.0f) * scale;
    float fontScale    = SafeFloat("component.keycap.fontScale", 0.85f);

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window || window->SkipItems) return;

    // Measure text at the scaled font size
    float baseFont = ImGui::GetFontSize();
    float chipFont = baseFont * fontScale;
    ImVec2 textSize = ImGui::CalcTextSize(label);
    textSize.x *= fontScale;
    textSize.y *= fontScale;

    ImVec2 chipSize(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 baseCursor = ImGui::GetCursorPos();

    // Vertically centre relative to one base text line so chips align with adjacent text
    float baseLineH = ImGui::GetTextLineHeight();
    float yShift = (baseLineH - chipSize.y) * 0.5f;
    if (yShift < 0.0f) yShift = 0.0f;
    cursor.y += yShift;

    ImDrawList* dl = window->DrawList;
    ImVec2 maxPt(cursor.x + chipSize.x, cursor.y + chipSize.y);
    dl->AddRectFilled(cursor, maxPt, ImGui::ColorConvertFloat4ToU32(bg), radius);
    dl->AddRect(cursor, maxPt, ImGui::ColorConvertFloat4ToU32(border), radius, 0, 1.0f);

    // text
    dl->AddText(ImGui::GetFont(), chipFont,
                ImVec2(cursor.x + padding.x, cursor.y + padding.y),
                ImGui::ColorConvertFloat4ToU32(text), label);

    // Reserve item-rect so SameLine works downstream
    ImVec2 advance(chipSize.x, std::max(baseLineH, chipSize.y));
    ImGui::Dummy(advance);
}

void DrawSeparator(const char* sep = "+") {
    ImGui::SameLine(0.0f, 2.0f * DesignSystem::DesignSystem::Instance().GetGlobalScale());
    ImVec4 col = SafeColor("semantic.color.text.muted", ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::TextColored(col, "%s", sep);
}

} // namespace

void KeyCap::DrawKeyCap(const char* label, bool useSameLine) {
    if (useSameLine) ImGui::SameLine(0.0f, 2.0f *
        DesignSystem::DesignSystem::Instance().GetGlobalScale());
    ImVec4 bg     = SafeColor("component.keycap.background", ImVec4(0.20f, 0.20f, 0.24f, 1.0f));
    ImVec4 border = SafeColor("component.keycap.border",     ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
    ImVec4 text   = SafeColor("component.keycap.text",       ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    DrawSingleChip(label, bg, border, text);
}

void KeyCap::DrawShortcut(const Shortcuts::EventSignature& sig, bool useSameLine) {
    DrawShortcutStyled(sig, State::Normal, useSameLine);
}

void KeyCap::DrawShortcutStyled(const Shortcuts::EventSignature& sig, State state,
                                bool useSameLine) {
    if (!sig.IsValid()) {
        if (useSameLine) ImGui::SameLine(0.0f, 2.0f);
        ImGui::TextColored(SafeColor("semantic.color.text.muted",
            ImVec4(0.6f, 0.6f, 0.6f, 1.0f)), "(unbound)");
        return;
    }

    ImVec4 bg     = SafeColor("component.keycap.background", ImVec4(0.20f, 0.20f, 0.24f, 1.0f));
    ImVec4 border = SafeColor("component.keycap.border",     ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
    ImVec4 text   = SafeColor("component.keycap.text",       ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    switch (state) {
        case State::Recording:
            border = SafeColor("component.shortcut.recording", ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
            break;
        case State::ConflictSoft:
            border = SafeColor("component.shortcut.conflict",     ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
            break;
        case State::ConflictHard:
            border = SafeColor("component.shortcut.conflictHard", ImVec4(0.9f, 0.26f, 0.26f, 1.0f));
            break;
        default: break;
    }

    std::vector<std::string> caps;
    if (sig.modifiers.any) {
        caps.emplace_back("Any");
    } else {
        if (sig.modifiers.ctrl)  caps.emplace_back(Shortcuts::ModifierDisplayName("Ctrl"));
        if (sig.modifiers.shift) caps.emplace_back(Shortcuts::ModifierDisplayName("Shift"));
        if (sig.modifiers.alt)   caps.emplace_back(Shortcuts::ModifierDisplayName("Alt"));
        if (sig.modifiers.super) caps.emplace_back(Shortcuts::ModifierDisplayName("Super"));
    }

    auto pushKeyText = [&]() {
        switch (sig.type) {
            case Shortcuts::EventType::KeyPress:
            case Shortcuts::EventType::KeyRelease:
            case Shortcuts::EventType::KeyClick:
            case Shortcuts::EventType::KeyDoubleClick: {
                caps.emplace_back(Shortcuts::KeyDisplayName(sig.key));
                break;
            }
            case Shortcuts::EventType::MousePress:
            case Shortcuts::EventType::MouseRelease:
            case Shortcuts::EventType::MouseClick:
            case Shortcuts::EventType::MouseDoubleClick:
            case Shortcuts::EventType::MouseDrag:
            case Shortcuts::EventType::MouseDragNorth:
            case Shortcuts::EventType::MouseDragSouth:
            case Shortcuts::EventType::MouseDragEast:
            case Shortcuts::EventType::MouseDragWest:
            case Shortcuts::EventType::MouseDragNorthEast:
            case Shortcuts::EventType::MouseDragNorthWest:
            case Shortcuts::EventType::MouseDragSouthEast:
            case Shortcuts::EventType::MouseDragSouthWest:
                caps.emplace_back(Shortcuts::MouseButtonName(sig.mouseButton));
                break;
            case Shortcuts::EventType::WheelUp:    caps.emplace_back("Wheel Up");   break;
            case Shortcuts::EventType::WheelDown:  caps.emplace_back("Wheel Dn");   break;
            case Shortcuts::EventType::WheelLeft:  caps.emplace_back("Wheel L");    break;
            case Shortcuts::EventType::WheelRight: caps.emplace_back("Wheel R");    break;
            case Shortcuts::EventType::WheelIn:    caps.emplace_back("Wheel In");   break;
            case Shortcuts::EventType::WheelOut:   caps.emplace_back("Wheel Out");  break;
            case Shortcuts::EventType::MouseMove:           caps.emplace_back("Mouse Move"); break;
            case Shortcuts::EventType::TrackpadPan:         caps.emplace_back("Trkpd Pan"); break;
            case Shortcuts::EventType::TrackpadZoom:        caps.emplace_back("Trkpd Zoom"); break;
            case Shortcuts::EventType::TrackpadRotate:      caps.emplace_back("Trkpd Rot"); break;
            case Shortcuts::EventType::TrackpadSmartRotate: caps.emplace_back("Trkpd SmtR"); break;
            default: caps.emplace_back("?"); break;
        }
    };
    pushKeyText();

    bool first = true;
    for (const auto& cap : caps) {
        bool sameLine = useSameLine || !first;
        if (sameLine) ImGui::SameLine(0.0f, 2.0f *
            DesignSystem::DesignSystem::Instance().GetGlobalScale());
        DrawSingleChip(cap.c_str(), bg, border, text);
        if (&cap != &caps.back()) DrawSeparator("+");
        first = false;
    }

    // Suffix descriptor for non-Press types
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImVec4 muted = SafeColor("semantic.color.text.muted", ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    auto suffix = [&](const char* text) {
        ImGui::SameLine(0.0f, 4.0f * ds.GetGlobalScale());
        ImGui::TextColored(muted, "%s", text);
    };
    switch (sig.type) {
        case Shortcuts::EventType::KeyRelease:       suffix("(release)"); break;
        case Shortcuts::EventType::KeyClick:         suffix("(click)");   break;
        case Shortcuts::EventType::KeyDoubleClick:   suffix("(double)");  break;
        case Shortcuts::EventType::MouseRelease:     suffix("(release)"); break;
        case Shortcuts::EventType::MouseClick:       suffix("(click)");   break;
        case Shortcuts::EventType::MouseDoubleClick: suffix("(double)");  break;
        case Shortcuts::EventType::MouseDrag:        suffix("(drag)");    break;
        case Shortcuts::EventType::MouseDragNorth:      suffix("(drag N)");  break;
        case Shortcuts::EventType::MouseDragSouth:      suffix("(drag S)");  break;
        case Shortcuts::EventType::MouseDragEast:       suffix("(drag E)");  break;
        case Shortcuts::EventType::MouseDragWest:       suffix("(drag W)");  break;
        case Shortcuts::EventType::MouseDragNorthEast:  suffix("(drag NE)"); break;
        case Shortcuts::EventType::MouseDragNorthWest:  suffix("(drag NW)"); break;
        case Shortcuts::EventType::MouseDragSouthEast:  suffix("(drag SE)"); break;
        case Shortcuts::EventType::MouseDragSouthWest:  suffix("(drag SW)"); break;
        default: break;
    }
}

float KeyCap::MeasureShortcutWidth(const Shortcuts::EventSignature& sig) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = ds.GetGlobalScale();
    ImVec2 padding = SafeVec2("component.keycap.padding", ImVec2(6.0f, 2.0f));
    padding.x *= scale;
    float fontScale = SafeFloat("component.keycap.fontScale", 0.85f);
    float w = 0.0f;
    int caps = 0;

    auto addCap = [&](const char* lbl) {
        ImVec2 t = ImGui::CalcTextSize(lbl);
        w += t.x * fontScale + padding.x * 2.0f;
        ++caps;
    };

    if (sig.modifiers.any) addCap("Any");
    else {
        if (sig.modifiers.ctrl)  addCap(Shortcuts::ModifierDisplayName("Ctrl"));
        if (sig.modifiers.shift) addCap(Shortcuts::ModifierDisplayName("Shift"));
        if (sig.modifiers.alt)   addCap(Shortcuts::ModifierDisplayName("Alt"));
        if (sig.modifiers.super) addCap(Shortcuts::ModifierDisplayName("Super"));
    }
    std::string kn = Shortcuts::KeyDisplayName(sig.key);
    addCap(kn.c_str());

    if (caps > 1) w += (caps - 1) * (ImGui::CalcTextSize("+").x + 4.0f * scale);
    return w;
}

} // namespace UI
