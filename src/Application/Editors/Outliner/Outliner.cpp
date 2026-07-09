#include "Application.h"

#include "OutlinerRowLayout.h"
#include <Ink/Geometry/Geometry.h>
#include <DesignSystem/DesignSystem.h>
#include <UI/Widgets/ScrollArea.h>
#include <UI/Widgets/PopupMenu.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Outliner editor — the Ink document's organisation tree, restoring the legacy
//  design (docs/Ink/ROADMAP.md Lot 9 rework) but rebuilt around a TWO-PASS,
//  culled renderer so it stays fluid on huge documents:
//
//    1. FLATTEN — walk the tree (collapse + filter + search) into a flat list
//       of visible rows. Pure computation, no ImGui, no drawing.
//    2. WINDOWED DRAW — advance the layout cursor row by row but only actually
//       build (ListRow, icons, text, buttons) the rows inside the scroll
//       viewport. Off-screen rows cost a single cursor advance.
//
//  This makes the editor O(visible rows), not O(document) — the fix for the
//  1000-node demo lag. Layers view rows are taller and carry a live,
//  draw-list-only vector preview of the object (no Vulkan target / texture).
// ─────────────────────────────────────────────────────────────────────────────

namespace App {

namespace { namespace DS = DesignSystem; using Tok = DesignSystem::Tok;

const char* NodeIcon(const Ink::Node& n) {
    switch (n.kind) {
        case Ink::NodeKind::Group:    return "folder";
        case Ink::NodeKind::Instance: return "swap_horiz";
        default:                      return "shape-category";
    }
}

// Layers-view preview scale (rows this many ui-units tall). 1 = normal row.
constexpr float kLayersRowScale = 2.4f;
} // namespace

// ── Filters / search ──────────────────────────────────────────────────────────

bool Application::OutlinerSearchHit(Ink::NodeId id) const {
    if (!project_.document || !outlinerCur_ || outlinerCur_->search[0] == '\0') return false;
    const Ink::Node* n = project_.document->Find(id);
    return n && ol::ContainsCI(n->name, outlinerCur_->search);
}

bool Application::OutlinerSubtreeSearchHit(Ink::NodeId id) const {
    if (!project_.document) return false;
    const Ink::Node* n = project_.document->Find(id);
    if (!n) return false;
    if (OutlinerSearchHit(id)) return true;
    for (Ink::NodeId c : n->children)
        if (OutlinerSubtreeSearchHit(c)) return true;
    return false;
}

bool Application::OutlinerPassesFilter(Ink::NodeId id) const {
    if (!project_.document || !outlinerCur_) return true;
    const Ink::Node* n = project_.document->Find(id);
    if (!n) return false;
    OutlinerState& o = *outlinerCur_;
    bool ok = true;
    if (n->kind == Ink::NodeKind::Group) { if (!o.showGroups) ok = false; }
    else                                  { if (!o.showObjects) ok = false; }
    if (ok) switch (o.objState) {
        case ObjStateFilter::Visible:    ok = n->visible; break;
        case ObjStateFilter::Selected:   ok = edit_.IsSelected(id); break;
        case ObjStateFilter::Active:     ok = (edit_.active == id); break;
        case ObjStateFilter::Selectable: ok = !n->locked; break;
        case ObjStateFilter::All:        default: break;
    }
    return o.invertFilter ? !ok : ok;
}

bool Application::OutlinerRowSelected(Ink::NodeId id) const {
    if (edit_.IsSelected(id)) return true;
    return outlinerCur_ && outlinerCur_->RowSelected(id);
}

bool Application::OutlinerInAnyCollection(Ink::NodeId id) const {
    if (!project_.document) return false;
    for (const Ink::Collection& c : project_.document->Collections())
        if (std::find(c.members.begin(), c.members.end(), id) != c.members.end())
            return true;
    return false;
}

// ── Selection click (plain / Shift-range / Ctrl / Alt) ────────────────────────

void Application::OutlinerSelectClick(Ink::NodeId id, bool isObject) {
    (void)isObject;
    ImGuiIO& io = ImGui::GetIO();
    OutlinerState& o = *outlinerCur_;

    if (io.KeyShift && o.active != 0) {
        const auto& order = o.rowOrder;
        int ia = -1, ib = -1;
        for (int i = 0; i < (int)order.size(); ++i) {
            if (order[i] == o.active) ia = i;
            if (order[i] == id)       ib = i;
        }
        if (ia >= 0 && ib >= 0) {
            if (ia > ib) std::swap(ia, ib);
            edit_.Clear();
            for (int i = ia; i <= ib; ++i)
                if (project_.document->Find(order[i])) edit_.SelectAdd(order[i]);
            edit_.active = o.active;
            return;
        }
    }
    if (io.KeyCtrl) {
        if (edit_.IsSelected(id)) edit_.Deselect(id); else edit_.SelectAdd(id);
        o.active = id;
        return;
    }
    if (io.KeyAlt) { edit_.SelectAdd(id); o.active = id; return; }
    edit_.SelectOnly(id);
    o.active = id;
}

// ── Pass 1: flatten the visible tree ──────────────────────────────────────────

void Application::OutlinerFlattenNode(Ink::NodeId id, int depth,
                                      std::vector<OutlinerRow>& out) {
    Ink::Document& doc = *project_.document;
    const Ink::Node* n = doc.Find(id);
    if (!n) return;
    OutlinerState& o = *outlinerCur_;
    const bool searching = o.search[0] != '\0';

    const bool passes = OutlinerPassesFilter(id);
    const bool searchOk = !searching || OutlinerSubtreeSearchHit(id);
    const bool drawSelf = passes && searchOk;
    const bool hasKids = !n->children.empty();

    if (drawSelf) {
        OutlinerRow r; r.id = id; r.kind = OutlinerRow::Kind::Object;
        r.depth = depth; r.hasChildren = hasKids;
        out.push_back(r);
        o.rowOrder.push_back(id);
    }
    if (hasKids && !o.IsCollapsed(id)) {
        const int childDepth = drawSelf ? depth + 1 : depth;
        // Top-of-stack first (reverse painter order) for a layer-stack read.
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it)
            OutlinerFlattenNode(*it, childDepth, out);
    }
}

