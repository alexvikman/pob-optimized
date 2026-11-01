#include "lua/SubScript.h"

#include "app/Context.h"
#include "core/Log.h"
#include "lua/Api.h"
#include "lua/LuaUtil.h"
#include "lua/ScriptHost.h"

#include <sstream>

namespace pob {

namespace {

constexpr const char* kSelfKey = "pob.subscript";

int Traceback(lua_State* L) {
    if (!lua_isstring(L, 1)) return 1;
    lua_getglobal(L, "debug");
    if (!lua_istable(L, -1)) return 1;
    lua_getfield(L, -1, "traceback");
    if (!lua_isfunction(L, -1)) return 1;
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    return 1;
}

void StopHook(lua_State* L, lua_Debug*) {
    luaL_error(L, "subscript aborted");
}

int NoExit(lua_State*) {
    return 0;
}

std::vector<std::string> SplitList(const std::string& list) {
    std::vector<std::string> out;
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

void* LightId(uint32_t id) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(id));
}

} // namespace

void PushSubValues(lua_State* L, const SubValues& values) {
    lua_checkstack(L, static_cast<int>(values.size()) + 1);
    for (auto& v : values) {
        if (auto* b = std::get_if<bool>(&v)) lua_pushboolean(L, *b);
        else if (auto* d = std::get_if<double>(&v)) lua_pushnumber(L, *d);
        else if (auto* s = std::get_if<std::string>(&v)) lua_pushlstring(L, s->data(), s->size());
        else lua_pushnil(L);
    }
}

bool ReadSubValues(lua_State* L, int first, SubValues& out, int* bad) {
    out.clear();
    int top = lua_gettop(L);
    for (int i = first; i <= top; ++i) {
        switch (lua_type(L, i)) {
        case LUA_TNIL: out.emplace_back(std::monostate{}); break;
        case LUA_TBOOLEAN: out.emplace_back(lua_toboolean(L, i) != 0); break;
        case LUA_TNUMBER: out.emplace_back(lua_tonumber(L, i)); break;
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(L, i, &len);
            out.emplace_back(std::string(s, len));
            break;
        }
        default:
            if (bad) *bad = i;
            return false;
        }
    }
    return true;
}

// --- SubScript --------------------------------------------------------------

SubScript::SubScript(uint32_t id, std::string script, std::vector<std::string> funcs, std::vector<std::string> subs,
                     SubValues args)
    : id_(id), script_(std::move(script)), funcs_(std::move(funcs)), subs_(std::move(subs)), args_(std::move(args)) {}

SubScript::~SubScript() {
    Abort();
    if (L_) lua_close(L_);
}

std::optional<std::string> SubScript::Start() {
    L_ = luaL_newstate();
    if (!L_) return "could not create Lua state";
    lua_pushlightuserdata(L_, this);
    lua_setfield(L_, LUA_REGISTRYINDEX, kSelfKey);
    luaL_openlibs(L_);
    SetupPackages(L_);
    lua_getglobal(L_, "os");
    lua_pushcfunction(L_, NoExit);
    lua_setfield(L_, -2, "exit");
    lua_pop(L_, 1);
    for (auto& name : funcs_) {
        lua_pushstring(L_, name.c_str());
        lua_pushcclosure(L_, FuncThunk, 1);
        lua_setglobal(L_, name.c_str());
    }
    for (auto& name : subs_) {
        lua_pushstring(L_, name.c_str());
        lua_pushcclosure(L_, SubThunk, 1);
        lua_setglobal(L_, name.c_str());
    }
    lua_pushcfunction(L_, Traceback);
    // PoB passes whole files such as UpdateCheck.lua, which start with "#@".
    BlankLeadingHashLine(script_);
    if (luaL_loadbuffer(L_, script_.data(), script_.size(), "subscript")) {
        std::string err = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "load failed";
        return err;
    }
    PushSubValues(L_, args_);
    thread_ = std::thread([this] { ThreadMain(); });
    return std::nullopt;
}

