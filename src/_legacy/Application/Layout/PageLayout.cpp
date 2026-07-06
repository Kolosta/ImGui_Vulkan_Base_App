#include "PageLayout.h"
#include <algorithm>
#include <cmath>

namespace App {

const char* PageLayoutModeName(PageLayoutMode m) {
    switch (m) {
        case PageLayoutMode::Manual:      return "Manual";
        case PageLayoutMode::LeftToRight: return "Left to Right";
        case PageLayoutMode::RightToLeft: return "Right to Left";
        case PageLayoutMode::TopToBottom: return "Top to Bottom";
        case PageLayoutMode::BottomToTop: return "Bottom to Top";
        case PageLayoutMode::Grid:        return "Grid";
        case PageLayoutMode::BookLeft:    return "Book (first left)";
        case PageLayoutMode::BookRight:   return "Book (first right)";
        case PageLayoutMode::SinglePage:  return "Single Page";
        case PageLayoutMode::SingleBookLeft:  return "Single Spread (left)";
        case PageLayoutMode::SingleBookRight: return "Single Spread (right)";
    }
    return "?";
}

// Number of spreads for a single-page/single-spread view over `n` eligible pages.
// single=false → n spreads of 1; book → ⌈n/2⌉; book+cover → page 1 alone, then
// pairs (so 1 + ⌈(n-1)/2⌉).
int SpreadCount(int n, bool book, bool cover) {
    if (n <= 0) return 0;
    if (!book) return n;
    if (cover) return 1 + (std::max(0, n - 1) + 1) / 2;
    return (n + 1) / 2;
}
// Eligible-page range [start, start+count) for spread index `s`.
void SpreadRange(int n, bool book, bool cover, int s, int& start, int& count) {
    if (!book) { start = std::clamp(s, 0, std::max(0, n - 1)); count = 1; return; }
    if (cover) {
        if (s <= 0) { start = 0; count = 1; }
        else        { start = 1 + (s - 1) * 2; count = 2; }
    } else { start = s * 2; count = 2; }
    if (start >= n) { start = std::max(0, n - 1); count = std::min(count, n - start); }
}

namespace {
bool IsHidden(const PageLayout& l, uint64_t id) {
    return std::find(l.hiddenPages.begin(), l.hiddenPages.end(), id)
           != l.hiddenPages.end();
}
}

std::vector<PageView> ComputePageViews(const PageLayout& layout,
                                       const Renderer::Document& doc) {
    const auto& abs = doc.artboards;
    std::vector<PageView> out(abs.size());
    for (size_t i = 0; i < abs.size(); ++i) { out[i].index = (int)i; }
    if (abs.empty()) return out;

    // The Single* modes show only ONE page (or one 2-up spread) at a time,
    // selected by pageIndex (the N-panel "Pages" tab drives it). Compute, among
    // the pages visible document-wide + per-view, the index set of the current
    // spread.
    const bool single = layout.mode == PageLayoutMode::SinglePage ||
                        layout.mode == PageLayoutMode::SingleBookLeft ||
                        layout.mode == PageLayoutMode::SingleBookRight;
    const bool singleBook = layout.mode == PageLayoutMode::SingleBookLeft ||
                            layout.mode == PageLayoutMode::SingleBookRight;

    // Visibility first (independent of arrangement). A page hidden DOCUMENT-WIDE
    // (ab.pageVisible == false, the Outliner eye) is invisible in every viewport
    // and drops out of the auto packing below; the per-VIEW hidden list, the
    // legacy single-page flag, and the Single* modes narrow it further.
    for (size_t i = 0; i < abs.size(); ++i) {
        bool vis = abs[i].pageVisible && !IsHidden(layout, abs[i].id);
        if (layout.singlePage) {
            int pi = std::clamp(layout.pageIndex, 0, (int)abs.size() - 1);
            vis = vis && ((int)i == pi);
        }
        out[i].visible = vis;
    }
    if (single) {
        // Among the currently-eligible pages (visible so far), keep only the
        // spread at pageIndex. Single = 1 page; spread = 2; with a cover, the
        // FIRST spread is the lone page 1, then pairs 2-3, 4-5…
        std::vector<size_t> elig;
        for (size_t i = 0; i < abs.size(); ++i) if (out[i].visible) elig.push_back(i);
        for (size_t i = 0; i < abs.size(); ++i) out[i].visible = false;
        if (!elig.empty()) {
            const bool cover = singleBook && layout.spreadCover;
            int start = 0, count = 0;
            SpreadRange((int)elig.size(), singleBook, cover,
                        std::max(0, layout.pageIndex), start, count);
            for (int k = 0; k < count && start + k < (int)elig.size(); ++k)
                out[elig[(size_t)(start + k)]].visible = true;
        }
    }

    const float g = std::max(0.0f, layout.gap);

    // Manual: pages stay where they are (free move).
    if (layout.mode == PageLayoutMode::Manual) {
        for (size_t i = 0; i < abs.size(); ++i) out[i].origin = abs[i].pos;
        return out;
    }

    // Auto layouts pack the VISIBLE pages in order; hidden pages keep their
    // ab.pos (they aren't drawn anyway). Indices iterate in document order.
    auto visibleIndices = [&]{
        std::vector<size_t> v;
        for (size_t i = 0; i < abs.size(); ++i) if (out[i].visible) v.push_back(i);
        return v;
    }();
    for (size_t i = 0; i < abs.size(); ++i) out[i].origin = abs[i].pos;  // default

    switch (layout.mode) {
        case PageLayoutMode::LeftToRight:
        case PageLayoutMode::RightToLeft: {
            // Common baseline y = 0; advance x by each page's width + gap.
            float x = 0.0f;
            bool rtl = (layout.mode == PageLayoutMode::RightToLeft);
            for (size_t k = 0; k < visibleIndices.size(); ++k) {
                size_t i = rtl ? visibleIndices[visibleIndices.size() - 1 - k]
                               : visibleIndices[k];
                out[i].origin = { x, 0.0f };
                x += abs[i].size.x + g;
            }
            break;
        }
        case PageLayoutMode::TopToBottom:
        case PageLayoutMode::BottomToTop: {
            float y = 0.0f;
            bool btt = (layout.mode == PageLayoutMode::BottomToTop);
            for (size_t k = 0; k < visibleIndices.size(); ++k) {
                size_t i = btt ? visibleIndices[visibleIndices.size() - 1 - k]
                               : visibleIndices[k];
                out[i].origin = { 0.0f, y };
                y += abs[i].size.y + g;
            }
            break;
        }
        case PageLayoutMode::Grid: {
            int cols = std::max(1, layout.gridCols);
            // Uniform cell = max page size, so rows/cols align cleanly.
            float cw = 0.0f, ch = 0.0f;
            for (size_t i : visibleIndices) {
                cw = std::max(cw, abs[i].size.x); ch = std::max(ch, abs[i].size.y);
            }
            for (size_t k = 0; k < visibleIndices.size(); ++k) {
                size_t i = visibleIndices[k];
                int col = (int)k % cols, row = (int)k / cols;
                out[i].origin = { col * (cw + g), row * (ch + g) };
            }
            break;
        }
        case PageLayoutMode::BookLeft:
        case PageLayoutMode::BookRight: {
            // 2-up spreads. BookRight = first page alone on the right (so a blank
            // left slot), like a book's cover; BookLeft = first page on the left.
            float cw = 0.0f, ch = 0.0f;
            for (size_t i : visibleIndices) {
                cw = std::max(cw, abs[i].size.x); ch = std::max(ch, abs[i].size.y);
            }
            int slot = (layout.mode == PageLayoutMode::BookRight) ? 1 : 0;
            for (size_t k = 0; k < visibleIndices.size(); ++k, ++slot) {
                size_t i = visibleIndices[k];
                int spread = slot / 2;          // which spread (row)
                int side   = slot % 2;          // 0 left page, 1 right page
                out[i].origin = { side * (cw + g), spread * (ch + g) };
            }
            break;
        }
        case PageLayoutMode::SinglePage: {
            // Exactly one page visible (filtered above) → place it at the origin.
            for (size_t i : visibleIndices) out[i].origin = { 0.0f, 0.0f };
            break;
        }
        case PageLayoutMode::SingleBookLeft:
        case PageLayoutMode::SingleBookRight: {
            // One spread (≤2 pages) side by side. A lone page (cover spread, or a
            // trailing odd page) goes to the side dictated by the mode.
            float cw = 0.0f;
            for (size_t i : visibleIndices) cw = std::max(cw, abs[i].size.x);
            const bool lonely = (visibleIndices.size() == 1);
            int slot;
            if (lonely)
                // A single page: Right mode → right slot; Left mode → left slot.
                slot = (layout.mode == PageLayoutMode::SingleBookRight) ? 1 : 0;
            else
                slot = 0;   // a full pair starts at the left slot
            for (size_t k = 0; k < visibleIndices.size(); ++k, ++slot)
                out[visibleIndices[k]].origin = { (slot % 2) * (cw + g), 0.0f };
            break;
        }
        default: break;
    }
    return out;
}

bool PageIsVisible(const PageLayout& layout, const Renderer::Document& doc, int index) {
    if (index < 0 || index >= (int)doc.artboards.size()) return false;
    return ComputePageViews(layout, doc)[(size_t)index].visible;
}

Renderer::Vec2 PageDisplayOrigin(const PageLayout& layout,
                                 const Renderer::Document& doc, int index) {
    if (index < 0 || index >= (int)doc.artboards.size()) return {0, 0};
    return ComputePageViews(layout, doc)[(size_t)index].origin;
}

} // namespace App
