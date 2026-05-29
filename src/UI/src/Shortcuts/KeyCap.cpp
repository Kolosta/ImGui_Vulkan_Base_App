#include <UI/Shortcuts/KeyCap.h>
#include <DesignSystem/DesignSystem.h>
#include <imgui_internal.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cfloat>

namespace UI {

namespace {

// Token-typed Safe* (Tok auto-follows TokName(): no silent theming loss on
// a Spectrum-2 key rename — a stale id would only hit the fallback).
ImVec4 SafeColor(DesignSystem::Tok t, ImVec4 fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetColor(t); }
    catch (...) { return fallback; }
}

float SafeFloat(DesignSystem::Tok t, float fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetFloat(t); }
    catch (...) { return fallback; }
}

ImVec2 SafeVec2(DesignSystem::Tok t, ImVec2 fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetVec2(t); }
    catch (...) { return fallback; }
}

// The font size used for chip glyphs (base font * token scale).
float ChipFontSize() {
    float fontScale = SafeFloat(DesignSystem::Tok::C_KeyCap_FontScale, 0.85f);
    return ImGui::GetFontSize() * fontScale;
}

// Natural chip height = glyph size + 2× vertical token padding.
float NaturalChipHeight() {
    float scale = DesignSystem::DesignSystem::Instance().GetGlobalScale();
    ImVec2 padding = SafeVec2(DesignSystem::Tok::C_KeyCap_Padding, ImVec2(6.0f, 2.0f));
    return ChipFontSize() + padding.y * scale * 2.0f;
}

// Width a chip takes for `label` at the current chip font + token padding.
float ChipWidth(const char* label) {
    float scale = DesignSystem::DesignSystem::Instance().GetGlobalScale();
    ImVec2 padding = SafeVec2(DesignSystem::Tok::C_KeyCap_Padding, ImVec2(6.0f, 2.0f));
    ImFont* font = ImGui::GetFont();
    float chipFont = ChipFontSize();
    ImVec2 ts = font->CalcTextSizeA(chipFont, FLT_MAX, 0.0f, label);
    return ts.x + padding.x * scale * 2.0f;
}

// Draw one chip with its TOP-LEFT at screen `pos`, exactly `boxH` tall, the
// label centred on both axes. Pure draw-list, no ImGui item/flow. Returns the
// chip width so callers can advance horizontally.
float DrawChipAt(ImDrawList* dl, const char* label, ImVec2 pos, float boxH,
                 ImVec4 bg, ImVec4 border, ImVec4 text) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = ds.GetGlobalScale();
    ImVec2 padding    = SafeVec2(DesignSystem::Tok::C_KeyCap_Padding, ImVec2(6.0f, 2.0f));
    padding.x *= scale;
    padding.y *= scale;
    float radius = SafeFloat(DesignSystem::Tok::C_KeyCap_CornerRadius, 4.0f) * scale;

    ImFont* font   = ImGui::GetFont();
    float   chipFont = ChipFontSize();
    ImVec2  ts = font->CalcTextSizeA(chipFont, FLT_MAX, 0.0f, label);
    float   chipW = ts.x + padding.x * 2.0f;

    ImVec2 maxPt(pos.x + chipW, pos.y + boxH);
    dl->AddRectFilled(pos, maxPt, ImGui::ColorConvertFloat4ToU32(bg), radius);
    dl->AddRect(pos, maxPt, ImGui::ColorConvertFloat4ToU32(border), radius, 0,
                1.0f * scale);
    ImVec2 textPos(pos.x + (chipW - ts.x) * 0.5f,
                   pos.y + (boxH - ts.y) * 0.5f);
    dl->AddText(font, chipFont, textPos, ImGui::ColorConvertFloat4ToU32(text), label);
    return chipW;
}

