// Stubs for host functions a future PoB may call before we implement them
// (docs/API.md §9).

#include "core/Log.h"
#include "lua/Api.h"
#include "lua/LuaUtil.h"

namespace pob {

namespace {

int l_HostLog(lua_State* L) {
    LogLine(luaL_checkstring(L, 1));
    return 0;
}

// Names that PoB checks for existence and that must stay nil when absent.
constexpr const char* kSafetyNet = R"lua(
local hostLog = ...
local allowNil = {
    UpdateProgress = true, -- only exists inside the update subscript
}
local rawset, type, debug, string = rawset, type, debug, string
setmetatable(_G, {
    __index = function(t, k)
        -- CamelCase names are host API functions; everything else (PoB's own
        -- accidental globals, CONSTANTS) stays nil like in the original.
        if type(k) ~= "string" or allowNil[k] or not k:match("^[A-Z][a-z][A-Za-z0-9]*$") then
            return nil
        end
        local info = debug.getinfo(2, "Sl")
        local where = info and string.format("%s:%d", info.short_src, info.currentline or 0) or "?"
        hostLog(string.format("[host] unimplemented API function '%s' (first called from %s)", k, where))
        local stub = function() end
        rawset(t, k, stub)
        return stub
    end,
})
)lua";

} // namespace

void InstallSafetyNet(lua_State* L) {
    if (luaL_loadstring(L, kSafetyNet) != 0) {
        Log("Safety net failed to load: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    lua_pushcfunction(L, l_HostLog);
    if (lua_pcall(L, 1, 0, 0) != 0) {
        Log("Safety net failed: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

} // namespace pob
