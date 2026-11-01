#pragma once

#include "render/DrawList.h"

#include <SDL3/SDL.h>

#include <vector>

namespace pob {

struct Texture;

// Draws a recorded DrawList with SDL_Renderer (Metal on macOS, Direct3D on
// Windows). Consecutive quads sharing texture and viewport become one
// SDL_RenderGeometry call.
class Renderer {
public:
    explicit Renderer(SDL_Renderer* renderer) : renderer_(renderer) {}

    // Renders and presents one frame. Returns false if some quads were skipped
    // because their textures are still loading.
    bool DrawFrame(const DrawList& list, const Color& clearColor);

    // Reads back the last rendered frame (before present) as RGBA8.
    bool Capture(std::vector<uint8_t>& rgba, int& w, int& h);
    void SetCaptureNext(bool on) { captureNext_ = on; }

private:
    struct BatchKey {
        SDL_Texture* tex = nullptr;
        uint32_t viewport = UINT32_MAX;
        bool clamp = false;
        bool operator==(const BatchKey&) const = default;
    };

    SDL_Texture* Resolve(Texture* tex, bool& pending);
    void Flush(const DrawList& list);

    SDL_Renderer* renderer_;
    BatchKey key_;
    std::vector<SDL_Vertex> vertices_;
    std::vector<int> indices_;
    bool captureNext_ = false;
    std::vector<uint8_t> captured_;
    int capturedW_ = 0, capturedH_ = 0;
};

} // namespace pob
