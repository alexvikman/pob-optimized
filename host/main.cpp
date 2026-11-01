// Path of Building native host.
//
// Usage: PathOfBuilding [--bundle DIR] [--data DIR] [--user DIR] [--check-boot [--eval FILE] [--input FILE] [--screenshot F]] [--size WxH] [--frames N]
//   --bundle DIR   PoB files in repository layout (default: bundled copy)
//   --data DIR     writable install directory (default: per-user app data)
//   --user DIR     user folder instead of Documents (PoB adds "Path of Building/")
//   --check-boot   boot to the main menu, print a report, exit 0 on success
//   --eval FILE    with --check-boot: run a Lua file after boot, print its result
//   --input FILE   with --check-boot: scripted mouse/keyboard input (see app/InputScript.h)
//   --screenshot F with --check-boot: save a PNG after boot (and --eval)
//   --size WxH     initial window size in points
//   --frames N     exit after N frames
//   --print-manifest FILE  print FILE as the host's updater sees it, then exit

#include "app/App.h"
#include "core/Install.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    pob::AppOptions options;
    for (int i = 1; i < argc; ++i) {
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (std::strcmp(argv[i], "--bundle") == 0) {
            if (const char* v = value()) options.paths.bundle = std::filesystem::absolute(pob::FromUtf8(v));
        } else if (std::strcmp(argv[i], "--data") == 0) {
            if (const char* v = value()) options.paths.data = std::filesystem::absolute(pob::FromUtf8(v));
        } else if (std::strcmp(argv[i], "--user") == 0) {
            if (const char* v = value()) options.paths.user = std::filesystem::absolute(pob::FromUtf8(v));
        } else if (std::strcmp(argv[i], "--check-boot") == 0) {
            options.checkBoot = true;
        } else if (std::strcmp(argv[i], "--eval") == 0) {
            if (const char* v = value()) options.evalFile = std::filesystem::absolute(pob::FromUtf8(v));
        } else if (std::strcmp(argv[i], "--input") == 0) {
            if (const char* v = value()) options.inputScript = std::filesystem::absolute(pob::FromUtf8(v));
        } else if (std::strcmp(argv[i], "--screenshot") == 0) {
            if (const char* v = value()) options.screenshot = std::filesystem::absolute(pob::FromUtf8(v));
        } else if (std::strcmp(argv[i], "--size") == 0) {
            if (const char* v = value()) std::sscanf(v, "%dx%d", &options.width, &options.height);
        } else if (std::strcmp(argv[i], "--print-manifest") == 0) {
            const char* v = value();
            std::ifstream in(pob::FromUtf8(v ? v : ""), std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), {});
            std::fputs(pob::TransformManifest(text, {}).c_str(), stdout);
            return in ? 0 : 1;
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
