#include "render/Textures.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <stb_image.h>
#include <webp/decode.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace pob {

namespace {

bool ReadHeader(const fs::path& path, int& w, int& h) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<unsigned char> head(64 * 1024);
    in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(in.gcount()));
    if (head.size() >= 12 && std::equal(head.begin(), head.begin() + 4, "RIFF") &&
        std::equal(head.begin() + 8, head.begin() + 12, "WEBP")) {
        return WebPGetInfo(head.data(), head.size(), &w, &h) != 0;
    }
    int comp = 0;
    return stbi_info_from_memory(head.data(), static_cast<int>(head.size()), &w, &h, &comp) != 0;
}

} // namespace

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
    if (!ReadHeader(path, tex->width, tex->height)) {
        Log("Could not load image '%s'", ToLuaPath(path).c_str());
        tex->width = tex->height = 0;
        tex->state = Texture::State::Failed;
    }
    cache_[key] = tex;
    return tex;
}

void TextureManager::Clear() {
    cache_.clear();
}

} // namespace pob