// Draw one chip. `fixedHeight` > 0 forces the chip box to that height and
// centres the label inside it (used by the status bar so chips match the bar);
// 0 uses the natural height and aligns to the current text line (inline use).
void DrawSingleChip(const char* label, ImVec4 bg, ImVec4 border, ImVec4 text,
                    float fixedHeight) {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale  = ds.GetGlobalScale();
    ImVec2 padding     = SafeVec2(DesignSystem::Tok::C_KeyCap_Padding, ImVec2(6.0f, 2.0f));
    padding.x *= scale;
    padding.y *= scale;
    float radius       = SafeFloat(DesignSystem::Tok::C_KeyCap_CornerRadius, 4.0f) * scale;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window || window->SkipItems) return;

    ImFont* font   = ImGui::GetFont();
    float   chipFont = ChipFontSize();
    // Measure at the ACTUAL render size (not base-size × scale, which mismatches
    // FreeType hinting and crops wide labels like "Ctrl").
    ImVec2 textSize = font->CalcTextSizeA(chipFont, FLT_MAX, 0.0f, label);

    float chipH = (fixedHeight > 0.0f) ? fixedHeight
                                       : (textSize.y + padding.y * 2.0f);
    float chipW = textSize.x + padding.x * 2.0f;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    // When not given a fixed height, align the chip to the current text line.
    if (fixedHeight <= 0.0f) {
        float baseLineH = ImGui::GetTextLineHeight();
        float yShift = (baseLineH - chipH) * 0.5f;
        if (yShift > 0.0f) cursor.y += yShift;
    }

    ImDrawList* dl = window->DrawList;
    ImVec2 maxPt(cursor.x + chipW, cursor.y + chipH);
    dl->AddRectFilled(cursor, maxPt, ImGui::ColorConvertFloat4ToU32(bg), radius);
    dl->AddRect(cursor, maxPt, ImGui::ColorConvertFloat4ToU32(border), radius, 0,
                1.0f * scale);

    // Centre the label both axes inside the chip box.
    ImVec2 textPos(cursor.x + (chipW - textSize.x) * 0.5f,
                   cursor.y + (chipH - textSize.y) * 0.5f);
    dl->AddText(font, chipFont, textPos, ImGui::ColorConvertFloat4ToU32(text), label);

    // Reserve the item rect so SameLine/advance works downstream.
    float advanceH = (fixedHeight > 0.0f) ? chipH
                                          : std::max(ImGui::GetTextLineHeight(), chipH);
    ImGui::Dummy(ImVec2(chipW, advanceH));
}

void DrawSeparator(const char* sep = "+") {
    ImGui::SameLine(0.0f, 2.0f * DesignSystem::DesignSystem::Instance().GetGlobalScale());
    ImVec4 col = SafeColor(DesignSystem::Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::TextColored(col, "%s", sep);
}

// Collect the chip labels of a signature ("Ctrl","Shift","A"). Shared by the
// flow-based and absolute-position renderers.
std::vector<std::string> CollectCaps(const Shortcuts::EventSignature& sig) {
    std::vector<std::string> caps;
    if (sig.modifiers.any) { caps.emplace_back("Any"); }
    else {
        if (sig.modifiers.ctrl)  caps.emplace_back(Shortcuts::ModifierDisplayName("Ctrl"));
        if (sig.modifiers.shift) caps.emplace_back(Shortcuts::ModifierDisplayName("Shift"));
        if (sig.modifiers.alt)   caps.emplace_back(Shortcuts::ModifierDisplayName("Alt"));
        if (sig.modifiers.super) caps.emplace_back(Shortcuts::ModifierDisplayName("Super"));
    }
    switch (sig.type) {
        case Shortcuts::EventType::KeyPress:
        case Shortcuts::EventType::KeyRelease:
        case Shortcuts::EventType::KeyClick:
        case Shortcuts::EventType::KeyDoubleClick:
            caps.emplace_back(Shortcuts::KeyDisplayName(sig.key)); break;
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
            caps.emplace_back(Shortcuts::MouseButtonName(sig.mouseButton)); break;
        case Shortcuts::EventType::WheelUp:    caps.emplace_back("Wheel Up");  break;
        case Shortcuts::EventType::WheelDown:  caps.emplace_back("Wheel Dn");  break;
        case Shortcuts::EventType::WheelLeft:  caps.emplace_back("Wheel L");   break;
        case Shortcuts::EventType::WheelRight: caps.emplace_back("Wheel R");   break;
        case Shortcuts::EventType::WheelIn:    caps.emplace_back("Wheel In");  break;
        case Shortcuts::EventType::WheelOut:   caps.emplace_back("Wheel Out"); break;
        case Shortcuts::EventType::MouseMove:           caps.emplace_back("Mouse Move"); break;
        case Shortcuts::EventType::TrackpadPan:         caps.emplace_back("Trkpd Pan");  break;
        case Shortcuts::EventType::TrackpadZoom:        caps.emplace_back("Trkpd Zoom"); break;
        case Shortcuts::EventType::TrackpadRotate:      caps.emplace_back("Trkpd Rot");  break;
        case Shortcuts::EventType::TrackpadSmartRotate: caps.emplace_back("Trkpd SmtR"); break;
        default: caps.emplace_back("?"); break;
    }
    return caps;
}

} // namespace

float KeyCap::ChipHeight(float fixedHeight) {
    return fixedHeight > 0.0f ? fixedHeight : NaturalChipHeight();
}

