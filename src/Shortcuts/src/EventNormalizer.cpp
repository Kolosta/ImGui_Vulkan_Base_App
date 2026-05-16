#include <Shortcuts/EventNormalizer.h>
#include <imgui.h>
#include <cmath>

namespace Shortcuts {

EventNormalizer& EventNormalizer::Instance() {
    static EventNormalizer* instance = new EventNormalizer();
    return *instance;
}

void EventNormalizer::Initialize() {
    frameEvents_.clear();
    for (auto& t : keyTracks_)   t = {};
    for (auto& t : mouseTracks_) t = {};
}

int EventNormalizer::Frame() {
    frameEvents_.clear();
    EmitKeyEvents();
    EmitMouseEvents();
    EmitWheelEvents();
    return static_cast<int>(frameEvents_.size());
}

void EventNormalizer::Consume(size_t index) {
    if (index < frameEvents_.size()) frameEvents_[index].consumed = true;
}

void EventNormalizer::EmitKeyEvents() {
    ImGuiIO& io = ImGui::GetIO();
    const float now = static_cast<float>(ImGui::GetTime());
    const ModifierMask mods = ModifierMask::FromImGuiIO();

    for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; ++i) {
        ImGuiKey key = static_cast<ImGuiKey>(i);
        if (IsModifierKey(key)) continue;

        bool downNow = ImGui::IsKeyDown(key);
        KeyTrack& t  = keyTracks_[i];

        bool pressed  = ImGui::IsKeyPressed(key, /*repeat=*/false);
        bool released = ImGui::IsKeyReleased(key);
        bool repeated = ImGui::IsKeyPressed(key, /*repeat=*/true) && !pressed;

        if (pressed) {
            Event e;
            e.signature.type = EventType::KeyPress;
            e.signature.key  = key;
            e.signature.modifiers = mods;
            e.signature.repeat = false;
            e.mousePos = io.MousePos;
            frameEvents_.push_back(e);

            t.wasDown    = true;
            t.pressTimeS = now;
        }

        if (repeated) {
            Event e;
            e.signature.type = EventType::KeyPress;
            e.signature.key  = key;
            e.signature.modifiers = mods;
            e.signature.repeat = true;
            e.mousePos = io.MousePos;
            frameEvents_.push_back(e);
        }

        if (released) {
            Event e;
            e.signature.type = EventType::KeyRelease;
            e.signature.key  = key;
            e.signature.modifiers = mods;
            e.mousePos = io.MousePos;
            frameEvents_.push_back(e);

            if (t.wasDown) {
                float held = (now - t.pressTimeS) * 1000.0f;
                if (held < kClickTimeMs) {
                    Event ce;
                    ce.signature.type = EventType::KeyClick;
                    ce.signature.key  = key;
                    ce.signature.modifiers = mods;
                    frameEvents_.push_back(ce);

                    if (t.lastClickTimeS > 0.0f &&
                        (now - t.lastClickTimeS) * 1000.0f < kDoubleTimeMs) {
                        Event de;
                        de.signature.type = EventType::KeyDoubleClick;
                        de.signature.key  = key;
                        de.signature.modifiers = mods;
                        frameEvents_.push_back(de);
                        t.lastClickTimeS = -1.0f;
                    } else {
                        t.lastClickTimeS = now;
                    }
                }
            }

            t.wasDown = false;
        }

        // safety: refresh wasDown if down state diverges (rare)
        if (!pressed && !released) t.wasDown = downNow;
    }
}

static MouseButton ButtonFromIndex(int idx) {
    switch (idx) {
        case ImGuiMouseButton_Left:   return MouseButton::Left;
        case ImGuiMouseButton_Right:  return MouseButton::Right;
        case ImGuiMouseButton_Middle: return MouseButton::Middle;
        case 3: return MouseButton::X1;
        case 4: return MouseButton::X2;
        default: return MouseButton::None;
    }
}

