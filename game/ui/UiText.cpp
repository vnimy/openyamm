#include "UiText.h"

#include "engine/AssetFileSystem.h"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
std::unordered_map<std::string, std::string> s_overrides;
// 以 * 结尾的条目按前缀匹配，覆盖源码里拼接版本号等参数的动态文案。
std::vector<std::pair<std::string, std::string>> s_prefixOverrides;
// 键统一成 ASCII 引号后的副本，供整串未命中时兜底查询。
std::unordered_map<std::string, std::string> s_normalizedOverrides;
bool s_loaded = false;

// 内容包里的英文正文混用了 ASCII 引号、UTF-8 弯引号，以及 MM7 早期导出时直接写进 lua 的
// CP1252 单字节（0x91/0x92/0x96/0x97/0x85 等）。同一句话因此有若干种字节写法，字形表还会把
// 孤立字节画成方块。这里把「能构成合法 UTF-8 的序列原样保留，形不成序列的孤立字节按 CP1252
// 还原成人可读字符」，既保住中文等多字节内容，又让同一句话归一成同一形态。
std::string normalizeQuotes(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t index = 0; index < text.size();)
    {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        if (lead < 0x80)
        {
            out += char(lead);
            ++index;
            continue;
        }

        size_t length = 0;
        if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
        }
        bool validSequence = length > 0 && index + length <= text.size();
        for (size_t offset = 1; validSequence && offset < length; ++offset)
        {
            validSequence = (static_cast<unsigned char>(text[index + offset]) & 0xC0) == 0x80;
        }
        if (validSequence)
        {
            out.append(text, index, length);
            index += length;
            continue;
        }

        switch (lead)
        {
        case 0x91:
        case 0x92:
            out += '\'';
            break;
        case 0x93:
        case 0x94:
            out += '"';
            break;
        case 0x85:
            out += "\xE2\x80\xA6"; // …
            break;
        case 0x96:
            out += "\xE2\x80\x93"; // –
            break;
        case 0x97:
            out += "\xE2\x80\x94"; // —
            break;
        case 0xA0:
            out += ' ';
            break;
        default:
            // 其余按 Latin-1 解成 UTF-8；0x80..0x9F 的 C1 控制字符直接丢掉。
            if (lead >= 0xA0)
            {
                out += char(0xC0 | (lead >> 6));
                out += char(0x80 | (lead & 0x3F));
            }
            break;
        }
        ++index;
    }
    return out;
}

// 表里用 \n 表示换行，便于在单行内写出多行文案。
std::string unescape(const std::string &text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n')
        {
            out += '\n';
            ++i;
        }
        else
        {
            out += text[i];
        }
    }
    return out;
}
}

void UiText::load(const OpenYAMM::Engine::AssetFileSystem &assetFileSystem)
{
    s_loaded = true;
    if (!s_overrides.empty())
    {
        return;
    }
    const std::optional<std::string> content = assetFileSystem.readTextFile("data_tables/english/ui_text.txt");
    if (!content || content->empty())
    {
        return;
    }
    size_t start = 0;
    while (start < content->size())
    {
        size_t end = content->find('\n', start);
        if (end == std::string::npos)
        {
            end = content->size();
        }
        std::string line = content->substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        const size_t tab = line.find('\t');
        if (tab == std::string::npos)
        {
            continue;
        }
        const std::string key = line.substr(0, tab);
        const std::string value = line.substr(tab + 1);
        if (key.empty())
        {
            continue;
        }
        // 原文里也可能带 \n（界面上的多行文案），键与值同样需要还原转义。
        if (!key.empty() && key.back() == '*')
        {
            s_prefixOverrides.emplace_back(unescape(key.substr(0, key.size() - 1)), unescape(value));
        }
        else
        {
            s_overrides[unescape(key)] = unescape(value);
        }
    }

    // 弯引号归一后的兜底表：表里的键一律写 ASCII 引号，运行时正文常带弯引号，
    // 所以每个键都要归一后入表，整串未命中时按归一形态命中。
    for (const auto &[key, value] : s_overrides)
    {
        s_normalizedOverrides.emplace(normalizeQuotes(key), value);
    }
}

std::string UiText::translate(const std::string &text)
{
    const auto found = s_overrides.find(text);
    if (found != s_overrides.end())
    {
        return found->second;
    }
    // 内容包里的英文正文混用了 ASCII 撇号与各种弯引号（MM7 的 lua 甚至直接带 0x91/0x92 原字节），
    // 同一句话的字节写法并不统一。整串没命中时按弯引号归一后的形态再查一次。
    const auto normalized = s_normalizedOverrides.find(normalizeQuotes(text));
    if (normalized != s_normalizedOverrides.end())
    {
        return normalized->second;
    }
    for (const auto &prefix : s_prefixOverrides)
    {
        if (text.size() >= prefix.first.size() && text.compare(0, prefix.first.size(), prefix.first) == 0)
        {
            // 前缀规则形如「Hit Points : 」+ 数值，后半段自己也可能要译（「Condition: Good」里的 Good），
            // 递归查一次，剩下的串必然更短，不会打转。
            return prefix.second + translate(text.substr(prefix.first.size()));
        }
    }
    return text;
}

std::string UiText::translateInContext(const std::string &context, const std::string &text)
{
    if (context.empty() || text.empty())
    {
        return translate(text);
    }

    const std::string key = context + ": " + text;
    const std::string translated = translate(key);
    return translated != key ? translated : translate(text);
}

std::string UiText::translateTemplate(
    const std::string &text, std::initializer_list<std::pair<std::string, std::string>> params)
{
    std::string out = translate(text);
    for (const auto &param : params)
    {
        const std::string placeholder = param.first;
        const std::string value = param.second;
        size_t pos = 0;
        while ((pos = out.find(placeholder, pos)) != std::string::npos)
        {
            out.replace(pos, placeholder.size(), value);
            pos += value.size();
        }
    }
    return out;
}

bool UiText::hasOverrides()
{
    return !s_overrides.empty() || !s_prefixOverrides.empty();
}

std::string UiText::titleWithName(const std::string &name, const std::string &title)
{
    if (title.empty())
    {
        return name;
    }

    if (!hasOverrides())
    {
        return name + " the " + title;
    }

    return translate(name) + "·" + translate(title);
}

namespace
{
bool s_unresolvedReportEnabled = false;
std::vector<std::string> s_unresolvedTexts;
// 上限只防跑飞：这份清单是给人看的，不是给机器跑全量的。
constexpr size_t MaxUnresolvedTexts = 400;

bool hasLatinRun(const std::string &text)
{
    size_t run = 0;

    for (const char character : text)
    {
        const bool isLatin = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        run = isLatin ? run + 1 : 0;

        if (run >= 2)
        {
            return true;
        }
    }

    return false;
}
}

void UiText::setUnresolvedReportEnabled(bool enabled)
{
    s_unresolvedReportEnabled = enabled;
}

bool UiText::unresolvedReportEnabled()
{
    return s_unresolvedReportEnabled;
}

void UiText::reportUnresolvedUiText(const std::string &text)
{
    if (!s_unresolvedReportEnabled || s_unresolvedTexts.size() >= MaxUnresolvedTexts || !hasLatinRun(text))
    {
        return;
    }

    if (std::find(s_unresolvedTexts.begin(), s_unresolvedTexts.end(), text) != s_unresolvedTexts.end())
    {
        return;
    }

    s_unresolvedTexts.push_back(text);
}

std::vector<std::string> UiText::takeUnresolvedUiText()
{
    std::vector<std::string> taken;
    taken.swap(s_unresolvedTexts);
    return taken;
}
