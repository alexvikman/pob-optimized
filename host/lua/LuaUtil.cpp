#include "lua/LuaUtil.h"

#include "core/Paths.h"

#include <cstdarg>
#include <fstream>
#include <sstream>

namespace pob {

void LuaFail(lua_State* L, const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    lua_pushvfstring(L, fmt, va);
    va_end(va);
    lua_error(L);
    // lua_error does not return.
    std::abort();
}

void CheckString(lua_State* L, const char* fn, int idx) {
    if (!lua_isstring(L, idx)) {
        LuaFail(L, "%s() argument %d: expected string, got %s", fn, idx, luaL_typename(L, idx));
    }
}

void CheckNumber(lua_State* L, const char* fn, int idx) {
    if (!lua_isnumber(L, idx)) {
        LuaFail(L, "%s() argument %d: expected number, got %s", fn, idx, luaL_typename(L, idx));
    }
}

double ArgNumber(lua_State* L, const char* fn, int idx) {
    CheckNumber(L, fn, idx);
    return lua_tonumber(L, idx);
}

std::string_view ArgString(lua_State* L, const char* fn, int idx) {
    CheckString(L, fn, idx);
    size_t len = 0;
    const char* s = lua_tolstring(L, idx, &len);
    return {s, len};
}

void PushPath(lua_State* L, const std::filesystem::path& p) {
    std::string s = ToLuaPath(p);
    lua_pushlstring(L, s.data(), s.size());
}

int LoadLuaFile(lua_State* L, const std::filesystem::path& file, const std::string& chunkName) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        lua_pushfstring(L, "cannot open %s", chunkName.c_str());
        return LUA_ERRFILE;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    size_t skip = (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) ? 3 : 0;
    if (text.size() > skip && text[skip] == '#') {
        size_t eol = text.find('\n', skip);
        text.replace(skip, (eol == std::string::npos ? text.size() : eol) - skip, "");
    }
    std::string name = "@" + chunkName;
    return luaL_loadbuffer(L, text.data(), text.size(), name.c_str());
}

bool IsUserData(lua_State* L, int idx, const char* metaName) {
    if (lua_type(L, idx) != LUA_TUSERDATA || !lua_getmetatable(L, idx)) return false;
    lua_getfield(L, LUA_REGISTRYINDEX, metaName);
    bool eq = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return eq;
}

} // namespace pob
