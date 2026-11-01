#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace pob {

using Color = std::array<float, 4>;

// UTF-8 decoded to codepoints, remembering each codepoint's source byte offset.
// Invalid bytes decode to U+FFFD one byte at a time (SimpleGraphic common.cpp:440).
struct IndexedText {
    std::u32string text;
    std::vector<size_t> offsets;
};
IndexedText IndexUtf8(std::string_view utf8);

void AppendUtf8(std::string& out, char32_t cp);

// Length of a colour escape at the start of `s` (^0-^9 → 2, ^xRRGGBB → 8), else 0.
int ColorEscapeLength(std::u32string_view s);
int ColorEscapeLength(std::string_view s);

// Applies the escape of length `len` at the start of `s` to rgb (alpha untouched).
void ReadColorEscape(std::u32string_view s, int len, Color& rgb);
void ReadColorEscape(std::string_view s, int len, Color& rgb);

std::string StripColorEscapes(std::string_view s);

} // namespace pob