void SubScript::ThreadMain() {
    // Stack: traceback, chunk, args...
    int nargs = static_cast<int>(args_.size());
    if (lua_pcall(L_, nargs, LUA_MULTRET, 1)) {
        error_ = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
    } else {
        int bad = 0;
        if (!ReadSubValues(L_, 2, results_, &bad)) {
            error_ = "Subscript return " + std::to_string(bad - 1) +
                     ": only nil, boolean, number and string can be returned from sub script";
        }
    }
    finished_ = true;
    if (auto& wake = Ctx().wake) wake();
}

int SubScript::FuncThunk(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kSelfKey);
    auto* self = static_cast<SubScript*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    const char* name = lua_tostring(L, lua_upvalueindex(1));
    Call call{name, {}};
    int bad = 0;
    if (!ReadSubValues(L, 1, call.args, &bad)) {
        return luaL_error(L, "%s() argument %d: only nil, boolean, number and string can be passed to the main script",
                          name, bad);
    }
    SubValues result;
    {
        std::unique_lock lock(self->mutex_);
        self->funcCall_ = std::move(call);
        self->funcResult_.reset();
        if (auto& wake = Ctx().wake) wake();
        self->cv_.wait(lock, [self] { return self->funcResult_.has_value() || self->aborting_; });
        if (self->funcResult_) result = std::move(*self->funcResult_);
        self->funcResult_.reset();
    }
    lua_settop(L, 0);
    PushSubValues(L, result);
    return static_cast<int>(result.size());
}

int SubScript::SubThunk(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kSelfKey);
    auto* self = static_cast<SubScript*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    const char* name = lua_tostring(L, lua_upvalueindex(1));
    Call call{name, {}};
    int bad = 0;
    if (!ReadSubValues(L, 1, call.args, &bad)) {
        return luaL_error(L, "%s() argument %d: only nil, boolean, number and string can be passed to the main script",
                          name, bad);
    }
    {
        std::lock_guard lock(self->mutex_);
        self->subCalls_.push_back(std::move(call));
    }
    if (auto& wake = Ctx().wake) wake();
    return 0;
}

bool SubScript::Pump(ScriptHost& host) {
    if (delivered_) return false;
    lua_State* ML = host.State();
    bool finished = finished_.load();

    std::deque<Call> calls;
    std::optional<Call> func;
    {
        std::lock_guard lock(mutex_);
        calls.swap(subCalls_);
        if (funcCall_ && !funcResult_) func = std::move(funcCall_);
        funcCall_.reset();
    }
    for (auto& call : calls) {
        int extra = host.PushCallback("OnSubCall");
        if (extra < 0) continue;
        lua_pushlstring(ML, call.name.data(), call.name.size());
        PushSubValues(ML, call.args);
        host.Call(extra + 1 + static_cast<int>(call.args.size()), 0);
    }
    if (func) {
        SubValues result;
        int base = lua_gettop(ML);
        int extra = host.PushCallback("OnSubCall");
        if (extra >= 0) {
            lua_pushlstring(ML, func->name.data(), func->name.size());
            PushSubValues(ML, func->args);
            if (host.Call(extra + 1 + static_cast<int>(func->args.size()), LUA_MULTRET)) {
                int bad = 0;
                if (!ReadSubValues(ML, base + 1, result, &bad)) {
                    Log("OnSubCall() return %d: only nil, boolean, number and string can be returned to sub script",
                        bad - base);
                    result.clear();
                }
            }
            lua_settop(ML, base);
        }
        {
            std::lock_guard lock(mutex_);
            funcResult_ = std::move(result);
        }
        cv_.notify_all();
    }

    if (!finished) return true;
    if (thread_.joinable()) thread_.join();
    delivered_ = true;
    if (error_) {
        int extra = host.PushCallback("OnSubError");
        if (extra >= 0) {
            lua_pushlightuserdata(ML, LightId(id_));
            lua_pushlstring(ML, error_->data(), error_->size());
            host.Call(extra + 2, 0);
        }
    } else {
        int extra = host.PushCallback("OnSubFinished");
        if (extra >= 0) {
            lua_pushlightuserdata(ML, LightId(id_));
            PushSubValues(ML, results_);
            host.Call(extra + 1 + static_cast<int>(results_.size()), 0);
        }
    }
    return false;
}

