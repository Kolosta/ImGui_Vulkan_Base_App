#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/EventNormalizer.h>
#include <Shortcuts/ToolManager.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>

namespace Shortcuts {

ShortcutManager& ShortcutManager::Instance() {
    static ShortcutManager* instance = new ShortcutManager();
    return *instance;
}

void ShortcutManager::Initialize() {
    Load();
}

void ShortcutManager::Shutdown() {
    Save();
    actions_.clear();
    bindings_.clear();
    modalStack_.clear();
}

// ─── Action registry ─────────────────────────────────────────────────────────

void ShortcutManager::RegisterAction(const Action& action,
                                     const std::vector<EventSignature>& defaultBindings) {
    actions_[action.id] = action;

    auto it = bindings_.find(action.id);
    if (it == bindings_.end()) {
        ShortcutBinding b;
        b.actionId = action.id;
        b.defaults = defaultBindings;
        b.current  = defaultBindings;
        b.enabled  = true;
        bindings_[action.id] = b;
    } else {
        // refresh defaults (the caller may have re-registered with a new
        // default set after a code change). Keep current overrides.
        it->second.defaults = defaultBindings;
        if (it->second.current.empty() && !defaultBindings.empty()) {
            it->second.current = defaultBindings;
        }
    }
}

void ShortcutManager::UnregisterAction(const std::string& actionId) {
    actions_.erase(actionId);
    bindings_.erase(actionId);
    NotifyChanged();
}

const Action* ShortcutManager::GetAction(const std::string& actionId) const {
    auto it = actions_.find(actionId);
    return it != actions_.end() ? &it->second : nullptr;
}

std::vector<const Action*> ShortcutManager::GetAllActions() const {
    std::vector<const Action*> out;
    out.reserve(actions_.size());
    for (const auto& [id, a] : actions_) out.push_back(&a);
    return out;
}

std::vector<const Action*> ShortcutManager::GetActionsByCategory(ActionCategory cat) const {
    std::vector<const Action*> out;
    for (const auto& [id, a] : actions_) {
        if (a.category == cat) out.push_back(&a);
    }
    return out;
}

std::vector<const Action*> ShortcutManager::GetActionsForContext(const ShortcutContext& ctx) const {
    std::vector<const Action*> out;
    for (const auto& [id, a] : actions_) {
        if (a.requiredContext.Matches(ctx)) out.push_back(&a);
    }
    return out;
}

// ─── Bindings ────────────────────────────────────────────────────────────────

const ShortcutBinding* ShortcutManager::GetBinding(const std::string& actionId) const {
    auto it = bindings_.find(actionId);
    return it != bindings_.end() ? &it->second : nullptr;
}

void ShortcutManager::SetBindings(const std::string& actionId,
                                   const std::vector<EventSignature>& sigs) {
    auto& b = bindings_[actionId];
    if (b.actionId.empty()) b.actionId = actionId;
    // Preserve existing per-entry enable flags by index when possible; new
    // entries default to enabled.
    std::vector<uint8_t> newEnabled(sigs.size(), 1);
    for (size_t i = 0; i < sigs.size() && i < b.currentEnabled.size(); ++i)
        newEnabled[i] = b.currentEnabled[i];
    b.current        = sigs;
    b.currentEnabled = newEnabled;
    NotifyChanged();
}

void ShortcutManager::AddBinding(const std::string& actionId, const EventSignature& sig) {
    auto& b = bindings_[actionId];
    if (b.actionId.empty()) b.actionId = actionId;
    if (std::find(b.current.begin(), b.current.end(), sig) == b.current.end()) {
        b.current.push_back(sig);
        b.currentEnabled.push_back(1);
        NotifyChanged();
    }
}

void ShortcutManager::RemoveBinding(const std::string& actionId, const EventSignature& sig) {
    auto it = bindings_.find(actionId);
    if (it == bindings_.end()) return;
    auto& v = it->second.current;
    auto& e_v = it->second.currentEnabled;
    for (size_t i = 0; i < v.size(); ) {
        if (v[i] == sig) {
            v.erase(v.begin() + i);
            if (i < e_v.size()) e_v.erase(e_v.begin() + i);
        } else { ++i; }
    }
    NotifyChanged();
}

void ShortcutManager::RestoreDefaults(const std::string& actionId) {
    auto it = bindings_.find(actionId);
    if (it == bindings_.end()) return;
    it->second.RestoreDefaults();
    NotifyChanged();
}

void ShortcutManager::RestoreAllDefaults() {
    for (auto& [id, b] : bindings_) b.RestoreDefaults();
    NotifyChanged();
}

void ShortcutManager::RestoreBindingAt(const std::string& actionId, int index) {
    auto it = bindings_.find(actionId);
    if (it == bindings_.end()) return;
    auto& b = it->second;
    if (index < 0 || index >= static_cast<int>(b.defaults.size())) return;
    if (index < static_cast<int>(b.current.size())) {
        if (b.current[index] == b.defaults[index]) return;
        b.current[index] = b.defaults[index];
    } else {
        b.current.push_back(b.defaults[index]);
    }
    NotifyChanged();
}

void ShortcutManager::SetEnabled(const std::string& actionId, bool enabled) {
    auto it = bindings_.find(actionId);
    if (it == bindings_.end()) return;
    if (it->second.enabled != enabled) {
        it->second.enabled = enabled;
        NotifyChanged();
    }
}

void ShortcutManager::SetEntryEnabled(const std::string& actionId, int index, bool enabled) {
    auto it = bindings_.find(actionId);
    if (it == bindings_.end()) return;
    if (index < 0 || index >= static_cast<int>(it->second.current.size())) return;
    if (it->second.IsEntryEnabled(index) != enabled) {
        it->second.SetEntryEnabled(index, enabled);
        NotifyChanged();
    }
}

std::string ShortcutManager::GetShortcutString(const std::string& actionId) const {
    const ShortcutBinding* b = GetBinding(actionId);
    if (!b || b->current.empty()) return "";
    return b->current.front().ToString();
}

std::vector<std::string> ShortcutManager::GetShortcutStrings(const std::string& actionId) const {
    std::vector<std::string> out;
    const ShortcutBinding* b = GetBinding(actionId);
    if (!b) return out;
    for (const auto& sig : b->current) out.push_back(sig.ToString());
    return out;
}

// ─── Conflict detection ──────────────────────────────────────────────────────

static bool ContextsOverlap(const ShortcutContext& a, const ShortcutContext& b) {
    auto fieldOverlap = [](const std::string& x, const std::string& y) {
        // both wildcard, or one is wildcard, or equal
        return x.empty() || y.empty() || x == y;
    };
    return fieldOverlap(a.window, b.window) &&
           fieldOverlap(a.editor, b.editor) &&
           fieldOverlap(a.region, b.region) &&
           fieldOverlap(a.mode,   b.mode)   &&
           fieldOverlap(a.tool,   b.tool);
}

static bool ContextsAreEquivalent(const ShortcutContext& a, const ShortcutContext& b) {
    return a.window == b.window && a.editor == b.editor &&
           a.region == b.region && a.mode == b.mode && a.tool == b.tool;
}

std::vector<ShortcutConflict> ShortcutManager::DetectConflicts() const {
    std::vector<ShortcutConflict> conflicts;
    std::vector<std::string> ids;
    ids.reserve(bindings_.size());
    for (const auto& [id, _] : bindings_) ids.push_back(id);

    for (size_t i = 0; i < ids.size(); ++i) {
        const auto& b1 = bindings_.at(ids[i]);
        if (!b1.enabled) continue;
        const Action* a1 = GetAction(ids[i]);
        if (!a1) continue;
        for (size_t j = i + 1; j < ids.size(); ++j) {
            const auto& b2 = bindings_.at(ids[j]);
            if (!b2.enabled) continue;
            const Action* a2 = GetAction(ids[j]);
            if (!a2) continue;
            if (!ContextsOverlap(a1->requiredContext, a2->requiredContext)) continue;

            for (const auto& s1 : b1.current) {
                for (const auto& s2 : b2.current) {
                    if (s1 == s2) {
                        ShortcutConflict c;
                        c.signature = s1;
                        c.actionId1 = ids[i];
                        c.actionId2 = ids[j];
                        c.context1  = a1->requiredContext;
                        c.context2  = a2->requiredContext;
                        c.isHard    = ContextsAreEquivalent(a1->requiredContext,
                                                            a2->requiredContext);
                        conflicts.push_back(c);
                    }
                }
            }
        }
    }
    return conflicts;
}

std::vector<ShortcutConflict> ShortcutManager::CheckCandidate(const std::string& actionId,
                                                               const EventSignature& candidate) const {
    std::vector<ShortcutConflict> out;
    const Action* aSelf = GetAction(actionId);
    if (!aSelf) return out;
    for (const auto& [otherId, otherBind] : bindings_) {
        if (otherId == actionId) continue;
        if (!otherBind.enabled) continue;
        const Action* aOther = GetAction(otherId);
        if (!aOther) continue;
        if (!ContextsOverlap(aSelf->requiredContext, aOther->requiredContext)) continue;
        for (const auto& sig : otherBind.current) {
            if (sig == candidate) {
                ShortcutConflict c;
                c.signature = candidate;
                c.actionId1 = actionId;
                c.actionId2 = otherId;
                c.context1  = aSelf->requiredContext;
                c.context2  = aOther->requiredContext;
                c.isHard    = ContextsAreEquivalent(aSelf->requiredContext,
                                                    aOther->requiredContext);
                out.push_back(c);
            }
        }
    }
    return out;
}

// ─── Safety: dangerous-binding gate ──────────────────────────────────────────

std::string ShortcutManager::IsDangerousBinding(const std::string& actionId,
                                                const EventSignature& candidate) const {
    const Action* a = GetAction(actionId);
    if (a && a->allowUnsafeMouseBindings) return ""; // opt-in: anything goes
    if (!candidate.IsValid()) return ""; // empty signatures handled elsewhere

    const auto& m = candidate.modifiers;
    const bool hasMod = m.ctrl || m.shift || m.alt || m.super;
    const bool isGlobalCtx = a ? (a->requiredContext.editor.empty() &&
                                   a->requiredContext.region.empty() &&
                                   a->requiredContext.mode.empty()   &&
                                   a->requiredContext.tool.empty()) : true;

    auto isButtonAction = [&](MouseButton btn) {
        if (candidate.mouseButton != btn) return false;
        return candidate.type == EventType::MousePress ||
               candidate.type == EventType::MouseRelease ||
               candidate.type == EventType::MouseClick ||
               candidate.type == EventType::MouseDoubleClick;
    };
    auto isDragAnyDir = [&](MouseButton btn) {
        bool dragType = candidate.type == EventType::MouseDrag ||
            (candidate.type >= EventType::MouseDragNorth &&
             candidate.type <= EventType::MouseDragSouthWest);
        return dragType && candidate.mouseButton == btn;
    };

    // ── Hard blocks across the board ────────────────────────────────────
    // Plain Left Mouse action (Press / Release / Click / DoubleClick /
    // Drag) without any modifier always blocks the UI: every click would
    // fire the shortcut instead of selecting/clicking widgets.  The user
    // has no way to escape the binding once saved, so we refuse it.
    if (isButtonAction(MouseButton::Left) && !hasMod)
        return "Left Mouse action without modifiers would block every click "
               "in the UI (no way to interact afterwards).";
    if (isDragAnyDir(MouseButton::Left) && !hasMod)
        return "Left Mouse Drag without modifiers would block scrolling "
               "and selection across the whole app.";
    if (isButtonAction(MouseButton::Middle) && !hasMod &&
        candidate.type == EventType::MousePress)
        return "Middle Mouse Press without modifiers conflicts with "
               "normal navigation.";

    // Continuous motion / trackpad / mouse-move bindings would fire
    // constantly and freeze the application.  Always require a modifier.
    if ((candidate.type == EventType::MouseMove ||
         candidate.type == EventType::TrackpadPan ||
         candidate.type == EventType::TrackpadZoom ||
         candidate.type == EventType::TrackpadRotate ||
         candidate.type == EventType::TrackpadSmartRotate) && !hasMod)
        return "Continuous motion events without modifiers would fire "
               "every frame and freeze the application.";

    // Globally-scoped actions: be stricter.
    if (isGlobalCtx) {
        if (candidate.type == EventType::MouseRelease &&
            candidate.mouseButton == MouseButton::Right && !hasMod)
            return "Right Mouse Release on a global action collides with "
                   "context menus.";
    }
    return "";
}

// ─── Per-frame ───────────────────────────────────────────────────────────────

void ShortcutManager::BeginFrame() {
    bestHoverSpecificity_ = 0;
    currentContext_.window = "main";          // restore main scope (popup may override)
    currentContext_.editor.clear();
    currentContext_.region.clear();
    currentContext_.mode.clear();
    currentContext_.tool   = Tools::ToolManager::Instance().GetActiveTool();
    currentContext_.focusedItemId = static_cast<int>(ImGui::GetHoveredID());
}

void ShortcutManager::RegisterRegionContext(const char* windowName,
                                             const std::string& editorId,
                                             const std::string& regionId) {
    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                ImGuiHoveredFlags_ChildWindows)) return;
    int spec = 0;
    if (!editorId.empty()) ++spec;
    if (!regionId.empty()) ++spec;
    if (spec >= bestHoverSpecificity_) {
        bestHoverSpecificity_ = spec;
        currentContext_.editor = editorId;
        currentContext_.region = regionId;
    }
    (void)windowName;
}

