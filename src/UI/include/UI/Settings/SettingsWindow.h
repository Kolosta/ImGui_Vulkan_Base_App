#pragma once

#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Blender-style "Preferences" window.
//
//  A real, separate OS window (via ImGui multi-viewport): when open it tears off
//  the main window and gets its own taskbar icon. It draws its OWN title bar
//  (app logo + "Preferences") instead of the native one, a left column of
//  grouped toggle buttons (one active at a time) that select which page is
//  shown, and the page content on the right.
//
//  Lot 1 builds the shell: window + title bar + left column + empty page
//  routing. Later lots fill each page (Theme cards, Customisation panels,
//  Keymap, Accessibility, …).
// ─────────────────────────────────────────────────────────────────────────────

namespace UI {

class SettingsWindow {
public:
    // The pages, in the order they appear in the left column. Grouped visually
    // by the button column (see SettingsWindow.cpp).
    enum class Page {
        General,
        Language,
        Theme,
        Customisation,
        Accessibility,
        Inputs,
        Keymap,
        Navigation,
        Icons,
        Dev,
    };

    // Render the window when *open is true. Sets *open to false when the user
    // closes it (title-bar close button). No-op when *open is false.
    void Render(bool* open);

    Page CurrentPage() const { return page_; }
    void SetPage(Page p) { page_ = p; }

private:
    void RenderTitleBar(float width);
    void RenderLeftColumn(float width, float height);
    void RenderPage(float width, float height);
    void RenderThemePage(float width, float height);
    void RenderCustomisationPage(float width, float height);
    void RenderAccessibilityPage(float width, float height);
    void RenderInputsPage(float width, float height);
    void RenderKeymapPage(float width, float height);
    void RenderGeneralPage(float width, float height);
    void RenderLanguagePage(float width, float height);
    void RenderNavigationPage(float width, float height);
    void RenderIconsPage(float width, float height);
    void RenderDevPage(float width, float height);

    // Shared page scaffold: a left-inset title + an optional one-line caption,
    // followed by a transparent, full-width scroll region the body fills. Keeps
    // every page laid out identically (same title position, same gaps).
    void BeginPageBody(const char* title, const char* caption = nullptr);
    void EndPageBody();

    Page page_ = Page::Theme;
    bool sysClose_ = false;   // set by the title-bar close button this frame
    // Customisation: edit overrides globally vs. for the current theme only.
    bool editGlobal_ = true;
    // Keymap search filter (shared with the panel-based shortcut list).
    char keymapSearch_[256] = { 0 };
};

} // namespace UI
