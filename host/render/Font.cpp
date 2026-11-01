// Text measurement and layout ported from SimpleGraphic's engine/render/r_font.cpp.
// SimpleGraphic: (c) 2016 David Gowor, MIT licence (see LICENSE-THIRD-PARTY.md).

#include "render/Font.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace pob {

namespace {

constexpr int kTofuSizeReduction = 3;

// Unhandled codepoints are drawn as "[U+XXXX]" in a smaller font.
std::u32string TofuString(char32_t cp) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "[U+%04X]", static_cast<unsigned>(cp));
    return std::u32string(buf, buf + std::strlen(buf));
}

} // namespace

bool Font::Load(const fs::path& fontDir, const std::string& name, TextureManager& textures) {
    std::ifstream tgf(fontDir / FromUtf8(name + ".tgf"));
    if (!tgf) {
        Log("Font '%s' not found in %s", name.c_str(), ToLuaPath(fontDir).c_str());
        return false;
    }
    std::string line;
    Size* cur = nullptr;
    while (std::getline(tgf, line)) {
        unsigned h, x, y, w;
        int sl, sr;
        if (std::sscanf(line.c_str(), "HEIGHT %u;", &h) == 1) {
            Size& s = sizes_.emplace_back();
            s.height = static_cast<int>(h);
            s.tex = textures.Acquire(fontDir / FromUtf8(name + "." + std::to_string(h) + ".tga"), 0);
            maxHeight_ = std::max(maxHeight_, s.height);
            cur = &s;
        } else if (cur && std::sscanf(line.c_str(), "GLYPH %u %u %u %d %d;", &x, &y, &w, &sl, &sr) == 5) {
            if (cur->numGlyph >= 128) continue;
            Glyph& g = cur->glyphs[cur->numGlyph++];
            float tw = cur->tex->width ? static_cast<float>(cur->tex->width) : 1.0f;
            float th = cur->tex->height ? static_cast<float>(cur->tex->height) : 1.0f;
            g.tcLeft = x / tw;
            g.tcRight = (x + w) / tw;
            g.tcTop = y / th;
            g.tcBottom = (y + cur->height) / th;
            g.width = static_cast<int>(w);
            g.spLeft = sl;
            g.spRight = sr;
        }
    }
    if (sizes_.empty()) return false;

    // Height → size index, including the round-up band below each size (r_font.cpp:99).
    heightMap_.assign(maxHeight_ + 1, 0);
    for (int i = 0; i < static_cast<int>(sizes_.size()); ++i) {
        int gh = sizes_[i].height;
        for (int h = gh; h <= maxHeight_; ++h) heightMap_[h] = i;
        if (i > 0) {
            int lim = (gh - sizes_[i - 1].height - 1) / 2;
            for (int b = 0; b < lim; ++b) heightMap_[gh - b - 1] = i;
        }
    }
    return true;
}

int Font::FindIndex(int height) const {
    if (height > maxHeight_) return static_cast<int>(sizes_.size()) - 1;
    if (height < 0) return 0;
    return heightMap_[height];
}

Font::Smaller Font::FindSmaller(int height, int idx, int reduction) const {
    Smaller ret{&sizes_[idx], 0};
    for (int i = idx - 1; i >= 0; --i) {
        int diff = height - sizes_[i].height;
        if (diff >= reduction) {
            ret.size = &sizes_[i];
            ret.yPad = static_cast<int>(std::ceil(diff / 2.0f));
            break;
        }
    }
    return ret;
}

int Font::LineWidth(const Size& fh, std::u32string_view str, int height, float scale) const {
    int idx = static_cast<int>(&fh - sizes_.data());
    const Size* tofu = FindSmaller(height, idx, kTofuSizeReduction).size;
    float width = 0.0f;
    for (size_t i = 0; i < str.size();) {
        char32_t ch = str[i];
        if (int esc = ColorEscapeLength(str.substr(i))) {
            i += esc;
        } else if (ch >= static_cast<unsigned>(fh.numGlyph)) {
            for (char32_t t : TofuString(ch)) width = std::ceil(width + tofu->Advance(t));
            ++i;
        } else if (ch == U'\t') {
            width = std::ceil(width + fh.Advance(U' ') * 4 * scale);
            ++i;
        } else {
            width = std::ceil(width + fh.Advance(ch) * scale);
            ++i;
        }
    }
    return static_cast<int>(width);
}

int Font::StringWidth(int height, std::u32string_view str) const {
    if (sizes_.empty() || str.empty()) return 0;
    const Size& fh = sizes_[FindIndex(height)];
    float scale = static_cast<float>(height) / fh.height;
    int max = 0;
    size_t start = 0;
    while (start <= str.size()) {
        size_t end = str.find(U'\n', start);
        if (end == std::u32string_view::npos) end = str.size();
        if (end > start) max = std::max(max, LineWidth(fh, str.substr(start, end - start), height, scale));
        if (end == str.size()) break;
        start = end + 1;
    }
    return max;
}

