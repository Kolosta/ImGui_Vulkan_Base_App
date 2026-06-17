#pragma once

#include <string>

namespace UI {

/**
 * Bottom status bar — contextual list of shortcuts on the left,
 * version label on the right.
 *
 * Stateless: reads everything from ShortcutManager and DesignSystem.
 * Caller is responsible for layout — call Render() once per frame.
 */
class StatusBar {
public:
    /** Render the bar at the current ImGui cursor position, occupying
     *  full width and the height defined by component.statusbar.height. */
    static void Render(const std::string& versionLabel);

    /** Height in physical pixels (already scaled). */
    static float Height();
};

} // namespace UI
