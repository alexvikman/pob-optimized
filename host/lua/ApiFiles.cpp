// Paths, directories and file search (docs/API.md §3.6).

#include "app/Context.h"
#include "lua/Api.h"
#include "lua/LuaUtil.h"

#include <chrono>
#include <cstring>
#include <string>

namespace pob {

namespace {

constexpr const char* kSearchMeta = "uisearchhandlemeta";

int PushPathResult(lua_State* L, const fs::path& p) {
    PushPath(L, p);
    PushPath(L, p); // SimpleGraphic's ANSI code page variant; UTF-8 here.
    return 2;
}

int l_GetScriptPath(lua_State* L) { return PushPathResult(L, Ctx().scriptPath); }
int l_GetRuntimePath(lua_State* L) { return PushPathResult(L, Ctx().runtimePath); }

int l_GetUserPath(lua_State* L) {
    auto& paths = Ctx().paths;
    if (!paths.user) {
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushstring(L, paths.userError.c_str());
        return 3;
    }
    return PushPathResult(L, *paths.user);
}

int l_GetWorkDir(lua_State* L) {
    PushPath(L, Ctx().scriptWorkDir);
    return 1;
}

int l_SetWorkDir(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: SetWorkDir(path)");
    fs::path dir = FromUtf8(ArgString(L, "SetWorkDir", 1));
    std::error_code ec;
    fs::current_path(dir, ec);
    if (!ec) Ctx().scriptWorkDir = fs::current_path(ec);
    return 0;
}

int PushFsError(lua_State* L, const std::error_code& ec) {
    lua_pushnil(L);
    lua_pushstring(L, ec ? ec.message().c_str() : "failed");
    return 2;
}

int l_MakeDir(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: MakeDir(path)");
    fs::path dir = FromUtf8(ArgString(L, "MakeDir", 1));
    std::error_code ec;
    // One level only; an existing directory reports failure like the original.
    if (!fs::create_directory(dir, ec)) {
        if (!ec) ec = std::make_error_code(std::errc::file_exists);
        return PushFsError(L, ec);
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_RemoveDir(lua_State* L) {
    int n = lua_gettop(L);
    LuaCheck(L, n >= 1, "Usage: RemoveDir(path, recurse)");
    fs::path dir = FromUtf8(ArgString(L, "RemoveDir", 1));
    bool recursive = false;
    if (n > 1) {
        if (!lua_isboolean(L, 2)) LuaFail(L, "RemoveDir() argument 2: expected boolean, got %s", luaL_typename(L, 2));
        recursive = lua_toboolean(L, 2);
    }
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        if (!ec) ec = std::make_error_code(std::errc::not_a_directory);
        return PushFsError(L, ec);
    }
    bool ok = recursive ? fs::remove_all(dir, ec) > 0 : fs::remove(dir, ec);
    if (!ok || ec) return PushFsError(L, ec);
    lua_pushboolean(L, 1);
    return 1;
}

// --- File search -----------------------------------------------------------

// Glob with * and ?, case-insensitive like the Windows original.
bool GlobMatch(std::u8string_view pat, std::u8string_view name) {
    size_t p = 0, n = 0, star = std::u8string_view::npos, mark = 0;
    auto lower = [](char8_t c) { return (c >= 'A' && c <= 'Z') ? char8_t(c - 'A' + 'a') : c; };
    while (n < name.size()) {
        if (p < pat.size() && (pat[p] == '?' || lower(pat[p]) == lower(name[n]))) {
            ++p;
            ++n;
        } else if (p < pat.size() && pat[p] == '*') {
            star = p++;
            mark = n;
        } else if (star != std::u8string_view::npos) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

struct SearchHandle {
    fs::directory_iterator it;
    std::u8string pattern;
    bool dirsOnly = false;
    bool valid = false;
    std::u8string fileName;
    uintmax_t fileSize = 0;
    double modified = 0;

    // Advances to the next match starting at `it` (inclusive).
    bool Seek() {
        std::error_code ec;
        for (; it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            auto name = it->path().filename().u8string();
            if (name == u8"." || name == u8"..") continue;
            bool isDir = it->is_directory(ec);
            if (isDir != dirsOnly || !GlobMatch(pattern, name)) continue;
            fileName = name;
            fileSize = isDir ? 0 : it->file_size(ec);
            auto ftime = it->last_write_time(ec);
            // Seconds since the Unix epoch (docs/API.md §3.6).
            // (clock_cast is not available everywhere; shift via the current time.)
            auto sys = std::chrono::system_clock::now() + (ftime - fs::file_time_type::clock::now());
            modified = std::chrono::duration<double>(sys.time_since_epoch()).count();
            return valid = true;
        }
        return valid = false;
    }
};

SearchHandle* GetSearch(lua_State* L, const char* method, bool needValid) {
    if (!IsUserData(L, 1, kSearchMeta)) LuaFail(L, "searchHandle:%s() must be used on a search handle", method);
    auto* h = static_cast<SearchHandle*>(lua_touserdata(L, 1));
    if (needValid && !h->valid) {
        LuaFail(L, "searchHandle:%s(): search handle is no longer valid (ran out of files to find)", method);
    }
    return h;
}

int l_NewFileSearch(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: NewFileSearch(spec[, findDirectories])");
    fs::path spec = FromUtf8(ArgString(L, "NewFileSearch", 1));
    bool dirsOnly = lua_toboolean(L, 2) != 0;

    fs::path parent = spec.parent_path();
    if (parent.empty()) parent = ".";
    std::error_code ec;
    fs::directory_iterator it(parent, ec);
    if (ec) return 0;

    auto* h = static_cast<SearchHandle*>(lua_newuserdata(L, sizeof(SearchHandle)));
    new (h) SearchHandle();
    h->it = std::move(it);
    h->pattern = spec.filename().u8string();
    h->dirsOnly = dirsOnly;
    lua_getfield(L, LUA_REGISTRYINDEX, kSearchMeta);
    lua_setmetatable(L, -2);
    if (!h->Seek()) {
        lua_pop(L, 1);
        return 0;
    }
    return 1;
}

int l_SearchGC(lua_State* L) {
    GetSearch(L, "__gc", false)->~SearchHandle();
    return 0;
}

int l_SearchNextFile(lua_State* L) {
    auto* h = GetSearch(L, "NextFile", true);
    std::error_code ec;
    h->it.increment(ec);
    if (ec || !h->Seek()) return 0;
    lua_pushboolean(L, 1);
    return 1;
}

int l_SearchGetFileName(lua_State* L) {
    auto* h = GetSearch(L, "GetFileName", true);
    lua_pushlstring(L, reinterpret_cast<const char*>(h->fileName.data()), h->fileName.size());
    return 1;
}

int l_SearchGetFileSize(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(GetSearch(L, "GetFileSize", true)->fileSize));
    return 1;
}

int l_SearchGetFileModifiedTime(lua_State* L) {
    lua_pushnumber(L, GetSearch(L, "GetFileModifiedTime", true)->modified);
    return 1;
}

} // namespace

void RegisterFileApi(lua_State* L) {
    SetGlobalFunction(L, "GetScriptPath", l_GetScriptPath);
    SetGlobalFunction(L, "GetRuntimePath", l_GetRuntimePath);
    SetGlobalFunction(L, "GetUserPath", l_GetUserPath);
    SetGlobalFunction(L, "GetWorkDir", l_GetWorkDir);
    SetGlobalFunction(L, "SetWorkDir", l_SetWorkDir);
    SetGlobalFunction(L, "MakeDir", l_MakeDir);
    SetGlobalFunction(L, "RemoveDir", l_RemoveDir);

    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    const luaL_Reg methods[] = {{"__gc", l_SearchGC},
                                {"NextFile", l_SearchNextFile},
                                {"GetFileName", l_SearchGetFileName},
                                {"GetFileSize", l_SearchGetFileSize},
                                {"GetFileModifiedTime", l_SearchGetFileModifiedTime},
                                {nullptr, nullptr}};
    luaL_setfuncs(L, methods, 0);
    lua_setfield(L, LUA_REGISTRYINDEX, kSearchMeta);
    SetGlobalFunction(L, "NewFileSearch", l_NewFileSearch);
}

} // namespace pob
