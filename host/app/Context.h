#pragma once

#include "core/Paths.h"
#include "render/DrawList.h"
#include "render/Font.h"
#include "render/Textures.h"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <functional>
#include <string>

namespace pob {

class SubScriptManager;

// Host state shared by the Lua API, the main loop and the renderer.
// Main thread only.
struct HostContext {
    HostPaths paths;

    // Lua-visible directories (docs/API.md §3.6).
    fs::path scriptPath;
    fs::path runtimePath;
    fs::path scriptWorkDir;

    SDL_Window* window = nullptr;

    // Rendering state.
    bool renderInit = false;
    bool dpiAware = false;
    int dpiOverridePercent = 0;
    bool inFrame = false;
    DrawList draw;
    TextureManager textures;
    FontSet fonts;
    bool fontsLoaded = false;
    Color lastColor{1, 1, 1, 1};
    Color clearColor{0, 0, 0, 1};

    // Input state (key codes from Input.h).
    std::array<bool, 256> keyDown{};
    float cursorX = 0, cursorY = 0; // window points

    // Script control.
    bool restartRequested = false;
    bool exitRequested = false;
    std::string exitMessage;

    std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
    SubScriptManager* subScripts = nullptr;

    // Wakes the main loop from any thread.
    std::function<void()> wake;

    // DPI scale applied to every Lua coordinate (docs/API.md §3.9).
    float Scale() const;
    // Virtual screen size as returned by GetScreenSize().
    int ScreenWidth() const;
    int ScreenHeight() const;
    float DisplayScale() const;
};

HostContext& Ctx();

} // namespace pob
