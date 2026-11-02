#include "app/App.h"

#include "app/AppUpdate.h"
#include "app/BootCheck.h"
#include "app/Context.h"
#include "app/FrameScheduler.h"
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
#ifdef _WIN32
    // Use the icon embedded by PathOfBuilding.rc (resource id 1) for the window.
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
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
    SDL_SetRenderVSync(renderer_, scheduler_->Config().vsync ? 1 : 0);
    // macOS runs a modal loop while the user drags the window edge; redraw from
    // the event watch so the content follows the resize.
    SDL_AddEventWatch(&App::EventWatch, this);
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
    // POB_HOST_STATS=1 logs frame rate and timings every 5 seconds.
    statsEnabled_ = SDL_getenv("POB_HOST_STATS") != nullptr;
    if (auto err = ResolvePaths(options.paths, ctx.paths)) {
        Log("%s", err->c_str());
        return 1;
    }
    LogInit(ctx.paths.data / "host.log");
    Log("Path of Building native host %s", POB_HOST_VERSION);
    Log("Bundle: %s", ToLuaPath(ctx.paths.bundle).c_str());
    Log("Data:   %s", ToLuaPath(ctx.paths.data).c_str());
    if (ctx.paths.user) {
        Log("User:   %s", ToLuaPath(*ctx.paths.user).c_str());
    } else {
        Log("User:   not found: %s (suggesting %s)", ctx.paths.userError.c_str(),
            ToLuaPath(ctx.paths.userSuggestion).c_str());
    }

    if (auto err = EnsureInstalled(ctx.paths)) {
        Log("Installation failed: %s", err->c_str());
        return 1;
    }
    ctx.scriptPath = ctx.paths.data;
    ctx.runtimePath = ctx.paths.data;

    scheduler_ = std::make_unique<FrameScheduler>(LoadLoopConfig(ctx.paths.data / "host-settings.ini"));
    // The self test never talks to GitHub about host releases.
    appUpdate_ = std::make_unique<AppUpdate>(ctx.paths.data / "host-state.ini",
                                             scheduler_->Config().checkForAppUpdates && !options.checkBoot);
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
        forceRender_ = true;
        scheduler_->RequestFrame();
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
    if (options.evalFile && !options.checkBoot) pendingEval_ = bootCheck.EvalCode();
    while (!quit_) {
        // Sleep until the scheduler wants a frame or an event arrives
        // (input, window changes, subscript results, finished image loads).
        auto now = std::chrono::steady_clock::now();
        auto next = scheduler_->NextFrame(now, Busy());
        SDL_Event ev;
        if (next > now) {
            Sint32 timeout = -1;
            if (next != FrameScheduler::kNever) {
                auto ms = std::chrono::ceil<std::chrono::milliseconds>(next - now).count();
                timeout = static_cast<Sint32>(std::min<long long>(ms, 60'000));
            }
            // The self test must keep checking its phases and timeouts.
            if (headless_ && (timeout < 0 || timeout > 250)) timeout = 250;
            if (SDL_WaitEventTimeout(&ev, timeout)) HandleEvent(ev);
        }
        while (SDL_PollEvent(&ev)) HandleEvent(ev);
        if (quit_) break;
        appUpdate_->Poll();
        UpdateWindowState();
        now = std::chrono::steady_clock::now();
        if (now >= scheduler_->NextFrame(now, Busy())) {
            if (!RunScheduledFrame(options)) break;
        }
        if (exitCode_) return *exitCode_;
        if (options.checkBoot) {
            auto result = bootCheck.Step(*script_, Settled());
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
    return exitCode_.value_or(0);
}

// Runs one frame plus what follows it (errors, restart, exit, --eval). Returns
// false when the app should leave the loop.
bool App::RunScheduledFrame(const AppOptions& options) {
    auto& ctx = Ctx();
    {
        RunFrame();
        if (statsEnabled_) LogStats(false);

        if (script_->HasFatalError()) {
            ReportFatal(script_->FatalError());
            exitCode_ = 1;
            return false;
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
                exitCode_ = 1;
                return false;
            }
            framesRun_ = 0;
            forceRender_ = true;
        }
        if (ctx.exitRequested) {
            if (!ctx.exitMessage.empty()) Log("Exit: %s", ctx.exitMessage.c_str());
            return false;
        }
        if (options.maxFrames && framesRun_ >= options.maxFrames) return false;
        if (!pendingEval_.empty() && Settled()) {
            Log("eval: %s", script_->Eval(pendingEval_.c_str()).c_str());
            pendingEval_.clear();
        }
    }
    return true;
}

bool App::Busy() const {
    return framesRun_ < kBootFrames || script_->HasActiveCoroutines();
}

bool App::Settled() const {
    return framesRun_ >= kBootFrames && !script_->HasActiveCoroutines() && !(subScripts_ && subScripts_->Any()) &&
           Ctx().textures.PendingCount() == 0 && lastFrameComplete_;
}

void App::UpdateWindowState() {
    if (headless_) {
        // CI machines have no visible screen; the self test behaves as if the
        // window were in front and focused.
        scheduler_->SetWindowState(true, true);
        return;
    }
    auto flags = SDL_GetWindowFlags(Ctx().window);
    bool visible = !(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED));
    bool focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    scheduler_->SetWindowState(visible, focused);
}

void App::RunFrame() {
    auto& ctx = Ctx();
    if (inFrame_) return;
    inFrame_ = true;
    auto start = std::chrono::steady_clock::now();
    ctx.draw.BeginFrame(ctx.ScreenWidth(), ctx.ScreenHeight());
    script_->Frame();
    if (ctx.fontsLoaded) appUpdate_->Draw(ctx.draw, ctx.fonts, ctx.Scale());
    ++framesRun_;
    auto luaDone = std::chrono::steady_clock::now();

    // PoB redraws everything every frame; if the result is identical to what is
    // on screen there is nothing to send to the GPU.
    uint64_t hash = ctx.draw.Hash();
    uint64_t texGen = ctx.textures.Generation();
    bool changed = hash != lastHash_;
    const auto& cfg = scheduler_->Config();
    bool skipIdentical = cfg.skipIdenticalFrames && cfg.mode == LoopConfig::Mode::Adaptive;
    bool render = forceRender_ || !lastFrameComplete_ || texGen != lastTextureGen_ || !skipIdentical;
    // A slow animation (the rotating jewel rings) changes every frame while the
    // mouse moves; until it has moved half an animation step from what is on
    // screen, the difference is not worth a GPU frame.
    if (!render && hash != lastPresentedHash_) {
        render = !(ctx.draw.MotionSince(presented_) < cfg.animationStepPx / 2);
    }
    if (render) {
        lastFrameComplete_ = draw_->DrawFrame(ctx.draw, ctx.clearColor);
        ctx.draw.CopyTo(presented_);
        lastPresentedHash_ = hash;
        lastTextureGen_ = texGen;
        forceRender_ = false;
        ++stats_.rendered;
    }
    lastHash_ = hash;
    auto end = std::chrono::steady_clock::now();
    scheduler_->FrameDone(start, changed, changed ? ctx.draw.MotionSincePrevious() : 0.0f);

    using ms = std::chrono::duration<double, std::milli>;
    ++stats_.frames;
    stats_.luaMs += ms(luaDone - start).count();
    stats_.renderMs += ms(end - luaDone).count();
    inFrame_ = false;
}

void App::LogStats(bool force) {
    auto now = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(now - stats_.since).count();
    if (!force && secs < 5.0) return;
    if (stats_.frames) {
        auto flags = SDL_GetWindowFlags(Ctx().window);
        Log("[stats] %.1f s: %d frames (%.1f fps), %d rendered, lua avg %.2f ms, render avg %.2f ms%s", secs,
            stats_.frames, stats_.frames / secs, stats_.rendered, stats_.luaMs / stats_.frames,
            stats_.renderMs / stats_.frames, (flags & SDL_WINDOW_INPUT_FOCUS) ? ", focused" : "");
    } else {
        Log("[stats] %.1f s: no frames", secs);
    }
    stats_ = Stats{};
}

bool App::EventWatch(void* userdata, SDL_Event* ev) {
    auto* app = static_cast<App*>(userdata);
    // data1 == 1 marks an expose sent from inside a live resize (SDL_video.c).
    if (ev->type == SDL_EVENT_WINDOW_EXPOSED && ev->window.data1 == 1 && !app->inFrame_ && app->script_) {
        app->forceRender_ = true;
        app->RunFrame();
    }
    return true;
}

void App::HandleEvent(const SDL_Event& ev) {
    auto& ctx = Ctx();
    auto now = std::chrono::steady_clock::now();
    if (ev.type == wakeEvent_) {
        scheduler_->RequestFrame();
        return;
    }
    switch (ev.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (script_->CanExit()) quit_ = true;
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_SHOWN:
        forceRender_ = true;
        scheduler_->RequestFrame();
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        scheduler_->RequestFrame();
        break;
    case SDL_EVENT_MOUSE_MOTION:
        ctx.cursorX = ev.motion.x;
        ctx.cursorY = ev.motion.y;
        scheduler_->OnInput(now);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        int key = KeyFromMouseButton(ev.button.button);
        if (!key) break;
        bool down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        ctx.cursorX = ev.button.x;
        ctx.cursorY = ev.button.y;
        // The host's own update notice sits on top of PoB and takes its clicks.
        float toPixels = ctx.dpiAware ? ctx.DisplayScale() : 1.0f;
        if (down && appUpdate_->HandleClick(ev.button.x * toPixels, ev.button.y * toPixels)) {
            scheduler_->OnInput(now);
            break;
        }
        ctx.keyDown[key] = down;
        scheduler_->OnInput(now);
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
        scheduler_->OnInput(now);
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        int key = KeyFromSdl(ev.key.key);
        if (!key) break;
        bool down = ev.type == SDL_EVENT_KEY_DOWN;
        ctx.keyDown[key] = down;
        scheduler_->OnInput(now);
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
        scheduler_->OnInput(now);
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
