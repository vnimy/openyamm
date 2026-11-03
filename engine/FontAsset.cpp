#include "engine/FontAsset.h"

#include "engine/AssetFileSystem.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <mutex>
#include <set>

namespace OpenYAMM::Engine
{
namespace
{
// Reserved CP1252 bytes (0x80..0x9F without a character) carry the legacy UI symbols. Unicode text never produces
// them, so they are keyed in a private use range to keep the bitmap art reachable without colliding with real code
// points.
constexpr uint32_t ReservedByteCodePointBase = 0xe000;

struct BitmapFont
{
    int firstChar = 0;
    int lastChar = 0;
    int fontHeight = 0;
    std::array<FontGlyphMetrics, 256> glyphMetrics = {{}};
    std::array<uint32_t, 256> glyphOffsets = {{}};
    std::vector<uint8_t> pixels;
};

// One glyph to bake: either a legacy FNT slot or a code point of the face's own outlines.
struct GlyphRequest
{
    uint32_t codePoint = 0;
    FontGlyphMetrics metrics;
    int bitmapSlot = -1;
};

std::string normalizedFontName(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character)
    {
        return std::tolower(character);
    });
    return name;
}

int32_t readInt32Le(const uint8_t *pBytes)
{
    return static_cast<int32_t>(
        static_cast<uint32_t>(pBytes[0])
        | (static_cast<uint32_t>(pBytes[1]) << 8)
        | (static_cast<uint32_t>(pBytes[2]) << 16)
        | (static_cast<uint32_t>(pBytes[3]) << 24));
}

uint32_t readUint32Le(const uint8_t *pBytes)
{
    return static_cast<uint32_t>(
        static_cast<uint32_t>(pBytes[0])
        | (static_cast<uint32_t>(pBytes[1]) << 8)
        | (static_cast<uint32_t>(pBytes[2]) << 16)
        | (static_cast<uint32_t>(pBytes[3]) << 24));
}

bool validateBitmapFont(
    const BitmapFont &font,
    const std::vector<uint8_t> &pixels)
{
    if (font.firstChar < 0
        || font.firstChar > 255
        || font.lastChar < 0
        || font.lastChar > 255
        || font.firstChar > font.lastChar
        || font.fontHeight <= 0)
    {
        return false;
    }

    for (int glyphIndex = 0; glyphIndex < 256; ++glyphIndex)
    {
        const FontGlyphMetrics &metrics = font.glyphMetrics[glyphIndex];

        if (glyphIndex < font.firstChar || glyphIndex > font.lastChar)
        {
            continue;
        }

        if (metrics.width < 0 || metrics.width > 1024 || metrics.leftSpacing < -512 || metrics.leftSpacing > 512
            || metrics.rightSpacing < -512 || metrics.rightSpacing > 512)
        {
            return false;
        }

        const uint64_t glyphSize = static_cast<uint64_t>(font.fontHeight) * static_cast<uint64_t>(metrics.width);
        const uint64_t glyphEnd = static_cast<uint64_t>(font.glyphOffsets[glyphIndex]) + glyphSize;

        if (glyphEnd > pixels.size())
        {
            return false;
        }
    }

    return true;
}

std::optional<BitmapFont> parseBitmapFont(const std::vector<uint8_t> &bytes)
{
    constexpr size_t FontHeaderSize = 32;
    constexpr size_t Mm7AtlasSize = 4096;

    if (bytes.size() < FontHeaderSize + Mm7AtlasSize)
    {
        return std::nullopt;
    }

    const uint8_t *pBytes = bytes.data();

    if (pBytes[2] != 8 || pBytes[3] != 0 || pBytes[4] != 0 || pBytes[6] != 0 || pBytes[7] != 0)
    {
        return std::nullopt;
    }

    BitmapFont font = {};
    font.firstChar = pBytes[0];
    font.lastChar = pBytes[1];
    font.fontHeight = pBytes[5];

    for (int glyphIndex = 0; glyphIndex < 256; ++glyphIndex)
    {
        const size_t metricOffset = FontHeaderSize + static_cast<size_t>(glyphIndex) * 12;
        font.glyphMetrics[glyphIndex].leftSpacing = readInt32Le(&pBytes[metricOffset]);
        font.glyphMetrics[glyphIndex].width = readInt32Le(&pBytes[metricOffset + 4]);
        font.glyphMetrics[glyphIndex].rightSpacing = readInt32Le(&pBytes[metricOffset + 8]);
    }

    for (int glyphIndex = 0; glyphIndex < 256; ++glyphIndex)
    {
        const size_t offsetPosition = FontHeaderSize + 256 * 12 + static_cast<size_t>(glyphIndex) * 4;
        font.glyphOffsets[glyphIndex] = readUint32Le(&pBytes[offsetPosition]);
    }

    font.pixels.assign(bytes.begin() + static_cast<ptrdiff_t>(FontHeaderSize + Mm7AtlasSize), bytes.end());

    if (!validateBitmapFont(font, font.pixels))
    {
        return std::nullopt;
    }

    return font;
}