void EventNormalizer::EmitMouseEvents() {
    ImGuiIO& io = ImGui::GetIO();
    const float now = static_cast<float>(ImGui::GetTime());
    const ModifierMask mods = ModifierMask::FromImGuiIO();

    for (int i = 0; i < 5; ++i) {
        MouseButton btn = ButtonFromIndex(i);
        if (btn == MouseButton::None) continue;

        bool clicked  = ImGui::IsMouseClicked(i, false);
        bool released = ImGui::IsMouseReleased(i);
        bool downNow  = ImGui::IsMouseDown(i);
        bool dbl      = ImGui::IsMouseDoubleClicked(i);
        MouseTrack& t = mouseTracks_[i];

        if (clicked) {
            Event e;
            e.signature.type = EventType::MousePress;
            e.signature.mouseButton = btn;
            e.signature.modifiers   = mods;
            e.mousePos = io.MousePos;
            frameEvents_.push_back(e);

            t.wasDown    = true;
            t.pressTimeS = now;
            t.pressPos   = io.MousePos;
            t.draggedThisHold = false;
            t.dragAccum  = ImVec2(0.0f, 0.0f);
        }

        if (downNow && t.wasDown) {
            ImVec2 totalDelta = ImVec2(io.MousePos.x - t.pressPos.x,
                                       io.MousePos.y - t.pressPos.y);
            float dist = std::sqrt(totalDelta.x * totalDelta.x +
                                   totalDelta.y * totalDelta.y);
            // Use a low gate (2 px) so the resolver gets the earliest
            // possible signal.  Per-binding `dragThreshold` is then
            // re-checked in ShortcutManager::DispatchEvent against
            // event.dragDelta so high-threshold bindings still wait.  We
            // re-emit each frame while dragging so the binding can fire
            // the moment its per-binding distance is crossed.
            const float kMinGate = 2.0f;
            if (dist > kMinGate) {
                // 8-way classification.  Use atan2 with screen y inverted so
                // "north" is up.  Each cone is 45° wide, centred on the axis.
                float ang = std::atan2(-totalDelta.y, totalDelta.x); // [-pi, pi]
                static const EventType kDirs[] = {
                    EventType::MouseDragEast,
                    EventType::MouseDragNorthEast,
                    EventType::MouseDragNorth,
                    EventType::MouseDragNorthWest,
                    EventType::MouseDragWest,
                    EventType::MouseDragSouthWest,
                    EventType::MouseDragSouth,
                    EventType::MouseDragSouthEast,
                };
                // Quantise angle into 0..7 buckets (E=0, NE=1, N=2, ...)
                float quant = ang / (3.14159265f / 4.0f); // [-4, 4]
                int idx = static_cast<int>(std::floor(quant + 0.5f)); // round
                idx = ((idx % 8) + 8) % 8;
                EventType base = kDirs[idx];

                Event e;
                e.signature.type = base;
                e.signature.mouseButton = btn;
                e.signature.modifiers   = mods;
                e.dragDelta = totalDelta;
                e.mousePos  = io.MousePos;
                frameEvents_.push_back(e);

                t.draggedThisHold = true;
            }
        }

        if (released) {
            Event e;
            e.signature.type = EventType::MouseRelease;
            e.signature.mouseButton = btn;
            e.signature.modifiers   = mods;
            e.mousePos = io.MousePos;
            frameEvents_.push_back(e);

            if (t.wasDown && !t.draggedThisHold) {
                float held = (now - t.pressTimeS) * 1000.0f;
                if (held < kClickTimeMs) {
                    Event ce;
                    ce.signature.type = EventType::MouseClick;
                    ce.signature.mouseButton = btn;
                    ce.signature.modifiers   = mods;
                    frameEvents_.push_back(ce);
                }
            }
            t.wasDown = false;
            t.draggedThisHold = false;
        }

        if (dbl) {
            Event e;
            e.signature.type = EventType::MouseDoubleClick;
            e.signature.mouseButton = btn;
            e.signature.modifiers   = mods;
            frameEvents_.push_back(e);
        }
    }
}

void EventNormalizer::EmitWheelEvents() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel == 0.0f) return;
    Event e;
    e.signature.type = io.MouseWheel > 0.0f ? EventType::WheelUp : EventType::WheelDown;
    e.signature.modifiers = ModifierMask::FromImGuiIO();
    e.wheelDelta = io.MouseWheel;
    e.mousePos = io.MousePos;
    frameEvents_.push_back(e);
}

} // namespace Shortcuts
