#include <Shortcuts/Event.h>
#include <cstring>

#if defined(__APPLE__)
  #define SHORTCUTS_PLATFORM_MAC 1
#else
  #define SHORTCUTS_PLATFORM_MAC 0
#endif

namespace Shortcuts {

const char* EventTypeName(EventType t) {
    switch (t) {
        case EventType::None:             return "None";
        case EventType::KeyPress:         return "Key Press";
        case EventType::KeyRelease:       return "Key Release";
        case EventType::KeyClick:         return "Key Click";
        case EventType::KeyDoubleClick:   return "Key Double Click";
        case EventType::MousePress:       return "Mouse Press";
        case EventType::MouseRelease:     return "Mouse Release";
        case EventType::MouseClick:       return "Mouse Click";
        case EventType::MouseDoubleClick: return "Mouse Double Click";
        case EventType::MouseDrag:           return "Mouse Drag (any)";
        case EventType::MouseDragNorth:      return "Mouse Drag N";
        case EventType::MouseDragSouth:      return "Mouse Drag S";
        case EventType::MouseDragEast:       return "Mouse Drag E";
        case EventType::MouseDragWest:       return "Mouse Drag W";
        case EventType::MouseDragNorthEast:  return "Mouse Drag NE";
        case EventType::MouseDragNorthWest:  return "Mouse Drag NW";
        case EventType::MouseDragSouthEast:  return "Mouse Drag SE";
        case EventType::MouseDragSouthWest:  return "Mouse Drag SW";
        case EventType::WheelUp:             return "Wheel Up";
        case EventType::WheelDown:           return "Wheel Down";
        case EventType::WheelLeft:           return "Wheel Left";
        case EventType::WheelRight:          return "Wheel Right";
        case EventType::WheelIn:             return "Wheel In";
        case EventType::WheelOut:            return "Wheel Out";
        case EventType::MouseMove:           return "Mouse Move";
        case EventType::TrackpadPan:         return "Trackpad Pan";
        case EventType::TrackpadZoom:        return "Trackpad Zoom";
        case EventType::TrackpadRotate:      return "Trackpad Rotate";
        case EventType::TrackpadSmartRotate: return "Trackpad Smart Rotate";
    }
    return "Unknown";
}

EventType EventTypeFromName(const char* name) {
    if (!name) return EventType::None;
    for (uint8_t i = 0; i <= static_cast<uint8_t>(EventType::TrackpadSmartRotate); ++i) {
        EventType t = static_cast<EventType>(i);
        if (std::strcmp(EventTypeName(t), name) == 0) return t;
    }
    return EventType::None;
}

const char* MouseButtonName(MouseButton b) {
    switch (b) {
        case MouseButton::None:   return "None";
        case MouseButton::Left:   return "Left Mouse";
        case MouseButton::Right:  return "Right Mouse";
        case MouseButton::Middle: return "Middle Mouse";
        case MouseButton::X1:     return "Mouse 4";
        case MouseButton::X2:     return "Mouse 5";
        case MouseButton::X3:     return "Mouse 6";
        case MouseButton::X4:     return "Mouse 7";
    }
    return "Unknown";
}

// ─── ModifierMask ────────────────────────────────────────────────────────────

bool ModifierMask::operator<(const ModifierMask& other) const {
    if (ctrl  != other.ctrl)  return ctrl  < other.ctrl;
    if (shift != other.shift) return shift < other.shift;
    if (alt   != other.alt)   return alt   < other.alt;
    if (super != other.super) return super < other.super;
    if (any   != other.any)   return any   < other.any;
    if (ctrlSide  != other.ctrlSide)  return ctrlSide  < other.ctrlSide;
    if (shiftSide != other.shiftSide) return shiftSide < other.shiftSide;
    if (altSide   != other.altSide)   return altSide   < other.altSide;
    return superSide < other.superSide;
}

int ModifierMask::Count() const {
    int n = 0;
    if (ctrl)  ++n;
    if (shift) ++n;
    if (alt)   ++n;
    if (super) ++n;
    return n;
}

// Helper: does an "observed" side selector satisfy a "required" side spec?
// `observedSide` here is just a pass-through field (in practice, observed
// masks always set Side=Both because FromImGuiIO captures the IO-level
// flags, not the physical key side — see EventNormalizer for the precise
// per-press side info that gets recorded into observed.*Side).
static bool SideMatch(ModifierSide required, ModifierSide observed) {
    if (required == ModifierSide::Both) return true;          // any side ok
    if (observed == ModifierSide::Both) return true;          // observed unspecified → permissive
    return required == observed;
}

bool ModifierMask::Match(const ModifierMask& observed) const {
    if (any) return true;
    if (ctrl  != observed.ctrl)  return false;
    if (shift != observed.shift) return false;
    if (alt   != observed.alt)   return false;
    if (super != observed.super) return false;
    if (ctrl  && !SideMatch(ctrlSide,  observed.ctrlSide))  return false;
    if (shift && !SideMatch(shiftSide, observed.shiftSide)) return false;
    if (alt   && !SideMatch(altSide,   observed.altSide))   return false;
    if (super && !SideMatch(superSide, observed.superSide)) return false;
    return true;
}

static const char* SideSuffix(ModifierSide s) {
    switch (s) {
        case ModifierSide::LeftOnly:  return "(L)";
        case ModifierSide::RightOnly: return "(R)";
        default: return "";
    }
}

std::string ModifierMask::ToString() const {
    if (any) return "Any+";
    std::string s;
    auto add = [&](const char* base, ModifierSide side) {
        s += ModifierDisplayName(base);
        const char* suf = SideSuffix(side);
        if (*suf) { s += suf; }
        s += "+";
    };
    if (ctrl)  add("Ctrl",  ctrlSide);
    if (shift) add("Shift", shiftSide);
    if (alt)   add("Alt",   altSide);
    if (super) add("Super", superSide);
    return s;
}

uint8_t ModifierMask::ToBits() const {
    uint8_t b = 0;
    if (ctrl)  b |= 0x01;
    if (shift) b |= 0x02;
    if (alt)   b |= 0x04;
    if (super) b |= 0x08;
    if (any)   b |= 0x10;
    return b;
}

uint8_t ModifierMask::SideBits() const {
    // 2 bits per modifier, ordered ctrl(0), shift(2), alt(4), super(6).
    auto enc = [](ModifierSide s) -> uint8_t { return static_cast<uint8_t>(s) & 0x03; };
    return static_cast<uint8_t>(
        (enc(ctrlSide)        ) |
        (enc(shiftSide)  << 2 ) |
        (enc(altSide)    << 4 ) |
        (enc(superSide)  << 6 ));
}

ModifierMask ModifierMask::FromBits(uint8_t bits) {
    ModifierMask m;
    m.ctrl  = (bits & 0x01) != 0;
    m.shift = (bits & 0x02) != 0;
    m.alt   = (bits & 0x04) != 0;
    m.super = (bits & 0x08) != 0;
    m.any   = (bits & 0x10) != 0;
    return m;
}

ModifierMask ModifierMask::FromBitsAndSides(uint8_t bits, uint8_t sideBits) {
    ModifierMask m = FromBits(bits);
    auto dec = [](uint8_t v) -> ModifierSide {
        v &= 0x03;
        if (v > 2) v = 0;
        return static_cast<ModifierSide>(v);
    };
    m.ctrlSide  = dec( sideBits        & 0x03);
    m.shiftSide = dec((sideBits >> 2)  & 0x03);
    m.altSide   = dec((sideBits >> 4)  & 0x03);
    m.superSide = dec((sideBits >> 6)  & 0x03);
    return m;
}

ModifierMask ModifierMask::FromImGuiIO() {
    ModifierMask m;
    ImGuiIO& io = ImGui::GetIO();
    m.ctrl  = io.KeyCtrl;
    m.shift = io.KeyShift;
    m.alt   = io.KeyAlt;
    m.super = io.KeySuper;
    // observed-side info: report which side actually fired
    auto sideOf = [](ImGuiKey leftK, ImGuiKey rightK) -> ModifierSide {
        bool L = ImGui::IsKeyDown(leftK);
        bool R = ImGui::IsKeyDown(rightK);
        if (L && R) return ModifierSide::Both;
        if (L)      return ModifierSide::LeftOnly;
        if (R)      return ModifierSide::RightOnly;
        return ModifierSide::Both;
    };
    if (m.ctrl)  m.ctrlSide  = sideOf(ImGuiKey_LeftCtrl,  ImGuiKey_RightCtrl);
    if (m.shift) m.shiftSide = sideOf(ImGuiKey_LeftShift, ImGuiKey_RightShift);
    if (m.alt)   m.altSide   = sideOf(ImGuiKey_LeftAlt,   ImGuiKey_RightAlt);
    if (m.super) m.superSide = sideOf(ImGuiKey_LeftSuper, ImGuiKey_RightSuper);
    return m;
}

// ─── EventSignature ──────────────────────────────────────────────────────────

bool EventSignature::operator==(const EventSignature& other) const {
    return type == other.type &&
           key  == other.key  &&
           mouseButton == other.mouseButton &&
           modifiers == other.modifiers &&
           repeat == other.repeat;
}

bool EventSignature::operator<(const EventSignature& other) const {
    if (type != other.type) return static_cast<int>(type) < static_cast<int>(other.type);
    if (key  != other.key)  return key < other.key;
    if (mouseButton != other.mouseButton)
        return static_cast<int>(mouseButton) < static_cast<int>(other.mouseButton);
    if (!(modifiers == other.modifiers)) return modifiers < other.modifiers;
    return repeat < other.repeat;
}

bool EventSignature::Match(const EventSignature& observed) const {
    auto isDragDir = [](EventType t) {
        return t == EventType::MouseDragNorth || t == EventType::MouseDragSouth ||
               t == EventType::MouseDragEast  || t == EventType::MouseDragWest  ||
               t == EventType::MouseDragNorthEast || t == EventType::MouseDragNorthWest ||
               t == EventType::MouseDragSouthEast || t == EventType::MouseDragSouthWest;
    };

    bool typeMatch = (type == observed.type);
    // "MouseDrag" (any direction) accepts any specific direction.
    if (!typeMatch && type == EventType::MouseDrag && isDragDir(observed.type))
        typeMatch = true;
    if (!typeMatch) return false;

    bool keyMatch = (key == ImGuiKey_None) || (key == observed.key);
    if (!keyMatch) return false;
    if (mouseButton != observed.mouseButton) return false;
    if (!modifiers.Match(observed.modifiers)) return false;
    if (!repeat && observed.repeat) return false;
    return true;
}

bool EventSignature::IsValid() const {
    // A binding using ONLY the "Any modifier" wildcard with no concrete key,
    // button or wheel input would match every observed event in its target
    // context — that is never what the user wants and it makes new/empty
    // shortcut slots fire on the first keypress.  Refuse those.
    auto modsHaveConcrete = [&]() {
        return modifiers.ctrl || modifiers.shift || modifiers.alt || modifiers.super;
    };
    switch (type) {
        case EventType::None:
            return false;
        case EventType::KeyPress:
        case EventType::KeyRelease:
        case EventType::KeyClick:
        case EventType::KeyDoubleClick:
            if (key == ImGuiKey_None) return false;
            if (IsModifierKey(key))   return false;
            return true;
        case EventType::MousePress:
        case EventType::MouseRelease:
        case EventType::MouseClick:
        case EventType::MouseDoubleClick:
        case EventType::MouseDrag:
        case EventType::MouseDragNorth:
        case EventType::MouseDragSouth:
        case EventType::MouseDragEast:
        case EventType::MouseDragWest:
        case EventType::MouseDragNorthEast:
        case EventType::MouseDragNorthWest:
        case EventType::MouseDragSouthEast:
        case EventType::MouseDragSouthWest:
            return mouseButton != MouseButton::None;
        case EventType::WheelUp:
        case EventType::WheelDown:
        case EventType::WheelLeft:
        case EventType::WheelRight:
        case EventType::WheelIn:
        case EventType::WheelOut:
            (void)modsHaveConcrete;
            return true;
        case EventType::MouseMove:
        case EventType::TrackpadPan:
        case EventType::TrackpadZoom:
        case EventType::TrackpadRotate:
        case EventType::TrackpadSmartRotate:
            // These continuous motion events have no concrete trigger
            // beyond their type; they're always valid as long as the type
            // is set.
            return true;
    }
    return false;
}

std::string EventSignature::ToString() const {
    std::string s = modifiers.ToString();
    switch (type) {
        case EventType::None:
            return "(none)";
        case EventType::KeyPress:
        case EventType::KeyRelease:
        case EventType::KeyClick:
        case EventType::KeyDoubleClick: {
            s += KeyDisplayName(key);
            if (type == EventType::KeyRelease)     s += " (release)";
            if (type == EventType::KeyClick)       s += " (click)";
            if (type == EventType::KeyDoubleClick) s += " (double)";
            break;
        }
        case EventType::MousePress:       s += MouseButtonName(mouseButton); s += " press"; break;
        case EventType::MouseRelease:     s += MouseButtonName(mouseButton); s += " release"; break;
        case EventType::MouseClick:       s += MouseButtonName(mouseButton); s += " click"; break;
        case EventType::MouseDoubleClick: s += MouseButtonName(mouseButton); s += " double"; break;
        case EventType::MouseDrag:           s += MouseButtonName(mouseButton); s += " drag";    break;
        case EventType::MouseDragNorth:      s += MouseButtonName(mouseButton); s += " drag N";  break;
        case EventType::MouseDragSouth:      s += MouseButtonName(mouseButton); s += " drag S";  break;
        case EventType::MouseDragEast:       s += MouseButtonName(mouseButton); s += " drag E";  break;
        case EventType::MouseDragWest:       s += MouseButtonName(mouseButton); s += " drag W";  break;
        case EventType::MouseDragNorthEast:  s += MouseButtonName(mouseButton); s += " drag NE"; break;
        case EventType::MouseDragNorthWest:  s += MouseButtonName(mouseButton); s += " drag NW"; break;
        case EventType::MouseDragSouthEast:  s += MouseButtonName(mouseButton); s += " drag SE"; break;
        case EventType::MouseDragSouthWest:  s += MouseButtonName(mouseButton); s += " drag SW"; break;
        case EventType::WheelUp:          s += "Wheel Up";    break;
        case EventType::WheelDown:        s += "Wheel Down";  break;
        case EventType::WheelLeft:        s += "Wheel Left";  break;
        case EventType::WheelRight:       s += "Wheel Right"; break;
        case EventType::WheelIn:          s += "Wheel In";    break;
        case EventType::WheelOut:         s += "Wheel Out";   break;
        case EventType::MouseMove:           s += "Mouse Move"; break;
        case EventType::TrackpadPan:         s += "Trackpad Pan"; break;
        case EventType::TrackpadZoom:        s += "Trackpad Zoom"; break;
        case EventType::TrackpadRotate:      s += "Trackpad Rotate"; break;
        case EventType::TrackpadSmartRotate: s += "Trackpad SmartRot"; break;
    }
    return s;
}

bool IsModifierKey(ImGuiKey key) {
    if (key == ImGuiKey_LeftCtrl  || key == ImGuiKey_RightCtrl  ||
        key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
        key == ImGuiKey_LeftAlt   || key == ImGuiKey_RightAlt   ||
        key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper)
        return true;

    // ImGui exposes "ReservedFor*" keys in the named-key range that fire
    // alongside the real Left/Right modifier keys.  Without filtering them
    // out, capture loops would record "Ctrl+ModCtrl" instead of waiting
    // for the user to add a real key.  See imgui.h §ImGuiKey enum.
#ifdef ImGuiKey_ReservedForModCtrl
    if (key == ImGuiKey_ReservedForModCtrl  ||
        key == ImGuiKey_ReservedForModShift ||
        key == ImGuiKey_ReservedForModAlt   ||
        key == ImGuiKey_ReservedForModSuper)
        return true;
#else
    // Not all imgui versions ship the macros; identify by name as a
    // fallback (cheap because we only call this during capture).
    const char* n = ImGui::GetKeyName(key);
    if (n) {
        if (std::strncmp(n, "ModCtrl",  7) == 0) return true;
        if (std::strncmp(n, "ModShift", 8) == 0) return true;
        if (std::strncmp(n, "ModAlt",   6) == 0) return true;
        if (std::strncmp(n, "ModSuper", 8) == 0) return true;
    }
#endif
    return false;
}

// ─── Pretty key naming ──────────────────────────────────────────────────────

const char* ModifierDisplayName(const char* base) {
    if (!base) return "";
    if (std::strcmp(base, "Super") == 0) {
#if SHORTCUTS_PLATFORM_MAC
        return "Cmd";
#else
        return "Win";
#endif
    }
    return base;
}

std::string KeyDisplayName(ImGuiKey key) {
    switch (key) {
        case ImGuiKey_None:        return "None";

        // Punctuation — ImGui returns the literal char which is hard to
        // read at small sizes; expand to words like Blender / DaVinci.
        case ImGuiKey_Apostrophe:  return "Apostrophe";
        case ImGuiKey_Comma:       return "Comma";
        case ImGuiKey_Minus:       return "Minus";
        case ImGuiKey_Period:      return "Period";
        case ImGuiKey_Slash:       return "Slash";
        case ImGuiKey_Semicolon:   return "Semicolon";
        case ImGuiKey_Equal:       return "Equal";
        case ImGuiKey_LeftBracket: return "LBracket";
        case ImGuiKey_Backslash:   return "Backslash";
        case ImGuiKey_RightBracket:return "RBracket";
        case ImGuiKey_GraveAccent: return "Grave";

        case ImGuiKey_Space:       return "Space";
        case ImGuiKey_Enter:       return "Enter";
        case ImGuiKey_Tab:         return "Tab";
        case ImGuiKey_Backspace:   return "Backspace";
        case ImGuiKey_Escape:      return "Esc";
        case ImGuiKey_Delete:      return "Del";
        case ImGuiKey_Insert:      return "Ins";

        case ImGuiKey_LeftArrow:   return "Left";
        case ImGuiKey_RightArrow:  return "Right";
        case ImGuiKey_UpArrow:     return "Up";
        case ImGuiKey_DownArrow:   return "Down";

        case ImGuiKey_KeypadEnter:    return "NumEnter";
        case ImGuiKey_KeypadDecimal:  return "NumDecimal";
        case ImGuiKey_KeypadDivide:   return "NumDivide";
        case ImGuiKey_KeypadMultiply: return "NumMultiply";
        case ImGuiKey_KeypadSubtract: return "NumSubtract";
        case ImGuiKey_KeypadAdd:      return "NumAdd";
        case ImGuiKey_KeypadEqual:    return "NumEqual";

        // Sided modifiers — keep the side info, but Win/Cmd-aware
        case ImGuiKey_LeftCtrl:    return "L Ctrl";
        case ImGuiKey_RightCtrl:   return "R Ctrl";
        case ImGuiKey_LeftShift:   return "L Shift";
        case ImGuiKey_RightShift:  return "R Shift";
        case ImGuiKey_LeftAlt:     return "L Alt";
#if SHORTCUTS_PLATFORM_MAC
        case ImGuiKey_RightAlt:    return "R Alt";
        case ImGuiKey_LeftSuper:   return "L Cmd";
        case ImGuiKey_RightSuper:  return "R Cmd";
#else
        case ImGuiKey_RightAlt:    return "AltGr";
        case ImGuiKey_LeftSuper:   return "L Win";
        case ImGuiKey_RightSuper:  return "R Win";
#endif

        default: break;
    }
    const char* fallback = ImGui::GetKeyName(key);
    return fallback ? std::string(fallback) : std::string("?");
}

} // namespace Shortcuts
