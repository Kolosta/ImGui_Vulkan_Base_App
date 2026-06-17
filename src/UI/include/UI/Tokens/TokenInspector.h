#pragma once

#include <DesignSystem/Core/Context.h>
#include <DesignSystem/Core/TokenValue.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <imgui.h>

namespace DesignSystem {

class Token;
class OverrideManager;

/**
 * Shared, stateless-ish inspector panel for a single token.
 *
 * The three Settings tabs (classic list, dev token tree, user-facing zone
 * editor) all need the exact same "show value / preview / chain / override"
 * machinery. Rather than duplicate ~400 lines three times, that logic lives
 * here once. Each view decides *which* token is selected and how it is
 * picked; TokenInspector renders the detail + override UI for it, with the
 * full feature set (actual vs original preview, global and/or per-theme
 * overrides, reference chain, constraints).
 *
 * The only mutable state is the in-progress "add override" value, which is
 * per-instance so two tabs don't fight over it.
 */
class TokenInspector {
public:
    TokenInspector();

    // Reset the pending "add override" form to the token's resolved value.
    void SyncToToken(const std::shared_ptr<Token>& token);

    // Full detail block: identity, constraint, default, actual value,
    // reference chain, per-theme override status.
    void RenderDetails(const std::string& tokenId,
                       Context& ctx, OverrideManager& mgr);

    // Override editor block: existing overrides (editable + removable) and
    // the "add new override" form (global or current-theme).
    void RenderOverridePanel(const std::string& tokenId,
                             Context& ctx, OverrideManager& mgr);

    /**
     * Shared 3-column property table: [Property | Global | Theme]. Open it
     * once with BeginPropertyTable(), emit any number of RenderScopedRow()
     * (each is one table row, aligned in those columns), then
     * EndPropertyTable(). The "Global"/"Theme" headers are shown exactly
     * once by the table itself. Returns false if the table couldn't open
     * (then skip the rows / EndPropertyTable).
     */
    bool BeginPropertyTable(const char* id);
    void EndPropertyTable();

    /**
     * One editable property row, scope-aware, rendered INSIDE the property
     * table above. It edits `tokenId` at `scope` ("" = the global token):
     * column 0 shows the name + state badge + live swatch + compare tooltip,
     * column 1 is the Global-override editor, column 2 the current-Theme one
     * (two independent pickers, no combo). Resolution cascades by scope
     * specificity. Returns true if anything changed this frame.
     *
     * Used by the dev token tree (one row per registered scope, incl. "") and
     * the user theme editor (Blender-style per-zone sections).
     */
    bool RenderScopedRow(const std::string& tokenId, const char* displayLabel,
                         const std::string& scope,
                         Context& ctx, OverrideManager& mgr);

    // Value preview primitives (also reused by the rows above).
    void RenderValuePreview(const char* label, const TokenValue& value,
                            const Context& ctx, bool showLabel);

private:
    void RenderColorPreview(const char* label, const ImVec4& color,
                            const Context& ctx);
    void RenderFloatPreview(const char* label, const std::string& tokenId,
                            float value);
    bool RenderValueEditor(const char* label, TokenValue& value,
                           const std::shared_ptr<Token>& token,
                           const Context& ctx);
    bool ValidateOverrideType(const TokenValue& value,
                              const std::shared_ptr<Token>& token);

    // Scratch value for the (legacy) standalone "add override" form, kept
    // for the classic editor's existing flow.
    TokenValue    newOverrideValue_;
};

} // namespace DesignSystem