void ShortcutManager::ProcessInput() {
    auto& norm = EventNormalizer::Instance();
    for (size_t i = 0; i < norm.Events().size(); ++i) {
        const Event& evt = norm.Events()[i];
        if (evt.consumed) continue;

        // Skip *bare* key events while a text input has focus, so typing
        // letters in the search box doesn't fire shortcuts.  Combinations
        // with Ctrl/Alt/Super/Shift+function keys are still dispatched —
        // those don't produce text and the user expects them to work even
        // while editing.
        ImGuiIO& io = ImGui::GetIO();
        bool isKeyEvent =
            evt.signature.type == EventType::KeyPress ||
            evt.signature.type == EventType::KeyRelease ||
            evt.signature.type == EventType::KeyClick ||
            evt.signature.type == EventType::KeyDoubleClick;
        if (isKeyEvent && io.WantTextInput) {
            const auto& m = evt.signature.modifiers;
            bool hasMod = m.ctrl || m.alt || m.super;
            ImGuiKey k = evt.signature.key;
            bool isFunctionKey =
                (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) ||
                k == ImGuiKey_Escape || k == ImGuiKey_Tab;
            if (!hasMod && !isFunctionKey) continue;
        }

        // Modal stack first
        if (!modalStack_.empty()) {
            if (modalStack_.back()->HandleEvent(evt)) {
                norm.Consume(i);
                continue;
            }
        }

        if (DispatchEvent(evt)) norm.Consume(i);
    }
}

