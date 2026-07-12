#include "Ink/View/View.h"

#include "Render/RendererInternal.h"

namespace Ink {

View::View() = default;
View::~View() = default;   // impl_ is owned and freed by the Renderer

void View::SetViewport(std::uint32_t width, std::uint32_t height) {
    detail::ViewImpl& v = *impl_;
    if (width == 0 || height == 0) return;
    if (v.width == width && v.height == height && v.HasTargets()) return;
    v.owner->CreateViewTargets(v, width, height);   // retires the old chain
}

void View::SetCamera(double panX, double panY, double zoom) {
    detail::ViewImpl& v = *impl_;
    v.panX = panX;
    v.panY = panY;
    v.zoom = zoom > 0.0 ? zoom : v.zoom;
}

void View::SetBackground(const Color& linearPremultiplied) {
    impl_->background = linearPremultiplied;
}

OverlayList& View::Overlay() { return impl_->overlay; }

void View::SetPreviewFilter(const std::vector<std::uint64_t>& owners) {
    detail::ViewImpl& v = *impl_;
    // Detect a real change so the view only re-records when the filter moves.
    bool changed = owners.size() != v.previewOwners.size();
    if (!changed)
        for (std::uint64_t o : owners)
            if (v.previewOwners.find(o) == v.previewOwners.end()) { changed = true; break; }
    if (!changed) return;
    v.previewOwners.clear();
    for (std::uint64_t o : owners) v.previewOwners.insert(o);
    ++v.previewFilterGen;
}

std::uint64_t View::Texture() const { return impl_->texture; }

} // namespace Ink
