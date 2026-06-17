#pragma once

#include "ZoneLayout.h"
#include <Renderer/Document/Document.h>
#include <vector>

namespace App {

// ─────────────────────────────────────────────────────────────────────────────
//  Per-viewport page arrangement (Lot 3).
//
//  An auto layout never mutates the shared Artboard::pos; instead each viewport
//  computes a DISPLAY ORIGIN per page (where that page is shown in THIS
//  viewport) and the renderer/picking add (displayOrigin − ab.pos) to the page
//  origin. Manual mode = displayOrigin == ab.pos (the free-moved position).
// ─────────────────────────────────────────────────────────────────────────────

struct PageView {
    int      index   = -1;     // artboard index, or −1 for a hidden/absent slot
    Renderer::Vec2 origin{0, 0};  // top-left where this page is DISPLAYED (doc-units)
    bool     visible = false;
};

// Compute, for every artboard, its display origin + visibility under `layout`.
// Returns a vector parallel to doc.artboards (index i → page i). Hidden pages
// (per-viewport hiddenPages, or all-but-one in singlePage mode) get visible=false.
std::vector<PageView> ComputePageViews(const PageLayout& layout,
                                       const Renderer::Document& doc);

// Single-page / single-spread paging helpers (shared with the N panel). `book`
// = 2-up; `cover` = first page alone, then pairs. SpreadCount = number of
// spreads over n eligible pages; SpreadRange = the [start,start+count) eligible
// indices of spread s.
int  SpreadCount(int n, bool book, bool cover);
void SpreadRange(int n, bool book, bool cover, int s, int& start, int& count);

// Convenience: the display origin to use for page `index` (its ab.pos plus the
// layout offset), and whether it is visible, for a single page.
bool PageIsVisible(const PageLayout& layout, const Renderer::Document& doc, int index);
Renderer::Vec2 PageDisplayOrigin(const PageLayout& layout,
                                 const Renderer::Document& doc, int index);

} // namespace App
