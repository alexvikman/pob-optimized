#include "render/Textures.h"

#include "app/Context.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <SDL3/SDL.h>
#include <stb_image.h>
#include <webp/decode.h>

#include <algorithm>
#include <fstream>

namespace pob {

namespace {

std::vector<uint8_t> ReadAll(const fs::path& path, size_t limit = SIZE_MAX) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::vector<uint8_t> data;
    if (limit == SIZE_MAX) {
        in.seekg(0, std::ios::end);
        data.resize(static_cast<size_t>(in.tellg()));
        in.seekg(0);
    } else {
        data.resize(limit);
    }
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<size_t>(in.gcount()));
    return data;
}

bool IsWebP(const std::vector<uint8_t>& d) {
    return d.size() >= 12 && std::equal(d.begin(), d.begin() + 4, "RIFF") && std::equal(d.begin() + 8, d.begin() + 12, "WEBP");
}

bool ReadHeader(const fs::path& path, int& w, int& h) {
    auto head = ReadAll(path, 64 * 1024);
    if (IsWebP(head)) return WebPGetInfo(head.data(), head.size(), &w, &h) != 0;
    int comp = 0;
    return stbi_info_from_memory(head.data(), static_cast<int>(head.size()), &w, &h, &comp) != 0;
}

// Decodes to RGBA8; returns false on failure.
bool Decode(Texture& tex) {
    auto data = ReadAll(tex.path);
    if (data.empty()) return false;
    int w = 0, h = 0;
    if (IsWebP(data)) {
        uint8_t* px = WebPDecodeRGBA(data.data(), data.size(), &w, &h);
        if (!px) return false;
        tex.pixels.assign(px, px + static_cast<size_t>(w) * h * 4);
        WebPFree(px);
    } else {
        int comp = 0;
        stbi_uc* px = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &comp, 4);
        if (!px) return false;
        tex.pixels.assign(px, px + static_cast<size_t>(w) * h * 4);
        stbi_image_free(px);
    }
    // Width/height were set from the header already (and may be read by the main
    // thread meanwhile); a mismatch means the file changed underneath us.
    return w == tex.width && h == tex.height;
}

std::shared_ptr<Texture> SolidTexture(int size, const uint8_t* gray) {
    auto tex = std::make_shared<Texture>();
    tex->width = tex->height = size;
    tex->pixels.resize(static_cast<size_t>(size) * size * 4);
    for (int i = 0; i < size * size; ++i) {
        uint8_t g = gray ? gray[i] : 0xFF;
        tex->pixels[i * 4 + 0] = tex->pixels[i * 4 + 1] = tex->pixels[i * 4 + 2] = g;
        tex->pixels[i * 4 + 3] = 0xFF;
    }
    tex->state = Texture::State::Ready;
    return tex;
}

// SimpleGraphic's t_defaultTexture (r_texture.cpp:34).
constexpr uint8_t kDefaultTexture[64] = {
    0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x00, 0x00, 0x7F, 0x7F,
    0x7F, 0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x00, 0x7F,
    0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x00, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
    0x7F, 0x7F, 0x7F, 0x7F, 0x00, 0x00, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
};

} // namespace

Texture::~Texture() {
    if (gpu) SDL_DestroyTexture(gpu);
}

TextureManager::TextureManager()
    : white_(SolidTexture(8, nullptr)), placeholder_(SolidTexture(8, kDefaultTexture)) {
    worker_ = std::thread([this] { WorkerMain(); });
}

TextureManager::~TextureManager() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    worker_.join();
}

std::shared_ptr<Texture> TextureManager::Acquire(const fs::path& path, int flags) {
    auto u8 = path.generic_u8string();
    std::string key(u8.begin(), u8.end());
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
    key += '|' + std::to_string(flags);

    if (auto it = cache_.find(key); it != cache_.end()) {
        if (auto tex = it->second.lock()) return tex;
    }
    auto tex = std::make_shared<Texture>();
    tex->path = path;
    tex->flags = flags;
    cache_[key] = tex;

    if (!ReadHeader(path, tex->width, tex->height)) {
        Log("Could not load image '%s'", ToLuaPath(path).c_str());
        tex->width = tex->height = 0;
        tex->state = Texture::State::Failed;
    } else if (flags & kTexAsync) {
        ++pending_;
        {
            std::lock_guard lock(mutex_);
            queue_.push_back(tex);
        }
        cv_.notify_one();
    } else {
        tex->state = Decode(*tex) ? Texture::State::Ready : Texture::State::Failed;
        if (tex->state == Texture::State::Failed) Log("Could not decode image '%s'", ToLuaPath(path).c_str());
    }
    return tex;
}

void TextureManager::WorkerMain() {
    for (;;) {
        std::shared_ptr<Texture> tex;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            // Highest loading priority first (imgHandle:SetLoadingPriority).
            auto it = std::max_element(queue_.begin(), queue_.end(), [](auto& a, auto& b) {
                return a->priority.load() < b->priority.load();
            });
            tex = std::move(*it);
            queue_.erase(it);
        }
        // Nobody else holds it: the handle was released before it finished loading.
        if (tex.use_count() > 1) {
            bool ok = Decode(*tex);
            if (!ok) Log("Could not decode image '%s'", ToLuaPath(tex->path).c_str());
            tex->state = ok ? Texture::State::Ready : Texture::State::Failed;
        }
        --pending_;
        // Safe to drop here: a texture that was never uploaded owns no GPU resources.
        tex.reset();
        if (auto& wake = Ctx().wake) wake();
    }
}

} // namespace pob