bool ShortcutManager::DispatchEvent(const Event& observedEvent) {
    struct Candidate {
        const Action*         action;
        const ShortcutBinding* binding;
        int                   specificity;
        int                   priority;
    };
    std::vector<Candidate> cands;

    for (const auto& [id, b] : bindings_) {
        if (!b.enabled) continue;
        const Action* a = GetAction(id);
        if (!a) continue;
        if (!a->requiredContext.Matches(currentContext_)) continue;
        if (a->pollFn && !a->pollFn()) continue;

        for (size_t si = 0; si < b.current.size(); ++si) {
            const auto& sig = b.current[si];
            if (!b.IsEntryEnabled(si)) continue;
            // Skip bindings that are not ready to fire (empty/half-edited,
            // or "Any+nothing" wildcards that would otherwise match
            // everything and trigger on the next keypress).
            if (!sig.IsValid()) continue;
            if (!sig.Match(observedEvent.signature)) continue;
            if (observedEvent.signature.repeat && !a->allowRepeat) continue;

            // Per-binding drag distance gate: if this signature is a
            // drag and the actual move hasn't reached the per-binding
            // threshold yet, defer until next frame (drag events are
            // re-emitted continuously while held).
            bool isDragSig =
                sig.type == EventType::MouseDrag ||
                (sig.type >= EventType::MouseDragNorth &&
                 sig.type <= EventType::MouseDragSouthWest);
            if (isDragSig) {
                float threshold = sig.dragThreshold;
                if (threshold <= 0.0f) {
                    threshold = EventNormalizer::Instance().GetDragThreshold();
                }
                float dx = observedEvent.dragDelta.x;
                float dy = observedEvent.dragDelta.y;
                float actual = std::sqrt(dx * dx + dy * dy);
                if (actual < threshold) continue;
            }

            cands.push_back({a, &b, a->requiredContext.Specificity(), a->priority});
            break;  // one match per action per event
        }
    }

    if (cands.empty()) return false;

    std::stable_sort(cands.begin(), cands.end(),
        [](const Candidate& x, const Candidate& y) {
            if (x.specificity != y.specificity) return x.specificity > y.specificity;
            return x.priority > y.priority;
        });

    const Action* winner = cands.front().action;
    if (winner->callback) winner->callback();
    return true;
}

