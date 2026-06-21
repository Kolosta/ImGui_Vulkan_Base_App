#include <UI/Shortcuts/ShortcutEditor.h>
#include <UI/Widgets/IconWidgets.h>
#include <UI/Shortcuts/KeyCap.h>
#include <UI/Shortcuts/ShortcutCaptureField.h>
#include <UI/Widgets/ButtonGroup.h>
#include <UI/Widgets/ScrollArea.h>
#include <UI/Widgets/Checkbox.h>
#include <Shortcuts/ShortcutManager.h>
#include <Shortcuts/EventNormalizer.h>
#include <Shortcuts/ToolManager.h>
#include <DesignSystem/DesignSystem.h>
#include <VectorGraphics/IconManager.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace UI {

namespace {

// Token-typed Safe* (Tok auto-follows TokName(): Spectrum-2 rename safe).
ImVec4 SafeColor(DesignSystem::Tok t, ImVec4 fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetColor(t); }
    catch (...) { return fallback; }
}

float SafeFloat(DesignSystem::Tok t, float fallback) {
    try { return DesignSystem::DesignSystem::Instance().GetFloat(t); }
    catch (...) { return fallback; }
}

bool MatchesSearch(const Shortcuts::Action& a, const char* needle) {
    if (!needle || !*needle) return true;
    auto contains = [needle](const std::string& haystack) {
        std::string h = haystack;
        std::string n = needle;
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return h.find(n) != std::string::npos;
    };
    if (contains(a.name) || contains(a.id) || contains(a.description)) return true;
    auto& sm = Shortcuts::ShortcutManager::Instance();
    for (const auto& s : sm.GetShortcutStrings(a.id)) {
        if (contains(s)) return true;
    }
    return false;
}


// ──────────────────────────────────────────────────────────────────────────
// Square icon button (uses VectorGraphics::IconManager + custom hit box)
// ──────────────────────────────────────────────────────────────────────────
bool IconSquareButton(const char* id, const char* iconId, float size,
                      const char* tooltip, ImVec4 tintIcon) {
    DesignSystem::DesignSystem::ComponentScope _cs("IconButton");
    ImGui::PushID(id);

    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = ds.GetGlobalScale();
    float radius = SafeFloat(DesignSystem::Tok::C_IconButton_CornerRadius, 3.0f) * scale;

    ImVec4 bg     = SafeColor(DesignSystem::Tok::C_IconButton_Background,
                              ImVec4(0.16f, 0.16f, 0.18f, 1.0f));
    ImVec4 bgHov  = SafeColor(DesignSystem::Tok::C_IconButton_BackgroundHover,
                              ImVec4(0.24f, 0.24f, 0.27f, 1.0f));
    ImVec4 bgAct  = SafeColor(DesignSystem::Tok::C_IconButton_BackgroundDown,
                              ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImVec4 border = SafeColor(DesignSystem::Tok::C_IconButton_Border,
                              ImVec4(0.40f, 0.40f, 0.45f, 1.0f));

    ImVec2 cur = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##btn", ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();

    ImVec4 fill = bg;
    if (active)       fill = bgAct;
    else if (hovered) fill = bgHov;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 maxPt(cur.x + size, cur.y + size);
    dl->AddRectFilled(cur, maxPt, ImGui::ColorConvertFloat4ToU32(fill), radius);
    dl->AddRect(cur, maxPt, ImGui::ColorConvertFloat4ToU32(border), radius, 0, 1.0f);

    // Inset the icon
    float pad = std::max(2.0f, std::floor(size * 0.18f));
    float iconSize = size - pad * 2.0f;
    auto& im = VectorGraphics::IconManager::Instance();
    auto md  = im.GetDefaultMetadata(iconId);
    // Force the icon's primary token into the requested tint by forcing
    // multicolor scheme with an explicit colour for every zone.
    md.scheme = VectorGraphics::IconColorScheme::Multicolor;
    for (auto& z : md.colorZones) z.customColor = tintIcon;
    im.RenderIcon(dl, iconId, ImVec2(cur.x + pad, cur.y + pad), iconSize, md);

    if (hovered && tooltip && *tooltip) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return clicked;
}

// ──────────────────────────────────────────────────────────────────────────
// Modifier group + single toggle, both built on the reusable ButtonGroup
// component so border rendering / states / tokens are consistent.
// ──────────────────────────────────────────────────────────────────────────

constexpr float kModGroupLogicalW = 60.0f;

float ModGroupWidth() {
    return kModGroupLogicalW *
           DesignSystem::DesignSystem::Instance().GetGlobalScale();
}

// Single stand-alone toggle (e.g. "Any", "Kbd|Mouse" pair element).
// Rendered through ButtonGroup so it shares the exact same visuals.
bool SingleToggle(const char* id, const char* label, bool active,
                  ImVec2 size, const char* tooltip = "") {
    ButtonGroup g(id);
    g.SetGrid({ size.x }, { size.y });
    g.AddCell(label, 0, 0, 1, 1, active, /*enabled=*/true,
              tooltip ? tooltip : "");
    return g.Render().clickedIndex == 0;
}

// Modifier group: "Ctrl" (full width, top) with a flush "L | R" sub-row.
// The whole thing is one ButtonGroup so the shared borders are crisp and
// the hovered/active cell owns its outline.  Footprint is fixed whether
// the side sub-row is enabled or not.
bool RenderModifierGroup(const char* baseName,
                        bool& enabled,
                        Shortcuts::ModifierSide& side) {
    float h    = ImGui::GetFrameHeight();
    float w    = ModGroupWidth();
    float subH = h * 0.65f;
    bool  changed = false;

    std::string display = Shortcuts::ModifierDisplayName(baseName);

    bool isL = (side == Shortcuts::ModifierSide::LeftOnly) ||
               (side == Shortcuts::ModifierSide::Both);
    bool isR = (side == Shortcuts::ModifierSide::RightOnly) ||
               (side == Shortcuts::ModifierSide::Both);

    ButtonGroup g((std::string("##modgrp_") + baseName).c_str());
    // 2 columns (w/2 each), 2 rows (main h, sub subH).
    g.SetGrid({ w * 0.5f, w * 0.5f }, { h, subH });
    // Row 0: main toggle spanning both columns.
    g.AddCell(display + "##main", /*col=*/0, /*row=*/0,
              /*colSpan=*/2, /*rowSpan=*/1, enabled, /*enabled=*/true,
              "Require " + display);
    // Row 1: L | R sub-toggles (disabled visual when modifier is off).
    g.AddCell("L##L", 0, 1, 1, 1, enabled && isL, enabled, "Left side");
    g.AddCell("R##R", 1, 1, 1, 1, enabled && isR, enabled, "Right side");

    ButtonGroup::Result r = g.Render();
    if (r.clickedIndex == 0) {              // main
        enabled = !enabled;
        if (!enabled) side = Shortcuts::ModifierSide::Both;
        changed = true;
    } else if (r.clickedIndex == 1 && enabled) {   // L
        if (isL && isR)       side = Shortcuts::ModifierSide::RightOnly;
        else if (isL && !isR) {/* keep L */}
        else                  side = isR ? Shortcuts::ModifierSide::Both
                                          : Shortcuts::ModifierSide::LeftOnly;
        changed = true;
    } else if (r.clickedIndex == 2 && enabled) {   // R
        if (isR && isL)       side = Shortcuts::ModifierSide::LeftOnly;
        else if (isR && !isL) {/* keep R */}
        else                  side = isL ? Shortcuts::ModifierSide::Both
                                          : Shortcuts::ModifierSide::RightOnly;
        changed = true;
    }
    return changed;
}

} // namespace

