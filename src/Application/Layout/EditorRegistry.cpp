#include "EditorRegistry.h"
#include <algorithm>

namespace App {

EditorRegistry& EditorRegistry::Instance() {
    static EditorRegistry inst;
    return inst;
}

void EditorRegistry::Register(EditorDescriptor desc) {
    for (auto& e : editors_) {
        if (e.id == desc.id) { e = std::move(desc); return; }  // replace by id
    }
    editors_.push_back(std::move(desc));
}

void EditorRegistry::UnregisterByPrefix(const std::string& prefix) {
    editors_.erase(
        std::remove_if(editors_.begin(), editors_.end(),
                       [&](const EditorDescriptor& e) {
                           return e.id.rfind(prefix, 0) == 0;  // starts_with
                       }),
        editors_.end());
}

const EditorDescriptor* EditorRegistry::Get(const std::string& id) const {
    for (const auto& e : editors_)
        if (e.id == id) return &e;
    return nullptr;
}

}  // namespace App
