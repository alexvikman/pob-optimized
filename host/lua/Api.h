#pragma once

struct lua_State;

namespace pob {

// Each registers one group of host globals (docs/API.md §3) into PoB's main state.
void RegisterCoreApi(lua_State* L);
void RegisterFileApi(lua_State* L);
void RegisterRenderApi(lua_State* L);
void RegisterSubScriptApi(lua_State* L);

// package.path and package.preload for native modules; used by every state,
// including subscripts (docs/API.md §5–§6).
void SetupPackages(lua_State* L);

// require("sha1"): PoB's module with native hash functions (lua/Sha1.cpp).
int luaopen_sha1_host(lua_State* L);

// Logs a missing host function once instead of failing (docs/API.md §9).
void InstallSafetyNet(lua_State* L);

} // namespace pob