void Application::OutlinerBuildRows(EditorState& st, std::vector<OutlinerRow>& out) {
    Ink::Document& doc = *project_.document;
    OutlinerState& o = st.outliner;

    if (o.display == OutlinerDisplayMode::Collections) {
        if (o.showCollections)
            for (const Ink::Collection& c : doc.Collections()) {
                OutlinerRow r; r.id = c.id; r.kind = OutlinerRow::Kind::CollectionHeader;
                r.depth = 0; r.hasChildren = !c.members.empty();
                out.push_back(r);
                if (!o.IsCollapsed(c.id))
                    for (Ink::NodeId m : c.members)
                        if (doc.Find(m)) OutlinerFlattenNode(m, 1, out);
            }
        // Page-orphan objects (not in any collection), under their page.
        for (const Ink::Page& page : doc.Pages())
            for (auto it = page.children.rbegin(); it != page.children.rend(); ++it)
                if (!OutlinerInAnyCollection(*it)) OutlinerFlattenNode(*it, 0, out);
    } else {
        // Layers: page header + its layer tree, top of stack first.
        for (const Ink::Page& page : doc.Pages()) {
            if (o.showPages) {
                OutlinerRow r; r.id = page.id; r.kind = OutlinerRow::Kind::PageHeader;
                r.depth = 0; r.hasChildren = !page.children.empty();
                out.push_back(r);
            }
            if (o.showPages && o.IsCollapsed(page.id)) continue;
            const int d = o.showPages ? 1 : 0;
            for (auto it = page.children.rbegin(); it != page.children.rend(); ++it)
                OutlinerFlattenNode(*it, d, out);
        }
    }
}

// ── Live lightweight vector preview (Layers view) ─────────────────────────────

