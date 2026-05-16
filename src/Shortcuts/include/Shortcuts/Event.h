#pragma once

#include <imgui.h>
#include <string>
#include <cstdint>

namespace Shortcuts {

enum class EventType : uint8_t {
    None = 0,
    KeyPress,
    KeyRelease,
    KeyClick,
    KeyDoubleClick,
    MousePress,
    MouseRelease,
    MouseClick,
    MouseDoubleClick,
    MouseDrag,            // any direction (alias of "Any")
    MouseDragNorth,
    MouseDragSouth,
    MouseDragEast,
    MouseDragWest,
    MouseDragNorthEast,
    MouseDragNorthWest,
    MouseDragSouthEast,
    MouseDragSouthWest,
    WheelUp,              // vertical wheel +
    WheelDown,            // vertical wheel -
    WheelLeft,            // horizontal wheel (tilt) +
    WheelRight,           // horizontal wheel (tilt) -
    WheelIn,              // pinch-out / zoom-in (trackpad)
    WheelOut,             // pinch-in  / zoom-out
    // High-level mouse / trackpad motion (no button, no key).
    MouseMove,
    TrackpadPan,
    TrackpadZoom,
    TrackpadRotate,
    TrackpadSmartRotate
};

const char* EventTypeName(EventType t);
EventType EventTypeFromName(const char* name);

enum class MouseButton : uint8_t {
    None = 0,
    Left,
    Right,
    Middle,
    X1,           // Button 4
    X2,           // Button 5
    X3,           // Button 6 (rare on standard mice; common on gaming mice)
    X4            // Button 7
};

const char* MouseButtonName(MouseButton b);

/**
 * Side restriction for a sided modifier (Ctrl, Shift, Alt, Super).
 *
 *   Both : either L or R counts (default once the modifier is enabled)
 *   LeftOnly  : only the left-side key counts
 *   RightOnly : only the right-side key counts
 *
 * The struct's bool flags (ctrl/shift/alt/super) decide whether the
 * modifier is required at all; the matching `*Side` field decides which
 * physical key satisfies it.
 */
enum class ModifierSide : uint8_t {
    Both     = 0,
    LeftOnly = 1,
    RightOnly = 2,
};

struct ModifierMask {
    bool ctrl  = false;
    bool shift = false;
    bool alt   = false;
    bool super = false;
    bool any   = false;       // wildcard: match regardless of modifiers held

    ModifierSide ctrlSide  = ModifierSide::Both;
    ModifierSide shiftSide = ModifierSide::Both;
    ModifierSide altSide   = ModifierSide::Both;
    ModifierSide superSide = ModifierSide::Both;

    bool operator==(const ModifierMask& other) const {
        return ctrl == other.ctrl && shift == other.shift &&
               alt  == other.alt  && super == other.super &&
               any  == other.any  &&
               ctrlSide == other.ctrlSide && shiftSide == other.shiftSide &&
               altSide  == other.altSide  && superSide == other.superSide;
    }
    bool operator!=(const ModifierMask& other) const { return !(*this == other); }
    bool operator<(const ModifierMask& other) const;

    bool Match(const ModifierMask& observed) const;
    int Count() const;
    std::string ToString() const;
    uint8_t ToBits() const;          // ctrl/shift/alt/super/any (5 bits)
    uint8_t SideBits() const;        // ctrlSide..superSide (2 bits each)
    static ModifierMask FromBits(uint8_t bits);
    static ModifierMask FromBitsAndSides(uint8_t bits, uint8_t sideBits);
    static ModifierMask FromImGuiIO();
};

struct EventSignature {
    EventType   type        = EventType::KeyPress;
    ImGuiKey    key         = ImGuiKey_None;
    MouseButton mouseButton = MouseButton::None;
    ModifierMask modifiers;
    bool        repeat      = false;

    // Per-binding drag distance override (logical pixels).
    // 0.0f means "use the design-system default token
    // semantic.shortcut.dragThreshold".  Only meaningful when `type` is
    // one of the MouseDrag* variants.
    float       dragThreshold = 0.0f;

    bool operator==(const EventSignature& other) const;
    bool operator!=(const EventSignature& other) const { return !(*this == other); }
    bool operator<(const EventSignature& other) const;

    bool Match(const EventSignature& observed) const;
    bool IsValid() const;
    std::string ToString() const;
};

struct Event {
    EventSignature signature;
    ImVec2  mousePos       = ImVec2(0.0f, 0.0f);
    ImVec2  dragDelta      = ImVec2(0.0f, 0.0f);
    float   wheelDelta     = 0.0f;
    bool    consumed       = false;
};

bool IsModifierKey(ImGuiKey key);

/**
 * Display name for a key, prettier than ImGui::GetKeyName for punctuation
 * and platform modifier symbols.  Examples:
 *   ImGuiKey_Comma          -> "Comma"
 *   ImGuiKey_Period         -> "Period"
 *   ImGuiKey_LeftSuper      -> "Win" (Windows / Linux) / "Cmd" (macOS)
 *   ImGuiKey_RightAlt       -> "AltGr" (Windows / Linux)
 *   ImGuiKey_Enter          -> "Enter"
 */
std::string KeyDisplayName(ImGuiKey key);

/** Display name for a generic modifier (used in mask rendering, not for a
 *  specific Left/Right key).  Returns "Ctrl", "Shift", "Alt", "Win"/"Cmd". */
const char* ModifierDisplayName(const char* base);

} // namespace Shortcuts
