#include "app/App.h"

#include "app/BootCheck.h"
#include "app/Context.h"
#include "app/Input.h"
#include "core/Install.h"
#include "core/Log.h"
#include "lua/ScriptHost.h"
#include "lua/SubScript.h"
#include "render/Renderer.h"
#include "render/Text.h"
#include "render/Textures.h"

#include <stb_image_write.h>

#include <curl/curl.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace pob {

namespace {

constexpr auto kDoubleClickTime = std::chrono::milliseconds(500);
constexpr float kDoubleClickRange = 5.0f;
// Frames PoB always gets after boot (ui_main.cpp:381).
constexpr int kBootFrames = 10;

} // namespace

App::App() = default;

App::~App() {
    script_.reset();
    subScripts_.reset();
    draw_.reset();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (auto* w = Ctx().window) SDL_DestroyWindow(w);
    Ctx().window = nullptr;
    SDL_Quit();
}

bool App::InitWindow(int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_Window* window = nullptr;
    if (!SDL_CreateWindowAndRenderer("Path of Building", width, height,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN, &window,
                                     &renderer_)) {
        Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, 800, 600);
    Ctx().window = window;
    Log("Renderer: %s", SDL_GetRendererName(renderer_));
    draw_ = std::make_unique<Renderer>(renderer_);

    wakeEvent_ = SDL_RegisterEvents(1);
    Uint32 wakeType = wakeEvent_;
    Ctx().wake = [wakeType] {
        SDL_Event ev{};
        ev.type = wakeType;
        SDL_PushEvent(&ev);
    };
    SDL_StartTextInput(window);
    return true;
}

int App::Run(const AppOptions& options) {
    auto& ctx = Ctx();
    headless_ = options.checkBoot;
    // POB_HOST_STATS=1 logs per-frame timings.
    statsEnabled_ = SDL_getenv("POB_HOST_STATS") != nullptr;
    if (auto err = ResolvePaths(options.paths, ctx.paths)) {
        Log("%s", err->c_str());
        return 1;
    }
    LogInit(ctx.paths.data / "host.log");
    Log("Path of Building native host %s", POB_HOST_VERSION);
    Log("Bundle: %s", ToLuaPath(ctx.paths.bundle).c_str());
    Log("Data:   %s", ToLuaPath(ctx.paths.data).c_str());

    if (auto err = EnsureInstalled(ctx.paths)) {
        Log("Installation failed: %s", err->c_str());
        return 1;
    }
    ctx.scriptPath = ctx.paths.data;
    ctx.runtimePath = ctx.paths.data;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (!InitWindow(options.width, options.height)) return 1;

    subScripts_ = std::make_unique<SubScriptManager>();
    ctx.subScripts = subScripts_.get();
    script_ = std::make_unique<ScriptHost>();
    if (!script_->Start()) {
        ReportFatal(script_->FatalError());
        return 1;
    }
    SDL_ShowWindow(ctx.window);

    BootCheck::Options checkOptions;
    if (options.evalFile) {
        std::ifstream in(*options.evalFile, std::ios::binary);
        checkOptions.evalCode.assign(std::istreambuf_iterator<char>(in), {});
    }
    if (options.screenshot) checkOptions.screenshot = *options.screenshot;
    if (options.inputScript) checkOptions.inputScript = *options.inputScript;
    checkOptions.requestCapture = [this] {
        draw_->SetCaptureNext(true);
        dirty_ = true;
    };
    checkOptions.saveCapture = [this](const fs::path& file) {
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        if (!draw_->Capture(px, w, h)) return false;
        std::string name = ToLuaPath(file);
        if (!stbi_write_png(name.c_str(), w, h, 4, px.data(), w * 4)) Log("Could not write %s", name.c_str());
        return true;
    };
    BootCheck bootCheck(std::move(checkOptions));
    while (!quit_) {
        // Interim pacing: event driven, ~60 fps while PoB has work in flight.
        // The low-power scheduler replaces this (docs/API.md §11).
        SDL_Event ev;
        int timeout = NeedsFrames() ? 16 : 250;
        if (SDL_WaitEventTimeout(&ev, timeout)) {
            HandleEvent(ev);
            while (SDL_PollEvent(&ev)) HandleEvent(ev);
        }
        if (quit_) break;
        RunFrame();

        if (script_->HasFatalError()) {
            ReportFatal(script_->FatalError());
            return 1;
        }
        if (ctx.exitRequested && ctx.restartAfterExit) {
            ctx.exitRequested = false;
            ctx.restartAfterExit = false;
            ctx.restartRequested = true;
        }
        if (ctx.restartRequested) {
            Log("Restarting script");
            // The window and renderer survive a restart, as in SimpleGraphic, so
            // RenderInit() in the new state is a no-op.
            script_->Shutdown();
            if (!script_->Start()) {
                ReportFatal(script_->FatalError());
                return 1;
            }
            framesRun_ = 0;
        }
        if (ctx.exitRequested) {
            if (!ctx.exitMessage.empty()) Log("Exit: %s", ctx.exitMessage.c_str());
            break;
        }
        if (options.maxFrames && framesRun_ >= options.maxFrames) break;
        if (options.checkBoot) {
            bool settled = framesRun_ >= kBootFrames && !NeedsFrames();
            auto result = bootCheck.Step(*script_, settled);
            if (result != BootCheck::Result::Running) {
                std::printf("%s", bootCheck.Report().c_str());
                Log("Boot check %s:\n%s", result == BootCheck::Result::Passed ? "passed" : "FAILED",
                    bootCheck.Report().c_str());
                script_->Shutdown();
                return result == BootCheck::Result::Passed ? 0 : 2;
            }
        }
    }
    script_->Shutdown();
    return 0;
}