ShortcutEditor::ShortcutEditor() {
    searchBuf_[0] = '\0';
}

// ─── window wrappers ─────────────────────────────────────────────────────────

void ShortcutEditor::Render(bool* p_open) {
    ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Shortcut Editor", p_open, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    RenderContent();
    ImGui::End();
}

void ShortcutEditor::RenderContent() {
    auto& sm = Shortcuts::ShortcutManager::Instance();
    sm.RegisterRegionContext("Settings", "shortcutEditor", "content");

    RenderToolbar();
    ImGui::Separator();

    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = ds.GetGlobalScale();
    const float treeWidth = 320.0f * scale;

    if (UI::BeginScroll("##TreePane", ImVec2(treeWidth, 0.0f), ImGuiChildFlags_Borders)) {
        RenderTreePane();
    }
    UI::EndScroll();

    ImGui::SameLine();

    if (UI::BeginScroll("##DetailPane", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        if (selectedActionId_.empty())
            ImGui::TextWrapped("Select an action in the tree to view and edit "
                               "its shortcuts.");
        else
            RenderDetailPane();
    }
    UI::EndScroll();

    if (showConflicts_) {
        ImGui::Separator();
        RenderConflictsList();
    }
}

// ─── toolbar (search/filter) ────────────────────────────────────────────────

void ShortcutEditor::RenderToolbar() {
    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = ds.GetGlobalScale();
    ImGui::SetNextItemWidth(280.0f * scale);
    ImGui::InputTextWithHint("##search", "Search (action, shortcut, id)...",
                             searchBuf_, sizeof(searchBuf_));
    ImGui::SameLine();
    UI::Checkbox("##conflicts", "Conflicts", &showConflicts_);
    ImGui::SameLine();
    UI::Checkbox("##modifiedOnly", "Modified only", &showOnlyOverridden_);

    ImGui::SameLine();
    if (ImGui::Button("Restore all")) {
        Shortcuts::ShortcutManager::Instance().RestoreAllDefaults();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset every shortcut in every action to its default");
}

// ─── tree pane ──────────────────────────────────────────────────────────────

void ShortcutEditor::RenderTreePane() {
    static const Shortcuts::ActionCategory kAllCats[] = {
        Shortcuts::ActionCategory::Application,
        Shortcuts::ActionCategory::File,
        Shortcuts::ActionCategory::Edit,
        Shortcuts::ActionCategory::View,
        Shortcuts::ActionCategory::Window,
        Shortcuts::ActionCategory::Tool,
        Shortcuts::ActionCategory::Selection,
        Shortcuts::ActionCategory::Transform,
        Shortcuts::ActionCategory::Navigation,
        Shortcuts::ActionCategory::Custom,
    };

    auto headerColor = SafeColor(DesignSystem::Tok::C_SectionHeader_Label,
                                 ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, headerColor);
    for (auto cat : kAllCats) DrawTreeForCategory(cat);
    ImGui::PopStyleColor();
}

void ShortcutEditor::DrawTreeForCategory(Shortcuts::ActionCategory cat) {
    auto& sm = Shortcuts::ShortcutManager::Instance();
    auto actions = sm.GetActionsByCategory(cat);

    std::vector<const Shortcuts::Action*> filtered;
    for (const auto* a : actions) {
        if (!MatchesSearch(*a, searchBuf_)) continue;
        if (showOnlyOverridden_) {
            const auto* b = sm.GetBinding(a->id);
            if (!b) continue;
            if (b->current == b->defaults) continue;
        }
        filtered.push_back(a);
    }
    if (filtered.empty()) return;

    int catIdx = static_cast<int>(cat);
    if (UI::IconCollapsingHeader("catHdr", Shortcuts::ActionCategoryName(cat),
                                 "", categoryOpen_[catIdx])) {
        categoryOpen_[catIdx] = true;
        std::map<std::string, std::vector<const Shortcuts::Action*>> bySub;
        for (const auto* a : filtered) {
            std::string key;
            const auto& c = a->requiredContext;
            if (!c.tool.empty())   key = "Tool: " + c.tool;
            else if (!c.editor.empty()) key = "Zone: " + c.editor;
            else key = "Global";
            bySub[key].push_back(a);
        }
        ImGui::Indent(8.0f);
        for (auto& [sub, group] : bySub) {
            if (sub != "Global") {
                ImGui::TextDisabled("%s", sub.c_str());
                ImGui::Indent(6.0f);
            }
            for (const auto* a : group) DrawActionLeaf(a);
            if (sub != "Global") ImGui::Unindent(6.0f);
        }
        ImGui::Unindent(8.0f);
    } else {
        categoryOpen_[catIdx] = false;
    }
}

void ShortcutEditor::DrawActionLeaf(const Shortcuts::Action* action) {
    auto& sm = Shortcuts::ShortcutManager::Instance();
    bool selected = (selectedActionId_ == action->id);

    ImGui::PushID(action->id.c_str());
    if (ImGui::Selectable(action->name.c_str(), selected, 0)) {
        if (selectedActionId_ != action->id) {
            selectedActionId_ = action->id;
        }
    }
    if (ImGui::IsItemHovered() && !action->description.empty())
        ImGui::SetTooltip("%s", action->description.c_str());

    const auto* b = sm.GetBinding(action->id);
    if (b && !b->current.empty()) {
        ImGui::SameLine();
        std::string label = b->current.front().ToString();
        if (b->current.size() > 1) label += " +" + std::to_string(b->current.size() - 1);
        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        float padRight = 6.0f;
        float availX = ImGui::GetContentRegionAvail().x;
        if (ts.x + padRight < availX)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availX - ts.x - padRight));
        ImVec4 muted = SafeColor(DesignSystem::Tok::S_Color_Text_Subtle,
                                 ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextColored(muted, "%s", label.c_str());
    }
    ImGui::PopID();
}

// ─── detail pane ────────────────────────────────────────────────────────────

void ShortcutEditor::RenderDetailPane() {
    auto& sm = Shortcuts::ShortcutManager::Instance();
    const Shortcuts::Action* a = sm.GetAction(selectedActionId_);
    if (!a) {
        ImGui::TextDisabled("Action not found.");
        return;
    }

    auto& ds = DesignSystem::DesignSystem::Instance();
    const float scale = ds.GetGlobalScale();
    float headerScale = SafeFloat(DesignSystem::Tok::C_SectionHeader_FontScale, 1.1f);
    ImVec4 headerColor = SafeColor(DesignSystem::Tok::C_SectionHeader_Label,
                                   ImVec4(0.95f, 0.95f, 0.95f, 1.0f));

    ImGui::PushStyleColor(ImGuiCol_Text, headerColor);
    ImGui::SetWindowFontScale(headerScale);
    ImGui::Text("%s", a->name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    if (!a->description.empty()) {
        ImGui::TextWrapped("%s", a->description.c_str());
    }

    ImGui::Spacing();
    ImVec4 muted = SafeColor(DesignSystem::Tok::S_Color_Text_Subtle,
                             ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::TextColored(muted, "ID: %s", a->id.c_str());
    ImGui::TextColored(muted, "Category: %s",
                       Shortcuts::ActionCategoryName(a->category));
    ImGui::TextColored(muted, "Context: %s", a->requiredContext.ToString().c_str());
    if (a->isModal) ImGui::TextColored(muted, "Modal: yes");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const Shortcuts::ShortcutBinding* b = sm.GetBinding(selectedActionId_);
    if (!b) {
        ImGui::TextDisabled("No binding.");
        return;
    }

    ImGui::Text("Shortcuts:");
    ImGui::Spacing();

    if (b->current.empty()) {
        ImGui::TextColored(muted, "(no shortcut bound)");
    } else {
        for (size_t i = 0; i < b->current.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));

            // The displayed signature comes from a pending draft (if one is
            // active because the user typed something dangerous and we
            // refused to commit) OR from the persisted binding.
            std::string pkey = PendingKey(selectedActionId_, static_cast<int>(i));
            auto pit = pendingEdits_.find(pkey);
            Shortcuts::EventSignature sig =
                (pit != pendingEdits_.end() && pit->second.active)
                ? pit->second.draft
                : b->current[i];

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * scale, 4.0f * scale));
            ImVec4 transparent = ImVec4(0,0,0,0);
            ImGui::PushStyleColor(ImGuiCol_Header,        transparent);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, transparent);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  transparent);

            float btnSize   = ImGui::GetFrameHeight();
            bool canRestore =
                (i < b->defaults.size()) &&
                !(b->current[i] == b->defaults[i]);

            float rightCluster = btnSize + 4.0f * scale;             // delete
            if (canRestore) rightCluster += btnSize + 4.0f * scale;  // restore

            auto conflicts = sm.CheckCandidate(selectedActionId_, b->current[i]);
            float conflictW = 0.0f;
            if (!conflicts.empty()) {
                char tag[16]; std::snprintf(tag, sizeof(tag), "[!] %zu", (int)conflicts.size());
                conflictW = ImGui::CalcTextSize(tag).x + 8.0f * scale;
                rightCluster += conflictW;
            }
            std::string danger = sm.IsDangerousBinding(selectedActionId_, sig);
            if (!danger.empty()) {
                rightCluster += ImGui::CalcTextSize("[BLOCK]").x + 8.0f * scale;
            }

            ImGuiTreeNodeFlags hdrFlags = ImGuiTreeNodeFlags_AllowOverlap |
                                          ImGuiTreeNodeFlags_FramePadding |
                                          ImGuiTreeNodeFlags_SpanAvailWidth;
            bool open = ImGui::TreeNodeEx("##rowhdr", hdrFlags, "%s", "");

            ImGui::SameLine();

            // ── Per-row enable checkbox (each shortcut entry has its own) ──
            ImGui::AlignTextToFramePadding();
            bool entryEn = b->IsEntryEnabled(i);
            if (UI::CheckboxBox("##rowen", &entryEn)) {
                sm.SetEntryEnabled(selectedActionId_, static_cast<int>(i), entryEn);
                b = sm.GetBinding(selectedActionId_);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Enable / disable this shortcut entry");
            ImGui::SameLine(0.0f, 6.0f * scale);

            // ── Input kind dropdown (Keyboard | Mouse) ──
            using namespace Shortcuts;
            auto isKeyTypeF = [](EventType t) {
                return t == EventType::KeyPress || t == EventType::KeyRelease ||
                       t == EventType::KeyClick || t == EventType::KeyDoubleClick;
            };
            bool isKbd = isKeyTypeF(sig.type);
            const char* kindLabel = isKbd ? "Kbd" : "Mouse";
            ImGui::SetNextItemWidth(80.0f * scale);
            if (ImGui::BeginCombo("##kindRow", kindLabel)) {
                if (ImGui::Selectable("Keyboard", isKbd)) {
                    if (!isKbd) {
                        sig.type = EventType::KeyPress;
                        sig.mouseButton = MouseButton::None;
                        if (sig.key == ImGuiKey_None) sig.key = ImGuiKey_A;
                        // Try to commit
                        std::string err = sm.IsDangerousBinding(selectedActionId_, sig);
                        if (err.empty()) {
                            auto sigs = b->current; sigs[i] = sig;
                            sm.SetBindings(selectedActionId_, sigs);
                            b = sm.GetBinding(selectedActionId_);
                            pendingEdits_.erase(pkey);
                        } else {
                            pendingEdits_[pkey] = { sig, err, true };
                        }
                    }
                }
                if (ImGui::Selectable("Mouse", !isKbd)) {
                    if (isKbd) {
                        sig.type = EventType::MouseClick;
                        sig.mouseButton = MouseButton::Left;
                        sig.key = ImGuiKey_None;
                        sig.modifiers.ctrl = true;
                        std::string err = sm.IsDangerousBinding(selectedActionId_, sig);
                        if (err.empty()) {
                            auto sigs = b->current; sigs[i] = sig;
                            sm.SetBindings(selectedActionId_, sigs);
                            b = sm.GetBinding(selectedActionId_);
                            pendingEdits_.erase(pkey);
                        } else {
                            pendingEdits_[pkey] = { sig, err, true };
                        }
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine(0.0f, 6.0f * scale);

            // ── Capture field — auto-sized to the available space minus right cluster ──
            float winInner  = ImGui::GetContentRegionAvail().x;
            float fieldW    = winInner - rightCluster - 6.0f * scale;
            if (fieldW < 80.0f * scale) fieldW = 80.0f * scale;
            ImGui::PushItemWidth(fieldW);
            ShortcutCaptureField::StatusOverride statusOv =
                ShortcutCaptureField::StatusOverride::None;
            if (!danger.empty()) statusOv = ShortcutCaptureField::StatusOverride::Error;
            else if (!conflicts.empty()) {
                bool hard = false;
                for (const auto& c : conflicts) if (c.isHard) { hard = true; break; }
                statusOv = hard ? ShortcutCaptureField::StatusOverride::Error
                                : ShortcutCaptureField::StatusOverride::Warning;
            }
            if (ShortcutCaptureField::Render(("##field" + std::to_string(i)).c_str(),
                                             sig,
                                             ShortcutCaptureField::Mode::Combo,
                                             /*withInputKindToggle=*/false,
                                             /*withDropdown=*/false,
                                             statusOv)) {
                std::string err = sm.IsDangerousBinding(selectedActionId_, sig);
                if (err.empty()) {
                    auto sigs = b->current;
                    sigs[i] = sig;
                    sm.SetBindings(selectedActionId_, sigs);
                    b = sm.GetBinding(selectedActionId_);
                    pendingEdits_.erase(pkey);
                } else {
                    pendingEdits_[pkey] = { sig, err, true };
                }
            }
            ImGui::PopItemWidth();

            // ── Danger tag (persistent — keeps showing even after the
            //    rejected commit so the user understands what happened) ──
            if (!danger.empty()) {
                ImVec4 col = SafeColor(DesignSystem::Tok::C_Shortcut_ConflictHard,
                                       ImVec4(0.9f, 0.26f, 0.26f, 1.0f));
                ImGui::SameLine(0.0f, 8.0f * scale);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::Text("[BLOCK]");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Unsafe binding rejected:\n%s",
                                      danger.c_str());
                ImGui::PopStyleColor();
            }

            // ── Conflict tag ──
            if (!conflicts.empty()) {
                bool hard = false;
                for (const auto& c : conflicts) if (c.isHard) hard = true;
                ImVec4 col = hard
                    ? SafeColor(DesignSystem::Tok::C_Shortcut_ConflictHard,
                                ImVec4(0.9f, 0.26f, 0.26f, 1.0f))
                    : SafeColor(DesignSystem::Tok::C_Shortcut_ConflictSoft,
                                ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
                ImGui::SameLine(0.0f, 8.0f * scale);
                ImGui::PushStyleColor(ImGuiCol_Text, col);
                ImGui::Text("[!] %zu", (size_t)conflicts.size());
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    for (const auto& c : conflicts) {
                        const auto* other = sm.GetAction(c.actionId2);
                        ImGui::Text("- %s %s",
                            other ? other->name.c_str() : c.actionId2.c_str(),
                            c.isHard ? "(hard)" : "(soft)");
                    }
                    ImGui::EndTooltip();
                }
                ImGui::PopStyleColor();
            }

            ImVec4 iconTint = SafeColor(DesignSystem::Tok::C_IconButton_Icon,
                                        ImVec4(0.85f, 0.85f, 0.85f, 1.0f));

            if (canRestore) {
                ImGui::SameLine(0.0f, 6.0f * scale);
                if (IconSquareButton("restore", "restore", btnSize,
                                     "Restore default", iconTint)) {
                    sm.RestoreBindingAt(selectedActionId_, static_cast<int>(i));
                    b = sm.GetBinding(selectedActionId_);
                }
            }

            ImGui::SameLine(0.0f, 4.0f * scale);
            ImVec4 dangerTint = SafeColor(DesignSystem::Tok::C_IconButton_IconNegative,
                                          ImVec4(0.95f, 0.45f, 0.45f, 1.0f));
            bool deleteRow = IconSquareButton("delete", "close", btnSize,
                                              "Remove this shortcut", dangerTint);

            ImGui::PopStyleColor(3);   // Header colors
            ImGui::PopStyleVar();      // FramePadding

            if (open) {
                RenderAdvancedEditorFor(selectedActionId_, sig, static_cast<int>(i));
                b = sm.GetBinding(selectedActionId_);
                ImGui::TreePop();
            }

            if (deleteRow) {
                sm.RemoveBinding(selectedActionId_, b->current[i]);
                ImGui::PopID();
                break;
            }

            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("+ Add a shortcut")) {
        Shortcuts::EventSignature empty;
        empty.type = Shortcuts::EventType::KeyPress;
        empty.key  = ImGuiKey_None;
        // Fresh binding is intentionally invalid; resolver skips it so it
        // can never fire until the user actually records something.
        sm.AddBinding(selectedActionId_, empty);
    }
    ImGui::SameLine();
    bool isDefault = (b->current == b->defaults);
    ImGui::BeginDisabled(isDefault);
    if (ImGui::Button("Restore all defaults for this action")) {
        sm.RestoreDefaults(selectedActionId_);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && !isDefault) {
        ImGui::SetTooltip("Reset every shortcut on THIS action to its default value\n"
                          "(also drops user-added shortcuts)");
    }
}

// ─── advanced editor inline (per-binding, two-way live-sync) ─────────────────

void ShortcutEditor::RenderAdvancedEditorFor(const std::string& actionId,
                                             Shortcuts::EventSignature& /*sigUnused*/,
                                             int bindingIndex) {
    using namespace Shortcuts;
    auto& sm  = Shortcuts::ShortcutManager::Instance();
    auto& ds  = DesignSystem::DesignSystem::Instance();
    const float scale = ds.GetGlobalScale();

    const ShortcutBinding* b = sm.GetBinding(actionId);
    if (!b || bindingIndex < 0 || bindingIndex >= (int)b->current.size()) return;

    // Use the pending draft if one exists, so the Advanced view stays in
    // sync with the row capture field while a dangerous attempt is shown.
    std::string pkey = PendingKey(actionId, bindingIndex);
    auto pit = pendingEdits_.find(pkey);
    EventSignature sig =
        (pit != pendingEdits_.end() && pit->second.active)
        ? pit->second.draft
        : b->current[bindingIndex];

    auto commit = [&]() {
        std::string err = sm.IsDangerousBinding(actionId, sig);
        if (!err.empty()) {
            // Park the draft + error so the row + advanced both keep
            // reflecting the user's current attempt.
            pendingEdits_[pkey] = { sig, err, true };
            return;
        }
        auto sigs = b->current;
        sigs[bindingIndex] = sig;
        sm.SetBindings(actionId, sigs);
        pendingEdits_.erase(pkey);
    };

    ImGui::Indent(16.0f * scale);
    ImGui::Spacing();

    // ── Type predicates ─────────────────────────────────────────────────
    auto isKeyT   = [](EventType t) {
        return t == EventType::KeyPress || t == EventType::KeyRelease ||
               t == EventType::KeyClick || t == EventType::KeyDoubleClick;
    };
    auto isMouseT = [](EventType t) {
        return !(t == EventType::None ||
                 t == EventType::KeyPress || t == EventType::KeyRelease ||
                 t == EventType::KeyClick || t == EventType::KeyDoubleClick);
    };
    auto isDragT = [](EventType t) {
        return t == EventType::MouseDrag ||
               (t >= EventType::MouseDragNorth &&
                t <= EventType::MouseDragSouthWest);
    };

    bool isKey   = isKeyT(sig.type);
    bool isMouse = isMouseT(sig.type);

    // NOTE: the Keyboard/Mouse input-kind selector lives in the row header
    // (next to the capture field), NOT here, to avoid a duplicate control.

    // ── Keyboard branch ────────────────────────────────────────────────
    if (isKey) {
        // Default to A on first switch (if no key yet).
        if (sig.key == ImGuiKey_None) {
            sig.key = ImGuiKey_A;
            commit();
        }
        // Sub-action combo
        struct K { EventType t; const char* label; };
        static const K kSub[] = {
            { EventType::KeyPress,       "Press" },
            { EventType::KeyRelease,     "Release" },
            { EventType::KeyClick,       "Click" },
            { EventType::KeyDoubleClick, "Double Click" },
        };
        int curIdx = 0;
        for (int i = 0; i < (int)IM_ARRAYSIZE(kSub); ++i)
            if (kSub[i].t == sig.type) { curIdx = i; break; }
        ImGui::SetNextItemWidth(160.0f * scale);
        if (ImGui::BeginCombo("Action##subkey", kSub[curIdx].label)) {
            for (int i = 0; i < (int)IM_ARRAYSIZE(kSub); ++i) {
                bool sel = (i == curIdx);
                if (ImGui::Selectable(kSub[i].label, sel)) {
                    sig.type = kSub[i].t;
                    commit();
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Single-key capture field with the key picker dropdown INTEGRATED
        // inside the field (right edge).  Clicking the arrow zone opens the
        // list; clicking the main zone records.
        ImGui::Spacing();
        ImGui::TextDisabled("Key (click to record, or use the list on the right):");
        EventSignature keyOnly;
        keyOnly.type = EventType::KeyPress;
        keyOnly.key  = sig.key;

        ImGui::PushItemWidth(300.0f * scale);
        if (ShortcutCaptureField::Render("##advKey", keyOnly,
                                         ShortcutCaptureField::Mode::SingleKey,
                                         /*withInputKindToggle=*/false,
                                         /*withDropdown=*/true)) {
            sig.key = keyOnly.key;
            if (sig.type != EventType::KeyPress &&
                sig.type != EventType::KeyRelease &&
                sig.type != EventType::KeyClick &&
                sig.type != EventType::KeyDoubleClick) {
                sig.type = EventType::KeyPress;
            }
            sig.mouseButton = MouseButton::None;
            commit();
        }
        ImGui::PopItemWidth();
    }

    // ── Mouse branch ───────────────────────────────────────────────────
    if (isMouse) {
        // The "Target" dropdown picks WHAT mouse input this binding listens
        // to.  Buttons (Left/Middle/Right/4-7) keep an associated
        // sub-action (Press/Release/Click/DoubleClick/Click Drag).
        // Continuous motion (Move/Trackpad*) and wheel entries set the
        // EventType directly and disable the sub-action UI.
        enum class Target {
            Btn = 0, Motion = 1, Wheel = 2
        };
        struct ButtonOpt { MouseButton b; const char* label; };
        static const ButtonOpt kBtns[] = {
            { MouseButton::Left,   "Left" },
            { MouseButton::Middle, "Middle" },
            { MouseButton::Right,  "Right" },
            { MouseButton::X1,     "Button 4" },
            { MouseButton::X2,     "Button 5" },
            { MouseButton::X3,     "Button 6" },
            { MouseButton::X4,     "Button 7" },
        };
        struct MotionOpt { EventType t; const char* label; };
        static const MotionOpt kMotions[] = {
            { EventType::MouseMove,           "Mouse Move" },
            { EventType::TrackpadPan,         "Trackpad Pan" },
            { EventType::TrackpadZoom,        "Trackpad Zoom" },
            { EventType::TrackpadRotate,      "Trackpad Rotate" },
            { EventType::TrackpadSmartRotate, "Trackpad Smart Rotate" },
        };
        struct WheelOpt { EventType t; const char* label; };
        static const WheelOpt kWheels[] = {
            { EventType::WheelUp,    "Wheel Up" },
            { EventType::WheelDown,  "Wheel Down" },
            { EventType::WheelLeft,  "Wheel Left" },
            { EventType::WheelRight, "Wheel Right" },
            { EventType::WheelIn,    "Wheel In" },
            { EventType::WheelOut,   "Wheel Out" },
        };

        // Compute current preview label
        std::string targetLabel;
        if (sig.type == EventType::MouseMove)           targetLabel = "Mouse Move";
        else if (sig.type == EventType::TrackpadPan)    targetLabel = "Trackpad Pan";
        else if (sig.type == EventType::TrackpadZoom)   targetLabel = "Trackpad Zoom";
        else if (sig.type == EventType::TrackpadRotate) targetLabel = "Trackpad Rotate";
        else if (sig.type == EventType::TrackpadSmartRotate) targetLabel = "Trackpad Smart Rotate";
        else if (sig.type == EventType::WheelUp)    targetLabel = "Wheel Up";
        else if (sig.type == EventType::WheelDown)  targetLabel = "Wheel Down";
        else if (sig.type == EventType::WheelLeft)  targetLabel = "Wheel Left";
        else if (sig.type == EventType::WheelRight) targetLabel = "Wheel Right";
        else if (sig.type == EventType::WheelIn)    targetLabel = "Wheel In";
        else if (sig.type == EventType::WheelOut)   targetLabel = "Wheel Out";
        else {
            // button-based: label by current mouseButton
            for (const auto& bo : kBtns)
                if (bo.b == sig.mouseButton) { targetLabel = bo.label; break; }
            if (targetLabel.empty()) targetLabel = "Left";
        }

        ImGui::SameLine(0.0f, 8.0f * scale);
        ImGui::SetNextItemWidth(180.0f * scale);
        if (ImGui::BeginCombo("Target##mouseTarget", targetLabel.c_str())) {
            // Buttons section
            for (const auto& bo : kBtns) {
                bool sel = (sig.mouseButton == bo.b) &&
                           (sig.type == EventType::MousePress ||
                            sig.type == EventType::MouseRelease ||
                            sig.type == EventType::MouseClick ||
                            sig.type == EventType::MouseDoubleClick ||
                            isDragT(sig.type));
                if (ImGui::Selectable(bo.label, sel)) {
                    sig.mouseButton = bo.b;
                    // If current type isn't button-compatible, default to Click
                    if (!(sig.type == EventType::MousePress ||
                          sig.type == EventType::MouseRelease ||
                          sig.type == EventType::MouseClick ||
                          sig.type == EventType::MouseDoubleClick ||
                          isDragT(sig.type))) {
                        sig.type = EventType::MouseClick;
                    }
                    sig.key = ImGuiKey_None;
                    commit();
                }
            }
            ImGui::Separator();
            for (const auto& mo : kMotions) {
                bool sel = (sig.type == mo.t);
                if (ImGui::Selectable(mo.label, sel)) {
                    sig.type = mo.t;
                    sig.mouseButton = MouseButton::None;
                    sig.key = ImGuiKey_None;
                    commit();
                }
            }
            ImGui::Separator();
            for (const auto& wo : kWheels) {
                bool sel = (sig.type == wo.t);
                if (ImGui::Selectable(wo.label, sel)) {
                    sig.type = wo.t;
                    sig.mouseButton = MouseButton::None;
                    sig.key = ImGuiKey_None;
                    commit();
                }
            }
            ImGui::EndCombo();
        }

        // Sub-action combo (only when target is a button)
        bool targetIsButton = (sig.type == EventType::MousePress ||
                               sig.type == EventType::MouseRelease ||
                               sig.type == EventType::MouseClick ||
                               sig.type == EventType::MouseDoubleClick ||
                               isDragT(sig.type)) &&
                              sig.mouseButton != MouseButton::None;

        if (targetIsButton) {
            struct K { EventType t; const char* label; };
            // Note: "Click Drag" is represented by any of the drag types.
            // We collapse them under a single sub-action and reveal a
            // direction sub-selector below.
            static const K kSubBtn[] = {
                { EventType::MousePress,       "Press" },
                { EventType::MouseRelease,     "Release" },
                { EventType::MouseClick,       "Click" },
                { EventType::MouseDoubleClick, "Double Click" },
                { EventType::MouseDrag,        "Click Drag" },
            };
            EventType subEq = sig.type;
            if (isDragT(subEq)) subEq = EventType::MouseDrag;
            int curIdx = 0;
            for (int i = 0; i < (int)IM_ARRAYSIZE(kSubBtn); ++i)
                if (kSubBtn[i].t == subEq) { curIdx = i; break; }

            ImGui::SameLine(0.0f, 8.0f * scale);
            ImGui::SetNextItemWidth(160.0f * scale);
            if (ImGui::BeginCombo("Action##submouse", kSubBtn[curIdx].label)) {
                for (int i = 0; i < (int)IM_ARRAYSIZE(kSubBtn); ++i) {
                    bool sel = (i == curIdx);
                    if (ImGui::Selectable(kSubBtn[i].label, sel)) {
                        sig.type = kSubBtn[i].t;
                        commit();
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        // Direction sub-selector (only when type is a drag)
        if (isDragT(sig.type)) {
            struct D { EventType t; const char* label; };
            static const D kDirs[] = {
                { EventType::MouseDrag,           "Any direction" },
                { EventType::MouseDragNorth,      "North (Up)" },
                { EventType::MouseDragSouth,      "South (Down)" },
                { EventType::MouseDragEast,       "East (Right)" },
                { EventType::MouseDragWest,       "West (Left)" },
                { EventType::MouseDragNorthEast,  "North-East" },
                { EventType::MouseDragNorthWest,  "North-West" },
                { EventType::MouseDragSouthEast,  "South-East" },
                { EventType::MouseDragSouthWest,  "South-West" },
            };
            int curIdx = 0;
            for (int i = 0; i < (int)IM_ARRAYSIZE(kDirs); ++i)
                if (kDirs[i].t == sig.type) { curIdx = i; break; }
            ImGui::SameLine(0.0f, 8.0f * scale);
            ImGui::SetNextItemWidth(160.0f * scale);
            if (ImGui::BeginCombo("Direction##dir", kDirs[curIdx].label)) {
                for (int i = 0; i < (int)IM_ARRAYSIZE(kDirs); ++i) {
                    bool sel = (i == curIdx);
                    if (ImGui::Selectable(kDirs[i].label, sel)) {
                        sig.type = kDirs[i].t;
                        commit();
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
    }

    // ── Drag distance threshold (only for drag types) ──────────────────
    if (isDragT(sig.type)) {
        float dsDefault = SafeFloat(DesignSystem::Tok::S_Config_DragThreshold, 6.0f);
        bool useCustom = sig.dragThreshold > 0.0f;
        if (UI::Checkbox("##overrideDrag", "Override drag distance", &useCustom)) {
            sig.dragThreshold = useCustom ? dsDefault : 0.0f;
            commit();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("If unchecked, uses the design-system value\n"
                              "(token: semantic.shortcut.dragThreshold = %.1f px)",
                              dsDefault);
        ImGui::SameLine();
        ImGui::BeginDisabled(!useCustom);
        float val = useCustom ? sig.dragThreshold : dsDefault;
        ImGui::SetNextItemWidth(160.0f * scale);
        if (ImGui::DragFloat("##dragdist", &val, 0.5f, 2.0f, 64.0f, "%.1f px")) {
            if (val < 2.0f)  val = 2.0f;
            if (val > 64.0f) val = 64.0f;
            sig.dragThreshold = val;
            commit();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImVec4 muted = SafeColor(DesignSystem::Tok::S_Color_Text_Subtle,
                                 ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextColored(muted, useCustom ? "(per-shortcut)" : "(from design system)");
    }

    // ── Modifiers — toggle buttons + L|R sub-toggles ───────────────────
    ImGui::Spacing();
    ImGui::TextDisabled("Modifiers");
    auto& m = sig.modifiers;
    bool dirty = false;

    if (RenderModifierGroup("Ctrl",  m.ctrl,  m.ctrlSide))  dirty = true;
    ImGui::SameLine(0.0f, 8.0f * scale);
    if (RenderModifierGroup("Shift", m.shift, m.shiftSide)) dirty = true;
    ImGui::SameLine(0.0f, 8.0f * scale);
    if (RenderModifierGroup("Alt",   m.alt,   m.altSide))   dirty = true;
    ImGui::SameLine(0.0f, 8.0f * scale);
    if (RenderModifierGroup("Super", m.super, m.superSide)) dirty = true;

    ImGui::SameLine(0.0f, 14.0f * scale);
    bool anyMod = m.any;
    if (SingleToggle("##mod_any", "Any", anyMod,
                     ImVec2(50.0f * scale, ImGui::GetFrameHeight()),
                     "Wildcard: matches regardless of which modifiers are held.\n"
                     "Disables individual modifier flags above.")) {
        m.any = !m.any;
        if (m.any) { m.ctrl = m.shift = m.alt = m.super = false; }
        dirty = true;
    }

    if (dirty) commit();

    // ── Repeat (real checkbox) ─────────────────────────────────────────
    ImGui::Spacing();
    bool rep = sig.repeat;
    if (UI::Checkbox("##repeat", "Repeat", &rep)) {
        sig.repeat = rep;
        commit();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Allow auto-repeat: fire the action repeatedly while the key is held.");

    // ── Validity warning (only when invalid) ──────────────────────────
    if (!sig.IsValid()) {
        ImGui::Spacing();
        ImVec4 warn = SafeColor(DesignSystem::Tok::C_Shortcut_ConflictSoft,
                                ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, warn);
        ImGui::TextWrapped("[!] This shortcut is not yet bound - pick a key, button or wheel above.");
        ImGui::PopStyleColor();
    }

    // ── Dangerous-binding warning ─────────────────────────────────────
    std::string danger = sm.IsDangerousBinding(actionId, sig);
    if (!danger.empty()) {
        ImGui::Spacing();
        ImVec4 col = SafeColor(DesignSystem::Tok::C_Shortcut_ConflictHard,
                               ImVec4(0.9f, 0.26f, 0.26f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextWrapped("[!] Unsafe binding: %s", danger.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("This action cannot be bound to that input "
                            "(set Action::allowUnsafeMouseBindings to true "
                            "in code to override).");
    }

    ImGui::Unindent(16.0f * scale);
}

// ─── conflicts panel ────────────────────────────────────────────────────────

void ShortcutEditor::RenderConflictsList() {
    auto& sm = Shortcuts::ShortcutManager::Instance();
    auto conflicts = sm.DetectConflicts();

    ImGui::Text("Conflicts detected: %zu", conflicts.size());
    if (conflicts.empty()) {
        ImVec4 ok = SafeColor(DesignSystem::Tok::S_Color_Positive_Default,
                              ImVec4(0.30f, 0.80f, 0.30f, 1.0f));
        ImGui::TextColored(ok, "[OK] No conflicts.");
        return;
    }

    ImVec4 hard = SafeColor(DesignSystem::Tok::C_Shortcut_ConflictHard,
                            ImVec4(0.9f, 0.26f, 0.26f, 1.0f));
    ImVec4 soft = SafeColor(DesignSystem::Tok::C_Shortcut_ConflictSoft,
                            ImVec4(1.0f, 0.75f, 0.0f, 1.0f));
    for (size_t i = 0; i < conflicts.size(); ++i) {
        const auto& c = conflicts[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextColored(c.isHard ? hard : soft, "%s",
                           c.signature.ToString().c_str());
        ImGui::SameLine();
        const auto* a1 = sm.GetAction(c.actionId1);
        const auto* a2 = sm.GetAction(c.actionId2);
        ImGui::Text("-> %s vs %s",
                    a1 ? a1->name.c_str() : c.actionId1.c_str(),
                    a2 ? a2->name.c_str() : c.actionId2.c_str());
        if (ImGui::SmallButton("Go to action 1")) selectedActionId_ = c.actionId1;
        ImGui::SameLine();
        if (ImGui::SmallButton("Go to action 2")) selectedActionId_ = c.actionId2;
        ImGui::PopID();
    }
}

} // namespace UI
