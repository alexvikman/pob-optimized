#pragma once

#include "render/DrawList.h"
#include "render/Textures.h"

#include <array>
#include <memory>
#include <string_view>
#include <vector>

namespace pob {

enum class TextAlign { Left, Center, Right, CenterX, RightX };

// A SimpleGraphic bitmap font: one .tgf description plus a .tga atlas per size.
// Measurement and layout follow r_font.cpp exactly (docs/API.md §4.3), because
// PoB's UI layout depends on the widths matching the original.
class Font {
public:
    bool Load(const fs::path& fontDir, const std::string& name, TextureManager& textures);

    int StringWidth(int height, std::u32string_view text) const;
    // Codepoint index of the caret position nearest to (curX, curY).
    size_t CursorIndex(int height, std::u32string_view text, int curX, int curY) const;
    // Draws text, updating `color` from embedded escapes.
    void Draw(DrawList& dl, float x, float y, TextAlign align, int height, Color& color,
              std::u32string_view text) const;

private:
    struct Glyph {
        float tcLeft = 0, tcRight = 0, tcTop = 0, tcBottom = 0;
        int width = 0, spLeft = 0, spRight = 0;
    };
    struct Size {
        int height = 0;
        std::shared_ptr<Texture> tex;
        int numGlyph = 0;
        std::array<Glyph, 128> glyphs{};

        const Glyph& Get(char32_t cp) const {
            static const Glyph kEmpty{};
            unsigned char c = static_cast<unsigned char>(cp);
            return c < numGlyph ? glyphs[c] : kEmpty;
        }
        int Advance(char32_t cp) const {
            const Glyph& g = Get(cp);
            return g.width + g.spLeft + g.spRight;
        }
    };
    struct Smaller {
        const Size* size;
        int yPad;
    };

    int FindIndex(int height) const;
    Smaller FindSmaller(int height, int idx, int reduction) const;
    int LineWidth(const Size& fh, std::u32string_view line, int height, float scale) const;
    size_t LineCursor(const Size& fh, std::u32string_view line, int height, float scale, int curX) const;
    void DrawLine(DrawList& dl, float x, float y, TextAlign align, int height, Color& color,
                  std::u32string_view line) const;

    std::vector<Size> sizes_;
    std::vector<int> heightMap_;
    int maxHeight_ = 0;
};

class FontSet {
public:
    // Font names accepted by DrawString & co, in SimpleGraphic's order.
    static constexpr std::array<const char*, 7> kNames = {
        "FIXED", "VAR", "VAR BOLD", "FONTIN SC", "FONTIN SC ITALIC", "FONTIN", "FONTIN ITALIC"};

    bool Load(const fs::path& runtimeDir, TextureManager& textures);
    const Font& Get(size_t index) const { return fonts_[index < fonts_.size() ? index : 0]; }

private:
    std::array<Font, 7> fonts_;
};

} // namespace pob
