#include "render/Renderer.h"

#include "app/Context.h"
#include "core/Log.h"
#include "render/Textures.h"

#include <cstring>

namespace pob {

SDL_Texture* Renderer::Resolve(Texture* tex, bool& pending) {
    auto& textures = Ctx().textures;
    if (!tex) tex = textures.White();
    switch (tex->state.load()) {
    case Texture::State::Pending:
        pending = true;
        return nullptr;
    case Texture::State::Failed:
        tex = textures.Placeholder();
        break;
    case Texture::State::Ready:
        break;
    }
    if (!tex->gpu) {
        tex->gpu = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, tex->width,
                                     tex->height);
        if (!tex->gpu) {
            Log("SDL_CreateTexture(%dx%d) failed: %s", tex->width, tex->height, SDL_GetError());
            tex->state = Texture::State::Failed;
            tex->pixels.clear();
            return Resolve(textures.Placeholder(), pending);
        }
        SDL_UpdateTexture(tex->gpu, nullptr, tex->pixels.data(), tex->width * 4);
        SDL_SetTextureBlendMode(tex->gpu, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex->gpu, (tex->flags & kTexNearest) ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
        tex->pixels.clear();
        tex->pixels.shrink_to_fit();
    }
    return tex->gpu;
}

void Renderer::Flush(const DrawList& list) {
    if (indices_.empty()) return;
    const Viewport& vp = list.Viewports()[key_.viewport];
    // The viewport translates and clips like SimpleGraphic's (r_main.cpp:443, 505).
    SDL_Rect rect{vp.x, vp.y, vp.w, vp.h};
    SDL_SetRenderViewport(renderer_, &rect);
    SDL_TextureAddressMode mode = key_.clamp ? SDL_TEXTURE_ADDRESS_CLAMP : SDL_TEXTURE_ADDRESS_WRAP;
    SDL_SetRenderTextureAddressMode(renderer_, mode, mode);
    SDL_RenderGeometry(renderer_, key_.tex, vertices_.data(), static_cast<int>(vertices_.size()), indices_.data(),
                       static_cast<int>(indices_.size()));
    vertices_.clear();
    indices_.clear();
}

bool Renderer::DrawFrame(const DrawList& list, const Color& clear) {
    bool pending = false;
    SDL_SetRenderViewport(renderer_, nullptr);
    SDL_SetRenderDrawColorFloat(renderer_, clear[0], clear[1], clear[2], clear[3]);
    SDL_RenderClear(renderer_);

    key_ = BatchKey{};
    for (auto& [layerKey, quads] : list.Layers()) {
        for (const Quad& q : quads) {
            const Viewport& vp = list.Viewports()[q.viewport];
            if (vp.w <= 0 || vp.h <= 0) continue;
            SDL_Texture* tex = Resolve(q.tex, pending);
            if (!tex) continue;
            BatchKey key{tex, q.viewport, q.tex && (q.tex->flags & kTexClamp)};
            if (!(key == key_)) {
                Flush(list);
                key_ = key;
            }
            int base = static_cast<int>(vertices_.size());
            SDL_FColor color{q.color[0], q.color[1], q.color[2], q.color[3]};
            for (int i = 0; i < 4; ++i) {
                vertices_.push_back(SDL_Vertex{{q.x[i], q.y[i]}, color, {q.u[i], q.v[i]}});
            }
            // 3-2
            // |/|
            // 0-1
            for (int idx : {0, 1, 2, 0, 2, 3}) indices_.push_back(base + idx);
        }
    }
    Flush(list);
    SDL_SetRenderViewport(renderer_, nullptr);

    if (captureNext_) {
        captureNext_ = false;
        if (SDL_Surface* s = SDL_RenderReadPixels(renderer_, nullptr)) {
            if (SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32)) {
                capturedW_ = rgba->w;
                capturedH_ = rgba->h;
                captured_.resize(static_cast<size_t>(rgba->w) * rgba->h * 4);
                for (int y = 0; y < rgba->h; ++y) {
                    std::memcpy(captured_.data() + static_cast<size_t>(y) * rgba->w * 4,
                                static_cast<uint8_t*>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch,
                                static_cast<size_t>(rgba->w) * 4);
                }
                SDL_DestroySurface(rgba);
            }
            SDL_DestroySurface(s);
        }
    }
    SDL_RenderPresent(renderer_);
    return !pending;
}

bool Renderer::Capture(std::vector<uint8_t>& rgba, int& w, int& h) {
    if (captured_.empty()) return false;
    rgba = captured_;
    w = capturedW_;
    h = capturedH_;
    return true;
}

} // namespace pob
