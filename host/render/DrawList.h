#pragma once

#include "render/Text.h"

#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace pob {

struct Texture;

struct Viewport {
    int x = 0, y = 0, w = 0, h = 0;
};

// One textured quad in physical pixels, relative to its viewport origin.
struct Quad {
    Texture* tex = nullptr;
    uint32_t viewport = 0;
    int stackLayer = 0;
    Color color{1, 1, 1, 1};
    float x[4]{}, y[4]{};
    float u[4]{}, v[4]{};
};

// Records a frame of PoB draw calls in SimpleGraphic's layer model
// (docs/API.md §3.2): layers sorted by (layer, subLayer), submission order within.
class DrawList {
public:
    void BeginFrame(int screenW, int screenH);

    void SetLayer(int layer, int subLayer);
    void SetSubLayer(int subLayer) { SetLayer(layer_, subLayer); }
    int SubLayer() const { return subLayer_; }

    void SetViewport(int x, int y, int w, int h);
    void ResetViewport() { SetViewport(0, 0, screenW_, screenH_); }

    // Text escapes change the draw colour for everything after them, as in
    // SimpleGraphic, where DrawString writes into the renderer's colour.
    Color& MutableColor() { return color_; }
    void SetColor(const Color& c) { color_ = c; }
    const Color& GetColor() const { return color_; }

    void AddQuad(Texture* tex, const float xy[8], const float uv[8], int stackLayer);
    void AddQuad(Texture* tex, const float xy[8], const float uv[8], int stackLayer, const Color& color);

    int ScreenWidth() const { return screenW_; }
    int ScreenHeight() const { return screenH_; }

    using LayerKey = std::pair<int, int>;
    const std::map<LayerKey, std::vector<Quad>>& Layers() const { return layers_; }

    // A copy of a recorded frame, to compare later frames against.
    struct Snapshot {
        std::map<LayerKey, std::vector<Quad>> layers;
        std::vector<Viewport> viewports;
        int screenW = 0, screenH = 0;
    };
    void CopyTo(Snapshot& out) const;
    const std::vector<Viewport>& Viewports() const { return viewports_; }
    size_t QuadCount() const;

    // Digest of everything that affects the rendered image.
    uint64_t Hash() const;

    // How far this frame moved compared with another one, in pixels: the
    // largest vertex displacement when only quad positions changed, or infinity
    // when anything else differs (quads added or removed, textures, colours,
    // viewports). Tells slow animations from fast ones and real changes.
    float MotionSince(const Snapshot& other) const;
    float MotionSincePrevious() const { return MotionSince(prev_); }
    static constexpr float kAnyChange = std::numeric_limits<float>::infinity();

private:
    std::map<LayerKey, std::vector<Quad>> layers_;
    std::vector<Viewport> viewports_;
    Snapshot prev_; // the previous frame, for MotionSincePrevious
    std::vector<Quad>* current_ = nullptr;
    int layer_ = 0, subLayer_ = 0;
    uint32_t viewport_ = 0;
    Color color_{1, 1, 1, 1};
    int screenW_ = 0, screenH_ = 0;
};

} // namespace pob
