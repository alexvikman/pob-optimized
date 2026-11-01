#pragma once

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <filesystem>
#include <string>
#include <string_view>

namespace pob {

// Raises a Lua error without location prefix, like SimpleGraphic's LAssert.
#if defined(__GNUC__) || defined(__clang__)
[[noreturn]] __attribute__((format(printf, 2, 3)))
#else
[[noreturn]]
#endif
void LuaFail(lua_State* L, const char* fmt, ...);

inline void LuaCheck(lua_State* L, bool cond, const char* message) {
    if (!cond) LuaFail(L, "%s", message);
}

// "Fn() argument N: expected T, got U" checks.
void CheckString(lua_State* L, const char* fn, int idx);
void CheckNumber(lua_State* L, const char* fn, int idx);
double ArgNumber(lua_State* L, const char* fn, int idx);
std::string_view ArgString(lua_State* L, const char* fn, int idx);

void PushPath(lua_State* L, const std::filesystem::path& p);

// Registers `fn` as a global.
inline void SetGlobalFunction(lua_State* L, const char* name, lua_CFunction fn) {
    lua_pushcfunction(L, fn);
    lua_setglobal(L, name);
}

// Blanks a leading '#' line (keeping the newline) that old LuaJIT skipped but
// current LuaJIT parses as a line-number directive; see LoadLuaFile.
void BlankLeadingHashLine(std::string& text);

// luaL_loadfile replacement. PoB files may start with a "#@" line that older
// LuaJIT skipped but current LuaJIT parses as a line-number directive; the line
// is blanked so line numbers in errors stay the same.
int LoadLuaFile(lua_State* L, const std::filesystem::path& file, const std::string& chunkName);

// Is the value at `idx` a userdata with the metatable stored in the registry as `metaName`?
bool IsUserData(lua_State* L, int idx, const char* metaName);

} // namespace pob