struct FreeTypeFace
{
    FT_Library library = nullptr;
    FT_Face face = nullptr;

    ~FreeTypeFace()
    {
        if (face != nullptr)
        {
            FT_Done_Face(face);
        }
        if (library != nullptr)
        {
            FT_Done_FreeType(library);
        }
    }
};

uint32_t cp1252CodePoint(int character)
{
    constexpr std::array<uint32_t, 32> Extended = {
        0x20ac, 0, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0, 0x17d, 0,
        0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0, 0x17e, 0x178};
    if (character < 32 || character == 127)
    {
        return 0;
    }
    return character >= 128 && character < 160 ? Extended[character - 128] : uint32_t(character);
}

// The optional `glyphs:` descriptor file lists the code points a face bakes, one hexadecimal code point per line; the
// generator tools/font_glyph_subset.py writes it from the shipped text tables so a CJK face keeps a small atlas.
std::set<uint32_t> parseGlyphSubset(const std::optional<std::string> &text)
{
    std::set<uint32_t> codePoints;

    if (!text)
    {
        return codePoints;
    }

    size_t index = 0;
    while (index < text->size())
    {
        size_t lineEnd = text->find('\n', index);
        if (lineEnd == std::string::npos)
        {
            lineEnd = text->size();
        }
        std::string line = text->substr(index, lineEnd - index);
        index = lineEnd + 1;
        const size_t comment = line.find('#');
        if (comment != std::string::npos)
        {
            line.resize(comment);
        }
        const size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
        {
            continue;
        }
        const size_t last = line.find_last_not_of(" \t\r");
        line = line.substr(first, last - first + 1);
        char *pEnd = nullptr;
        const unsigned long value = std::strtoul(line.c_str(), &pEnd, 16);
        if (pEnd != nullptr && *pEnd == '\0' && value > 0 && value <= 0x10ffff)
        {
            codePoints.insert(uint32_t(value));
        }
    }

    return codePoints;
}

// Cells are uniform so the renderer keeps its padded quad maths: every glyph owns a cell of the widest glyph plus the
// padding, laid out in a near-square grid. A subset of a few thousand CJK glyphs therefore stays one texture.
struct AtlasLayout
{
    int columns = 1;
    int cellWidth = 1;
    int cellHeight = 1;
    int width = 1;
    int height = 1;
};

std::optional<AtlasLayout> planAtlas(size_t glyphCount, int cellWidth, int cellHeight, int atlasScale, std::string &error)
{
    constexpr int MaxAtlasSize = 8192;

    if (glyphCount == 0)
    {
        error = "font has no glyphs to bake";
        return std::nullopt;
    }

    const int physicalCellWidth = std::max(1, cellWidth * atlasScale);
    const int physicalCellHeight = std::max(1, cellHeight * atlasScale);
    const int maxColumns = std::max(1, MaxAtlasSize / physicalCellWidth);
    int columns = std::max(1, int(std::ceil(std::sqrt(double(glyphCount)))));
    columns = std::min(columns, maxColumns);

    while (columns > 1 && (int((glyphCount + size_t(columns) - 1) / size_t(columns)) * physicalCellHeight) > MaxAtlasSize)
    {
        --columns;
    }

    const int rows = int((glyphCount + size_t(columns) - 1) / size_t(columns));

    if (columns * physicalCellWidth > MaxAtlasSize || rows * physicalCellHeight > MaxAtlasSize)
    {
        error = "font atlas exceeds texture limit (" + std::to_string(glyphCount) + " glyphs of "
            + std::to_string(physicalCellWidth) + "x" + std::to_string(physicalCellHeight) + " pixels)";
        return std::nullopt;
    }

    AtlasLayout layout;
    layout.columns = columns;
    layout.cellWidth = cellWidth;
    layout.cellHeight = cellHeight;
    layout.width = columns * physicalCellWidth;
    layout.height = rows * physicalCellHeight;
    return layout;
}

