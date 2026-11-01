#pragma once

#include "core/Paths.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <memory>
#include <optional>

namespace pob {

class AppUpdate;
class FrameScheduler;
class Renderer;
class ScriptHost;
class SubScriptManager;

struct AppOptions {
    PathOverrides paths;
    // Boot PoB, wait for background work to settle, print a report and exit
    // with 0 if PoB reached its main menu without errors.
    bool checkBoot = false;
    // With checkBoot: Lua file run in PoB's state after boot; result is printed.
    std::optional<fs::path> evalFile;
    // With checkBoot: scripted input run after boot/eval (app/InputScript.h).
    std::optional<fs::path> inputScript;
    // With checkBoot: save a PNG of the frame after boot (and eval).
    std::optional<fs::path> screenshot;
    // Initial window size in points.
    int width = 1280, height = 800;
    // Exit after this many frames (0 = run until closed).
    int maxFrames = 0;
};

class App {
public:
    App();
    ~App();
    int Run(const AppOptions& options);

private:
    bool InitWindow(int width, int height);
    void HandleEvent(const SDL_Event& ev);
    void RunFrame();
    bool RunScheduledFrame(const AppOptions& options);
    // Background work that needs frames to progress (boot, PoB coroutines).
    bool Busy() const;
    // Nothing left to do: used by the self test to know PoB has settled.
    bool Settled() const;
    void UpdateWindowState();
    void LogStats(bool force);
    static bool EventWatch(void* userdata, SDL_Event* ev);
    void ReportFatal(const std::string& msg);

    std::unique_ptr<ScriptHost> script_;
    std::unique_ptr<SubScriptManager> subScripts_;
    SDL_Renderer* renderer_ = nullptr;
    std::unique_ptr<Renderer> draw_;
    bool lastFrameComplete_ = true;
    Uint32 wakeEvent_ = 0;
    bool quit_ = false;
    std::optional<int> exitCode_;
    std::unique_ptr<FrameScheduler> scheduler_;
    std::unique_ptr<AppUpdate> appUpdate_;
    bool inFrame_ = false;
    bool forceRender_ = true;
    uint64_t lastHash_ = 0;
    uint64_t lastPresentedHash_ = 0;
    uint64_t lastTextureGen_ = 0;
    // --eval without --check-boot: run once PoB has settled.
    std::string pendingEval_;
    int framesRun_ = 0;
    bool statsEnabled_ = false;
    struct Stats {
        std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
        int frames = 0, rendered = 0;
        double luaMs = 0, renderMs = 0;
    } stats_;
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
