#pragma once

#include <Shortcuts/Event.h>
#include <Shortcuts/ShortcutContext.h>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace Shortcuts {

enum class ActionCategory : uint8_t {
    Application = 0,
    File,
    Edit,
    View,
    Window,
    Tool,
    Selection,
    Transform,
    Navigation,
    Custom
};

const char* ActionCategoryName(ActionCategory c);

/**
 * Single executable command bound to one or more event signatures.
 *
 *   id            "file.save"
 *   name          "Save File"
 *   description   tooltip text
 *   category      grouping bucket in the editor tree
 *   requiredContext   only fires when current context matches (empty = global)
 *   callback      synchronously executed body
 *   pollFn        optional gate; return false to skip
 *   isModal       launches a modal session (callback may push to ShortcutManager)
 *   allowRepeat   if false, repeat events are dropped before reaching this action
 *   priority      tiebreaker after specificity (higher = wins)
 */
struct Action {
    std::string  id;
    std::string  name;
    std::string  description;
    ActionCategory category = ActionCategory::Application;
    ShortcutContext requiredContext;
    std::function<void()> callback;
    std::function<bool()> pollFn;
    bool isModal     = false;
    bool allowRepeat = false;
    int  priority    = 0;

    /**
     * When false (default), the editor refuses to bind this action to
     * "dangerous" inputs that would block normal application interaction
     * (plain Left/Middle Mouse Press without modifiers, plain mouse drag
     * without modifiers, etc.).  Specific actions that genuinely need
     * those inputs — typically tool-scoped actions on a canvas region —
     * can opt-in by setting this to true.
     */
    bool allowUnsafeMouseBindings = false;
};

struct ShortcutBinding {
    std::string actionId;
    std::vector<EventSignature> defaults;
    std::vector<EventSignature> current;

    /** Per-binding enable flags: parallel to `current`.  An empty vector
     *  is treated as "all enabled".  When the user disables a single
     *  shortcut entry the entry remains in `current` but does not fire. */
    std::vector<uint8_t> currentEnabled;

    /** Whole-action enable: legacy global flag.  When false, every
     *  signature in `current` is skipped regardless of `currentEnabled`. */
    bool enabled = true;

    bool IsEntryEnabled(size_t i) const {
        if (i >= current.size()) return false;
        if (i >= currentEnabled.size()) return true;
        return currentEnabled[i] != 0;
    }
    void SetEntryEnabled(size_t i, bool on) {
        if (i >= current.size()) return;
        if (currentEnabled.size() < current.size())
            currentEnabled.resize(current.size(), 1);
        currentEnabled[i] = on ? 1 : 0;
    }

    void RestoreDefaults() {
        current = defaults;
        currentEnabled.assign(defaults.size(), 1);
    }
};

} // namespace Shortcuts
