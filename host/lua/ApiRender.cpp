// Rendering API: screen, draw state, images and text (docs/API.md §3.1–§3.4, §4.3).

#include "app/Context.h"
#include "core/Log.h"
#include "lua/Api.h"
#include "lua/LuaUtil.h"

#include <cmath>
#include <memory>

namespace pob {

namespace {

constexpr const char* kImageMeta = "uiimghandlemeta";

const char* const kAlignNames[] = {"LEFT", "CENTER", "RIGHT", "CENTER_X", "RIGHT_X", nullptr};
const char* const kFontNames[] = {"FIXED", "VAR", "VAR BOLD", "FONTIN SC", "FONTIN SC ITALIC", "FONTIN", "FONTIN ITALIC", nullptr};

struct ImageHandle {
    std::shared_ptr<Texture> tex;
};

void RequireRenderer(lua_State* L) {
    if (!Ctx().renderInit) LuaFail(L, "Renderer is not initialised");
}

void RequireFrame(lua_State* L, const char* fn) {
    RequireRenderer(L);
    if (!Ctx().inFrame) LuaFail(L, "%s() called outside of OnFrame", fn);
}

// Logical text height → physical, rounded up to even (ui_api.cpp:1098).
int ScaledTextHeight(double logical) {
    int h = static_cast<int>(std::lround(logical * Ctx().Scale()));
    return h <= 1 ? 1 : (h + 1) & ~1;
}

// --- Screen ----------------------------------------------------------------

int l_RenderInit(lua_State* L) {
    auto& ctx = Ctx();
    int n = lua_gettop(L);
    bool dpiAware = false;
    for (int i = 1; i <= n; ++i) {
        CheckString(L, "RenderInit", i);
        if (std::string_view(lua_tostring(L, i)) == "DPI_AWARE") dpiAware = true;
    }
    if (ctx.renderInit) return 0;
    ctx.renderInit = true;
    ctx.dpiAware = dpiAware;
    if (!ctx.fontsLoaded) {
        ctx.fontsLoaded = ctx.fonts.Load(ctx.runtimePath, ctx.textures);
        if (!ctx.fontsLoaded) Log("Some fonts failed to load from %s", ToLuaPath(ctx.runtimePath).c_str());
    }
    return 0;
}

int l_GetScreenSize(lua_State* L) {
    lua_pushinteger(L, Ctx().ScreenWidth());
    lua_pushinteger(L, Ctx().ScreenHeight());
    return 2;
}

int l_GetScreenScale(lua_State* L) {
    lua_pushnumber(L, Ctx().Scale());
    return 1;
}

int l_SetDPIScaleOverridePercent(lua_State* L) {
    RequireRenderer(L);
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: SetDPIScaleOverridePercent(percent)");
    Ctx().dpiOverridePercent = static_cast<int>(ArgNumber(L, "SetDPIScaleOverridePercent", 1));
    return 0;
}

int l_GetDPIScaleOverridePercent(lua_State* L) {
    RequireRenderer(L);
    lua_pushinteger(L, Ctx().dpiOverridePercent);
    return 1;
}

int l_SetClearColor(lua_State* L) {
    RequireRenderer(L);
    int n = lua_gettop(L);
    LuaCheck(L, n >= 3, "Usage: SetClearColor(red, green, blue[, alpha])");
    auto& c = Ctx().clearColor;
    for (int i = 1; i <= 3; ++i) c[i - 1] = static_cast<float>(ArgNumber(L, "SetClearColor", i));
    c[3] = (n >= 4 && !lua_isnil(L, 4)) ? static_cast<float>(ArgNumber(L, "SetClearColor", 4)) : 1.0f;
    return 0;
}

int l_GetAsyncCount(lua_State* L) {
    RequireRenderer(L);
    lua_pushinteger(L, Ctx().textures.PendingCount());
    return 1;
}

// --- Draw state ------------------------------------------------------------

int l_SetDrawLayer(lua_State* L) {
    RequireFrame(L, "SetDrawLayer");
    int n = lua_gettop(L);
    LuaCheck(L, n >= 1, "Usage: SetDrawLayer({layer|nil}[, subLayer])");
    if (!lua_isnumber(L, 1) && !lua_isnil(L, 1)) {
        LuaFail(L, "SetDrawLayer() argument 1: expected number or nil, got %s", luaL_typename(L, 1));
    }
    if (n >= 2) CheckNumber(L, "SetDrawLayer", 2);
    auto& dl = Ctx().draw;
    if (lua_isnil(L, 1)) {
        LuaCheck(L, n >= 2, "SetDrawLayer(): must provide subLayer if layer is nil");
        dl.SetSubLayer(static_cast<int>(lua_tointeger(L, 2)));
    } else {
        dl.SetLayer(static_cast<int>(lua_tointeger(L, 1)), n >= 2 ? static_cast<int>(lua_tointeger(L, 2)) : 0);
    }
    return 0;
}

int l_GetDrawLayer(lua_State* L) {
    lua_pushinteger(L, Ctx().draw.SubLayer());
    return 1;
}

int l_SetViewport(lua_State* L) {
    RequireFrame(L, "SetViewport");
    auto& ctx = Ctx();
    int n = lua_gettop(L);
    if (n == 0) {
        ctx.draw.ResetViewport();
        return 0;
    }
    LuaCheck(L, n >= 4, "Usage: SetViewport([x, y, width, height])");
    double v[4];
    for (int i = 0; i < 4; ++i) v[i] = ArgNumber(L, "SetViewport", i + 1);
    float s = ctx.Scale();
    int x = static_cast<int>(std::lround(v[0] * s));
    int y = static_cast<int>(std::lround(v[1] * s));
    int w = static_cast<int>(std::ceil(v[2] * s));
    int h = static_cast<int>(std::ceil(v[3] * s));
    if (h == 0) {
        // A zero height means "whole screen" in SimpleGraphic (r_main.cpp:1493).
        ctx.draw.SetViewport(x, y, ctx.ScreenWidth(), ctx.ScreenHeight());
    } else {
        ctx.draw.SetViewport(x, y, w, h);
    }
    return 0;
}

int l_SetDrawColor(lua_State* L) {
    RequireFrame(L, "SetDrawColor");
    int n = lua_gettop(L);
    LuaCheck(L, n >= 1, "Usage: SetDrawColor(red, green, blue[, alpha]) or SetDrawColor(escapeStr)");
    Color c{1, 1, 1, 1};
    if (lua_type(L, 1) == LUA_TSTRING) {
        size_t len = 0;
        const char* s = lua_tolstring(L, 1, &len);
        int esc = ColorEscapeLength(std::string_view(s, len));
        LuaCheck(L, esc != 0, "SetDrawColor() argument 1: invalid color escape sequence");
        ReadColorEscape(std::string_view(s, len), esc, c);
        c[3] = 1.0f;
    } else {
        LuaCheck(L, n >= 3, "Usage: SetDrawColor(red, green, blue[, alpha]) or SetDrawColor(escapeStr)");
        for (int i = 1; i <= 3; ++i) c[i - 1] = static_cast<float>(ArgNumber(L, "SetDrawColor", i));
        if (n >= 4 && !lua_isnil(L, 4)) {
            if (!lua_isnumber(L, 4)) {
                LuaFail(L, "SetDrawColor() argument 4: expected number or nil, got %s", luaL_typename(L, 4));
            }
            c[3] = static_cast<float>(lua_tonumber(L, 4));
        }
    }
    Ctx().draw.SetColor(c);
    Ctx().lastColor = c;
    return 0;
}

int l_GetDrawColor(lua_State* L) {
    RequireRenderer(L);
    for (float v : Ctx().lastColor) lua_pushnumber(L, v);
    return 4;
}

// --- Images ----------------------------------------------------------------

Texture* ImageArg(lua_State* L, const char* fn) {
    if (lua_isnil(L, 1)) return Ctx().textures.White();
    if (!IsUserData(L, 1, kImageMeta)) {
        LuaFail(L, "%s() argument 1: expected image handle or nil, got %s", fn, luaL_typename(L, 1));
    }
    auto* h = static_cast<ImageHandle*>(lua_touserdata(L, 1));
    if (!h->tex) LuaFail(L, "%s(): image handle has no image loaded", fn);
    return h->tex.get();
}

int StackArg(lua_State* L, const char* fn, int idx) {
    if (!lua_isnumber(L, idx)) LuaFail(L, "%s() argument %d: expected integer, got %s", fn, idx, luaL_typename(L, idx));
    int v = static_cast<int>(lua_tointeger(L, idx));
    if (v <= 0) LuaFail(L, "%s() argument %d: expected positive integer, got %d", fn, idx, v);
    return v - 1;
}

int l_DrawImage(lua_State* L) {
    RequireFrame(L, "DrawImage");
    int n = lua_gettop(L);
    const char* usage =
        "Usage: DrawImage({imgHandle|nil}, left, top, width, height[, tcLeft, tcTop, tcRight, tcBottom][, stackIdx])";
    if (n != 5 && n != 6 && n != 9 && n != 10) LuaFail(L, "%s", usage);
    Texture* tex = ImageArg(L, "DrawImage");

    float s = Ctx().Scale();
    float r[4];
    for (int i = 0; i < 4; ++i) r[i] = static_cast<float>(ArgNumber(L, "DrawImage", 2 + i)) * s;
    float tc[4] = {0, 0, 1, 1};
    if (n >= 9) {
        for (int i = 0; i < 4; ++i) tc[i] = static_cast<float>(ArgNumber(L, "DrawImage", 6 + i));
    }
    int stack = (n == 6 || n == 10) ? StackArg(L, "DrawImage", n) : 0;

    const float x0 = r[0], y0 = r[1], x1 = r[0] + r[2], y1 = r[1] + r[3];
    const float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
    const float uv[8] = {tc[0], tc[1], tc[2], tc[1], tc[2], tc[3], tc[0], tc[3]};
    Ctx().draw.AddQuad(tex, xy, uv, stack);
    return 0;
}

int l_DrawImageQuad(lua_State* L) {
    RequireFrame(L, "DrawImageQuad");
    int n = lua_gettop(L);
    const char* usage = "Usage: DrawImageQuad({imgHandle|nil}, x1, y1, x2, y2, x3, y3, x4, y4[, s1, t1, s2, t2, s3, "
                        "t3, s4, t4][, stackIdx])";
    if (n != 9 && n != 10 && n != 17 && n != 18) LuaFail(L, "%s", usage);
    Texture* tex = ImageArg(L, "DrawImageQuad");

    float s = Ctx().Scale();
    float xy[8];
    for (int i = 0; i < 8; ++i) xy[i] = static_cast<float>(ArgNumber(L, "DrawImageQuad", 2 + i)) * s;
    float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    if (n >= 17) {
        for (int i = 0; i < 8; ++i) uv[i] = static_cast<float>(ArgNumber(L, "DrawImageQuad", 10 + i));
    }
    int stack = (n == 10 || n == 18) ? StackArg(L, "DrawImageQuad", n) : 0;
    Ctx().draw.AddQuad(tex, xy, uv, stack);
    return 0;
}

// --- Text ------------------------------------------------------------------

int l_DrawString(lua_State* L) {
    RequireFrame(L, "DrawString");
    LuaCheck(L, lua_gettop(L) >= 6, "Usage: DrawString(left, top, align, height, font, text)");
    CheckNumber(L, "DrawString", 1);
    CheckNumber(L, "DrawString", 2);
    if (!lua_isstring(L, 3) && !lua_isnil(L, 3)) {
        LuaFail(L, "DrawString() argument 3: expected string or nil, got %s", luaL_typename(L, 3));
    }
    CheckNumber(L, "DrawString", 4);
    CheckString(L, "DrawString", 5);
    CheckString(L, "DrawString", 6);

    auto& ctx = Ctx();
    float s = ctx.Scale();
    float left = static_cast<float>(lua_tonumber(L, 1) * s);
    float top = static_cast<float>(lua_tonumber(L, 2) * s);
    auto align = static_cast<TextAlign>(luaL_checkoption(L, 3, "LEFT", kAlignNames));
    int height = ScaledTextHeight(lua_tonumber(L, 4));
    int font = luaL_checkoption(L, 5, "FIXED", kFontNames);
    size_t len = 0;
    const char* text = lua_tolstring(L, 6, &len);

    auto indexed = IndexUtf8(std::string_view(text, len));
    ctx.fonts.Get(static_cast<size_t>(font)).Draw(ctx.draw, left, top, align, height, ctx.draw.MutableColor(),
                                                  indexed.text);
    ctx.lastColor = ctx.draw.GetColor();
    return 0;
}

int l_DrawStringWidth(lua_State* L) {
    RequireRenderer(L);
    LuaCheck(L, lua_gettop(L) >= 3, "Usage: DrawStringWidth(height, font, text)");
    CheckNumber(L, "DrawStringWidth", 1);
    CheckString(L, "DrawStringWidth", 2);
    CheckString(L, "DrawStringWidth", 3);
    auto& ctx = Ctx();
    int height = ScaledTextHeight(lua_tonumber(L, 1));
    int font = luaL_checkoption(L, 2, "FIXED", kFontNames);
    size_t len = 0;
    const char* text = lua_tolstring(L, 3, &len);
    int width = 0;
    if (len) width = ctx.fonts.Get(static_cast<size_t>(font)).StringWidth(height, IndexUtf8({text, len}).text);
    lua_pushnumber(L, width / ctx.Scale());
    return 1;
}

int l_DrawStringCursorIndex(lua_State* L) {
    RequireRenderer(L);
    LuaCheck(L, lua_gettop(L) >= 5, "Usage: DrawStringCursorIndex(height, font, text, cursorX, cursorY)");
    CheckNumber(L, "DrawStringCursorIndex", 1);
    CheckString(L, "DrawStringCursorIndex", 2);
    CheckString(L, "DrawStringCursorIndex", 3);
    CheckNumber(L, "DrawStringCursorIndex", 4);
    CheckNumber(L, "DrawStringCursorIndex", 5);
    auto& ctx = Ctx();
    float s = ctx.Scale();
    int height = ScaledTextHeight(lua_tonumber(L, 1));
    int font = luaL_checkoption(L, 2, "FIXED", kFontNames);
    size_t len = 0;
    const char* text = lua_tolstring(L, 3, &len);
    int curX = static_cast<int>(std::lround(lua_tonumber(L, 4) * s));
    int curY = static_cast<int>(std::lround(lua_tonumber(L, 5) * s));

    size_t byteIndex = 0;
    if (len) {
        auto indexed = IndexUtf8({text, len});
        size_t cp = ctx.fonts.Get(static_cast<size_t>(font)).CursorIndex(height, indexed.text, curX, curY);
        byteIndex = cp < indexed.offsets.size() ? indexed.offsets[cp] : len;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(byteIndex + 1));
    return 1;
}

int l_StripEscapes(lua_State* L) {
    LuaCheck(L, lua_gettop(L) >= 1, "Usage: StripEscapes(string)");
    std::string out = StripColorEscapes(ArgString(L, "StripEscapes", 1));
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// --- Image handles ---------------------------------------------------------

ImageHandle* GetImage(lua_State* L, const char* method, bool loaded) {
    if (!IsUserData(L, 1, kImageMeta)) LuaFail(L, "imgHandle:%s() must be used on an image handle", method);
    auto* h = static_cast<ImageHandle*>(lua_touserdata(L, 1));
    if (loaded && !h->tex) LuaFail(L, "imgHandle:%s(): image handle has no image loaded", method);
    return h;
}

int l_NewImageHandle(lua_State* L) {
    auto* h = static_cast<ImageHandle*>(lua_newuserdata(L, sizeof(ImageHandle)));
    new (h) ImageHandle();
    lua_getfield(L, LUA_REGISTRYINDEX, kImageMeta);
    lua_setmetatable(L, -2);
    return 1;
}

int l_ImgGC(lua_State* L) {
    GetImage(L, "__gc", false)->~ImageHandle();
    return 0;
}

int l_ImgLoad(lua_State* L) {
    RequireRenderer(L);
    auto* h = GetImage(L, "Load", false);
    int n = lua_gettop(L);
    LuaCheck(L, n >= 2, "Usage: imgHandle:Load(fileName[, flag1[, flag2...]])");
    if (!lua_isstring(L, 2)) LuaFail(L, "imgHandle:Load() argument 1: expected string, got %s", luaL_typename(L, 2));
    auto& ctx = Ctx();
    fs::path file = FromUtf8(lua_tostring(L, 2));
    if (file.is_relative()) file = ctx.scriptWorkDir / file;

    int flags = 0;
    for (int i = 3; i <= n; ++i) {
        if (lua_type(L, i) != LUA_TSTRING) continue;
        std::string_view flag = lua_tostring(L, i);
        if (flag == "ASYNC") flags |= kTexAsync;
        else if (flag == "CLAMP") flags |= kTexClamp;
        else if (flag == "MIPMAP") flags |= kTexMipmap;
        else if (flag == "NEAREST") flags |= kTexNearest;
        else LuaFail(L, "imgHandle:Load(): unrecognised flag '%s'", std::string(flag).c_str());
    }
    h->tex = ctx.textures.Acquire(file, flags);
    return 0;
}

int l_ImgUnload(lua_State* L) {
    GetImage(L, "Unload", false)->tex.reset();
    return 0;
}

int l_ImgIsValid(lua_State* L) {
    lua_pushboolean(L, GetImage(L, "IsValid", false)->tex != nullptr);
    return 1;
}

int l_ImgIsLoading(lua_State* L) {
    auto* h = GetImage(L, "IsLoading", true);
    lua_pushboolean(L, h->tex->state == Texture::State::Pending);
    return 1;
}

int l_ImgSetLoadingPriority(lua_State* L) {
    auto* h = GetImage(L, "SetLoadingPriority", true);
    LuaCheck(L, lua_gettop(L) >= 2, "Usage: imgHandle:SetLoadingPriority(pri)");
    h->tex->priority = static_cast<int>(ArgNumber(L, "imgHandle:SetLoadingPriority", 2));
    return 0;
}

int l_ImgImageSize(lua_State* L) {
    auto* h = GetImage(L, "ImageSize", true);
    lua_pushinteger(L, h->tex->width);
    lua_pushinteger(L, h->tex->height);
    return 2;
}

} // namespace

void RegisterRenderApi(lua_State* L) {
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    const luaL_Reg methods[] = {{"__gc", l_ImgGC},
                                {"Load", l_ImgLoad},
                                {"Unload", l_ImgUnload},
                                {"IsValid", l_ImgIsValid},
                                {"IsLoading", l_ImgIsLoading},
                                {"SetLoadingPriority", l_ImgSetLoadingPriority},
                                {"ImageSize", l_ImgImageSize},
                                {nullptr, nullptr}};
    luaL_setfuncs(L, methods, 0);
    lua_setfield(L, LUA_REGISTRYINDEX, kImageMeta);

    const luaL_Reg globals[] = {{"RenderInit", l_RenderInit},
                                {"GetScreenSize", l_GetScreenSize},
                                {"GetScreenScale", l_GetScreenScale},
                                {"SetDPIScaleOverridePercent", l_SetDPIScaleOverridePercent},
                                {"GetDPIScaleOverridePercent", l_GetDPIScaleOverridePercent},
                                {"SetClearColor", l_SetClearColor},
                                {"GetAsyncCount", l_GetAsyncCount},
                                {"SetDrawLayer", l_SetDrawLayer},
                                {"GetDrawLayer", l_GetDrawLayer},
                                {"SetViewport", l_SetViewport},
                                {"SetDrawColor", l_SetDrawColor},
                                {"GetDrawColor", l_GetDrawColor},
                                {"DrawImage", l_DrawImage},
                                {"DrawImageQuad", l_DrawImageQuad},
                                {"DrawString", l_DrawString},
                                {"DrawStringWidth", l_DrawStringWidth},
                                {"DrawStringCursorIndex", l_DrawStringCursorIndex},
                                {"StripEscapes", l_StripEscapes},
                                {"NewImageHandle", l_NewImageHandle},
                                {nullptr, nullptr}};
    for (const luaL_Reg* r = globals; r->name; ++r) SetGlobalFunction(L, r->name, r->func);
}

} // namespace pob
