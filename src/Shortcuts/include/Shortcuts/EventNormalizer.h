#pragma once

#include <Shortcuts/Event.h>
#include <vector>
#include <array>

namespace Shortcuts {

/**
 * Reads ImGui IO each frame and emits a queue of normalised events.
 *
 * Detection rules:
 *   KeyPress       : ImGui::IsKeyPressed(key, repeat=false)
 *   KeyRelease     : ImGui::IsKeyReleased(key)
 *   KeyClick       : last press → release within < 250 ms
 *   KeyDoubleClick : two clicks within 350 ms (uses ImGui internal)
 *   MousePress/Release : ImGui::IsMouseClicked / Released
 *   MouseClick     : press → release without exceeding drag threshold
 *   MouseDoubleClick : ImGui::IsMouseDoubleClicked
 *   MouseDrag      : when MouseDelta cumulates past kDragThreshold logical px
 *   MouseDragN/S/E/W : drag with dominant axis sign
 *   WheelUp / WheelDown : io.MouseWheel != 0
 *
 * The popup capture path uses Frame() output directly — no need to
 * subscribe.  ProcessInput() also drains the same Frame() queue.
 */
class EventNormalizer {
public:
    static EventNormalizer& Instance();

    /** Reset internal state (called once after ImGui init). */
    void Initialize();

    /**
     * Drain ImGui IO into the event queue. Call once per frame BEFORE
     * any consumer reads `Events()`.
     * Returns the count of events emitted this frame.
     */
    int Frame();

    /** Events normalised this frame (read-only view). */
    const std::vector<Event>& Events() const { return frameEvents_; }

    /** Mark an event as consumed (so further consumers can skip it). */
    void Consume(size_t index);

    /** Override the drag-distance threshold (logical pixels).  Application
     *  feeds this from the design-system token semantic.shortcut.dragThreshold
     *  every frame so DS overrides take effect immediately. */
    void SetDragThreshold(float pixels) { dragThreshold_ = pixels; }
    float GetDragThreshold() const { return dragThreshold_; }

private:
    EventNormalizer() = default;

    void EmitKeyEvents();
    void EmitMouseEvents();
    void EmitWheelEvents();

    std::vector<Event> frameEvents_;

    static constexpr float kClickTimeMs   = 250.0f;
    static constexpr float kDoubleTimeMs  = 350.0f;
    float dragThreshold_ = 6.0f;     // logical pixels (settable via DS token)

    struct KeyTrack {
        bool  wasDown      = false;
        float pressTimeS   = 0.0f;
        float lastClickTimeS = -1.0f;
    };
    std::array<KeyTrack, ImGuiKey_NamedKey_END> keyTracks_;

    struct MouseTrack {
        bool   wasDown      = false;
        float  pressTimeS   = 0.0f;
        ImVec2 pressPos     = ImVec2(0.0f, 0.0f);
        bool   draggedThisHold = false;
        ImVec2 dragAccum    = ImVec2(0.0f, 0.0f);
    };
    std::array<MouseTrack, 5> mouseTracks_;
};

} // namespace Shortcuts
