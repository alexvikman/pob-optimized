// Path of Building native host.
//
// Usage: PathOfBuilding [--bundle DIR] [--data DIR] [--check-boot [--eval FILE] [--screenshot F]] [--size WxH] [--frames N]
//   --bundle DIR   PoB files in repository layout (default: bundled copy)
//   --data DIR     writable install directory (default: per-user app data)
//   --check-boot   boot to the main menu, print a report, exit 0 on success
//   --eval FILE    with --check-boot: run a Lua file after boot, print its result
//   --screenshot F with --check-boot: save a PNG after boot (and --eval)
//   --size WxH     initial window size in points
//   --frames N     exit after N frames

#include "app/App.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    pob::AppOptions options;
    for (int i = 1; i < argc; ++i) {
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (std::strcmp(argv[i], "--bundle") == 0) {
            if (const char* v = value()) options.paths.bundle = std::filesystem::absolute(pob::FromUtf8(v));
        } else if (std::strcmp(argv[i], "--data") == 0) {
            if (const char* v = value()) options.paths.data = std::filesystem::absolute(pob::FromUtf8(v));
        } else if (std::strcmp(argv[i], "--check-boot") == 0) {
            options.checkBoot = true;
        } else if (std::strcmp(argv[i], "--eval") == 0) {
            if (const char* v = value()) options.evalFile = std::filesystem::absolute(pob::FromUtf8(v));
        } else if (std::strcmp(argv[i], "--screenshot") == 0) {
            if (const char* v = value()) options.screenshot = std::filesystem::absolute(pob::FromUtf8(v));
        } else if (std::strcmp(argv[i], "--size") == 0) {
            if (const char* v = value()) std::sscanf(v, "%dx%d", &options.width, &options.height);
        } else if (std::strcmp(argv[i], "--frames") == 0) {
            if (const char* v = value()) options.maxFrames = std::atoi(v);
        } else if (std::strncmp(argv[i], "-psn_", 5) == 0) {
            // macOS Finder process serial number; ignore.
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 64;
        }
    }
    pob::App app;
    return app.Run(options);
}
