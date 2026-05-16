#pragma once

#include <Shortcuts/Event.h>
#include <string>
#include <vector>

namespace Shortcuts {

struct ModalKeymapHint {
    EventSignature trigger;
    std::string    label;
};

/**
 * Base class for a modal operator (Blender's "modal handler").
 *
 * Lifecycle:
 *   ShortcutManager::PushModal(unique_ptr) — installs as topmost
 *   For each frame, ShortcutManager forwards events to top->HandleEvent(...)
 *   Returning true consumes the event and keeps the session active
 *   Returning false propagates the event to the normal resolver
 *
 * The session is responsible for calling ShortcutManager::PopModal()
 * (typically from OnConfirm / OnCancel).
 *
 * Hints() lets the status bar advertise the modal's sub-keymap.
 */
class ModalSession {
public:
    virtual ~ModalSession() = default;

    virtual const std::string& Name() const = 0;
    virtual bool HandleEvent(const Event& e) = 0;
    virtual void OnConfirm() {}
    virtual void OnCancel() {}
    virtual std::vector<ModalKeymapHint> Hints() const { return {}; }
};

} // namespace Shortcuts