void Application::OutlinerDrawPreview(Ink::NodeId id, ImVec2 mn, ImVec2 mx) {
    Ink::Document& doc = *project_.document;
    auto& ds = DS::DesignSystem::Instance();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Card background + frame.
    const float rad = ol::SafeFloat(Tok::S_CornerRadius_Control, 4.0f) * 0.5f * ol::Gs();
    dl->AddRectFilled(mn, mx, ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 1)), rad);
    dl->AddRect(mn, mx, ImGui::ColorConvertFloat4ToU32(
        ol::SafeColor(Tok::S_Color_Border_Default, ImVec4(0.4f,0.4f,0.4f,1))), rad);

    // Collect the paths of this node's subtree in WORLD space and their bbox.
    struct Seg { std::vector<Ink::DVec2> pts; bool closed; Ink::Color fill; bool hasFill; Ink::Color stroke; bool hasStroke; };
    std::vector<Seg> segs;
    Ink::DRect bb;
    std::function<void(Ink::NodeId)> collect = [&](Ink::NodeId nid) {
        const Ink::Node* n = doc.Find(nid);
        if (!n || !n->visible) return;
        if (n->kind == Ink::NodeKind::Path && !n->path.Empty()) {
            const Ink::DMat23 w = doc.WorldTransform(nid);
            auto polys = Ink::geom::Flatten(n->path, 2.0);
            const bool hasFill = !n->style.fills.empty() && n->style.fills.front().enabled;
            const bool hasStroke = !n->style.strokes.empty() && n->style.strokes.front().enabled;
            for (auto& pl : polys) {
                Seg s; s.closed = pl.closed; s.hasFill = hasFill; s.hasStroke = hasStroke;
                if (hasFill)   s.fill   = n->style.fills.front().paint.color;
                if (hasStroke) s.stroke = n->style.strokes.front().paint.color;
                s.pts.reserve(pl.points.size());
                for (auto& p : pl.points) { Ink::DVec2 wp = w.Apply(p); s.pts.push_back(wp); bb.Grow(wp); }
                segs.push_back(std::move(s));
            }
        }
        for (Ink::NodeId c : n->children) collect(c);
        // Cap the work: a deep/huge subtree preview is not worth stalling on.
        if (segs.size() > 400) return;
    };
    collect(id);
    if (!bb.valid || segs.empty()) return;

    // Fit the bbox into the card with padding (aspect-preserving).
    const float padF = 0.12f;
    const float cw = (mx.x - mn.x) * (1 - 2 * padF), chh = (mx.y - mn.y) * (1 - 2 * padF);
    const double bw = std::max(1e-6, bb.max.x - bb.min.x);
    const double bh = std::max(1e-6, bb.max.y - bb.min.y);
    const double sc = std::min(cw / bw, chh / bh);
    const float ox = mn.x + (mx.x - mn.x - (float)(bw * sc)) * 0.5f;
    const float oy = mn.y + (mx.y - mn.y - (float)(bh * sc)) * 0.5f;
    auto map = [&](Ink::DVec2 p) {
        return ImVec2(ox + (float)((p.x - bb.min.x) * sc),
                      oy + (float)((p.y - bb.min.y) * sc));
    };
    auto col = [](const Ink::Color& c) {
        // linear → sRGB for display.
        auto s = [](float u){ return u <= 0.0031308f ? u*12.92f : 1.055f*std::pow(u,1/2.4f)-0.055f; };
        return ImGui::ColorConvertFloat4ToU32(ImVec4(s(c.r), s(c.g), s(c.b), c.a));
    };
    dl->PushClipRect(mn, mx, true);
    for (const Seg& s : segs) {
        if (s.pts.size() < 2) continue;
        static std::vector<ImVec2> poly; poly.clear();
        for (auto& p : s.pts) poly.push_back(map(p));
        if (s.hasFill && s.closed && poly.size() >= 3)
            dl->AddConvexPolyFilled(poly.data(), (int)poly.size(), col(s.fill));
        const ImU32 lc = s.hasStroke ? col(s.stroke)
            : ImGui::ColorConvertFloat4ToU32(ol::SafeColor(Tok::S_Color_Text_Subtle, ImVec4(.5f,.5f,.5f,1)));
        dl->AddPolyline(poly.data(), (int)poly.size(),
                        lc, s.closed ? ImDrawFlags_Closed : 0, 1.0f);
    }
    dl->PopClipRect();
    (void)ds;
}

