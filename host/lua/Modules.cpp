// package.path / package.preload setup and the lzip module (docs/API.md §6).

#include "app/Context.h"
#include "lua/Api.h"
#include "lua/LuaUtil.h"

#include <unzip.h>

#include <algorithm>
#include <string>
#include <vector>

extern "C" {
int luaopen_lcurl(lua_State* L);
int luaopen_lcurl_safe(lua_State* L);
int luaopen_utf8(lua_State* L);
int luaopen_socket_core(lua_State* L);
}

namespace pob {

namespace {

// --- lzip: read-only zip archives, API of SimpleGraphic's libs/LZip --------

constexpr const char* kZipMeta = "pob.lzip.zip";
constexpr const char* kZipFileMeta = "pob.lzip.file";

struct LZip {
    unzFile uf = nullptr;
    std::vector<std::string> names;
    std::vector<uLong> sizes;
};

struct LZipFile {
    std::string data;
    size_t pos = 0;
    bool open = false;
};

LZip* CheckZip(lua_State* L, bool open) {
    auto* z = static_cast<LZip*>(luaL_checkudata(L, 1, kZipMeta));
    if (open && !z->uf) luaL_error(L, "zip file is closed");
    return z;
}

LZipFile* CheckZipFile(lua_State* L, bool open) {
    auto* f = static_cast<LZipFile*>(luaL_checkudata(L, 1, kZipFileMeta));
    if (open && !f->open) luaL_error(L, "zipped file is closed");
    return f;
}

int l_zip_open(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    unzFile uf = unzOpen(path);
    if (!uf) return 0;
    auto* z = static_cast<LZip*>(lua_newuserdata(L, sizeof(LZip)));
    new (z) LZip();
    z->uf = uf;
    if (unzGoToFirstFile(uf) == UNZ_OK) {
        do {
            char name[1024];
            unz_file_info info;
            if (unzGetCurrentFileInfo(uf, &info, name, sizeof(name), nullptr, 0, nullptr, 0) == UNZ_OK) {
                z->names.emplace_back(name);
                z->sizes.push_back(info.uncompressed_size);
            }
        } while (unzGoToNextFile(uf) == UNZ_OK);
    }
    luaL_getmetatable(L, kZipMeta);
    lua_setmetatable(L, -2);
    return 1;
}

int l_zip_close(lua_State* L) {
    auto* z = CheckZip(L, false);
    if (z->uf) unzClose(z->uf);
    z->uf = nullptr;
    return 0;
}

int l_zip_gc(lua_State* L) {
    l_zip_close(L);
    static_cast<LZip*>(lua_touserdata(L, 1))->~LZip();
    return 0;
}

int l_zip_numfiles(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(CheckZip(L, true)->names.size()));
    return 1;
}

int l_zip_filename(lua_State* L) {
    auto* z = CheckZip(L, true);
    lua_Integer i = luaL_checkinteger(L, 2);
    if (i < 1 || i > static_cast<lua_Integer>(z->names.size())) return 0;
    lua_pushstring(L, z->names[i - 1].c_str());
    return 1;
}

int l_zip_filesize(lua_State* L) {
    auto* z = CheckZip(L, true);
    lua_Integer i = luaL_checkinteger(L, 2);
    if (i < 1 || i > static_cast<lua_Integer>(z->sizes.size())) return 0;
    lua_pushnumber(L, static_cast<lua_Number>(z->sizes[i - 1]));
    return 1;
}

int l_zip_openfile(lua_State* L) {
    auto* z = CheckZip(L, true);
    const char* name = luaL_checkstring(L, 2);
    // 2 = case-insensitive, matching the Windows original.
    if (unzLocateFile(z->uf, name, 2) != UNZ_OK || unzOpenCurrentFile(z->uf) != UNZ_OK) return 0;
    unz_file_info info;
    unzGetCurrentFileInfo(z->uf, &info, nullptr, 0, nullptr, 0, nullptr, 0);
    std::string data(info.uncompressed_size, '\0');
    int got = data.empty() ? 0 : unzReadCurrentFile(z->uf, data.data(), static_cast<unsigned>(data.size()));
    unzCloseCurrentFile(z->uf);
    if (got < 0 || static_cast<size_t>(got) != data.size()) return 0;

    auto* f = static_cast<LZipFile*>(lua_newuserdata(L, sizeof(LZipFile)));
    new (f) LZipFile();
    f->data = std::move(data);
    f->open = true;
    luaL_getmetatable(L, kZipFileMeta);
    lua_setmetatable(L, -2);
    return 1;
}

int l_file_close(lua_State* L) {
    auto* f = CheckZipFile(L, false);
    f->open = false;
    f->data.clear();
    return 0;
}

int l_file_gc(lua_State* L) {
    static_cast<LZipFile*>(lua_touserdata(L, 1))->~LZipFile();
    return 0;
}

int l_file_read(lua_State* L) {
    auto* f = CheckZipFile(L, true);
    size_t left = f->data.size() - f->pos;
    size_t count = left;
    if (lua_type(L, 2) == LUA_TNUMBER) count = std::min(left, static_cast<size_t>(lua_tointeger(L, 2)));
    if (count == 0 && left == 0) return 0;
    lua_pushlstring(L, f->data.data() + f->pos, count);
    f->pos += count;
    return 1;
}

int l_file_length(lua_State* L) {
    lua_pushnumber(L, static_cast<lua_Number>(CheckZipFile(L, true)->data.size()));
    return 1;
}

int luaopen_lzip(lua_State* L) {
    const luaL_Reg zipMethods[] = {{"__gc", l_zip_gc},
                                   {"Close", l_zip_close},
                                   {"GetNumFiles", l_zip_numfiles},
                                   {"GetFileName", l_zip_filename},
                                   {"GetFileSize", l_zip_filesize},
                                   {"OpenFile", l_zip_openfile},
                                   {nullptr, nullptr}};
    const luaL_Reg fileMethods[] = {{"__gc", l_file_gc},
                                    {"Close", l_file_close},
                                    {"Read", l_file_read},
                                    {"Length", l_file_length},
                                    {nullptr, nullptr}};
    luaL_newmetatable(L, kZipMeta);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, zipMethods, 0);
    lua_pop(L, 1);
    luaL_newmetatable(L, kZipFileMeta);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, fileMethods, 0);
    lua_pop(L, 1);

    lua_newtable(L);
    lua_pushcfunction(L, l_zip_open);
    lua_setfield(L, -2, "open");
    return 1;
}

void Preload(lua_State* L, const char* name, lua_CFunction fn) {
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, fn);
    lua_setfield(L, -2, name);
    lua_pop(L, 2);
}

} // namespace

void SetupPackages(lua_State* L) {
    // Pure-Lua libraries live in <runtime>/lua; absolute so that subscript threads
    // do not depend on the process working directory.
    std::string lib = ToLuaPath(Ctx().runtimePath / "lua");
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    std::string path = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    path = "./?.lua;" + lib + "/?.lua;" + lib + "/?/init.lua;" + path;
    lua_pushstring(L, path.c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);

    Preload(L, "lcurl", luaopen_lcurl);
    Preload(L, "lcurl.safe", luaopen_lcurl_safe);
    Preload(L, "lua-utf8", luaopen_utf8);
    Preload(L, "lzip", luaopen_lzip);
    Preload(L, "socket.core", luaopen_socket_core);
}

} // namespace pob