// ─── Modal stack ─────────────────────────────────────────────────────────────

void ShortcutManager::PushModal(std::unique_ptr<ModalSession> session) {
    if (session) modalStack_.push_back(std::move(session));
}

void ShortcutManager::PopModal() {
    if (!modalStack_.empty()) modalStack_.pop_back();
}

ModalSession* ShortcutManager::TopModal() const {
    return modalStack_.empty() ? nullptr : modalStack_.back().get();
}

// ─── Status bar helpers ──────────────────────────────────────────────────────

std::vector<const Action*> ShortcutManager::GetStatusBarActions(int maxCount) const {
    struct Entry { const Action* a; int spec; int prio; };
    std::vector<Entry> entries;

    for (const auto& [id, a] : actions_) {
        if (!a.requiredContext.Matches(currentContext_)) continue;
        const ShortcutBinding* b = GetBinding(id);
        if (!b || !b->enabled || b->current.empty()) continue;
        entries.push_back({&a, a.requiredContext.Specificity(), a.priority});
    }

    std::stable_sort(entries.begin(), entries.end(),
        [](const Entry& x, const Entry& y) {
            if (x.spec != y.spec) return x.spec > y.spec;
            if (x.prio != y.prio) return x.prio > y.prio;
            return static_cast<int>(x.a->category) < static_cast<int>(y.a->category);
        });

    if (static_cast<int>(entries.size()) > maxCount) entries.resize(maxCount);
    std::vector<const Action*> out;
    out.reserve(entries.size());
    for (const auto& e : entries) out.push_back(e.a);
    return out;
}