void SubScript::Abort() {
    if (!thread_.joinable()) return;
    if (!finished_) {
        // A line hook raises an error at the next line; a pending function call is
        // released without results (ui_subscript.cpp:348).
        lua_sethook(L_, StopHook, LUA_MASKLINE | LUA_MASKCOUNT, 1);
        {
            std::lock_guard lock(mutex_);
            aborting_ = true;
        }
        cv_.notify_all();
    }
    thread_.join();
    delivered_ = true;
}

// --- Manager ----------------------------------------------------------------

std::variant<uint32_t, std::string> SubScriptManager::Launch(std::string script, const std::string& funcList,
                                                             const std::string& subList, SubValues args) {
    uint32_t id = nextId_++;
    auto ss = std::make_unique<SubScript>(id, std::move(script), SplitList(funcList), SplitList(subList),
                                          std::move(args));
    if (auto err = ss->Start()) return *err;
    scripts_.emplace(id, std::move(ss));
    return id;
}

bool SubScriptManager::IsRunning(uint32_t id) const {
    auto it = scripts_.find(id);
    return it != scripts_.end() && it->second->Running();
}

bool SubScriptManager::Abort(uint32_t id) {
    auto it = scripts_.find(id);
    if (it == scripts_.end()) return false;
    if (id == pumping_) {
        abortPumping_ = true;
        return true;
    }
    scripts_.erase(it);
    return true;
}

void SubScriptManager::Pump(ScriptHost& host) {
    // Callbacks may launch or abort subscripts, so iterate over a snapshot of ids.
    std::vector<uint32_t> ids;
    for (auto& [id, ss] : scripts_) ids.push_back(id);
    for (uint32_t id : ids) {
        auto it = scripts_.find(id);
        if (it == scripts_.end()) continue;
        pumping_ = id;
        bool alive = it->second->Pump(host);
        pumping_ = 0;
        if (!alive || abortPumping_) scripts_.erase(id);
        abortPumping_ = false;
    }
}

void SubScriptManager::Clear() {
    scripts_.clear();
}

// --- Lua API ----------------------------------------------------------------

namespace {

uint32_t IdArg(lua_State* L, const char* fn) {
    if (!lua_islightuserdata(L, 1)) {
        LuaFail(L, "%s() argument 1: expected subscript ID, got %s", fn, luaL_typename(L, 1));
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lua_touserdata(L, 1)));
}

int l_LaunchSubScript(lua_State* L) {
    int n = lua_gettop(L);
    LuaCheck(L, n >= 3, "Usage: LaunchSubScript(scriptText, funcList, subList[, ...])");
    for (int i = 1; i <= 3; ++i) CheckString(L, "LaunchSubScript", i);
    SubValues args;
    int bad = 0;
    if (!ReadSubValues(L, 4, args, &bad)) {
        LuaFail(L, "LaunchSubScript() argument %d: only nil, boolean, number and string types can be passed to sub script",
                bad);
    }
    size_t len = 0;
    const char* script = lua_tolstring(L, 1, &len);
    auto result = Ctx().subScripts->Launch(std::string(script, len), lua_tostring(L, 2), lua_tostring(L, 3),
                                           std::move(args));
    if (auto* err = std::get_if<std::string>(&result)) LuaFail(L, "%s", err->c_str());
    lua_pushlightuserdata(L, LightId(std::get<uint32_t>(result)));
    return 1;
}

int l_AbortSubScript(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: AbortSubScript(ssID)");
    uint32_t id = IdArg(L, "AbortSubScript");
    LuaCheck(L, Ctx().subScripts->IsRunning(id), "AbortSubScript(): subscript isn't running");
    Ctx().subScripts->Abort(id);
    return 0;
}

int l_IsSubScriptRunning(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: IsSubScriptRunning(ssID)");
    uint32_t id = IdArg(L, "IsSubScriptRunning");
    lua_pushboolean(L, Ctx().subScripts->IsRunning(id));
    return 1;
}

} // namespace

void RegisterSubScriptApi(lua_State* L) {
    SetGlobalFunction(L, "LaunchSubScript", l_LaunchSubScript);
    SetGlobalFunction(L, "AbortSubScript", l_AbortSubScript);
    SetGlobalFunction(L, "IsSubScriptRunning", l_IsSubScriptRunning);
}

} // namespace pob