void allocateAtlas(FontAtlasImage &image)
{
    FontAtlas &atlas = image.atlas;
    const size_t byteCount = size_t(atlas.atlasWidth) * atlas.atlasHeight * 4;
    atlas.mainAtlasPixels.assign(byteCount, 255);
    image.shadowPixels.assign(byteCount, 0);
    // White even outside the ink prevents dark fringes with straight-alpha
    // bilinear filtering; the tint multiplies, so glyphs must start white.
    for (size_t offset = 3; offset < byteCount; offset += 4)
    {
        atlas.mainAtlasPixels[offset] = 0;
    }
}

int atlasCellX(const FontAtlas &atlas, int columns, int cellIndex)
{
    return (cellIndex % columns) * (atlas.atlasCellWidth + 2 * atlas.atlasPadding);
}

int atlasCellY(const FontAtlas &atlas, int columns, int cellIndex)
{
    return (cellIndex / columns) * (atlas.fontHeight + 2 * atlas.atlasPadding);
}

void copyBitmapGlyph(const BitmapFont &font, int character, int cellIndex, int columns, FontAtlasImage &image)
{
    FontAtlas &atlas = image.atlas;
    const int scale = atlas.atlasScale;
    const int cellX = atlasCellX(atlas, columns, cellIndex) * scale;
    const int cellY = atlasCellY(atlas, columns, cellIndex) * scale;
    const int width = font.glyphMetrics[character].width;
    for (int y = 0; y < font.fontHeight * scale; ++y)
    {
        for (int x = 0; x < width * scale; ++x)
        {
            const uint8_t value = font.pixels[font.glyphOffsets[character] + size_t(y / scale) * width + x / scale];
            const size_t offset = (size_t(cellY + atlas.atlasPadding * scale + y) * atlas.atlasWidth
                + cellX + atlas.atlasPadding * scale + x) * 4;
            if (value > 1)
            {
                atlas.mainAtlasPixels[offset + 0] = 255;
                atlas.mainAtlasPixels[offset + 1] = 255;
                atlas.mainAtlasPixels[offset + 2] = 255;
                atlas.mainAtlasPixels[offset + 3] = 255;
            }
            image.shadowPixels[offset + 3] = value == 1 ? 255 : 0;
        }
    }
}
}

uint32_t decodeUtf8(std::string_view text, size_t &index)
{
    if (index >= text.size())
    {
        return 0;
    }

    const uint8_t lead = uint8_t(text[index]);
    const size_t length = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3
        : (lead & 0xf8) == 0xf0 ? 4 : 0;

    if (length == 0 || index + length > text.size())
    {
        ++index;
        return 0xfffd;
    }

    uint32_t point = length == 1 ? lead : lead & (0xff >> (length + 1));

    for (size_t offset = 1; offset < length; ++offset)
    {
        const uint8_t next = uint8_t(text[index + offset]);

        if ((next & 0xc0) != 0x80)
        {
            ++index;
            return 0xfffd;
        }

        point = (point << 6) | (next & 0x3f);
    }

    index += length;
    return point;
}

bool faceUsesOutlineMetrics(const AssetFileSystem &assetFileSystem, const std::string &fontName)
{
    static std::mutex mutex;
    static std::unordered_map<std::string, bool> cache;

    const std::string name = normalizedFontName(fontName);

    {
        const std::lock_guard<std::mutex> lock(mutex);
        const auto found = cache.find(name);

        if (found != cache.end())
        {
            return found->second;
        }
    }

    bool outlineMetrics = false;
    const std::optional<std::string> descriptorText = assetFileSystem.readTextFile("fonts/truetype/" + name + ".yml");

    if (descriptorText)
    {
        try
        {
            const YAML::Node descriptor = YAML::Load(*descriptorText);
            // A baked glyph subset (the CJK faces) used to serve every display scale from one atlas baked at the
            // layout size, which blurred every size that did not match it: the atlas was magnified at draw time.
            // The subset only limits *which* code points are baked, so honouring the requested height costs one
            // raster per size actually used and is what makes the text sharp.
            outlineMetrics = descriptor["metrics"].as<std::string>("legacy") == "freetype";
        }
        catch (const YAML::Exception &)
        {
            outlineMetrics = false;
        }
    }

    {
        const std::lock_guard<std::mutex> lock(mutex);
        cache[name] = outlineMetrics;
    }

    return outlineMetrics;
}

