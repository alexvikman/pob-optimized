#include "render/Text.h"

namespace pob {

namespace {

// Palette for ^0-^9 (SimpleGraphic common.h:110).
constexpr float kPalette[10][3] = {
    {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f},
    {1.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.7f, 0.7f, 0.7f}, {0.4f, 0.4f, 0.4f},
};

bool IsHex(char32_t c) {
    return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'f') || (c >= U'A' && c <= U'F');
}

int HexValue(char32_t c) {
    if (c >= U'a') return c - U'a' + 10;
    if (c >= U'A') return c - U'A' + 10;
    return c - U'0';
}

template <typename Str>
int EscapeLength(Str s) {
    if (s.size() < 2 || s[0] != '^') return 0;
    auto d = s[1];
    if (d >= '0' && d <= '9') return 2;
    if (s.size() >= 8 && (d == 'x' || d == 'X')) {
        for (int i = 2; i < 8; ++i) {
            if (!IsHex(static_cast<char32_t>(static_cast<unsigned char>(s[i])))) return 0;
        }
        return 8;
    }
    return 0;
}

template <typename Str>
void ReadEscape(Str s, int len, Color& rgb) {
    if (len == 2) {
        const float* p = kPalette[s[1] - '0'];
        rgb[0] = p[0];
        rgb[1] = p[1];
        rgb[2] = p[2];
    } else if (len == 8) {
        for (int c = 0; c < 3; ++c) {
            auto hi = static_cast<char32_t>(static_cast<unsigned char>(s[2 + c * 2]));
            auto lo = static_cast<char32_t>(static_cast<unsigned char>(s[3 + c * 2]));
            rgb[c] = ((HexValue(hi) << 4) | HexValue(lo)) / 255.0f;
        }
    }
}

} // namespace

IndexedText IndexUtf8(std::string_view input) {
    IndexedText ret;
    ret.offsets.reserve(input.size());
    ret.text.reserve(input.size());
    auto b = reinterpret_cast<const unsigned char*>(input.data());
    size_t n = input.size();
    for (size_t i = 0; i < n;) {
        size_t left = n - i;
        ret.offsets.push_back(i);
        char32_t cp;
        if (b[i] < 0x80) {
            cp = b[i];
            i += 1;
        } else if (left >= 2 && (b[i] >> 5) == 0b110 && (b[i + 1] >> 6) == 0b10) {
            cp = (char32_t(b[i] & 0x1F) << 6) | (b[i + 1] & 0x3F);
            i += 2;
        } else if (left >= 3 && (b[i] >> 4) == 0b1110 && (b[i + 1] >> 6) == 0b10 && (b[i + 2] >> 6) == 0b10) {
            cp = (char32_t(b[i] & 0x0F) << 12) | (char32_t(b[i + 1] & 0x3F) << 6) | (b[i + 2] & 0x3F);
            i += 3;
        } else if (left >= 4 && (b[i] >> 3) == 0b11110 && (b[i + 1] >> 6) == 0b10 && (b[i + 2] >> 6) == 0b10 &&
                   (b[i + 3] >> 6) == 0b10) {
            cp = (char32_t(b[i] & 0x07) << 18) | (char32_t(b[i + 1] & 0x3F) << 12) |
                 (char32_t(b[i + 2] & 0x3F) << 6) | (b[i + 3] & 0x3F);
            i += 4;
        } else {
            cp = 0xFFFD;
            i += 1;
        }
        ret.text.push_back(cp);
    }
    return ret;
}

void AppendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

int ColorEscapeLength(std::u32string_view s) { return EscapeLength(s); }
int ColorEscapeLength(std::string_view s) { return EscapeLength(s); }
void ReadColorEscape(std::u32string_view s, int len, Color& rgb) { ReadEscape(s, len, rgb); }
void ReadColorEscape(std::string_view s, int len, Color& rgb) { ReadEscape(s, len, rgb); }

std::string StripColorEscapes(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    while (!s.empty()) {
        if (int len = ColorEscapeLength(s)) {
            s.remove_prefix(len);
        } else {
            out += s[0];
            s.remove_prefix(1);
        }
    }
    return out;
}

} // namespace pob
