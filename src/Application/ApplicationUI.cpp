// #include "Application.h"
// #include <DesignSystem/DesignSystem.h>
// #include <Shortcuts/ShortcutManager.h>
// #include <UI/IconManager.h>

// namespace App {

// void Application::RenderDockSpace() {
//     ImGuiViewport* viewport = ImGui::GetMainViewport();
//     ImGui::SetNextWindowPos(viewport->WorkPos);
//     ImGui::SetNextWindowSize(viewport->WorkSize);
//     ImGui::SetNextWindowViewport(viewport->ID);

//     ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
//     window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
//     window_flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
//     window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
//     window_flags |= ImGuiWindowFlags_MenuBar;

//     ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
//     ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
//     ImGui::Begin("DockSpace", nullptr, window_flags);
//     ImGui::PopStyleVar(3);

//     ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
//     ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

//     ImGui::End();
// }

// void Application::RenderMainMenuBar() {
//     if (ImGui::BeginMainMenuBar()) {
//         auto& sm = Shortcuts::ShortcutManager::Instance();

//         if (ImGui::BeginMenu("File")) {
//             std::string newShortcut = sm.GetShortcutString("action.new");
//             if (ImGui::MenuItem("New", newShortcut.c_str())) TestAction_NewFile();

//             std::string openShortcut = sm.GetShortcutString("action.open");
//             if (ImGui::MenuItem("Open", openShortcut.c_str())) TestAction_OpenFile();

//             std::string saveShortcut = sm.GetShortcutString("action.save");
//             if (ImGui::MenuItem("Save", saveShortcut.c_str())) TestAction_SaveFile();

//             ImGui::Separator();

//             std::string quitShortcut = sm.GetShortcutString("action.quit");
//             if (ImGui::MenuItem("Quit", quitShortcut.c_str())) TestAction_Quit();

//             ImGui::EndMenu();
//         }

//         if (ImGui::BeginMenu("Edit")) {
//             if (ImGui::MenuItem("Token Editor", nullptr, showTokenEditor_)) {
//                 showTokenEditor_ = !showTokenEditor_;
//             }
//             if (ImGui::MenuItem("Shortcut Editor", nullptr, showShortcutEditor_)) {
//                 showShortcutEditor_ = !showShortcutEditor_;
//             }
//             if (ImGui::MenuItem("Icon Editor", nullptr, showIconEditor_)) {
//                 showIconEditor_ = !showIconEditor_;
//             }
//             ImGui::EndMenu();
//         }

//         if (ImGui::BeginMenu("Windows")) {
//             ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo_);
//             ImGui::EndMenu();
//         }

//         ImGui::EndMainMenuBar();
//     }
// }

// void Application::RenderToolbar() {
//     ImGui::Begin("Toolbar", nullptr,
//         ImGuiWindowFlags_NoCollapse |
//         ImGuiWindowFlags_NoTitleBar |
//         ImGuiWindowFlags_NoResize);

//     Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Toolbar", Shortcuts::ShortcutZone::Toolbar);

//     ImGui::Text("Tools");
//     ImGui::Separator();

//     const float iconSize = 20.0f;
//     const float buttonHeight = 36.0f;

//     // Tool 1
//     if (ImGui::BeginChild("##tool1", ImVec2(0, buttonHeight), false)) {
//         UI::IconManager::Instance().RenderIcon("tool1", iconSize);
//         ImGui::SameLine();
//         ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
//         if (ImGui::Selectable("Tool 1", false))
//             TestAction_Tool1();
//     }
//     ImGui::EndChild();

//     // Tool 2
//     if (ImGui::BeginChild("##tool2", ImVec2(0, buttonHeight), false)) {
//         UI::IconManager::Instance().RenderIcon("tool2", iconSize);
//         ImGui::SameLine();
//         ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
//         if (ImGui::Selectable("Tool 2", false))
//             TestAction_Tool2();
//     }
//     ImGui::EndChild();

//     ImGui::Separator();

//     // Bottom icons
//     UI::IconManager::Instance().RenderIcon("logo_carto", 32.0f);
    
//     UI::IconManager::Instance().RenderIcon("settings", 24.0f);

//     ImGui::End();
// }

// } // namespace App







#include "Application.h"
#include <DesignSystem/DesignSystem.h>
#include <Shortcuts/ShortcutManager.h>
#include <UI/IconManager.h>

namespace App {

void Application::RenderDockSpace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
    window_flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    window_flags |= ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ImGui::End();
}

void Application::RenderMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        auto& sm = Shortcuts::ShortcutManager::Instance();

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New", sm.GetShortcutString("action.new").c_str())) TestAction_NewFile();
            if (ImGui::MenuItem("Open", sm.GetShortcutString("action.open").c_str())) TestAction_OpenFile();
            if (ImGui::MenuItem("Save", sm.GetShortcutString("action.save").c_str())) TestAction_SaveFile();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", sm.GetShortcutString("action.quit").c_str())) TestAction_Quit();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Token Editor", nullptr, showTokenEditor_)) {
                showTokenEditor_ = !showTokenEditor_;
            }
            if (ImGui::MenuItem("Shortcut Editor", nullptr, showShortcutEditor_)) {
                showShortcutEditor_ = !showShortcutEditor_;
            }
            if (ImGui::MenuItem("Icon Editor", nullptr, showIconEditor_)) {
                showIconEditor_ = !showIconEditor_;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem("ImGui Demo", nullptr, &showImGuiDemo_);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void Application::RenderToolbar() {
    ImGui::Begin("Toolbar", nullptr,
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize);

    Shortcuts::ShortcutManager::Instance().RegisterWindowZone("Toolbar", Shortcuts::ShortcutZone::Toolbar);

    ImGui::Text("Tools");
    ImGui::Separator();

    const float iconSize = 20.0f;
    const float buttonHeight = 36.0f;

    // Tool 1 - uses global template
    if (ImGui::BeginChild("##tool1", ImVec2(0, buttonHeight), false)) {
        UI::IconManager::Instance().RenderIcon("tool1", iconSize);
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
        if (ImGui::Selectable("Tool 1", false))
            TestAction_Tool1();
    }
    ImGui::EndChild();

    // Tool 2 - uses global template
    if (ImGui::BeginChild("##tool2", ImVec2(0, buttonHeight), false)) {
        UI::IconManager::Instance().RenderIcon("tool2", iconSize);
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
        if (ImGui::Selectable("Tool 2", false))
            TestAction_Tool2();
    }
    ImGui::EndChild();

    ImGui::Separator();

    // Bottom icons - use global template
    UI::IconManager::Instance().RenderIcon("logo_carto", 32.0f);
    UI::IconManager::Instance().RenderIcon("settings", 24.0f);

    ImGui::End();
}

} // namespace App