std::optional<FontAtlasImage> loadTrueTypeFontAtlas(const AssetFileSystem &assetFileSystem, const std::string &fontName,
                                                    std::string &error, int pixelHeight)
{
    error.clear();
    const std::string name = normalizedFontName(fontName);
    if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
    {
        error = "Invalid outline font name";
        return std::nullopt;
    }
    const std::optional<std::string> descriptorText = assetFileSystem.readTextFile("fonts/truetype/" + name + ".yml");
    FontAtlasImage image;
    FontAtlas &atlas = image.atlas;
    std::string file;
    std::string glyphSubsetFile;
    int baseline = 0;
    int requestedPadding = -1;
    std::optional<int> weight;
    if (!descriptorText)
    {
        error = "missing outline font descriptor: " + name;
        return std::nullopt;
    }
    try
    {
        const YAML::Node descriptor = YAML::Load(*descriptorText);
        file = descriptor["file"].as<std::string>();
        atlas.fontHeight = descriptor["logical_height"].as<int>();
        baseline = descriptor["baseline"].as<int>();
        if (descriptor["weight"])
        {
            weight = descriptor["weight"].as<int>();
        }
        if (descriptor["padding"])
        {
            requestedPadding = descriptor["padding"].as<int>();
        }
        if (descriptor["glyphs"])
        {
            glyphSubsetFile = descriptor["glyphs"].as<std::string>();
        }
        const std::string encoding = descriptor["encoding"].as<std::string>();
        if (descriptor["metrics"].as<std::string>() != "freetype" ||
            (encoding != "utf-8" && encoding != "windows-1252") || file.empty() ||
            file.find_first_of("/\\") != std::string::npos || atlas.fontHeight < 8 || atlas.fontHeight > 128 ||
            baseline < 0 || baseline > atlas.fontHeight || (weight && (*weight < 1 || *weight > 1000)) ||
            (requestedPadding >= 0 && (requestedPadding < 0 || requestedPadding > 32)) ||
            glyphSubsetFile.find_first_of("/\\") != std::string::npos)
        {
            error = "Invalid independent outline font descriptor: " + name;
            return std::nullopt;
        }
    }
    catch (const YAML::Exception &exception)
    {
        error = "Invalid outline descriptor: " + name + ": " + exception.what();
        return std::nullopt;
    }
    if (pixelHeight < 0 || pixelHeight > 512)
    {
        error = "Invalid outline font pixel height";
        return std::nullopt;
    }
    const int logicalHeight = atlas.fontHeight;
    atlas.fontHeight = pixelHeight > 0 ? pixelHeight : logicalHeight;
    baseline = int(std::lround(float(baseline) * atlas.fontHeight / logicalHeight));
    atlas.atlasPadding = requestedPadding >= 0
        ? requestedPadding
        : int(std::ceil(8.0f * atlas.fontHeight / logicalHeight)) + 1;
    const std::optional<std::vector<uint8_t>> bytes = assetFileSystem.readBinaryFile("fonts/truetype/" + file);
    FreeTypeFace font;
    atlas.atlasScale = 1;
    atlas.pixelSized = true;
    if (!bytes || bytes->empty() || bytes->size() > 64 * 1024 * 1024 || FT_Init_FreeType(&font.library) != 0 ||
        FT_New_Memory_Face(font.library, bytes->data(), FT_Long(bytes->size()), 0, &font.face) != 0 ||
        !FT_IS_SCALABLE(font.face) || FT_Select_Charmap(font.face, FT_ENCODING_UNICODE) != 0 ||
        FT_Set_Pixel_Sizes(font.face, 0, atlas.fontHeight * atlas.atlasScale) != 0)
    {
        error = "Unable to open outline font: " + file;
        return std::nullopt;
    }
    if (weight)
    {
        FT_MM_Var *pVariations = nullptr;
        if (FT_Get_MM_Var(font.face, &pVariations) != 0)
        {
            error = "Font has no variable weight axis: " + file;
            return std::nullopt;
        }
        std::vector<FT_Fixed> coordinates(pVariations->num_axis);
        const FT_Fixed requestedWeight = FT_Fixed(*weight) * 65536;
        bool validWeight = false;
        for (size_t axisIndex = 0; axisIndex < coordinates.size(); ++axisIndex)
        {
            const FT_Var_Axis &axis = pVariations->axis[axisIndex];
            coordinates[axisIndex] = axis.def;
            if (axis.tag == FT_MAKE_TAG('w', 'g', 'h', 't'))
            {
                validWeight = requestedWeight >= axis.minimum && requestedWeight <= axis.maximum;
                coordinates[axisIndex] = requestedWeight;
            }
        }
        FT_Done_MM_Var(font.library, pVariations);
        if (!validWeight
            || FT_Set_Var_Design_Coordinates(font.face, coordinates.size(), coordinates.data()) != 0)
        {
            error = "Invalid variable font weight: " + file;
            return std::nullopt;
        }
    }

    // CP1252 first, so a Latin face keeps the same coverage; the descriptor's subset adds the code points the shipped
    // translations need (CJK faces), and a face that lacks one simply skips it.
    std::set<uint32_t> codePoints;
    for (int character = 32; character <= 255; ++character)
    {
        const uint32_t point = cp1252CodePoint(character);
        if (point != 0)
        {
            codePoints.insert(point);
        }
    }
    if (!glyphSubsetFile.empty())
    {
        const std::set<uint32_t> subset =
            parseGlyphSubset(assetFileSystem.readTextFile("fonts/truetype/" + glyphSubsetFile));
        codePoints.insert(subset.begin(), subset.end());
    }

    std::vector<GlyphRequest> requests;
    requests.reserve(codePoints.size());
    for (const uint32_t codePoint : codePoints)
    {
        if (FT_Load_Char(font.face, codePoint, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP) != 0)
        {
            continue;
        }
        const FT_GlyphSlot pGlyph = font.face->glyph;
        const float divisor = 64.0f * atlas.atlasScale;
        GlyphRequest request;
        request.codePoint = codePoint;
        request.metrics.leftSpacing = int(std::floor(pGlyph->metrics.horiBearingX / divisor));
        request.metrics.width = int(std::ceil((pGlyph->metrics.horiBearingX + pGlyph->metrics.width) / divisor))
            - request.metrics.leftSpacing;
        request.metrics.rightSpacing = int(std::round(pGlyph->advance.x / divisor)) - request.metrics.leftSpacing
            - request.metrics.width;
        // Keep fractional horizontal spacing while light hinting fits glyphs to the
        // target pixel size.
        request.metrics.outlineAdvance = pGlyph->linearHoriAdvance / 65536.0f;
        atlas.atlasCellWidth = std::max(atlas.atlasCellWidth, request.metrics.width);
        requests.push_back(request);
    }
    if (requests.empty())
    {
        error = "outline font has no bakeable glyphs: " + file;
        return std::nullopt;
    }

    if (FT_HAS_KERNING(font.face))
    {
        for (const GlyphRequest &previous : requests)
        {
            // Kerning only pairs the Latin block; CJK advances are uniform, and the quadratic walk would explode.
            if (previous.codePoint > 0x2ff)
            {
                continue;
            }
            for (const GlyphRequest &current : requests)
            {
                if (current.codePoint > 0x2ff)
                {
                    continue;
                }
                FT_Vector adjustment{};
                FT_Get_Kerning(font.face, FT_Get_Char_Index(font.face, previous.codePoint),
                               FT_Get_Char_Index(font.face, current.codePoint), FT_KERNING_UNFITTED, &adjustment);
                if (adjustment.x != 0)
                {
                    atlas.kerningPairs[uint64_t(previous.codePoint) << 32 | current.codePoint] =
                        adjustment.x / (64.0f * atlas.atlasScale);
                }
            }
        }
    }

    // Rasterize first, then size the cell from the ink that actually came back: hinted outlines can be a pixel wider
    // than their metrics, and CJK faces mix wide and narrow glyphs in one atlas.
    struct RasterizedGlyph
    {
        const GlyphRequest *pRequest = nullptr;
        std::vector<uint8_t> pixels;
        int width = 0;
        int rows = 0;
        int pitch = 0;
        int bitmapLeft = 0;
        int bitmapTop = 0;
    };

    std::vector<RasterizedGlyph> rasters;
    rasters.reserve(requests.size());
    int cellWidth = 1;
    int cellHeight = 1;

    for (const GlyphRequest &request : requests)
    {
        if (FT_Load_Char(font.face, request.codePoint, FT_LOAD_TARGET_LIGHT | FT_LOAD_NO_BITMAP) != 0 ||
            FT_Render_Glyph(font.face->glyph, FT_RENDER_MODE_NORMAL) != 0)
        {
            continue;
        }

        const FT_GlyphSlot pGlyph = font.face->glyph;
        const FT_Bitmap &raster = pGlyph->bitmap;

        if (raster.pixel_mode != FT_PIXEL_MODE_GRAY && raster.width != 0 && raster.rows != 0)
        {
            continue;
        }

        RasterizedGlyph glyph;
        glyph.pRequest = &request;
        glyph.width = int(raster.width);
        glyph.rows = int(raster.rows);
        glyph.pitch = int(std::abs(raster.pitch));
        glyph.bitmapLeft = pGlyph->bitmap_left;
        glyph.bitmapTop = pGlyph->bitmap_top;

        if (raster.width != 0 && raster.rows != 0)
        {
            glyph.pixels.resize(size_t(raster.width) * size_t(raster.rows));
            for (unsigned int row = 0; row < raster.rows; ++row)
            {
                const uint8_t *pRow = raster.buffer
                    + (raster.pitch >= 0 ? row : raster.rows - 1 - row) * size_t(std::abs(raster.pitch));
                std::copy_n(pRow, raster.width, glyph.pixels.begin() + ptrdiff_t(size_t(row) * raster.width));
            }
        }

        const int originX = atlas.atlasPadding - request.metrics.leftSpacing + glyph.bitmapLeft;
        const int originY = baseline + atlas.atlasPadding - glyph.bitmapTop;
        cellWidth = std::max(cellWidth, std::max(request.metrics.width + 2 * atlas.atlasPadding,
                                                 originX + glyph.width + atlas.atlasPadding));
        cellHeight = std::max(cellHeight, std::max(atlas.fontHeight + 2 * atlas.atlasPadding,
                                                   originY + glyph.rows + atlas.atlasPadding));
        rasters.push_back(std::move(glyph));
    }

    if (rasters.empty())
    {
        error = "outline font has no renderable glyphs: " + file;
        return std::nullopt;
    }

    const std::optional<AtlasLayout> layout =
        planAtlas(rasters.size(), cellWidth, cellHeight, atlas.atlasScale, error);
    if (!layout)
    {
        return std::nullopt;
    }
    atlas.atlasWidth = layout->width;
    atlas.atlasHeight = layout->height;
    allocateAtlas(image);

    for (size_t cellIndex = 0; cellIndex < rasters.size(); ++cellIndex)
    {
        const RasterizedGlyph &raster = rasters[cellIndex];
        const GlyphRequest &request = *raster.pRequest;
        const int scale = atlas.atlasScale;
        const int originX = (atlas.atlasPadding - request.metrics.leftSpacing) * scale + raster.bitmapLeft;
        const int originY = (baseline + atlas.atlasPadding) * scale - raster.bitmapTop;
        const int cellPixelWidth = cellWidth * scale;
        const int cellPixelHeight = cellHeight * scale;
        const int cellX = (int(cellIndex) % layout->columns) * cellPixelWidth;
        const int cellY = (int(cellIndex) / layout->columns) * cellPixelHeight;
        FontGlyph glyph;
        glyph.metrics = request.metrics;
        glyph.cellX = (int(cellIndex) % layout->columns) * cellWidth;
        glyph.cellY = (int(cellIndex) / layout->columns) * cellHeight;
        glyph.cellWidth = cellWidth;
        glyph.cellHeight = cellHeight;
        atlas.glyphs[request.codePoint] = glyph;

        for (int y = 0; y < raster.rows * scale; ++y)
        {
            for (int x = 0; x < raster.width * scale; ++x)
            {
                const uint8_t value = raster.pixels[size_t(y / scale) * size_t(raster.width) + size_t(x / scale)];
                // bitmapLeft 为负（或度量与墨迹不一致）时墨迹可能落到格子外；钳制到图集
                // 边界内，避免 size_t 回绕成巨大偏移导致越界写崩溃。
                const int destX = cellX + originX + x;
                const int destY = cellY + originY + y;
                if (destX < 0 || destX >= atlas.atlasWidth || destY < 0 || destY >= atlas.atlasHeight)
                {
                    continue;
                }
                const size_t offset = (size_t(destY) * atlas.atlasWidth + size_t(destX)) * 4;
                atlas.mainAtlasPixels[offset + 3] = value;
                // The renderer positions this mask one display pixel away; do not bake
                // a second offset into it.
                image.shadowPixels[offset + 3] = value;
            }
        }
    }

    return image;
}

