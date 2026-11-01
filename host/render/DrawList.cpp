#include "render/DrawList.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pob {

namespace {

// FNV-1a, 64-bit. Fast enough for a few thousand quads per frame.
struct Hasher {
    uint64_t h = 1469598103934665603ull;
    void Bytes(const void* p, size_t n) {
        auto b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    template <typename T>
    void Value(const T& v) {
        Bytes(&v, sizeof(v));
    }
};

} // namespace

void DrawList::BeginFrame(int screenW, int screenH) {
    prevLayers_.swap(layers_);
    prevViewports_.swap(viewports_);
    prevScreenW_ = screenW_;
    prevScreenH_ = screenH_;
    layers_.clear();
    viewports_.clear();
    screenW_ = screenW;
    screenH_ = screenH;
    layer_ = subLayer_ = 0;
    current_ = &layers_[{0, 0}];
    color_ = {1, 1, 1, 1};
    ResetViewport();
}

void DrawList::SetLayer(int layer, int subLayer) {
    if (current_ && layer == layer_ && subLayer == subLayer_) return;
    layer_ = layer;
    subLayer_ = subLayer;
    current_ = &layers_[{layer, subLayer}];
}

void DrawList::SetViewport(int x, int y, int w, int h) {
    Viewport vp{x, y, w, h};
    if (!viewports_.empty()) {
        auto& last = viewports_.back();
        if (last.x == x && last.y == y && last.w == w && last.h == h) {
            viewport_ = static_cast<uint32_t>(viewports_.size() - 1);
            return;
        }
    }
    viewports_.push_back(vp);
    viewport_ = static_cast<uint32_t>(viewports_.size() - 1);
}

void DrawList::AddQuad(Texture* tex, const float xy[8], const float uv[8], int stackLayer) {
    AddQuad(tex, xy, uv, stackLayer, color_);
}

void DrawList::AddQuad(Texture* tex, const float xy[8], const float uv[8], int stackLayer, const Color& color) {
    Quad& q = current_->emplace_back();
    q.tex = tex;
    q.viewport = viewport_;
    q.stackLayer = stackLayer;
    q.color = color;
    for (int i = 0; i < 4; ++i) {
        q.x[i] = xy[i * 2];
        q.y[i] = xy[i * 2 + 1];
        q.u[i] = uv[i * 2];
        q.v[i] = uv[i * 2 + 1];
    }
}

size_t DrawList::QuadCount() const {
    size_t n = 0;
    for (auto& [key, quads] : layers_) n += quads.size();
    return n;
}

uint64_t DrawList::Hash() const {
    Hasher h;
    h.Value(screenW_);
    h.Value(screenH_);
    for (auto& [key, quads] : layers_) {
        if (quads.empty()) continue;
        h.Value(key);
        for (auto& q : quads) {
            const Viewport& vp = viewports_[q.viewport];
            h.Value(vp);
            h.Value(q.tex);
            h.Value(q.stackLayer);
            h.Bytes(q.color.data(), sizeof(float) * 4);
            h.Bytes(q.x, sizeof(q.x));
            h.Bytes(q.y, sizeof(q.y));
            h.Bytes(q.u, sizeof(q.u));
            h.Bytes(q.v, sizeof(q.v));
        }
    }
    return h.h;
}

float DrawList::MotionSincePrevious() const {
    if (screenW_ != prevScreenW_ || screenH_ != prevScreenH_ || layers_.size() != prevLayers_.size() ||
        viewports_.size() != prevViewports_.size()) {
        return kAnyChange;
    }
    for (size_t i = 0; i < viewports_.size(); ++i) {
        const Viewport &a = viewports_[i], &b = prevViewports_[i];
        if (a.x != b.x || a.y != b.y || a.w != b.w || a.h != b.h) return kAnyChange;
    }
    float motion = 0;
    for (auto cur = layers_.begin(), prev = prevLayers_.begin(); cur != layers_.end(); ++cur, ++prev) {
        if (cur->first != prev->first || cur->second.size() != prev->second.size()) return kAnyChange;
        for (size_t i = 0; i < cur->second.size(); ++i) {
            const Quad &a = cur->second[i], &b = prev->second[i];
            if (a.tex != b.tex || a.viewport != b.viewport || a.stackLayer != b.stackLayer || a.color != b.color ||
                std::memcmp(a.u, b.u, sizeof(a.u)) != 0 || std::memcmp(a.v, b.v, sizeof(a.v)) != 0) {
                return kAnyChange;
            }
            for (int k = 0; k < 4; ++k) {
                motion = std::max({motion, std::fabs(a.x[k] - b.x[k]), std::fabs(a.y[k] - b.y[k])});
            }
        }
    }
    return motion;
}

} // namespace pob