// ── Pass 2: draw one flattened row ────────────────────────────────────────────

void Application::OutlinerDrawRow(EditorState& st, const OutlinerRow& rrow, float) {
    Ink::Document& doc = *project_.document;
    auto& ds = DS::DesignSystem::Instance();
    OutlinerState& o = st.outliner;
    const bool layers = (o.display == OutlinerDisplayMode::Layers);
    const bool preview = layers && rrow.kind == OutlinerRow::Kind::Object;

    // Common ListRow config.
    UI::ListRowConfig cfg;
    cfg.id = ImGui::GetID((void*)(uintptr_t)rrow.id);
    cfg.zebraOdd = (UI::ListRowZebraIndex() & 1);
    cfg.zebraColor = ImGui::ColorConvertFloat4ToU32(
        ol::SafeColor(Tok::S_Color_Background_Layer2, ImVec4(0.15f,0.15f,0.15f,1)));
    cfg.bandMarginLeft = ol::BandMargin();
    cfg.cornerRadius = ol::SafeFloat(Tok::S_CornerRadius_Control, 4.0f) * ol::Gs();

    // ── Collection header ──
    if (rrow.kind == OutlinerRow::Kind::CollectionHeader) {
        const Ink::Collection* c = doc.FindCollection(rrow.id);
        if (!c) { UI::ListRow dummy(cfg); return; }
        cfg.selected = o.RowSelected(rrow.id);
        ImVec4 selc = ds.GetColor(Tok::C_Outliner_Row_Selected);
        cfg.colors.hover    = ImGui::ColorConvertFloat4ToU32(ImVec4(selc.x, selc.y, selc.z, 0.55f));
        cfg.colors.selected = ImGui::ColorConvertFloat4ToU32(selc);
        UI::ListRow row(cfg);
        ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
        ImGui::PushID((int)rrow.id);
        ol::DotGutter();
        bool open = !o.IsCollapsed(rrow.id);
        ol::Chevron("##cch", open);
        if (open == o.IsCollapsed(rrow.id)) o.ToggleCollapsed(rrow.id);
        ol::SlotSwatch(ImVec4(c->colorTag.r, c->colorTag.g, c->colorTag.b, c->colorTag.a));
        const float nameX = ImGui::GetCursorScreenPos().x;
        const float eyeSlot = ol::RowH();
        const float eyeX = row.BandRight() - 6.0f * ol::Gs() - eyeSlot;
        if (o.renaming == rrow.id) {
            ImGui::SetCursorScreenPos(ImVec2(nameX, row.RowTop() + (ol::RowH()-ImGui::GetTextLineHeight())*0.5f));
            ImGui::SetNextItemWidth(std::max(40.0f, eyeX - nameX - 4.0f));
            ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##crename", o.renameBuf, sizeof o.renameBuf,
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                doc.SetCollectionName(rrow.id, o.renameBuf); o.renaming = 0;
            }
            if (ImGui::IsItemDeactivated()) o.renaming = 0;
        } else {
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(nameX, row.RowTop() + (ol::RowH()-ImGui::GetTextLineHeight())*0.5f),
                ol::LabelColor(false, !c->visible), c->name.empty() ? "(collection)" : c->name.c_str());
        }
        row.SuppressInputIn(eyeX, eyeX + eyeSlot);
        {
            auto& im = VectorGraphics::IconManager::Instance();
            const char* icon = c->visible ? "eye" : "eye-closed";
            const float isz = ol::IconSize();
            const ImRect er(ImVec2(eyeX, row.RowTop()), ImVec2(eyeX+eyeSlot, row.RowTop()+ol::RowH()));
            const bool ehov = er.Contains(ImGui::GetIO().MousePos);
            ImVec4 tint = ol::SafeColor(c->visible ? Tok::S_Color_Text_Subtle : Tok::S_Color_Text_Disabled, ImVec4(.6f,.6f,.6f,1));
            if (ehov) tint = ds.GetColor(Tok::S_Color_Accent_Default);
            auto md = im.GetDefaultMetadata(icon);
            if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
            im.RenderIcon(ImGui::GetWindowDrawList(), icon,
                ImVec2(eyeX+(eyeSlot-isz)*0.5f, row.RowTop()+(ol::RowH()-isz)*0.5f), isz, md);
            if (ehov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                doc.SetCollectionVisible(rrow.id, !c->visible);
        }
        ImGui::PopID();
        const UI::ListRowInput& in = row.Input();
        if (in.doubleClicked) { o.renaming = rrow.id; std::snprintf(o.renameBuf, sizeof o.renameBuf, "%s", c->name.c_str()); }
        if (in.rightClicked) { outlinerCtxOpen_ = true; outlinerCtxPos_ = ImGui::GetIO().MousePos; outlinerCtxNode_ = rrow.id; }
        return;
    }

    // ── Page header ──
    if (rrow.kind == OutlinerRow::Kind::PageHeader) {
        const Ink::Page* page = doc.FindPage(rrow.id);
        if (!page) { UI::ListRow dummy(cfg); return; }
        UI::ListRow row(cfg);
        ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
        ImGui::PushID((int)rrow.id);
        ol::DotGutter();
        bool open = !o.IsCollapsed(rrow.id);
        ol::Chevron("##pch", open);
        if (open == o.IsCollapsed(rrow.id)) o.ToggleCollapsed(rrow.id);
        ol::SlotIcon("folder", ds.GetColor(Tok::S_Color_Text_Subtle));
        char label[160];
        std::snprintf(label, sizeof label, "%s  (%dx%d)",
                      page->name.empty() ? "Page" : page->name.c_str(),
                      (int)page->size.x, (int)page->size.y);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(ImGui::GetCursorScreenPos().x, row.RowTop() + (ol::RowH()-ImGui::GetTextLineHeight())*0.5f),
            ol::LabelColor(false, false), label);
        ImGui::PopID();
        return;
    }

    // ── Object / group ──
    const Ink::Node* n = doc.Find(rrow.id);
    if (!n) { UI::ListRow dummy(cfg); return; }
    const bool searching = o.search[0] != '\0';
    const bool selfHit = OutlinerSearchHit(rrow.id);
    const bool selected = OutlinerRowSelected(rrow.id);
    const bool active = (edit_.active == rrow.id);

    cfg.selected = selected; cfg.active = active;
    auto colf = [&](Tok normal, Tok search, float a) {
        ImVec4 cc = ol::SafeColor(selfHit && searching ? search : normal, ImVec4(0.3f,0.5f,0.9f,1));
        cc.w = a; return ImGui::ColorConvertFloat4ToU32(cc);
    };
    cfg.colors.hover         = colf(Tok::C_Outliner_Row_Hover, Tok::C_Outliner_Search_Hover, 0.55f);
    cfg.colors.selected      = colf(Tok::C_Outliner_Row_Selected, Tok::C_Outliner_Search_Selected, 1.0f);
    cfg.colors.selectedHover = colf(Tok::C_Outliner_Row_SelectedHover, Tok::C_Outliner_Search_SelectedHover, 1.0f);
    cfg.colors.active        = colf(Tok::C_Outliner_Row_Active, Tok::C_Outliner_Search_Active, 1.0f);
    cfg.colors.activeHover   = colf(Tok::C_Outliner_Row_ActiveHover, Tok::C_Outliner_Search_ActiveHover, 1.0f);
    if (selfHit && searching && !selected)
        cfg.colors.idle = colf(Tok::C_Outliner_Search_Visual, Tok::C_Outliner_Search_Visual, 0.45f);

    UI::ListRow row(cfg);
    ImGui::SetCursorScreenPos(ImVec2(row.ContentX(), row.RowTop()));
    ImGui::PushID((int)rrow.id);

    ol::DotGutter();
    for (int d = 0; d < rrow.depth; ++d) ol::ChevronSpacer();
    if (rrow.hasChildren) {
        bool open = !o.IsCollapsed(rrow.id);
        ol::Chevron("##ch", open);
        if (open == o.IsCollapsed(rrow.id)) o.ToggleCollapsed(rrow.id);
    } else {
        ol::ChevronSpacer();
    }

    const float eyeSlot = ol::RowH();
    const float eyeX = row.BandRight() - 6.0f * ol::Gs() - eyeSlot;

    // Preview card (Layers view) or the flat type icon.
    if (preview) {
        const float sz = row.RowH() * 0.86f;
        const ImVec2 pmin(ImGui::GetCursorScreenPos().x, row.RowTop() + (row.RowH()-sz)*0.5f);
        OutlinerDrawPreview(rrow.id, pmin, ImVec2(pmin.x + sz, pmin.y + sz));
        ImGui::Dummy(ImVec2(sz + 6.0f * ol::Gs(), row.RowH()));
        ImGui::SameLine(0.0f, 0.0f);
    } else {
        ol::SlotIcon(NodeIcon(*n), ds.GetColor(Tok::S_Color_Text_Default));
    }

    const float nameX = ImGui::GetCursorScreenPos().x;
    if (o.renaming == rrow.id) {
        ImGui::SetCursorScreenPos(ImVec2(nameX, row.RowTop() + (row.RowH()-ImGui::GetTextLineHeight())*0.5f));
        ImGui::SetNextItemWidth(std::max(40.0f, eyeX - nameX - 4.0f));
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", o.renameBuf, sizeof o.renameBuf,
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            Action_RenameNode(rrow.id, o.renameBuf); o.renaming = 0;
        }
        if (ImGui::IsItemDeactivated()) o.renaming = 0;
    } else {
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(nameX, row.RowTop() + (row.RowH()-ImGui::GetTextLineHeight())*0.5f),
            ol::LabelColor(selfHit && searching, !n->visible),
            n->name.empty() ? "(unnamed)" : n->name.c_str());
    }

    if (active) {
        ImVec4 dc = ol::SafeColor(Tok::S_State_Active_OnPage, ImVec4(0.95f,0.6f,0.2f,1));
        ol::ActiveDotAt(row.BandLeft(), row.RowTop(), ImGui::ColorConvertFloat4ToU32(dc));
    }

    row.SuppressInputIn(eyeX, eyeX + eyeSlot);
    {
        auto& im = VectorGraphics::IconManager::Instance();
        const char* icon = n->visible ? "eye" : "eye-closed";
        const float isz = ol::IconSize();
        const ImRect er(ImVec2(eyeX, row.RowTop()), ImVec2(eyeX+eyeSlot, row.RowTop()+ol::RowH()));
        const bool ehov = er.Contains(ImGui::GetIO().MousePos);
        ImVec4 tint = ol::SafeColor(n->visible ? Tok::S_Color_Text_Subtle : Tok::S_Color_Text_Disabled, ImVec4(.6f,.6f,.6f,1));
        if (ehov) tint = ds.GetColor(Tok::S_Color_Accent_Default);
        auto md = im.GetDefaultMetadata(icon);
        if (!md.colorZones.empty()) md.colorZones[0].customColor = tint;
        im.RenderIcon(ImGui::GetWindowDrawList(), icon,
            ImVec2(eyeX+(eyeSlot-isz)*0.5f, row.RowTop()+(ol::RowH()-isz)*0.5f), isz, md);
        if (ehov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            Action_ToggleNodeVisible(rrow.id);
    }
    ImGui::PopID();

    const UI::ListRowInput& in = row.Input();
    if (in.doubleClicked) {
        o.renaming = rrow.id;
        std::snprintf(o.renameBuf, sizeof o.renameBuf, "%s", n->name.c_str());
    } else if (in.clicked) {
        OutlinerSelectClick(rrow.id, n->kind != Ink::NodeKind::Group);
    }
    if (in.rightClicked) {
        if (!OutlinerRowSelected(rrow.id)) OutlinerSelectClick(rrow.id, true);
        outlinerCtxOpen_ = true; outlinerCtxPos_ = ImGui::GetIO().MousePos; outlinerCtxNode_ = rrow.id;
    }
    if (active && o.reqScrollToActive) ImGui::SetScrollHereY(0.5f);
}

