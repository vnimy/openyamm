#include "game/debug/ImGuiCjkFont.h"

#include "engine/AssetFileSystem.h"

#include <imgui.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
namespace
{
// 汉化补丁的字形子集只收基本多文种平面，ImWchar（16 位）装得下。
constexpr unsigned long MaxCodePoint = 0xFFFF;
constexpr ImWchar FirstLatinCodePoint = 0x20;
constexpr ImWchar LastLatinCodePoint = 0xFF;
constexpr float MinConsoleFontPixelHeight = 10.0f;
constexpr float MaxConsoleFontPixelHeight = 80.0f;

std::vector<ImWchar> sortedCodePoints(const std::string &glyphList)
{
    std::vector<ImWchar> codePoints;

    size_t start = 0;

    while (start < glyphList.size())
    {
        size_t end = glyphList.find('\n', start);

        if (end == std::string::npos)
        {
            end = glyphList.size();
        }

        std::string line = glyphList.substr(start, end - start);
        start = end + 1;

        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        char *pEnd = nullptr;
        const unsigned long value = std::strtoul(line.c_str(), &pEnd, 16);

        if (pEnd == line.c_str() || value == 0 || value > MaxCodePoint)
        {
            continue;
        }

        codePoints.push_back(ImWchar(value));
    }

    std::sort(codePoints.begin(), codePoints.end());
    codePoints.erase(std::unique(codePoints.begin(), codePoints.end()), codePoints.end());
    return codePoints;
}

// ImGui 的区间表要成对的 [first, last] 并以 0 结尾；相邻码位并成一段，表因此比逐字列举小得多。
std::vector<ImWchar> buildGlyphRanges(const std::string &glyphList)
{
    const std::vector<ImWchar> codePoints = sortedCodePoints(glyphList);
    std::vector<ImWchar> ranges;
    ranges.reserve(codePoints.size() * 2 + 5);
    ranges.push_back(FirstLatinCodePoint);
    ranges.push_back(LastLatinCodePoint);

    for (size_t index = 0; index < codePoints.size();)
    {
        size_t runEnd = index + 1;

        while (runEnd < codePoints.size() && codePoints[runEnd] == ImWchar(codePoints[runEnd - 1] + 1))
        {
            ++runEnd;
        }

        ranges.push_back(codePoints[index]);
        ranges.push_back(codePoints[runEnd - 1]);
        index = runEnd;
    }

    ranges.push_back(0);
    return ranges;
}
}

void installImGuiCjkFont(const OpenYAMM::Engine::AssetFileSystem &assetFileSystem, float pixelHeight)
{
    // ImGui 不接管这份内存（FontDataOwnedByAtlas = false），缓冲要活得和进程一样久。
    static std::optional<std::vector<uint8_t>> s_fontBytes;
    static std::vector<ImWchar> s_glyphRanges;

    if (!s_fontBytes)
    {
        s_fontBytes = assetFileSystem.readBinaryFile("fonts/truetype/openyamm_cjk.ttf");
    }

    if (!s_fontBytes || s_fontBytes->empty())
    {
        return;
    }

    if (s_glyphRanges.empty())
    {
        const std::optional<std::string> glyphList = assetFileSystem.readTextFile("fonts/truetype/cjk_glyphs.txt");
        s_glyphRanges = buildGlyphRanges(glyphList ? *glyphList : std::string {});
    }

    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = 1;
    config.OversampleV = 1;
    // 中日韩字形宽窄一致，像素吸附反而让混排的字距忽宽忽窄。
    config.PixelSnapH = false;

    const ImWchar *pRanges = s_glyphRanges.size() > 2 ? s_glyphRanges.data()
                                                      : io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
    const float bakedHeight = std::clamp(pixelHeight, MinConsoleFontPixelHeight, MaxConsoleFontPixelHeight);
    io.Fonts->AddFontFromMemoryTTF(
        s_fontBytes->data(), int(s_fontBytes->size()), bakedHeight, &config, pRanges);
}
}
