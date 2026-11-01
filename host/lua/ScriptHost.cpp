#include "lua/ScriptHost.h"

#include "app/Context.h"
#include "app/Input.h"
#include "core/Log.h"
#include "lua/Api.h"
#include "lua/LuaUtil.h"
#include "lua/SubScript.h"

namespace pob {

namespace {

// From lua.c: adds a traceback to error messages.
int Traceback(lua_State* L) {
    if (!lua_isstring(L, 1)) return 1;
    lua_getglobal(L, "debug");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }
    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return 1;
    }
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    return 1;
}

int Panic(lua_State* L) {
    Log("Unprotected Lua error: %s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "?");
    return 0;
}

} // namespace

ScriptHost::~ScriptHost() {
    Shutdown();
}

bool ScriptHost::Start() {
    auto& ctx = Ctx();
    fatalError_.clear();
    ctx.restartRequested = false;
    ctx.exitRequested = false;
    ctx.inFrame = false;

    L_ = luaL_newstate();
    if (!L_) {
        fatalError_ = "Unable to create Lua state";
        return false;
    }
    lua_atpanic(L_, Panic);
    lua_pushcfunction(L_, Traceback);
    lua_setfield(L_, LUA_REGISTRYINDEX, "traceback");

    luaL_openlibs(L_);
    SetupPackages(L_);
    RegisterCoreApi(L_);
    RegisterFileApi(L_);
    RegisterRenderApi(L_);
    RegisterSubScriptApi(L_);
    InstallSafetyNet(L_);

    std::error_code ec;
    ctx.scriptWorkDir = ctx.scriptPath;
    fs::current_path(ctx.scriptWorkDir, ec);

    if (LoadLuaFile(L_, ctx.scriptPath / "Launch.lua", "Launch.lua") != 0) {
        fatalError_ = std::string("Error loading 'Launch.lua':\n") + lua_tostring(L_, -1);
        return false;
    }
    lua_newtable(L_);
    lua_pushstring(L_, "Launch.lua");
    lua_rawseti(L_, -2, 0);
    lua_setglobal(L_, "arg");
    lua_pushstring(L_, "Launch.lua");
    if (!Call(1, 0)) return false;

    if (!ctx.exitRequested && !ctx.restartRequested) {
        int extra = PushCallback("OnInit");
        if (extra >= 0 && !Call(extra, 0)) return false;
    }
    if (!ctx.exitRequested && !ctx.restartRequested) {
        int extra = PushCallback("OnFrame");
        if (extra < 0) {
            Log("Script didn't set frame callback, exiting");
            ctx.exitRequested = true;
        } else {
            lua_pop(L_, 1 + extra);
        }
    }
    return true;
}

void ScriptHost::Shutdown() {
    if (!L_) return;
    int extra = PushCallback("OnExit");
    if (extra >= 0) Call(extra, 0);
    if (Ctx().subScripts) Ctx().subScripts->Clear();
    lua_close(L_);
    L_ = nullptr;
    activeCoroutines_ = false;
}

int ScriptHost::PushCallback(const char* name) {
    if (!L_) return -1;
    lua_getfield(L_, LUA_REGISTRYINDEX, "uicallbacks");
    lua_getfield(L_, -1, name);
    if (lua_isfunction(L_, -1)) {
        lua_remove(L_, -2);
        return 0;
    }
    lua_pop(L_, 1);
    lua_getfield(L_, -1, "MainObject");
    lua_remove(L_, -2);
    if (lua_istable(L_, -1)) {
        lua_getfield(L_, -1, name);
        if (lua_isfunction(L_, -1)) {
            lua_insert(L_, -2); // function, self
            return 1;
        }
        lua_pop(L_, 2);
    } else {
        lua_pop(L_, 1);
    }
    return -1;
}

bool ScriptHost::Call(int nargs, int nresults) {
    auto& ctx = Ctx();
    int funcIdx = lua_gettop(L_) - nargs;
    lua_getfield(L_, LUA_REGISTRYINDEX, "traceback");
    lua_insert(L_, funcIdx);
    int err = lua_pcall(L_, nargs, nresults, funcIdx);
    if (err) {
        std::string msg = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "(error object is not a string)";
        lua_pop(L_, 1);
        lua_remove(L_, funcIdx);
        if (!ctx.exitRequested) {
            fatalError_ = "--- SCRIPT ERROR ---\nRuntime error in 'Launch.lua':\n" + msg;
            Log("%s", fatalError_.c_str());
        }
        ScanCoroutines();
        return false;
    }
    lua_remove(L_, funcIdx);
    ScanCoroutines();
    return true;
}

void ScriptHost::ScanCoroutines() {
    // PoB tracks its coroutines in coroutine._list() (Common.lua:54).
    activeCoroutines_ = false;
    lua_getglobal(L_, "coroutine");
    lua_getfield(L_, -1, "_list");
    if (lua_isfunction(L_, -1) && lua_pcall(L_, 0, 1, 0) == 0 && lua_istable(L_, -1)) {
        lua_pushnil(L_);
        while (lua_next(L_, -2)) {
            lua_State* co = lua_tothread(L_, -2);
            if (co && lua_status(co) == LUA_YIELD) activeCoroutines_ = true;
            lua_pop(L_, 1);
        }
    }
    lua_settop(L_, lua_gettop(L_) - 2);
}

void ScriptHost::Frame() {
    auto& ctx = Ctx();
    if (!L_) return;
    ctx.inFrame = true;
    if (ctx.subScripts) ctx.subScripts->Pump(*this);
    int extra = PushCallback("OnFrame");
    if (extra >= 0) Call(extra, 0);
    ctx.inFrame = false;
}

void ScriptHost::KeyDown(int key, bool doubleClick) {
    int extra = PushCallback("OnKeyDown");
    if (extra < 0) return;
    std::string name = KeyName(key);
    lua_pushlstring(L_, name.data(), name.size());
    lua_pushboolean(L_, doubleClick);
    Call(extra + 2, 0);
}

void ScriptHost::KeyUp(int key) {
    int extra = PushCallback("OnKeyUp");
    if (extra < 0) return;
    std::string name = KeyName(key);
    lua_pushlstring(L_, name.data(), name.size());
    lua_pushboolean(L_, 0);
    Call(extra + 2, 0);
}

void ScriptHost::Char(const std::string& ch) {
    int extra = PushCallback("OnChar");
    if (extra < 0) return;
    lua_pushlstring(L_, ch.data(), ch.size());
    lua_pushboolean(L_, 0);
    Call(extra + 2, 0);
}

bool ScriptHost::CanExit() {
    int extra = PushCallback("CanExit");
    if (extra < 0) return true;
    if (!Call(extra, 1)) return true;
    bool ok = lua_toboolean(L_, -1) != 0;
    lua_pop(L_, 1);
    return ok;
}

std::string ScriptHost::Eval(const char* code) {
    if (!L_) return "error: no Lua state";
    if (luaL_loadstring(L_, code) || lua_pcall(L_, 0, 1, 0)) {
        std::string err = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "?";
        lua_pop(L_, 1);
        return "error: " + err;
    }
    std::string out = lua_isnil(L_, -1) ? "" : (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "?");
    lua_pop(L_, 1);
    return out;
}

} // namespace pob