// ── Render entry ──────────────────────────────────────────────────────────────

void Application::RenderOutliner(EditorState& st) {
    auto& ds = DS::DesignSystem::Instance();
    outlinerCur_ = &st.outliner;
    if (!project_.document) {
        ImGui::PushStyleColor(ImGuiCol_Text, ds.GetColor(Tok::S_Color_Text_Subtle));
        ImGui::TextUnformatted("No document");
        ImGui::PopStyleColor();
        return;
    }
    edit_.Prune(*project_.document);
    st.outliner.rowOrder.clear();
    if (edit_.active != Ink::kNullNode) st.outliner.active = edit_.active;

    // Viewport-sync upkeep.
    if (st.outliner.syncTarget &&
        !zoneLayout_.IsLiveEditorState(st.outliner.syncTarget, CoreEditor::Viewport))
        st.outliner.syncTarget = nullptr;
    if (st.outliner.syncPicking) {
        const bool noViewport = zoneLayout_.CountEditors(CoreEditor::Viewport) == 0;
        if (noViewport || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            st.outliner.syncPicking = false;
            if (outlinerPickingState_ == &st.outliner) outlinerPickingState_ = nullptr;
        } else {
            UI::DrawTooltipTranslucent("Select a viewport to synchronise with",
                                       ImGui::GetIO().MousePos, ds.GetFloat(Tok::S_Opacity_Moderate));
        }
    }

    // ── Pass 1: flatten ──
    std::vector<OutlinerRow> rows;
    rows.reserve(256);
    OutlinerBuildRows(st, rows);

    // ── Pass 2: windowed draw ──
    if (UI::BeginScroll("##outlinerScroll", ImVec2(0, 0))) {
        UI::ListRowResetZebra();
        const bool layers = (st.outliner.display == OutlinerDisplayMode::Layers);
        // Set the per-row band scale ONCE for the whole list (Layers = taller).
        UI::ListRowSetBandScale(layers ? kLayersRowScale : 1.0f);
        const float stripeH = UI::ListRowStripeHeight();

        const float scrollY = ImGui::GetScrollY();
        const float viewTop = scrollY;
        const float viewBot = scrollY + ImGui::GetWindowHeight();
        const float startY = ImGui::GetCursorPosY();   // local Y of the first row

        for (std::size_t i = 0; i < rows.size(); ++i) {
            const float rowTop = startY + (float)i * stripeH;
            const float rowBot = rowTop + stripeH;
            // Cull rows fully outside the visible window: advance the cursor
            // only (one Dummy), do NOT build the row.
            if (rowBot < viewTop - stripeH || rowTop > viewBot + stripeH) {
                ImGui::SetCursorPosY(rowTop);
                ImGui::Dummy(ImVec2(1.0f, stripeH));
                UI::ListRowAdvanceZebra();   // keep parity in step with row index
                continue;
            }
            ImGui::SetCursorPosY(rowTop);
            OutlinerDrawRow(st, rows[i], stripeH);
        }
        // Reserve the full content height so the scrollbar range is correct.
        ImGui::SetCursorPosY(startY + (float)rows.size() * stripeH);
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        UI::ListRowSetBandScale(1.0f);

        // Empty-space clicks (only when the pointer isn't over a row).
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
            const float my = ImGui::GetMousePos().y - ImGui::GetWindowPos().y + scrollY;
            const bool belowRows = my > startY + (float)rows.size() * stripeH;
            if (belowRows && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) edit_.Clear();
            if (belowRows && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                outlinerCtxOpen_ = true; outlinerCtxPos_ = ImGui::GetIO().MousePos;
                outlinerCtxNode_ = Ink::kNullNode;
            }
        }
    }
    UI::EndScroll();
    st.outliner.reqScrollToActive = false;

    RenderOutlinerContextMenu(st);
}

} // namespace App
