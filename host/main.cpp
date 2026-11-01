// M0: an empty SDL3 window with an embedded LuaJIT state.
//
// Usage: PathOfBuilding [--smoke]
//   --smoke  run the startup checks, draw one frame and exit (for CI).

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <curl/curl.h>
#include <zlib.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <cstdio>
#include <cstring>

namespace {

// Exercises the parts of LuaJIT that PoB depends on (bit, ffi, jit).
constexpr const char* kLuaSelfTest = R"lua(
    local bit = require("bit")
    local ffi = require("ffi")
    assert(bit.band(0xff, 0x0f) == 0x0f, "bit library broken")
    assert(ffi.sizeof("int32_t") == 4, "ffi broken")
    return string.format("%s (%s, %s, jit %s)", jit.version, jit.arch, jit.os,
        jit.status() and "on" or "off")
)lua";

bool RunLuaSelfTest(lua_State* L) {
    if (luaL_loadstring(L, kLuaSelfTest) != 0 || lua_pcall(L, 0, 1, 0) != 0) {
        SDL_Log("Lua self-test failed: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    SDL_Log("LuaJIT: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
    return true;
}

void Draw(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
}

} // namespace

int main(int argc, char** argv) {
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) smoke = true;
    }

    SDL_Log("Path of Building native host %s", POB_HOST_VERSION);
    SDL_Log("SDL %d.%d.%d, zlib %s, %s", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
            SDL_MICRO_VERSION, zlibVersion(), curl_version());

    lua_State* L = luaL_newstate();
    if (!L) {
        SDL_Log("luaL_newstate failed");
        return 1;
    }
    luaL_openlibs(L);
    if (!RunLuaSelfTest(L)) {
        lua_close(L);
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        lua_close(L);
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Path of Building", 1280, 800,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &window, &renderer)) {
        SDL_Log("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        SDL_Quit();
        lua_close(L);
        return 1;
    }
    SDL_Log("Renderer: %s", SDL_GetRendererName(renderer));

    Draw(renderer);

    // Block on events instead of spinning; only redraw when something changed.
    bool running = !smoke;
    while (running) {
        SDL_Event ev;
        if (!SDL_WaitEvent(&ev)) break;
        do {
            switch (ev.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_EXPOSED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                Draw(renderer);
                break;
            default:
                break;
            }
        } while (SDL_PollEvent(&ev));
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    lua_close(L);
    return 0;
}
