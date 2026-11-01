#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace pob {

namespace fs = std::filesystem;

enum TextureFlags : int {
    kTexAsync = 1 << 0,
    kTexClamp = 1 << 1,
    kTexMipmap = 1 << 2,
    kTexNearest = 1 << 3,
};

struct Texture {
    enum class State { Pending, Ready, Failed };

    fs::path path;
    int flags = 0;
    // Known as soon as the handle is created (header read), even for async loads,
    // because imgHandle:ImageSize() must not block (docs/API.md §3.4).
    int width = 0;
    int height = 0;
    std::atomic<State> state{State::Pending};
};

// Shares textures by (case-insensitive path, flags) like SimpleGraphic's shader list.
class TextureManager {
public:
    std::shared_ptr<Texture> Acquire(const fs::path& path, int flags);
    // Solid white texture used for DrawImage(nil, ...).
    Texture* White() { return &white_; }
    void Clear();

private:
    std::unordered_map<std::string, std::weak_ptr<Texture>> cache_;
    Texture white_;
};

} // namespace pob
