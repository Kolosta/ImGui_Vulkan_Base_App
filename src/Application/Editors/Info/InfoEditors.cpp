#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ToolManager.h>
#include <UI/Widgets/ScrollArea.h>
#include <imgui.h>
#include <cstdio>

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok; }

// Append one line to the action feed (newest at the back). Bounded so it never
// grows without limit; the Info editor shows the most recent first.
void Application::LogInfoAction(const std::string& text) {
    LogInfoAction(text, std::string());
}

void Application::LogInfoAction(const std::string& text, const std::string& detail) {
    constexpr size_t kMax = 2000;
    infoLog_.push_back({ (uint64_t)ImGui::GetFrameCount(), text, detail });
    if (infoLog_.size() > kMax)
        infoLog_.erase(infoLog_.begin(),
                       infoLog_.begin() + (long)(infoLog_.size() - kMax));
}

// "key=value, key=value" — the parameter dump shown under an action (Blender's
// info-log style, adapted: our own keys, not a Python call).
std::string Application::FormatActionDetail(
    const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string out;
    for (size_t i = 0; i < kv.size(); ++i) {
        if (i) out += ", ";
        out += kv[i].first; out += "="; out += kv[i].second;
    }
    return out;
}

// ── "Info" editor: a live feed of the last actions (Blender info-log style) ───
void Application::RenderInfoEditor() {
    auto& ds = DS::DesignSystem::Instance();
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Default));

    if (infoLog_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("No actions yet.");
        ImGui::PopStyleColor();
        ImGui::PopStyleColor();
        return;
    }

    // Console-style feed: OLDEST at the top, NEWEST at the bottom, in a scroll
    // region with the Blender-style overlay scrollbar (in-margin, no content
    // shift). Auto-scroll to the bottom on a new entry — unless the user has
    // scrolled up to read history. Horizontal overflow keeps the native bar.
    ImU32 subtle = ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Text_Subtle));
    if (UI::BeginScroll("##infoFeed", ImVec2(0, 0), 0,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const InfoEntry& e : infoLog_) {
            ImGui::PushStyleColor(ImGuiCol_Text, subtle);
            ImGui::Text("[%llu]", (unsigned long long)e.frame);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextUnformatted(e.text.c_str());
            // Parameter dump on the same line, dimmed (e.g. "value=(…) orient=GLOBAL").
            if (!e.detail.empty()) {
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, subtle);
                ImGui::TextUnformatted(e.detail.c_str());
                ImGui::PopStyleColor();
            }
        }
        // Stick to the bottom when a new line arrived and we were already at (or
        // near) the bottom; honour the user scrolling up.
        static size_t s_lastCount = 0;
        if (infoLog_.size() != s_lastCount) {
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
                ImGui::SetScrollHereY(1.0f);
            s_lastCount = infoLog_.size();
        }
    }
    UI::EndScroll();
    ImGui::PopStyleColor();
}

// ── "Dev Panel": live debug data — the undo/redo lists, document state, etc. ──
void Application::RenderDevDataEditor() {
    auto& ds = DS::DesignSystem::Instance();
    ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Default));
    ImU32 subtle = ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Text_Subtle));
    ImU32 accent = ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Accent_Default));

    // ── Document undo/redo history ────────────────────────────────────────────
    // Returns with the Ink engine's command-based undo (docs/Ink/ROADMAP.md
    // Lot 8); until then only the Preferences history exists.
    ImGui::SeparatorText("Undo / Redo — Document");
    ImGui::PushStyleColor(ImGuiCol_Text, subtle);
    ImGui::TextUnformatted("(offline during the Ink engine rework — Lot 8)");
    ImGui::PopStyleColor();

    // ── Preferences history ───────────────────────────────────────────────────
    ImGui::SeparatorText("Undo / Redo — Preferences");
    ImGui::PushStyleColor(ImGuiCol_Text, subtle);
    ImGui::Text("steps=%d  current=%d  undo=%s  redo=%s  buffer=%d",
                prefsUndo_.Size(), prefsUndo_.CurrentIndex(),
                prefsUndo_.CanUndo() ? "yes" : "no",
                prefsUndo_.CanRedo() ? "yes" : "no",
                undoBufferSteps_);
    ImGui::PopStyleColor();
    {
        const auto& labels = prefsUndo_.Labels();
        const int cur = prefsUndo_.CurrentIndex();
        for (int i = 0; i < (int)labels.size(); ++i) {
            const char* tag = (i == cur) ? " <= current"
                            : (i <  cur) ? " (undo)" : " (redo)";
            if (i == cur) ImGui::PushStyleColor(ImGuiCol_Text, accent);
            else          ImGui::PushStyleColor(ImGuiCol_Text, subtle);
            ImGui::Text("  %2d: %s%s", i, labels[(size_t)i].c_str(), tag);
            ImGui::PopStyleColor();
        }
    }

    // ── App / project snapshot ────────────────────────────────────────────────
    ImGui::SeparatorText("Project");
    ImGui::PushStyleColor(ImGuiCol_Text, subtle);
    ImGui::Text("name=%s  dirty=%s  module=%s",
                project_.name.empty() ? "(unsaved)" : project_.name.c_str(),
                project_.dirty ? "yes" : "no",
                project_.moduleId.empty() ? "(classic)" : project_.moduleId.c_str());
    ImGui::Text("active tool=%s",
                Shortcuts::Tools::ToolManager::Instance().GetActiveTool().c_str());
    ImGui::PopStyleColor();

    // ── Ink engine (docs/Ink/) — the same counters ink_bench reports ─────────
    ImGui::SeparatorText("Ink Engine");
    ImGui::PushStyleColor(ImGuiCol_Text, subtle);
    if (ink_) {
        const Ink::Stats& s = ink_->GetStats();
        ImGui::Text("record=%.3f ms  gpu=%.3f ms", s.recordMs, s.gpuMs);
        ImGui::Text("draws=%u  triangles=%u  instances=%u",
                    s.drawCalls, s.triangles, s.instances);
        ImGui::Text("views=%u  re-rendered=%u  (0 re-rendered = steady-state)",
                    s.views, s.viewsRendered);
    } else {
        ImGui::TextUnformatted("(engine unavailable — Vulkan 1.3 required)");
    }
    ImGui::PopStyleColor();

    // ── Recent actions (last 12, newest first) with their parameter dump ──────
    ImGui::SeparatorText("Recent Actions");
    {
        int shown = 0;
        for (auto it = infoLog_.rbegin(); it != infoLog_.rend() && shown < 12; ++it, ++shown) {
            ImGui::PushStyleColor(ImGuiCol_Text, subtle);
            ImGui::Text("[%llu]", (unsigned long long)it->frame);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextUnformatted(it->text.c_str());
            if (!it->detail.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, subtle);
                ImGui::TextWrapped("    %s", it->detail.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (shown == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, subtle);
            ImGui::TextUnformatted("  (none yet)");
            ImGui::PopStyleColor();
        }
    }

    ImGui::PopStyleColor();
}

} // namespace App
