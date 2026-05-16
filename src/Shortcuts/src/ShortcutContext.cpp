#include <Shortcuts/ShortcutContext.h>

namespace Shortcuts {

bool ShortcutContext::operator==(const ShortcutContext& o) const {
    return window == o.window && editor == o.editor && region == o.region &&
           mode == o.mode && tool == o.tool && focusedItemId == o.focusedItemId;
}

bool ShortcutContext::Matches(const ShortcutContext& observed) const {
    if (!window.empty() && window != observed.window) return false;
    if (!editor.empty() && editor != observed.editor) return false;
    if (!region.empty() && region != observed.region) return false;
    if (!mode.empty()   && mode   != observed.mode)   return false;
    if (!tool.empty()   && tool   != observed.tool)   return false;
    if (focusedItemId != 0 && focusedItemId != observed.focusedItemId) return false;
    return true;
}

int ShortcutContext::Specificity() const {
    int n = 0;
    if (!window.empty()) ++n;
    if (!editor.empty()) ++n;
    if (!region.empty()) ++n;
    if (!mode.empty())   ++n;
    if (!tool.empty())   ++n;
    if (focusedItemId != 0) ++n;
    return n;
}

std::string ShortcutContext::ToString() const {
    std::string s = "{";
    bool first = true;
    auto add = [&](const char* name, const std::string& v) {
        if (v.empty()) return;
        if (!first) s += ", ";
        s += name;
        s += "=";
        s += v;
        first = false;
    };
    add("window", window);
    add("editor", editor);
    add("region", region);
    add("mode",   mode);
    add("tool",   tool);
    if (focusedItemId != 0) {
        if (!first) s += ", ";
        s += "id=";
        s += std::to_string(focusedItemId);
        first = false;
    }
    s += first ? " global }" : " }";
    return s;
}

} // namespace Shortcuts
