// package.path / package.preload setup and the lzip module (docs/API.md §6).

#include "app/Context.h"
#include "core/Install.h"
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

// --- lcurl.safe wrapper ----------------------------------------------------
//
// Wraps easy handles so that a download of PoB's own manifest.xml is normalised
// for this host before PoB's UpdateCheck.lua sees it (docs/API.md §8.4). All
// other requests pass through untouched.

constexpr const char* kCurlWrapper = R"lua(
local real, transform = ...
local function pack(...) return { n = select("#", ...), ... } end
local manifestPattern = "^https://raw%.githubusercontent%.com/PathOfBuildingCommunity/PathOfBuilding/[^/]+/manifest%.xml$"

local function wrapEasy(e)
    local url, writer, writerArgs
    local proxy = {}
    local function forward(name)
        local f = e[name]
        if type(f) ~= "function" then return f end
        return function(self, ...)
            local r = pack(f(e, ...))
            if r[1] == e then r[1] = self end -- keep method chaining on the proxy
            return unpack(r, 1, r.n)
        end
    end
    setmetatable(proxy, { __index = function(t, k) local v = forward(k) rawset(t, k, v) return v end })
    function proxy:setopt_url(u, ...) url = u return forward("setopt_url")(self, u, ...) end
    -- Arguments are forwarded exactly: lcurl treats (fn) and (fn, ctx) differently,
    -- and an explicit nil counts as a context.
    function proxy:setopt(opt, ...)
        if opt == real.OPT_URL then url = ... end
        if opt == real.OPT_WRITEFUNCTION then writer, writerArgs = ..., pack(...) end
        return forward("setopt")(self, opt, ...)
    end
    function proxy:setopt_writefunction(...)
        writer, writerArgs = ..., pack(...)
        return forward("setopt_writefunction")(self, ...)
    end
    function proxy:perform(...)
        if not (url and writer and url:match(manifestPattern)) then
            return forward("perform")(self, ...)
        end
        local chunks = {}
        e:setopt_writefunction(function(d) chunks[#chunks + 1] = d return true end)
        local ok, err = e:perform(...)
        if not ok then return ok, err end
        local text = transform(table.concat(chunks))
        if type(writer) == "function" then
            if writerArgs.n >= 2 then writer(writerArgs[2], text) else writer(text) end
        else
            writer:write(text)
        end
        return self
    end
    return proxy
end

local M = setmetatable({}, { __index = real })
function M.easy(...)
    local e, err = real.easy(...)
    if not e then return e, err end
    return wrapEasy(e)
end
return M
)lua";

int l_TransformManifest(lua_State* L) {
    size_t len = 0;
    const char* text = luaL_checklstring(L, 1, &len);
    std::string out = TransformManifest(std::string_view(text, len), {});
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int luaopen_lcurl_safe_host(lua_State* L) {
    if (luaL_loadstring(L, kCurlWrapper) != 0) return lua_error(L);
    lua_pushcfunction(L, luaopen_lcurl_safe);
    lua_call(L, 0, 1);
    lua_pushcfunction(L, l_TransformManifest);
    lua_call(L, 2, 1);
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
    Preload(L, "lcurl.safe", luaopen_lcurl_safe_host);
    Preload(L, "lua-utf8", luaopen_utf8);
    Preload(L, "lzip", luaopen_lzip);
    Preload(L, "socket.core", luaopen_socket_core);
    Preload(L, "sha1", luaopen_sha1_host);
}

} // namespace pob
