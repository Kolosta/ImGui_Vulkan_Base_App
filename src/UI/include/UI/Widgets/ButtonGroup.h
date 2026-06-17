#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include <functional>

namespace UI {

/**
 * Reusable "fused" button group (think MUI ButtonGroup) that supports an
 * arbitrary mix of horizontal AND vertical placement with differently
 * sized cells.
 *
 * Design goals:
 *   - Each cell is a *real* `ImGui::Button`, so all native behaviour is
 *     preserved: keyboard navigation, focus ring, activation, tooltips,
 *     disabled state, repeat, etc.
 *   - Adjacent cells share a single, crisp border.  Every grid edge is
 *     drawn exactly once (no double-stroking → no bright dots at
 *     crossings, no gaps at corners).
 *   - Only the *outer* corners of the whole group are rounded.
 *   - The cell currently hovered / active / selected "owns" its border:
 *     its outline (all 4 edges) is drawn on top of neighbours with the
 *     state colour, so the highlight is never visually mixed with a
 *     neighbour's normal border.
 *   - Every dimension / radius / colour comes from design-system tokens.
 *
 * Layout model:
 *   Cells are placed on an integer grid (col,row) with a (colSpan,rowSpan).
 *   The caller provides the pixel size of each grid column / row.  This is
 *   enough to express e.g. a "Ctrl on top, L|R underneath" group:
 *
 *     col:   0     1
 *     row 0: [  Ctrl  ]   (colSpan = 2)
 *     row 1: [ L ][ R ]
 *
 * Usage:
 *   UI::ButtonGroup g("##mods");
 *   g.SetGrid({fullW},            // column widths  (here a single column…)
 *             {mainH, subH});     // row heights
 *   g.AddCell("Ctrl", 0,0, 1,1, ctrlOn);          // spans the only column
 *   // For the L|R row we instead use two columns – see ButtonGroup.cpp
 *   // for a complete modifier-group example.
 *   ButtonGroup::CellResult r = g.Render();
 *   if (r.clickedIndex == 0) { ... }
 */
class ButtonGroup {
public:
    // Where the label (and optional icon) sit inside a cell.
    enum class Align { Center, Left };

    struct Cell {
        std::string label;       // may contain "##id"
        int   col = 0, row = 0;
        int   colSpan = 1, rowSpan = 1;
        bool  selected = false;  // toggled / active-value state
        bool  enabled  = true;   // false → ImGui disabled
        std::string tooltip;     // optional
        std::string icon;        // optional icon id, drawn left of the label
        Align align = Align::Center;  // Left = full-width nav-list look
    };

    struct Result {
        int clickedIndex = -1;   // index into the cells vector, -1 = none
    };

    explicit ButtonGroup(const char* id) : id_(id) {}

    /** Define the pixel sizes of each grid column and row. */
    void SetGrid(std::vector<float> colWidths, std::vector<float> rowHeights) {
        colW_ = std::move(colWidths);
        rowH_ = std::move(rowHeights);
    }

    void AddCell(const Cell& c) { cells_.push_back(c); }
    void AddCell(const std::string& label, int col, int row,
                 int colSpan, int rowSpan, bool selected,
                 bool enabled = true, const std::string& tooltip = "") {
        Cell c{}; c.label = label; c.col = col; c.row = row;
        c.colSpan = colSpan; c.rowSpan = rowSpan; c.selected = selected;
        c.enabled = enabled; c.tooltip = tooltip;
        cells_.push_back(c);
    }

    void Clear() { cells_.clear(); }

    /** Render the whole group at the current cursor position.  Returns the
     *  index of the cell that was clicked this frame (or -1). */
    Result Render();

    /** Total pixel size the group will occupy (for layout budgeting). */
    ImVec2 CalcSize() const;

private:
    std::string        id_;
    std::vector<float> colW_;
    std::vector<float> rowH_;
    std::vector<Cell>  cells_;
};

} // namespace UI
