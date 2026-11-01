#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct SDL_Texture;

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

    ~Texture();

    fs::path path;
    int flags = 0;
    // Known as soon as the handle is created (header read), even for async loads,
    // because imgHandle:ImageSize() must not block (docs/API.md §3.4).
    int width = 0;
    int height = 0;
    std::atomic<State> state{State::Pending};
    std::atomic<int> priority{0};

    // Decoded RGBA8 pixels, kept until the renderer uploads them (main thread).
    std::vector<uint8_t> pixels;
    SDL_Texture* gpu = nullptr;
};

// Shares textures by (case-insensitive path, flags) like SimpleGraphic's shader
// list. Synchronous loads decode immediately; "ASYNC" loads decode on a worker
// thread, highest priority first, and wake the main loop when done.
class TextureManager {
public:
    TextureManager();
    ~TextureManager();

    std::shared_ptr<Texture> Acquire(const fs::path& path, int flags);
    // Solid white texture used for DrawImage(nil, ...).
    Texture* White() { return white_.get(); }
    // 8×8 grey pattern drawn for images that failed to load (r_texture.cpp:605).
    Texture* Placeholder() { return placeholder_.get(); }
    // Number of async loads not finished yet (GetAsyncCount()).
    int PendingCount() const { return pending_.load(); }

private:
    void WorkerMain();

    std::unordered_map<std::string, std::weak_ptr<Texture>> cache_;
    std::shared_ptr<Texture> white_;
    std::shared_ptr<Texture> placeholder_;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::shared_ptr<Texture>> queue_;
    std::atomic<int> pending_{0};
    bool stop_ = false;
    std::thread worker_;
};

} // namespace pob
