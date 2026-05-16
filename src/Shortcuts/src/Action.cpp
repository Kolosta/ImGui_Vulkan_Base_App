#include <Shortcuts/Action.h>

namespace Shortcuts {

const char* ActionCategoryName(ActionCategory c) {
    switch (c) {
        case ActionCategory::Application: return "Application";
        case ActionCategory::File:        return "File";
        case ActionCategory::Edit:        return "Edit";
        case ActionCategory::View:        return "View";
        case ActionCategory::Window:      return "Window";
        case ActionCategory::Tool:        return "Tool";
        case ActionCategory::Selection:   return "Selection";
        case ActionCategory::Transform:   return "Transform";
        case ActionCategory::Navigation:  return "Navigation";
        case ActionCategory::Custom:      return "Custom";
    }
    return "Unknown";
}

} // namespace Shortcuts