bool App::NeedsFrames() const {
    return dirty_ || !lastFrameComplete_ || framesRun_ < kBootFrames || script_->HasActiveCoroutines() ||
           (subScripts_ && subScripts_->Any()) || Ctx().textures.PendingCount() > 0;
}

void App::RunFrame() {
    auto& ctx = Ctx();
    dirty_ = false;
    auto start = std::chrono::steady_clock::now();
    ctx.draw.BeginFrame(ctx.ScreenWidth(), ctx.ScreenHeight());
    script_->Frame();
    ++framesRun_;

    auto luaDone = std::chrono::steady_clock::now();
    lastFrameComplete_ = draw_->DrawFrame(ctx.draw, ctx.clearColor);
    if (statsEnabled_) {
        auto end = std::chrono::steady_clock::now();
        using ms = std::chrono::duration<double, std::milli>;
        Log("[stats] frame %d: lua %.2f ms, render %.2f ms, %zu quads", framesRun_, ms(luaDone - start).count(),
            ms(end - luaDone).count(), ctx.draw.QuadCount());
    }
}

void App::HandleEvent(const SDL_Event& ev) {
    auto& ctx = Ctx();
    if (ev.type == wakeEvent_) return;
    switch (ev.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (script_->CanExit()) quit_ = true;
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        dirty_ = true;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        ctx.cursorX = ev.motion.x;
        ctx.cursorY = ev.motion.y;
        dirty_ = true;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int key = KeyFromMouseButton(ev.button.button);
        if (!key) break;
        bool down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        ctx.cursorX = ev.button.x;
        ctx.cursorY = ev.button.y;
        ctx.keyDown[key] = down;
        dirty_ = true;
        if (!down) {
            script_->KeyUp(key);
            break;
        }
        // Double click: same button again within 500 ms and 5 px (sys_video.cpp:578).
        auto now = std::chrono::steady_clock::now();
        bool dbl = lastClick_ && lastClick_->key == key && now - lastClick_->time <= kDoubleClickTime &&
                   std::fabs(lastClick_->x - ev.button.x) <= kDoubleClickRange &&
                   std::fabs(lastClick_->y - ev.button.y) <= kDoubleClickRange;
        if (dbl) lastClick_.reset();
        else lastClick_ = LastClick{key, ev.button.x, ev.button.y, now};
        script_->KeyDown(key, dbl);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        // Trackpads send many fractional deltas; emit one wheel step per unit.
        wheelAccum_ += ev.wheel.y;
        while (std::fabs(wheelAccum_) >= 1.0f) {
            int key = wheelAccum_ > 0 ? kKeyWheelUp : kKeyWheelDown;
            wheelAccum_ -= wheelAccum_ > 0 ? 1.0f : -1.0f;
            script_->KeyDown(key, false);
            script_->KeyUp(key);
        }
        dirty_ = true;
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        int key = KeyFromSdl(ev.key.key);
        if (!key) break;
        bool down = ev.type == SDL_EVENT_KEY_DOWN;
        ctx.keyDown[key] = down;
        dirty_ = true;
        if (down) {
            script_->KeyDown(key, false);
            if (char ch = ExtraCharForKey(key)) script_->Char(std::string(1, ch));
        } else {
            script_->KeyUp(key);
        }
        break;
    }
    case SDL_EVENT_TEXT_INPUT: {
        auto text = IndexUtf8(ev.text.text ? ev.text.text : "");
        for (char32_t cp : text.text) {
            std::string ch;
            AppendUtf8(ch, cp);
            script_->Char(ch);
        }
        dirty_ = true;
        break;
    }
    default:
        break;
    }
}

void App::ReportFatal(const std::string& msg) {
    Log("Fatal: %s", msg.c_str());
    if (headless_) return;
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Path of Building", msg.c_str(), Ctx().window);
}

} // namespace pob
