// Callbacks, modules, console and general system functions (docs/API.md §2, §3.7,
// §3.8, §3.10).

#include "app/Context.h"
#include "app/Input.h"
#include "core/Log.h"
#include "lua/Api.h"
#include "lua/LuaUtil.h"

#include <SDL3/SDL.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace pob {

namespace {

constexpr size_t kMaxZlibSize = 128ull << 20;

// --- Callbacks -------------------------------------------------------------

int l_SetCallback(lua_State* L) {
    int n = lua_gettop(L);
    LuaCheck(L, n >= 1, "Usage: SetCallback(name[, func])");
    CheckString(L, "SetCallback", 1);
    if (n >= 2 && !lua_isfunction(L, 2) && !lua_isnil(L, 2)) {
        LuaFail(L, "SetCallback() argument 2: expected function or nil, got %s", luaL_typename(L, 2));
    }
    lua_settop(L, 2);
    lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
    lua_pushvalue(L, 1);
    lua_pushvalue(L, 2);
    lua_settable(L, -3);
    return 0;
}

int l_GetCallback(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: GetCallback(name)");
    CheckString(L, "GetCallback", 1);
    lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
    lua_pushvalue(L, 1);
    lua_gettable(L, -2);
    return 1;
}

int l_SetMainObject(lua_State* L) {
    if (lua_gettop(L) >= 1 && !lua_istable(L, 1) && !lua_isnil(L, 1)) {
        LuaFail(L, "SetMainObject() argument 1: expected table or nil, got %s", luaL_typename(L, 1));
    }
    lua_settop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, "uicallbacks");
    lua_pushvalue(L, 1);
    lua_setfield(L, -2, "MainObject");
    return 0;
}

// --- Modules ---------------------------------------------------------------

// Loads a module file relative to the script directory (ui_api.cpp:1864). The
// chunk name stays relative, so error messages read like the original.
int LoadModuleFile(lua_State* L, const char* fn) {
    std::string name(ArgString(L, fn, 1));
    fs::path file = FromUtf8(name);
    if (!file.has_extension()) file.replace_extension(".lua");
    auto& ctx = Ctx();
    fs::path full = file.is_absolute() ? file : ctx.scriptPath / file;
    return LoadLuaFile(L, full, ToLuaPath(file));
}

int l_LoadModule(lua_State* L) {
    int n = lua_gettop(L);
    LuaCheck(L, n >= 1, "Usage: LoadModule(name[, ...])");
    int err = LoadModuleFile(L, "LoadModule");
    if (err) {
        std::string msg = lua_tostring(L, -1) ? lua_tostring(L, -1) : "?";
        LuaFail(L, "LoadModule() error loading '%s' (%d):\n%s", lua_tostring(L, 1), err, msg.c_str());
    }
    lua_replace(L, 1);
    lua_call(L, n - 1, LUA_MULTRET);
    return lua_gettop(L);
}

int l_PLoadModule(lua_State* L) {
    int n = lua_gettop(L);
    LuaCheck(L, n >= 1, "Usage: PLoadModule(name[, ...])");
    if (LoadModuleFile(L, "PLoadModule")) return 1;
    lua_replace(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, "traceback");
    lua_insert(L, 1);
    if (lua_pcall(L, n - 1, LUA_MULTRET, 1)) return 1;
    lua_pushnil(L);
    lua_replace(L, 1);
    return lua_gettop(L);
}

int l_PCall(lua_State* L) {
    int n = lua_gettop(L);
    LuaCheck(L, n >= 1, "Usage: PCall(func[, ...])");
    if (!lua_isfunction(L, 1)) LuaFail(L, "PCall() argument 1: expected function, got %s", luaL_typename(L, 1));
    lua_getfield(L, LUA_REGISTRYINDEX, "traceback");
    lua_insert(L, 1);
    if (lua_pcall(L, n - 1, LUA_MULTRET, 1)) return 1;
    lua_pushnil(L);
    lua_replace(L, 1);
    return lua_gettop(L);
}

// --- Console ---------------------------------------------------------------

std::string StripForLog(std::string_view s) {
    return StripColorEscapes(s);
}

int l_ConPrintf(lua_State* L) {
    int n = lua_gettop(L);
    LuaCheck(L, n >= 1, "Usage: ConPrintf(fmt[, ...])");
    CheckString(L, "ConPrintf", 1);
    lua_pushvalue(L, lua_upvalueindex(1)); // string.format
    lua_insert(L, 1);
    lua_call(L, n, 1);
    LuaCheck(L, lua_isstring(L, 1), "ConPrintf() error: string.format returned non-string");
    size_t len = 0;
    const char* s = lua_tolstring(L, 1, &len);
    LogLine(StripForLog({s, len}));
    return 0;
}

