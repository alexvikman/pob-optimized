#pragma once

#include "core/Paths.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <memory>
#include <optional>

namespace pob {

class ScriptHost;
class SubScriptManager;

struct AppOptions {
    PathOverrides paths;
    // Boot PoB, wait for background work to settle, print a report and exit
    // with 0 if PoB reached its main menu without errors.
    bool checkBoot = false;
    // With checkBoot: Lua file run in PoB's state after boot; result is printed.
    std::optional<fs::path> evalFile;
    // Exit after this many frames (0 = run until closed).
    int maxFrames = 0;
};

class App {
public:
    App();
    ~App();
    int Run(const AppOptions& options);

private:
    bool InitWindow();
    void HandleEvent(const SDL_Event& ev);
    void RunFrame();
    bool NeedsFrames() const;
    void ReportFatal(const std::string& msg);

    std::unique_ptr<ScriptHost> script_;
    std::unique_ptr<SubScriptManager> subScripts_;
    SDL_Renderer* renderer_ = nullptr;
    Uint32 wakeEvent_ = 0;
    bool quit_ = false;
    bool dirty_ = true;
    int framesRun_ = 0;
    bool headless_ = false; // --check-boot: report errors on stdout, no dialogs
    float wheelAccum_ = 0;

    struct LastClick {
        int key;
        float x, y;
        std::chrono::steady_clock::time_point time;
    };
    std::optional<LastClick> lastClick_;
};

} // namespace pob
