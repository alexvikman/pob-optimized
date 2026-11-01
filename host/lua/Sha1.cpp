// Native SHA-1 for PoB's `sha1` Lua module.
//
// UpdateCheck.lua hashes every installed file (~770 MB) on each start to verify
// the install; in pure Lua that is ~35 s of a full core. The host loads PoB's own
// runtime/lua/sha1 module unchanged and only replaces its sha1.sha1/sha1.binary
// with this implementation (hmac and the call form use those two).

#include "lua/Api.h"
#include "lua/LuaUtil.h"

#include <cstdint>
#include <cstring>

namespace pob {

namespace {

uint32_t Rol(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

void Sha1(const unsigned char* data, size_t len, unsigned char out[20]) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    auto block = [&h](const unsigned char* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) |
                   uint32_t(p[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t t = Rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = Rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    };

    size_t full = len / 64;
    for (size_t i = 0; i < full; ++i) block(data + i * 64);
    unsigned char tail[128] = {};
    size_t rest = len - full * 64;
    std::memcpy(tail, data + full * 64, rest);
    tail[rest] = 0x80;
    size_t tailLen = rest + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = uint64_t(len) * 8;
    for (int i = 0; i < 8; ++i) tail[tailLen - 1 - i] = static_cast<unsigned char>(bits >> (i * 8));
    block(tail);
    if (tailLen == 128) block(tail + 64);
    for (int i = 0; i < 5; ++i) {
        out[i * 4] = static_cast<unsigned char>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<unsigned char>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<unsigned char>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<unsigned char>(h[i]);
    }
}

int l_sha1_binary(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    unsigned char out[20];
    Sha1(reinterpret_cast<const unsigned char*>(s), len, out);
    lua_pushlstring(L, reinterpret_cast<const char*>(out), sizeof(out));
    return 1;
}

int l_sha1_hex(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    unsigned char out[20];
    Sha1(reinterpret_cast<const unsigned char*>(s), len, out);
    static const char* kHex = "0123456789abcdef";
    char hex[40];
    for (int i = 0; i < 20; ++i) {
        hex[i * 2] = kHex[out[i] >> 4];
        hex[i * 2 + 1] = kHex[out[i] & 15];
    }
    lua_pushlstring(L, hex, sizeof(hex));
    return 1;
}

// Loads PoB's own sha1 module through the normal Lua searcher, then swaps in the
// native hash functions.
constexpr const char* kSha1Loader = R"lua(
local hex, binary = ...
local loader = package.loaders[2]("sha1")
if type(loader) ~= "function" then error("sha1 module not found:" .. tostring(loader)) end
local m = loader("sha1")
m.sha1 = hex
m.binary = binary
return m
)lua";

} // namespace

int luaopen_sha1_host(lua_State* L) {
    if (luaL_loadstring(L, kSha1Loader) != 0) return lua_error(L);
    lua_pushcfunction(L, l_sha1_hex);
    lua_pushcfunction(L, l_sha1_binary);
    lua_call(L, 2, 1);
    return 1;
}

} // namespace pob
