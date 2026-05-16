#pragma once

#include <string>
#include <vector>
#include <map>

namespace Shortcuts {
namespace Tools {

struct ToolDef {
    std::string id;          // "tool.brush"
    std::string name;        // "Brush"
    std::string iconId;      // VectorGraphics icon id
    std::vector<std::string> actionIds;
};

class ToolManager {
public:
    static ToolManager& Instance();

    void Initialize();
    void Shutdown();

    void RegisterTool(const ToolDef& def);
    void UnregisterTool(const std::string& id);

    void SetActiveTool(const std::string& id);
    const std::string& GetActiveTool() const { return activeToolId_; }
    bool IsToolActive(const std::string& id) const { return id == activeToolId_; }

    const ToolDef* GetTool(const std::string& id) const;
    std::vector<const ToolDef*> GetAllTools() const;

    /** Cycle to next registered tool (used by tool.cycleNext binding). */
    void CycleNext();

private:
    ToolManager() = default;

    std::map<std::string, ToolDef> tools_;
    std::vector<std::string>       order_;
    std::string                    activeToolId_;
};

} // namespace Tools
} // namespace Shortcuts