// ─── Persistence ─────────────────────────────────────────────────────────────

void ShortcutManager::NotifyChanged() {
    Save();
}

void ShortcutManager::Save() {
    std::ofstream out("shortcuts.dat", std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "[shortcuts] failed to open shortcuts.dat for writing\n";
        return;
    }
    uint32_t magic   = kMagic;
    uint32_t version = kVersion;
    out.write(reinterpret_cast<const char*>(&magic),   sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));

    uint32_t count = static_cast<uint32_t>(bindings_.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [id, b] : bindings_) {
        uint32_t idLen = static_cast<uint32_t>(id.length());
        out.write(reinterpret_cast<const char*>(&idLen), sizeof(idLen));
        out.write(id.data(), idLen);

        uint8_t enabled = b.enabled ? 1 : 0;
        out.write(reinterpret_cast<const char*>(&enabled), sizeof(enabled));

        uint32_t evCount = static_cast<uint32_t>(b.current.size());
        out.write(reinterpret_cast<const char*>(&evCount), sizeof(evCount));

        for (size_t si = 0; si < b.current.size(); ++si) {
            const auto& sig = b.current[si];
            uint8_t  type     = static_cast<uint8_t>(sig.type);
            int32_t  key      = static_cast<int32_t>(sig.key);
            uint8_t  mb       = static_cast<uint8_t>(sig.mouseButton);
            uint8_t  mods     = sig.modifiers.ToBits();
            uint8_t  sides    = sig.modifiers.SideBits();
            uint8_t  flags    = sig.repeat ? 0x01 : 0x00;
            uint8_t  entryEn  = b.IsEntryEnabled(si) ? 1 : 0;
            float    dragT    = sig.dragThreshold;
            out.write(reinterpret_cast<const char*>(&type),    sizeof(type));
            out.write(reinterpret_cast<const char*>(&key),     sizeof(key));
            out.write(reinterpret_cast<const char*>(&mb),      sizeof(mb));
            out.write(reinterpret_cast<const char*>(&mods),    sizeof(mods));
            out.write(reinterpret_cast<const char*>(&sides),   sizeof(sides));
            out.write(reinterpret_cast<const char*>(&flags),   sizeof(flags));
            out.write(reinterpret_cast<const char*>(&entryEn), sizeof(entryEn));
            out.write(reinterpret_cast<const char*>(&dragT),   sizeof(dragT));
        }
    }
}

