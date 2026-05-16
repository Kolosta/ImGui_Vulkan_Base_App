#pragma once

#include <string>

namespace Shortcuts {

/**
 * Hierarchical context describing where the user currently is.
 * Empty fields act as wildcards (a binding declared in a wildcard context
 * matches any observed value).
 *
 * Hierarchy (from most general to most specific):
 *     window  → editor → region → mode → tool
 *
 * Plus a `focusedItemId` (ImGui ID) for widget-precise actions.
 */
struct ShortcutContext {
    std::string window  = "main";   // top-level (always "main" today)
    std::string editor  = "";       // panel/tab id
    std::string region  = "";       // toolbar/header/content/popup
    std::string mode    = "";       // edit-mode if any
    std::string tool    = "";       // active tool id
    int focusedItemId   = 0;        // ImGui hovered widget id (0 = none)

    bool operator==(const ShortcutContext& other) const;
    bool operator!=(const ShortcutContext& other) const { return !(*this == other); }

    /**
     * True iff every non-empty field of *this matches `observed`.
     * Empty / 0 fields are wildcards.
     */
    bool Matches(const ShortcutContext& observed) const;

    /**
     * Number of non-wildcard fields. Higher = more specific = higher priority.
     */
    int Specificity() const;

    /** Human-readable rendering, e.g. "{ editor=themePreview, tool=brush }". */
    std::string ToString() const;
};

} // namespace Shortcuts