size_t Font::LineCursor(const Size& fh, std::u32string_view str, int height, float scale, int curX) const {
    int idx = static_cast<int>(&fh - sizes_.data());
    const Size* tofu = FindSmaller(height, idx, kTofuSizeReduction).size;
    float x = 0.0f;
    size_t i = 0;
    while (i < str.size()) {
        char32_t ch = str[i];
        if (int esc = ColorEscapeLength(str.substr(i))) {
            i += esc;
        } else if (ch >= static_cast<unsigned>(fh.numGlyph)) {
            for (char32_t t : TofuString(ch)) {
                x = std::ceil(x + tofu->Advance(t));
                if (curX <= x) return i;
            }
            ++i;
        } else if (ch == U'\t') {
            float full = fh.Advance(U' ') * 4.0f * scale;
            float half = std::ceil(full / 2.0f);
            x = std::ceil(x + half);
            if (curX <= x) break;
            x = std::ceil(x + full - half);
            if (curX <= x) break;
            ++i;
        } else {
            x = std::ceil(x + fh.Advance(ch) * scale);
            if (curX <= x) break;
            ++i;
        }
    }
    return i;
}

size_t Font::CursorIndex(int height, std::u32string_view str, int curX, int curY) const {
    if (sizes_.empty()) return 0;
    const Size& fh = sizes_[FindIndex(height)];
    float scale = static_cast<float>(height) / fh.height;
    size_t lineStart = 0;
    size_t lastIndex = 0;
    int lineY = height;
    while (lineStart < str.size()) {
        size_t end = str.find(U'\n', lineStart);
        if (end == std::u32string_view::npos) end = str.size();
        lastIndex = LineCursor(fh, str.substr(lineStart, end - lineStart), height, scale, curX);
        if (curY <= lineY || end == str.size()) break;
        lineStart = end + 1;
        lineY += height;
    }
    return lineStart + lastIndex;
}

void Font::Draw(DrawList& dl, float x, float y, TextAlign align, int height, Color& color,
                std::u32string_view str) const {
    if (sizes_.empty() || str.empty()) return;
    size_t start = 0;
    while (start <= str.size()) {
        size_t end = str.find(U'\n', start);
        if (end == std::u32string_view::npos) end = str.size();
        if (end > start) DrawLine(dl, x, y, align, height, color, str.substr(start, end - start));
        y += height;
        if (end == str.size()) break;
        start = end + 1;
    }
}

void Font::DrawLine(DrawList& dl, float posX, float posY, TextAlign align, int height, Color& color,
                    std::u32string_view str) const {
    const int screenW = dl.ScreenWidth();

    // Off-screen lines only apply their colour escapes.
    if (posY >= dl.ScreenHeight() || posY <= -height) {
        for (size_t i = 0; i < str.size();) {
            if (int esc = ColorEscapeLength(str.substr(i))) {
                ReadColorEscape(str.substr(i), esc, color);
                color[3] = 1.0f;
                i += esc;
            } else {
                ++i;
            }
        }
        return;
    }

    int idx = FindIndex(height);
    const Size& fh = sizes_[idx];
    float scale = static_cast<float>(height) / fh.height;
    Smaller tofu = FindSmaller(height, idx, kTofuSizeReduction);

    float x = posX;
    float y = std::floor(posY);
    if (align != TextAlign::Left) {
        float width = static_cast<float>(LineWidth(fh, str, height, scale));
        switch (align) {
        case TextAlign::Center: x = std::floor((screenW - width) / 2.0f + posX); break;
        case TextAlign::Right: x = std::floor(screenW - width - posX); break;
        case TextAlign::CenterX: x = std::floor(posX - width / 2.0f); break;
        case TextAlign::RightX: x = std::floor(posX - width); break;
        default: break;
        }
    }
    x = std::round(x);

    auto drawCodepoint = [&](const Size& sz, int h, float sc, int yShift, char32_t cp) {
        float cpY = y + yShift;
        const Glyph& g = sz.Get(cp);
        x += g.spLeft * sc;
        if (g.width) {
            float w = g.width * sc;
            if (x + w >= 0 && x < screenW) {
                const float xy[8] = {x, cpY, x + w, cpY, x + w, cpY + h, x, cpY + h};
                const float uv[8] = {g.tcLeft, g.tcTop, g.tcRight, g.tcTop, g.tcRight, g.tcBottom, g.tcLeft, g.tcBottom};
                dl.AddQuad(sz.tex.get(), xy, uv, 0, color);
            }
            x += w;
        }
        x += g.spRight * sc;
        x = std::ceil(x);
    };

    for (size_t i = 0; i < str.size();) {
        char32_t ch = str[i];
        if (ch >= static_cast<unsigned>(fh.numGlyph)) {
            for (char32_t t : TofuString(ch)) drawCodepoint(*tofu.size, tofu.size->height, 1.0f, tofu.yPad, t);
            ++i;
            continue;
        }
        if (int esc = ColorEscapeLength(str.substr(i))) {
            ReadColorEscape(str.substr(i), esc, color);
            color[3] = 1.0f;
            i += esc;
            continue;
        }
        if (ch == U'\t') {
            // No ceil here, unlike the width calculation (SimpleGraphic quirk).
            x += (fh.Advance(U' ') << 2) * scale;
            ++i;
            continue;
        }
        drawCodepoint(fh, height, scale, 0, ch);
        ++i;
    }
}

bool FontSet::Load(const fs::path& runtimeDir, TextureManager& textures) {
    static constexpr std::array<const char*, 7> kFiles = {
        "Bitstream Vera Sans Mono", "Liberation Sans",          "Liberation Sans Bold", "Fontin SmallCaps",
        "Fontin SmallCaps Italic",  "Fontin",                   "Fontin Italic"};
    fs::path dir = runtimeDir / "SimpleGraphic" / "Fonts";
    bool ok = true;
    for (size_t i = 0; i < kFiles.size(); ++i) ok = fonts_[i].Load(dir, kFiles[i], textures) && ok;
    return ok;
}

} // namespace pob