void ShortcutManager::Load() {
    std::ifstream in("shortcuts.dat", std::ios::binary);
    if (!in.is_open()) return;

    uint32_t magic = 0, version = 0;
    in.read(reinterpret_cast<char*>(&magic),   sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!in.good() || magic != kMagic || version != kVersion) {
        // Stale or unknown format - silently ignore so defaults stick.
        return;
    }

    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!in.good()) return;

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idLen = 0;
        in.read(reinterpret_cast<char*>(&idLen), sizeof(idLen));
        if (!in.good() || idLen > 4096) return;
        std::string actionId(idLen, '\0');
        in.read(actionId.data(), idLen);

        uint8_t enabled = 1;
        in.read(reinterpret_cast<char*>(&enabled), sizeof(enabled));

        uint32_t evCount = 0;
        in.read(reinterpret_cast<char*>(&evCount), sizeof(evCount));
        if (!in.good() || evCount > 64) return;

        std::vector<EventSignature> sigs;
        std::vector<uint8_t>        sigEnabled;
        sigs.reserve(evCount);
        sigEnabled.reserve(evCount);
        for (uint32_t j = 0; j < evCount; ++j) {
            uint8_t type = 0; int32_t key = 0;
            uint8_t mb = 0; uint8_t mods = 0; uint8_t sides = 0;
            uint8_t flags = 0; uint8_t entryEn = 1; float dragT = 0.0f;
            in.read(reinterpret_cast<char*>(&type),    sizeof(type));
            in.read(reinterpret_cast<char*>(&key),     sizeof(key));
            in.read(reinterpret_cast<char*>(&mb),      sizeof(mb));
            in.read(reinterpret_cast<char*>(&mods),    sizeof(mods));
            in.read(reinterpret_cast<char*>(&sides),   sizeof(sides));
            in.read(reinterpret_cast<char*>(&flags),   sizeof(flags));
            in.read(reinterpret_cast<char*>(&entryEn), sizeof(entryEn));
            in.read(reinterpret_cast<char*>(&dragT),   sizeof(dragT));
            EventSignature sig;
            sig.type = static_cast<EventType>(type);
            sig.key  = static_cast<ImGuiKey>(key);
            sig.mouseButton  = static_cast<MouseButton>(mb);
            sig.modifiers    = ModifierMask::FromBitsAndSides(mods, sides);
            sig.repeat       = (flags & 0x01) != 0;
            sig.dragThreshold = dragT;
            sigs.push_back(sig);
            sigEnabled.push_back(entryEn);
        }

        // Stash in a pending map so RegisterAction can pick them up if
        // called after Load. For now just upsert.
        ShortcutBinding b;
        b.actionId = actionId;
        b.current  = sigs;
        b.currentEnabled = sigEnabled;
        b.enabled  = enabled != 0;
        // defaults remain whatever code register_action will set later
        auto it = bindings_.find(actionId);
        if (it != bindings_.end()) b.defaults = it->second.defaults;
        bindings_[actionId] = b;
    }
}

} // namespace Shortcuts