std::optional<FontAtlasImage> loadFontAtlas(
    const AssetFileSystem &assetFileSystem,
    const std::vector<uint8_t> &bitmapBytes,
    const std::string &fontName,
    std::string &error)
{
    error.clear();
    const std::optional<BitmapFont> bitmap = parseBitmapFont(bitmapBytes);
    if (!bitmap)
    {
        error = "invalid legacy FNT";
        return std::nullopt;
    }

    FontAtlasImage image;
    FontAtlas &atlas = image.atlas;
    atlas.fontHeight = bitmap->fontHeight;
    atlas.atlasCellWidth = 1;
    for (int character = bitmap->firstChar; character <= bitmap->lastChar; ++character)
    {
        atlas.atlasCellWidth = std::max(atlas.atlasCellWidth, bitmap->glyphMetrics[character].width);
    }
    atlas.atlasScale = 4;
    atlas.pixelSized = false;
    // Includes reconstructed italic overhangs (notably Z-caron) and the one-pixel shadow.
    atlas.atlasPadding = 5;

    const std::string name = normalizedFontName(fontName);
    if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
    {
        error = "invalid TTF replacement name";
        return std::nullopt;
    }
    const std::string descriptorPath = "fonts/truetype/" + name + ".yml";
    const std::optional<std::string> descriptorText = assetFileSystem.readTextFile(descriptorPath);
    if (!descriptorText)
    {
        error = "missing TTF descriptor: " + descriptorPath;
        return std::nullopt;
    }

    std::string fileName;
    int baseline = 0;
    bool blackInk = false;
    bool synthesizeShadow = true;
    try
    {
        const YAML::Node descriptor = YAML::Load(*descriptorText);
        fileName = descriptor["file"].as<std::string>();
        baseline = descriptor["baseline"].as<int>();
        const std::string presentation = descriptor["presentation"].as<std::string>("tintable-with-shadow");
        const std::string encoding = descriptor["encoding"].as<std::string>();
        if (presentation != "tintable-with-shadow" && presentation != "black-ink" && presentation != "tintable")
        {
            error = "unsupported TTF presentation: " + descriptorPath;
            return std::nullopt;
        }
        blackInk = presentation == "black-ink";
        synthesizeShadow = presentation == "tintable-with-shadow";
        if (descriptor["logical_height"].as<int>() != atlas.fontHeight
            || (encoding != "windows-1252" && encoding != "utf-8")
            || baseline < 0 || baseline > atlas.fontHeight
            || fileName.empty() || fileName.find_first_of("/\\") != std::string::npos)
        {
            error = "TTF descriptor metrics, encoding or filename invalid: " + descriptorPath;
            return std::nullopt;
        }
    }
    catch (const YAML::Exception &exception)
    {
        error = "invalid TTF descriptor: " + descriptorPath + ": " + exception.what();
        return std::nullopt;
    }

    const std::string fontPath = "fonts/truetype/" + fileName;
    const std::optional<std::vector<uint8_t>> bytes = assetFileSystem.readBinaryFile(fontPath);
    if (!bytes || bytes->empty() || bytes->size() > 64 * 1024 * 1024)
    {
        error = "missing, empty or oversized TTF: " + fontPath;
        return std::nullopt;
    }
    FreeTypeFace font;
    if (FT_Init_FreeType(&font.library) != 0
        || FT_New_Memory_Face(font.library, bytes->data(), FT_Long(bytes->size()), 0, &font.face) != 0
        || !FT_IS_SCALABLE(font.face)
        || FT_Select_Charmap(font.face, FT_ENCODING_UNICODE) != 0
        || FT_Set_Pixel_Sizes(font.face, 0, atlas.fontHeight * atlas.atlasScale) != 0)
    {
        error = "FreeType could not open scalable Unicode font: " + fontPath;
        return std::nullopt;
    }

    std::vector<GlyphRequest> requests;
    requests.reserve(256);
    for (int character = bitmap->firstChar; character <= bitmap->lastChar; ++character)
    {
        GlyphRequest request;
        request.bitmapSlot = character;
        request.metrics = bitmap->glyphMetrics[character];
        const uint32_t codePoint = cp1252CodePoint(character);
        // Reserved byte slots are legacy UI symbols, outside the replacement's printable encoding.
        request.codePoint = codePoint != 0 ? codePoint : ReservedByteCodePointBase + uint32_t(character);
        requests.push_back(request);
    }

    const std::optional<AtlasLayout> layout = planAtlas(
        requests.size(), atlas.atlasCellWidth + 2 * atlas.atlasPadding, atlas.fontHeight + 2 * atlas.atlasPadding,
        atlas.atlasScale, error);
    if (!layout)
    {
        return std::nullopt;
    }
    atlas.atlasWidth = layout->width;
    atlas.atlasHeight = layout->height;
    allocateAtlas(image);

    for (size_t cellIndex = 0; cellIndex < requests.size(); ++cellIndex)
    {
        const GlyphRequest &request = requests[cellIndex];
        if (request.codePoint >= ReservedByteCodePointBase && request.codePoint <= ReservedByteCodePointBase + 0xff)
        {
            copyBitmapGlyph(*bitmap, request.bitmapSlot, int(cellIndex), layout->columns, image);
            FontGlyph glyph;
            glyph.metrics = request.metrics;
            glyph.cellX = atlasCellX(atlas, layout->columns, int(cellIndex));
            glyph.cellY = atlasCellY(atlas, layout->columns, int(cellIndex));
            glyph.cellWidth = request.metrics.width + 2 * atlas.atlasPadding;
            glyph.cellHeight = atlas.fontHeight + 2 * atlas.atlasPadding;
            atlas.glyphs[request.codePoint] = glyph;
            continue;
        }
        const FT_UInt glyphIndex = FT_Get_Char_Index(font.face, request.codePoint);
        if (glyphIndex == 0
            || FT_Load_Glyph(font.face, glyphIndex, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0
            || FT_Render_Glyph(font.face->glyph, FT_RENDER_MODE_NORMAL) != 0)
        {
            error = "TTF missing or unable to rasterize code point " + std::to_string(request.codePoint) + ": "
                + fontPath;
            return std::nullopt;
        }
        const FT_GlyphSlot pGlyph = font.face->glyph;
        const FT_Bitmap &raster = pGlyph->bitmap;
        const int scale = atlas.atlasScale;
        const int width = (request.metrics.width + 2 * atlas.atlasPadding) * scale;
        const int height = (atlas.fontHeight + 2 * atlas.atlasPadding) * scale;
        const int originX = pGlyph->bitmap_left + (atlas.atlasPadding - request.metrics.leftSpacing) * scale;
        const int originY = (baseline + atlas.atlasPadding) * scale - pGlyph->bitmap_top;
        const int cellX = (int(cellIndex) % layout->columns) * (atlas.atlasCellWidth + 2 * atlas.atlasPadding) * scale;
        const int cellY = (int(cellIndex) / layout->columns) * (atlas.fontHeight + 2 * atlas.atlasPadding) * scale;
        if (raster.width != 0 && raster.rows != 0
            && (raster.pixel_mode != FT_PIXEL_MODE_GRAY || originX < 0 || originY < 0
                || originX + int(raster.width) > width || originY + int(raster.rows) > height))
        {
            error = "TTF glyph exceeds padded legacy cell at code point "
                + std::to_string(request.codePoint) + ": " + fontPath;
            return std::nullopt;
        }
        FontGlyph glyph;
        glyph.metrics = request.metrics;
        glyph.cellX = atlasCellX(atlas, layout->columns, int(cellIndex));
        glyph.cellY = atlasCellY(atlas, layout->columns, int(cellIndex));
        glyph.cellWidth = request.metrics.width + 2 * atlas.atlasPadding;
        glyph.cellHeight = atlas.fontHeight + 2 * atlas.atlasPadding;
        atlas.glyphs[request.codePoint] = glyph;
        for (unsigned int y = 0; y < raster.rows; ++y)
        {
            const uint8_t *pRow = raster.buffer + (raster.pitch >= 0 ? y : raster.rows - 1 - y)
                * size_t(std::abs(raster.pitch));
            for (unsigned int x = 0; x < raster.width; ++x)
            {
                const size_t offset = (size_t(cellY + originY + y) * atlas.atlasWidth + cellX + originX + x) * 4;
                // Black-ink sources use the native black layer at the original coordinates.
                // Preserve caller offsets and avoid adding a second, shifted copy of the ink.
                if (blackInk)
                {
                    image.shadowPixels[offset + 3] = pRow[x];
                    continue;
                }
                atlas.mainAtlasPixels[offset + 3] = pRow[x];
                // Legacy HUD callers expect a shadow already shifted one logical pixel.
                if (synthesizeShadow && originX + int(x) + scale < width && originY + int(y) + scale < height)
                {
                    const size_t shadowOffset = offset + (size_t(scale) * atlas.atlasWidth + scale) * 4;
                    image.shadowPixels[shadowOffset + 3] = pRow[x];
                }
            }
        }
    }
    return image;
}
}
