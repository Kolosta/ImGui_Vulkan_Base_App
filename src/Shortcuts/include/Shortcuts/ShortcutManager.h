#pragma once

#include <Shortcuts/Event.h>
#include <Shortcuts/ShortcutContext.h>
#include <Shortcuts/Action.h>
#include <Shortcuts/ModalSession.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Shortcuts {

/**
 * Conflict descriptor produced by DetectConflicts() and HasConflict().
 *
 *   isHard = true  : same key in fully overlapping context (will collide
 *                    every frame - shown red in editor)
 *   isHard = false : overlap is partial (one context is more specific or
 *                    has a different region) - shown yellow as warning
 */
struct ShortcutConflict {
    EventSignature signature;
    std::string actionId1;
    std::string actionId2;
    ShortcutContext context1;
    ShortcutContext context2;
    bool isHard = false;
};

class ShortcutManager {
public:
    static ShortcutManager& Instance();

    // ─── Lifecycle ────────────────────────────────────────────────────────
    void Initialize();
    void Shutdown();

    // ─── Action registry ──────────────────────────────────────────────────
    void RegisterAction(const Action& action,
                        const std::vector<EventSignature>& defaultBindings = {});
    void UnregisterAction(const std::string& actionId);

    const Action* GetAction(const std::string& actionId) const;
    std::vector<const Action*> GetAllActions() const;
    std::vector<const Action*> GetActionsByCategory(ActionCategory cat) const;
    std::vector<const Action*> GetActionsForContext(const ShortcutContext& ctx) const;

    // ─── Bindings ─────────────────────────────────────────────────────────
    const ShortcutBinding* GetBinding(const std::string& actionId) const;
    void SetBindings(const std::string& actionId, const std::vector<EventSignature>& sigs);
    void AddBinding (const std::string& actionId, const EventSignature& sig);
    void RemoveBinding(const std::string& actionId, const EventSignature& sig);
    void RestoreDefaults(const std::string& actionId);
    void RestoreAllDefaults();

    /**
     * Restore the original default value of binding `index` while leaving
     * other custom bindings intact.  Behaviour:
     *   - if index < defaults.size() and index < current.size():
     *       overwrite current[index] with defaults[index]
     *   - if index < defaults.size() but index >= current.size():
     *       append defaults[index]
     *   - otherwise: do nothing (no default to restore at that slot)
     */
    void RestoreBindingAt(const std::string& actionId, int index);
    void SetEnabled(const std::string& actionId, bool enabled);
    /** Toggle a single shortcut entry within the action's binding list. */
    void SetEntryEnabled(const std::string& actionId, int index, bool enabled);

    /** First binding rendered as a string ("Ctrl+S" etc.). Empty if none. */
    std::string GetShortcutString(const std::string& actionId) const;
    /** Full list of bindings as ToString() for displaying multi-shortcut actions. */
    std::vector<std::string> GetShortcutStrings(const std::string& actionId) const;

    // ─── Conflict detection ───────────────────────────────────────────────
    std::vector<ShortcutConflict> DetectConflicts() const;
    /** Conflicts that would occur if `candidate` were added to actionId. */
    std::vector<ShortcutConflict> CheckCandidate(const std::string& actionId,
                                                 const EventSignature& candidate) const;

    /**
     * Validate that the candidate event signature is safe to bind on the
     * given action.  Returns an empty string when the binding is allowed.
     * Returns a human-readable reason when it is not (e.g. "Left mouse
     * press without modifiers would block all UI interaction").  Actions
     * with `allowUnsafeMouseBindings = true` bypass the check.
     */
    std::string IsDangerousBinding(const std::string& actionId,
                                   const EventSignature& candidate) const;

    // ─── Per-frame ────────────────────────────────────────────────────────
    /** Called by Application after EventNormalizer::Frame(). Updates context,
     *  drains the event queue and dispatches matching actions. */
    void ProcessInput();

    /** Region/editor scope helper, called during the render pass of each panel.
     *  Updates currentContext_ when the calling window is hovered. */
    void RegisterRegionContext(const char* windowName,
                               const std::string& editorId,
                               const std::string& regionId = "content");

    /** Force the editor/region for this frame regardless of hover (for popups). */
    void SetCurrentContext(const ShortcutContext& ctx) { currentContext_ = ctx; }
    const ShortcutContext& GetCurrentContext() const { return currentContext_; }

    /** Called once per frame by Application before any panel renders, to clear
     *  the per-frame "best hover" state and pull tool from ToolManager. */
    void BeginFrame();

    // ─── Modal stack ──────────────────────────────────────────────────────
    void PushModal(std::unique_ptr<ModalSession> session);
    void PopModal();
    ModalSession* TopModal() const;

    // ─── Status-bar helpers ───────────────────────────────────────────────
    /** Up to `maxCount` most relevant actions to display, sorted by
     *  context specificity desc then category. */
    std::vector<const Action*> GetStatusBarActions(int maxCount = 5) const;

    // ─── Persistence (manual; auto-saved on every mutation) ───────────────
    void Save();
    void Load();

private:
    ShortcutManager() = default;

    bool DispatchEvent(const Event& observedEvent);
    void NotifyChanged();

    // storage
    std::map<std::string, Action>          actions_;
    std::map<std::string, ShortcutBinding> bindings_;
    ShortcutContext                        currentContext_;
    int                                    bestHoverSpecificity_ = 0;
    std::vector<std::unique_ptr<ModalSession>> modalStack_;

    // persistence
    static constexpr uint32_t kMagic   = 0x53484354; // "SHCT"
    static constexpr uint32_t kVersion = 6;
};

} // namespace Shortcuts