int l_print(lua_State* L) {
    int n = lua_gettop(L);
    std::string line;
    lua_getglobal(L, "tostring");
    for (int i = 1; i <= n; ++i) {
        lua_pushvalue(L, -1);
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        const char* s = lua_tostring(L, -1);
        LuaCheck(L, s != nullptr, "print() error: tostring returned non-string");
        if (i > 1) line += ' ';
        line += s;
        lua_pop(L, 1);
    }
    LogLine(StripForLog(line));
    return 0;
}

void PrintTable(lua_State* L, int index, int level, bool recurse, int seenIdx, int tostringIdx) {
    lua_checkstack(L, 6);
    lua_pushnil(L);
    while (lua_next(L, index)) {
        std::string line(level * 2, ' ');
        if (lua_type(L, -2) == LUA_TSTRING) {
            line += "[\"" + std::string(lua_tostring(L, -2)) + "\"] = ";
        } else {
            lua_pushvalue(L, tostringIdx);
            lua_pushvalue(L, -3);
            lua_call(L, 1, 1);
            line += std::string(lua_tostring(L, -1) ? lua_tostring(L, -1) : "?") + " = ";
            lua_pop(L, 1);
        }
        if (lua_type(L, -1) == LUA_TTABLE) {
            bool expand = recurse;
            if (expand) {
                lua_pushvalue(L, -1);
                lua_rawget(L, seenIdx);
                expand = !lua_toboolean(L, -1);
                lua_pop(L, 1);
            }
            char ptr[32];
            std::snprintf(ptr, sizeof(ptr), "table: %p", lua_topointer(L, -1));
            if (expand) {
                lua_pushvalue(L, -1);
                lua_pushboolean(L, 1);
                lua_rawset(L, seenIdx);
                LogLine(StripForLog(line + ptr + " {"));
                PrintTable(L, lua_gettop(L), level + 1, true, seenIdx, tostringIdx);
                LogLine(std::string(level * 2, ' ') + "}");
            } else {
                LogLine(StripForLog(line + ptr + " { ... }"));
            }
        } else if (lua_type(L, -1) == LUA_TSTRING) {
            LogLine(StripForLog(line + "\"" + lua_tostring(L, -1) + "\""));
        } else {
            lua_pushvalue(L, tostringIdx);
            lua_pushvalue(L, -2);
            lua_call(L, 1, 1);
            LogLine(StripForLog(line + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?")));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
}

int l_ConPrintTable(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: ConPrintTable(tbl[, noRecurse])");
    if (!lua_istable(L, 1)) LuaFail(L, "ConPrintTable() argument 1: expected table, got %s", luaL_typename(L, 1));
    bool recurse = !lua_toboolean(L, 2);
    lua_settop(L, 1);
    lua_getglobal(L, "tostring"); // 2
    lua_newtable(L);              // 3: seen tables
    lua_pushvalue(L, 1);
    lua_pushboolean(L, 1);
    lua_rawset(L, 3);
    PrintTable(L, 1, 0, recurse, 3, 2);
    return 0;
}

int l_ConExecute(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: ConExecute(cmd)");
    // PoB only sends SimpleGraphic window cvars ("set vid_mode 8"); nothing to do.
    Log("ConExecute: %s (ignored)", std::string(ArgString(L, "ConExecute", 1)).c_str());
    return 0;
}

int l_ConClear(lua_State*) {
    return 0;
}

// --- Process control -------------------------------------------------------

int l_Restart(lua_State*) {
    Ctx().restartRequested = true;
    return 0;
}

int l_Exit(lua_State* L) {
    auto& ctx = Ctx();
    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        if (!lua_isstring(L, 1)) LuaFail(L, "Exit() argument 1: expected string or nil, got %s", luaL_typename(L, 1));
        ctx.exitMessage = lua_tostring(L, 1);
    }
    ctx.exitRequested = true;
    return 0;
}

int l_GetTime(lua_State* L) {
    auto elapsed = std::chrono::steady_clock::now() - Ctx().startTime;
    lua_pushinteger(L, static_cast<lua_Integer>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
    return 1;
}

int l_SpawnProcess(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: SpawnProcess(cmdName[, args])");
    std::string cmd(ArgString(L, "SpawnProcess", 1));
    const char* args = lua_tostring(L, 2);
    // PoB only spawns processes for the Windows updater (docs/API.md §8.4).
    Log("SpawnProcess(%s, %s) is not supported yet", cmd.c_str(), args ? args : "");
    return 0;
}

int l_OpenURL(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: OpenURL(url)");
    std::string url(ArgString(L, "OpenURL", 1));
    // PoB only opens web pages, but some URLs come from user-editable text
    // (EditControl.lua:487); never hand file: or app schemes to the OS.
    std::string scheme = url.substr(0, url.find(':'));
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) { return std::tolower(c); });
    if (url.find(':') == std::string::npos || (scheme != "http" && scheme != "https")) {
        Log("OpenURL rejected (only http/https allowed): %s", url.c_str());
        lua_pushstring(L, "Only http and https links can be opened");
        return 1;
    }
    if (!SDL_OpenURL(url.c_str())) {
        lua_pushstring(L, SDL_GetError());
        return 1;
    }
    return 0;
}

int l_SetProfiling(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: SetProfiling(isEnabled)");
    return 0;
}

int l_TakeScreenshot(lua_State*) {
    Log("TakeScreenshot is not supported");
    return 0;
}

int l_SetForeground(lua_State*) {
    if (auto* w = Ctx().window) SDL_RaiseWindow(w);
    return 0;
}

int l_SetWindowTitle(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: SetWindowTitle(title)");
    std::string title(ArgString(L, "SetWindowTitle", 1));
    if (auto* w = Ctx().window) SDL_SetWindowTitle(w, title.c_str());
    return 0;
}

int l_GetCloudProvider(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: GetCloudProvider(path)");
    CheckString(L, "GetCloudProvider", 1);
    return 0; // No cloud sync provider detection on this platform.
}

// --- Input -----------------------------------------------------------------

int l_GetCursorPos(lua_State* L) {
    auto& ctx = Ctx();
    // Cursor is tracked in window points; Lua wants logical units.
    float toPixels = ctx.dpiAware ? ctx.DisplayScale() : 1.0f;
    float scale = ctx.Scale();
    lua_pushinteger(L, static_cast<lua_Integer>(std::lround(ctx.cursorX * toPixels / scale)));
    lua_pushinteger(L, static_cast<lua_Integer>(std::lround(ctx.cursorY * toPixels / scale)));
    return 2;
}

int l_SetCursorPos(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 2, "Usage: SetCursorPos(x, y)");
    auto& ctx = Ctx();
    float toPoints = ctx.Scale() / (ctx.dpiAware ? ctx.DisplayScale() : 1.0f);
    float x = static_cast<float>(ArgNumber(L, "SetCursorPos", 1)) * toPoints;
    float y = static_cast<float>(ArgNumber(L, "SetCursorPos", 2)) * toPoints;
    if (ctx.window) SDL_WarpMouseInWindow(ctx.window, x, y);
    return 0;
}

int l_ShowCursor(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: ShowCursor(doShow)");
    return 0;
}

int l_IsKeyDown(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: IsKeyDown(keyName)");
    auto name = ArgString(L, "IsKeyDown", 1);
    LuaCheck(L, !name.empty(), "IsKeyDown() argument 1: string is empty");
    int key = KeyForName(std::string(name).c_str());
    LuaCheck(L, key != kKeyNone, "IsKeyDown(): unrecognised key name");
    lua_pushboolean(L, Ctx().keyDown[static_cast<size_t>(key)]);
    return 1;
}

int l_Copy(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: Copy(string)");
    std::string text(ArgString(L, "Copy", 1));
    SDL_SetClipboardText(text.c_str());
    return 0;
}

int l_Paste(lua_State* L) {
    char* text = SDL_GetClipboardText();
    if (!text) return 0;
    bool empty = text[0] == 0;
    if (!empty) lua_pushstring(L, text);
    SDL_free(text);
    return empty ? 0 : 1;
}

// --- Compression -----------------------------------------------------------

int PushZError(lua_State* L, const char* msg) {
    lua_pushnil(L);
    lua_pushstring(L, msg);
    return 2;
}

int l_Deflate(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: Deflate(string, [isGzip])");
    auto in = ArgString(L, "Deflate", 1);
    if (!lua_isboolean(L, 2) && !lua_isnoneornil(L, 2)) {
        LuaFail(L, "Deflate() argument 2: expected boolean or nil, got %s", luaL_typename(L, 2));
    }
    bool gzip = lua_toboolean(L, 2);
    if (in.size() > kMaxZlibSize) return PushZError(L, "Input larger than 128 MiB");

    z_stream z{};
    deflateInit2(&z, Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS + (gzip ? 16 : 0), 8, Z_DEFAULT_STRATEGY);
    uLong bound = std::min<uLong>(deflateBound(&z, static_cast<uLong>(in.size())), kMaxZlibSize);
    std::vector<Bytef> out(bound);
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    z.avail_in = static_cast<uInt>(in.size());
    z.next_out = out.data();
    z.avail_out = static_cast<uInt>(out.size());
    int err = deflate(&z, Z_FINISH);
    deflateEnd(&z);
    if (err != Z_STREAM_END) return PushZError(L, zError(err));
    lua_pushlstring(L, reinterpret_cast<const char*>(out.data()), z.total_out);
    return 1;
}

int l_Inflate(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: Inflate(string)");
    auto in = ArgString(L, "Inflate", 1);
    if (in.size() > kMaxZlibSize) return PushZError(L, "Input larger than 128 MiB");

    std::vector<Bytef> out(std::max<size_t>(in.size() * 4, 1024));
    z_stream z{};
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    z.avail_in = static_cast<uInt>(in.size());
    inflateInit2(&z, MAX_WBITS + 32); // auto-detect zlib/gzip
    int err;
    for (;;) {
        z.next_out = out.data() + z.total_out;
        z.avail_out = static_cast<uInt>(out.size() - z.total_out);
        err = inflate(&z, Z_NO_FLUSH);
        if (err != Z_OK) break;
        if (z.avail_out == 0) {
            if (out.size() > kMaxZlibSize) break;
            out.resize(out.size() * 2);
        }
    }
    inflateEnd(&z);
    if (err != Z_STREAM_END) return PushZError(L, zError(err));
    lua_pushlstring(L, reinterpret_cast<const char*>(out.data()), z.total_out);
    return 1;
}

} // namespace

