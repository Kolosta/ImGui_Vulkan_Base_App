#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ToolManager.h>
#include <UI/Widgets/ScrollArea.h>
#include <UI/Widgets/ListRow.h>
#include <UI/Widgets/TreeRow.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>

namespace App {

namespace {
namespace DS = DesignSystem;
using Tok = DesignSystem::Tok;
namespace tr = UI::Tree;
}

// Append one line to the action feed (newest at the back). Bounded so it never
// grows without limit; the Info editor shows the most recent first.
void Application::LogInfoAction(const std::string& text) {
    LogInfoAction(text, std::string());
}

void Application::LogInfoAction(const std::string& text, const std::string& detail) {
    constexpr size_t kMax = 2000;
    InfoEntry e;
    e.frame = (uint64_t)ImGui::GetFrameCount();
    e.text = text;
    e.detail = detail;
    infoLog_.push_back(std::move(e));
    if (infoLog_.size() > kMax)
        infoLog_.erase(infoLog_.begin(),
                       infoLog_.begin() + (long)(infoLog_.size() - kMax));
}

void Application::LogInfoAction(const std::string& text, const std::string& api,
                                const InfoFields& fields) {
    LogInfoAction(text, FormatActionDetail(fields));
    infoLog_.back().api = api;
    infoLog_.back().fields = fields;
}

// "key=value, key=value" — the parameter dump shown under an action (Blender's
// info-log style, adapted: our own keys, not a Python call).
std::string Application::FormatActionDetail(const InfoFields& kv) {
    std::string out;
    for (size_t i = 0; i < kv.size(); ++i) {
        if (i) out += ", ";
        out += kv[i].first; out += "="; out += kv[i].second;
    }
    return out;
}

// Naming what was acted on. One item earns its name; several earn a count,
// because a log line listing forty names is a log line nobody reads.
std::string Application::DescribeNodes(const std::vector<Ink::NodeId>& ids) const {
    if (ids.empty()) return "none";
    if (!project_.document) return std::to_string(ids.size()) + " objects";
    if (ids.size() == 1) {
        const Ink::Node* n = project_.document->Find(ids[0]);
        if (n && !n->name.empty()) return n->name;
        return "object #" + std::to_string((unsigned long long)ids[0]);
    }
    return std::to_string(ids.size()) + " objects";
}

std::string Application::DescribeCollections(const std::vector<Ink::NodeId>& ids) const {
    if (ids.empty()) return "none";
    if (!project_.document) return std::to_string(ids.size()) + " collections";
    if (ids.size() == 1) {
        const Ink::Collection* c = project_.document->FindCollection(ids[0]);
        if (c && !c->name.empty()) return c->name;
        return "collection #" + std::to_string((unsigned long long)ids[0]);
    }
    return std::to_string(ids.size()) + " collections";
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

    // The same rows as the Palette editor: a zebra-striped list whose header
    // carries the action and its API name, and whose expanded body carries the
    // full record. Oldest at the top, newest at the bottom.
    ImU32 subtle = ImGui::GetColorU32(ds.GetColor(Tok::S_Color_Text_Subtle));
    if (UI::BeginScroll("##infoFeed", ImVec2(0, 0), 0, 0)) {
        UI::ListRowResetZebra();
        UI::ListRowSetBandScale(1.0f);
        const float gs = tr::Gs();
        const ImU32 zebra = ImGui::ColorConvertFloat4ToU32(
            tr::SafeColor(Tok::S_Color_Background_Layer2, ImVec4(0.15f,0.15f,0.15f,1)));
        const ImU32 textCol = ImGui::ColorConvertFloat4ToU32(
            tr::SafeColor(Tok::C_Outliner_Text, ImVec4(0.85f, 0.85f, 0.85f, 1)));
        ImDrawList* fdl = ImGui::GetWindowDrawList();
        ImDrawListSplitter zsplit;
        zsplit.Split(fdl, 2);
        zsplit.SetCurrentChannel(fdl, 1);

        for (std::size_t i = 0; i < infoLog_.size(); ++i) {
            const InfoEntry& e = infoLog_[i];
            const std::uint64_t key = (std::uint64_t)i;
            const bool striped = (i & 1) != 0;

            UI::ListRowConfig cfg;
            cfg.id = (ImGuiID)(key * 2654435761u + 17u);
            cfg.zebraOdd = striped;
            cfg.zebraColor = zebra;
            cfg.bandMarginLeft = tr::BandMargin();
            cfg.cornerRadius = tr::SafeFloat(Tok::S_CornerRadius_Control, 4.0f) * gs;
            cfg.bgSplitter = &zsplit;
            {
                ImVec4 hov = tr::SafeColor(Tok::C_Outliner_Row_Hover,
                                           ImVec4(0.3f, 0.5f, 0.9f, 1));
                hov.w = 0.35f;
                cfg.colors.hover = ImGui::ColorConvertFloat4ToU32(hov);
            }
            float stripeBot = 0.0f;
            bool toggled = false;
            {
                UI::ListRow row(cfg);
                stripeBot = row.StripeBottom();
                ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
                ImGui::PushID((int)cfg.id);
                tr::DotGutter();
                bool open = infoOpen_.count(key) != 0;
                const bool was = open;
                if (e.fields.empty()) tr::ChevronSpacer(); else tr::Chevron("##ex", open);
                if (open != was) toggled = true;
                const float ty = row.RowTop() +
                                 (tr::RowH() - ImGui::GetTextLineHeight()) * 0.5f;
                float x = ImGui::GetCursorScreenPos().x + 4.0f * gs;
                char fr[24];
                std::snprintf(fr, sizeof fr, "[%llu]", (unsigned long long)e.frame);
                fdl->AddText(ImVec2(x, ty), subtle, fr);
                x += ImGui::CalcTextSize(fr).x + 6.0f * gs;
                fdl->AddText(ImVec2(x, ty), textCol, e.text.c_str());
                x += ImGui::CalcTextSize(e.text.c_str()).x + 8.0f * gs;
                // The API name, right where the eye already is: this is the
                // handle the action answers to, not decoration.
                if (!e.api.empty()) {
                    fdl->AddText(ImVec2(x, ty), subtle, e.api.c_str());
                    x += ImGui::CalcTextSize(e.api.c_str()).x + 8.0f * gs;
                }
                if (!e.detail.empty() && infoOpen_.count(key) == 0)
                    fdl->AddText(ImVec2(x, ty), subtle, e.detail.c_str());
                ImGui::PopID();
            }
            if (toggled) {
                if (infoOpen_.count(key)) infoOpen_.erase(key);
                else infoOpen_.insert(key);
            }
            ImGui::SetCursorScreenPos(
                ImVec2(ImGui::GetCurrentWindow()->WorkRect.Min.x, stripeBot));
            if (!infoOpen_.count(key) || e.fields.empty()) continue;

            // The expanded record, on the row's own zebra shade.
            const float bodyTop = ImGui::GetCursorScreenPos().y;
            ImGui::PushID((int)cfg.id + 1);
            ImGui::Indent(tr::DotGutterW() + tr::ChevronSlotW());
            for (const InfoField& f : e.fields) {
                ImGui::PushStyleColor(ImGuiCol_Text, subtle);
                ImGui::TextUnformatted(f.first.c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 8.0f * gs);
                ImGui::TextUnformatted(f.second.c_str());
            }
            ImGui::Unindent(tr::DotGutterW() + tr::ChevronSlotW());
            ImGui::PopID();
            if (striped) {
                ImGuiWindow* w = ImGui::GetCurrentWindow();
                zsplit.SetCurrentChannel(fdl, 0);
                fdl->AddRectFilled(ImVec2(w->WorkRect.Min.x, bodyTop),
                                   ImVec2(w->WorkRect.Max.x +
                                              ImGui::GetStyle().ScrollbarSize,
                                          ImGui::GetCursorScreenPos().y),
                                   zebra);
                zsplit.SetCurrentChannel(fdl, 1);
            }
        }
        zsplit.Merge(fdl);
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
