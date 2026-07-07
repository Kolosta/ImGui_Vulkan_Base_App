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

std::uint64_t View::Texture() const { return impl_->texture; }

} // namespace Ink
