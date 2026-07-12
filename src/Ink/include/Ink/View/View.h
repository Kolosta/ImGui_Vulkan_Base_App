#pragma once

#include "Ink/Core/Math.h"
#include "Ink/View/OverlayList.h"
#include <cstdint>
#include <vector>

namespace Ink {

class Renderer;
namespace detail { struct ViewImpl; struct RendererImpl; }

// ─────────────────────────────────────────────────────────────────────────────
//  View — one canvas: what a Viewport zone talks to
//  (docs/Ink/ARCHITECTURE.md §7). Owned by Renderer, acquired per frame with
//  Renderer::AcquireView(key). The app sets the viewport size, the camera and
//  the background, fills the overlay list, and blits Texture() with a single
//  ImGui::Image — every pixel inside is Ink's.
//
//  Camera contract (matches the zone layout's EditorState):
//      screen_px = (doc - pan) · zoom
// ─────────────────────────────────────────────────────────────────────────────
class View {
public:
    ~View();

    // Target size in pixels. Recreates the render targets when it changes
    // (old ones are retired through the frame garbage ring — never destroyed
    // while a submitted frame may still reference them).
    void SetViewport(std::uint32_t width, std::uint32_t height);

    // pan = document point at the canvas origin; zoom = px per document unit.
    // Kept in double end-to-end; narrowed per frame relative to the view.
    void SetCamera(double panX, double panY, double zoom);

    // Canvas background (linear premultiplied; resolve design tokens app-side
    // and convert with SrgbToLinearPremultiplied — the engine is token-free).
    void SetBackground(const Color& linearPremultiplied);

    // Editor overlay primitives for THIS frame (cleared after recording).
    OverlayList& Overlay();

    // PREVIEW mode (Outliner thumbnails): render ONLY the given owner set in
    // isolation — the node plus its layer subtree — through the real pipeline
    // (identical strokes / patterns / transparency / MSAA to a viewport).
    // Empty clears it back to a normal full-scene view. Cheap: it just filters
    // the command build; call once per frame before EndFrame like the camera.
    void SetPreviewFilter(const std::vector<std::uint64_t>& owners);

    // The sampled canvas texture handle as registered through the app's
    // TextureHooks (an ImTextureID app-side). 0 until SetViewport ran, or
    // when no hooks were provided (headless / bench).
    std::uint64_t Texture() const;

private:
    friend class Renderer;
    friend struct detail::RendererImpl;
    View();
    detail::ViewImpl* impl_ = nullptr;
};

} // namespace Ink