void KeyCap::DrawKeyCap(const char* label, bool useSameLine, float fixedHeight) {
    DesignSystem::DesignSystem::ComponentScope _cs("KeyCap");
    if (useSameLine) ImGui::SameLine(0.0f, 2.0f *
        DesignSystem::DesignSystem::Instance().GetGlobalScale());
    ImVec4 bg     = SafeColor(DesignSystem::Tok::C_KeyCap_Background, ImVec4(0.20f, 0.20f, 0.24f, 1.0f));
    ImVec4 border = SafeColor(DesignSystem::Tok::C_KeyCap_Border,ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
    ImVec4 text   = SafeColor(DesignSystem::Tok::C_KeyCap_Label,ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    DrawSingleChip(label, bg, border, text, fixedHeight);
}

void KeyCap::DrawShortcut(const Shortcuts::EventSignature& sig, bool useSameLine,
                          float fixedHeight) {
    DrawShortcutStyled(sig, State::Normal, useSameLine, fixedHeight);
}

void KeyCap::DrawShortcutStyled(const Shortcuts::EventSignature& sig, State state,
                                bool useSameLine, float fixedHeight) {
    DesignSystem::DesignSystem::ComponentScope _cs("KeyCap");
    if (!sig.IsValid()) {
        if (useSameLine) ImGui::SameLine(0.0f, 2.0f);
        ImGui::TextColored(SafeColor(DesignSystem::Tok::S_Color_Text_Subtle,
            ImVec4(0.6f, 0.6f, 0.6f, 1.0f)), "(unbound)");
        return;
    }

    ImVec4 bg     = SafeColor(DesignSystem::Tok::C_KeyCap_Background, ImVec4(0.20f, 0.20f, 0.24f, 1.0f));
    ImVec4 border = SafeColor(DesignSystem::Tok::C_KeyCap_Border,ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
    ImVec4 text   = SafeColor(DesignSystem::Tok::C_KeyCap_Label,ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    switch (state) {
        case State::Recording:
            border = SafeColor(DesignSystem::Tok::C_Shortcut_Recording, ImVec4(0.95f, 0.30f, 0.30f, 1.0f));
            break;
        case State::ConflictSoft:
            border = SafeColor(DesignSystem::Tok::C_Shortcut_ConflictSoft, ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
            break;
        case State::ConflictHard:
            border = SafeColor(DesignSystem::Tok::C_Shortcut_ConflictHard, ImVec4(0.9f, 0.26f, 0.26f, 1.0f));
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
        DrawSingleChip(cap.c_str(), bg, border, text, fixedHeight);
        if (&cap != &caps.back()) DrawSeparator("+");
        first = false;
    }

    // Suffix descriptor for non-Press types
    auto& ds = DesignSystem::DesignSystem::Instance();
    ImVec4 muted = SafeColor(DesignSystem::Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
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
    ImVec2 padding = SafeVec2(DesignSystem::Tok::C_KeyCap_Padding, ImVec2(6.0f, 2.0f));
    padding.x *= scale;
    float fontScale = SafeFloat(DesignSystem::Tok::C_KeyCap_FontScale, 0.85f);
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

float KeyCap::MeasureShortcut(const Shortcuts::EventSignature& sig) {
    if (!sig.IsValid()) return ImGui::CalcTextSize("(unbound)").x;
    const float scale = DesignSystem::DesignSystem::Instance().GetGlobalScale();
    const float gap   = 2.0f * scale;          // between chip and "+"
    const float plusW = ImGui::CalcTextSize("+").x;
    std::vector<std::string> caps = CollectCaps(sig);
    float w = 0.0f;
    for (size_t i = 0; i < caps.size(); ++i) {
        w += ChipWidth(caps[i].c_str());
        if (i + 1 < caps.size()) w += gap + plusW + gap;
    }
    return w;
}

float KeyCap::DrawShortcutAt(const Shortcuts::EventSignature& sig,
                             ImVec2 topLeft, float rowHeight) {
    DesignSystem::DesignSystem::ComponentScope _cs("KeyCap");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!sig.IsValid()) {
        ImVec4 muted = SafeColor(DesignSystem::Tok::S_Color_Text_Subtle,
                                 ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImVec2 ts = ImGui::CalcTextSize("(unbound)");
        dl->AddText(ImVec2(topLeft.x, topLeft.y + (rowHeight - ts.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(muted), "(unbound)");
        return ts.x;
    }
    ImVec4 bg     = SafeColor(DesignSystem::Tok::C_KeyCap_Background, ImVec4(0.20f, 0.20f, 0.24f, 1.0f));
    ImVec4 border = SafeColor(DesignSystem::Tok::C_KeyCap_Border,    ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
    ImVec4 text   = SafeColor(DesignSystem::Tok::C_KeyCap_Label,     ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImVec4 muted  = SafeColor(DesignSystem::Tok::S_Color_Text_Subtle, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

    (void)muted;
    const float scale = DesignSystem::DesignSystem::Instance().GetGlobalScale();
    const float gap   = 2.0f * scale;   // small space between adjacent chips

    std::vector<std::string> caps = CollectCaps(sig);
    float x = topLeft.x;
    for (size_t i = 0; i < caps.size(); ++i) {
        x += DrawChipAt(dl, caps[i].c_str(), ImVec2(x, topLeft.y), rowHeight,
                        bg, border, text);
        if (i + 1 < caps.size()) x += gap;   // chips side by side, no "+"
    }
    return x - topLeft.x;
}

} // namespace UI