void RegisterCoreApi(lua_State* L) {
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "uicallbacks");
    SetGlobalFunction(L, "SetCallback", l_SetCallback);
    SetGlobalFunction(L, "GetCallback", l_GetCallback);
    SetGlobalFunction(L, "SetMainObject", l_SetMainObject);

    SetGlobalFunction(L, "LoadModule", l_LoadModule);
    SetGlobalFunction(L, "PLoadModule", l_PLoadModule);
    SetGlobalFunction(L, "PCall", l_PCall);

    lua_getglobal(L, "string");
    lua_getfield(L, -1, "format");
    lua_pushcclosure(L, l_ConPrintf, 1);
    lua_setglobal(L, "ConPrintf");
    lua_pop(L, 1);
    SetGlobalFunction(L, "ConPrintTable", l_ConPrintTable);
    SetGlobalFunction(L, "ConExecute", l_ConExecute);
    SetGlobalFunction(L, "ConClear", l_ConClear);
    SetGlobalFunction(L, "print", l_print);

    SetGlobalFunction(L, "Restart", l_Restart);
    SetGlobalFunction(L, "Exit", l_Exit);
    SetGlobalFunction(L, "GetTime", l_GetTime);
    SetGlobalFunction(L, "SpawnProcess", l_SpawnProcess);
    SetGlobalFunction(L, "OpenURL", l_OpenURL);
    SetGlobalFunction(L, "SetProfiling", l_SetProfiling);
    SetGlobalFunction(L, "TakeScreenshot", l_TakeScreenshot);
    SetGlobalFunction(L, "SetForeground", l_SetForeground);
    SetGlobalFunction(L, "SetWindowTitle", l_SetWindowTitle);
    SetGlobalFunction(L, "GetCloudProvider", l_GetCloudProvider);

    SetGlobalFunction(L, "GetCursorPos", l_GetCursorPos);
    SetGlobalFunction(L, "SetCursorPos", l_SetCursorPos);
    SetGlobalFunction(L, "ShowCursor", l_ShowCursor);
    SetGlobalFunction(L, "IsKeyDown", l_IsKeyDown);
    SetGlobalFunction(L, "Copy", l_Copy);
    SetGlobalFunction(L, "Paste", l_Paste);

    SetGlobalFunction(L, "Deflate", l_Deflate);
    SetGlobalFunction(L, "Inflate", l_Inflate);

    lua_getglobal(L, "os");
    lua_pushcfunction(L, l_Exit);
    lua_setfield(L, -2, "exit");
    lua_pop(L, 1);
}

} // namespace pob
