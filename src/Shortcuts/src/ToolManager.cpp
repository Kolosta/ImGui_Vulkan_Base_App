#include <Shortcuts/ToolManager.h>
#include <algorithm>

namespace Shortcuts {
namespace Tools {

ToolManager& ToolManager::Instance() {
    static ToolManager* instance = new ToolManager();
    return *instance;
}

void ToolManager::Initialize() {
    activeToolId_.clear();
}

void ToolManager::Shutdown() {
    tools_.clear();
    order_.clear();
    activeToolId_.clear();
}

void ToolManager::RegisterTool(const ToolDef& def) {
    if (tools_.find(def.id) == tools_.end()) {
        order_.push_back(def.id);
    }
    tools_[def.id] = def;
}

void ToolManager::UnregisterTool(const std::string& id) {
    tools_.erase(id);
    order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
    if (activeToolId_ == id) activeToolId_.clear();
}

void ToolManager::SetActiveTool(const std::string& id) {
    if (id.empty()) { activeToolId_.clear(); return; }
    if (tools_.find(id) == tools_.end()) return;
    activeToolId_ = id;
}

const ToolDef* ToolManager::GetTool(const std::string& id) const {
    auto it = tools_.find(id);
    return it != tools_.end() ? &it->second : nullptr;
}

std::vector<const ToolDef*> ToolManager::GetAllTools() const {
    std::vector<const ToolDef*> out;
    out.reserve(order_.size());
    for (const auto& id : order_) {
        auto it = tools_.find(id);
        if (it != tools_.end()) out.push_back(&it->second);
    }
    return out;
}

void ToolManager::CycleNext() {
    if (order_.empty()) return;
    if (activeToolId_.empty()) { activeToolId_ = order_.front(); return; }
    auto it = std::find(order_.begin(), order_.end(), activeToolId_);
    if (it == order_.end()) { activeToolId_ = order_.front(); return; }
    ++it;
    if (it == order_.end()) it = order_.begin();
    activeToolId_ = *it;
}

} // namespace Tools
} // namespace Shortcuts
