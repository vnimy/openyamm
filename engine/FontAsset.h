#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace OpenYAMM::Engine
{
class AssetFileSystem;

struct FontGlyphMetrics
{
    int leftSpacing = 0;
    int width = 0;
    int rightSpacing = 0;
    float outlineAdvance = 0;

    float advance() const
    {
        return outlineAdvance > 0 ? outlineAdvance : float(leftSpacing + width + rightSpacing);
    }
};

// One glyph of the atlas, keyed by Unicode code point. Text is UTF-8 and the game ships Latin and CJK faces, so a face
// is a sparse code point map rather than the 256 byte slots of the legacy FNT. The cell rectangle is the padded quad the
// renderer samples, in atlas pixels before the supersampling scale (atlasScale) is applied.
struct FontGlyph
{
    FontGlyphMetrics metrics;
    int cellX = 0;
    int cellY = 0;
    int cellWidth = 0;
    int cellHeight = 0;
};

// Legacy faces keep logical geometry with a supersampled atlas. Independent
// faces use screen pixels.
struct FontAtlas
{
    int fontHeight = 0;
    int atlasCellWidth = 0;
    int atlasWidth = 0;
    int atlasHeight = 0;
    int atlasScale = 1;
    int atlasPadding = 0;
    // Independent outline faces rasterize at the display pixel height, so callers keep requesting scaled sizes; a
    // legacy FNT replacement stays at its bitmap size and is scaled geometrically.
    bool pixelSized = false;
    std::unordered_map<uint32_t, FontGlyph> glyphs;
    // Both code points of a pair, packed as previous << 32 | current.
    std::unordered_map<uint64_t, float> kerningPairs;
    std::vector<uint8_t> mainAtlasPixels;

    const FontGlyph *glyph(uint32_t codePoint) const
    {
        const auto found = glyphs.find(codePoint);
        return found == glyphs.end() ? nullptr : &found->second;
    }

    float kerning(uint32_t previous, uint32_t current) const
    {
        const auto found = kerningPairs.find(uint64_t(previous) << 32 | current);
        return found == kerningPairs.end() ? 0.0f : found->second;
    }
};

struct FontAtlasImage
{
    FontAtlas atlas;
    std::vector<uint8_t> shadowPixels;
};

// Decodes one UTF-8 code point starting at `index` and advances it. An invalid sequence yields U+FFFD and consumes a
// single byte, so callers always make progress.
uint32_t decodeUtf8(std::string_view text, size_t &index);

// True when the face's descriptor declares `metrics: freetype`: it keeps its own outline advances and is rasterized at
// the display pixel height instead of replacing a legacy FNT layout. Cached per face name for the process lifetime.
bool faceUsesOutlineMetrics(const AssetFileSystem &assetFileSystem, const std::string &fontName);

// A legacy face: its outlines come from fonts/truetype/<name>.yml and the font file it names, while the original FNT
// supplies the layout metrics and the reserved symbol glyphs (CP1252 bytes without a character). A missing or invalid
// replacement returns an explicit error.
std::optional<FontAtlasImage> loadFontAtlas(
    const AssetFileSystem &assetFileSystem,
    const std::vector<uint8_t> &bitmapBytes,
    const std::string &fontName,
    std::string &error);

// Independent outline faces use their own advances, without requiring a legacy
// FNT. Rasterize at the requested display height; zero uses the descriptor's
// logical height.
std::optional<FontAtlasImage> loadTrueTypeFontAtlas(const AssetFileSystem &assetFileSystem, const std::string &fontName,
                                                    std::string &error, int pixelHeight = 0);
}
