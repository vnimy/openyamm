#include "game/debug/AiAdvicePrompt.h"

#include "game/party/Party.h"
#include "game/party/SkillData.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
namespace
{
// The instruction half of the prompt. It is Chinese on purpose: this feature asks a model for advice a player reads
// in the game's own language, the same way the standalone save tool asks for it.
constexpr std::string_view PromptIntroduction = R"(你是一位《魔法门 6/7/8》(MMerge) 的老玩家兼战术顾问。下面是一份从正在进行的这一局游戏里
即时导出的数据，职业名、技能名与熟练度名就是游戏内显示的中文。

【语言要求 · 必须遵守】
- 全部回答使用简体中文，包括技能名、职业名、属性名与地图名。
- 只依据给出的数据作答；需要推断时明确标注「推断」，不要编造数据里没有的信息。

请输出以下五节：
1. 队伍总评：这套队伍的强项与短板（每人一句话），以及当前最紧迫的一个问题。
2. 逐人成长路线：每人 3 步，按优先级排序；每步写清做什么（升哪个技能到什么熟练度、换什么装备、
   用哪个背包物品）、在哪里做、大约要花多少金币或技能点。
3. 短期行动清单：结合当前地图与已解锁的自动笔记，5-8 条可勾选执行。
4. 风险与浪费：没学的关键技能、穿不上的好装备、空着的装备位、金币与食物储备、异常状态。
5. 任务下一步与优先级：按自动笔记逐条给出下一步具体动作，并标注依据来自数据还是你的推断。

【这份数据的范围】
- 包含：队伍每人的职业、等级、属性、生命法力技能点、技能与熟练度，金币、食物、当前地图、已解锁的自动笔记。
- 不包含：背包与装备明细、法术书、各地图的运行时状态（怪物、物件、门）。问到这些时请直说数据里没有。

数据如下：
)";

std::string singleLine(const std::string &text)
{
    std::string result = text;

    for (char &character : result)
    {
        if (character == '\n' || character == '\r' || character == '\t')
        {
            character = ' ';
        }
    }

    return result;
}

void appendSkills(std::ostringstream &out, const Character &member)
{
    std::vector<std::string> skills;

    for (const std::pair<const std::string, CharacterSkill> &entry : member.skills)
    {
        const CharacterSkill &skill = entry.second;
        const std::string skillName = displaySkillName(skill.name.empty() ? entry.first : skill.name);
        skills.push_back(
            skillName + " " + std::to_string(skill.level) + "（" + localizedMasteryName(skill.mastery) + "）");
    }

    if (skills.empty())
    {
        out << "- 技能：无\n";
        return;
    }

    // The skill map is unordered; sorting keeps the pasted prompt stable between two clicks.
    std::sort(skills.begin(), skills.end());
    out << "- 技能：" << skills.front();

    for (size_t index = 1; index < skills.size(); ++index)
    {
        out << "、" << skills[index];
    }

    out << '\n';
}

void appendMember(std::ostringstream &out, const Character &member, size_t memberIndex)
{
    const std::string className = displayClassName(member.className.empty() ? member.role : member.className);

    out << "\n### " << memberIndex << ". " << member.name << "（" << className << " " << member.level << " 级）\n\n";
    out << "- 属性：力量 " << member.might
        << " / 智力 " << member.intellect
        << " / 个性 " << member.personality
        << " / 耐力 " << member.endurance
        << " / 速度 " << member.speed
        << " / 准确 " << member.accuracy
        << " / 幸运 " << member.luck << '\n';
    out << "- 生命 " << member.health << "/" << Party::effectiveMaximumHealth(member)
        << "，法力 " << member.spellPoints << "/" << Party::effectiveMaximumSpellPoints(member)
        << "，技能点 " << member.skillPoints << '\n';
    appendSkills(out, member);
}
}

std::string buildAiAdvicePrompt(
    const Party &party,
    std::string_view worldId,
    std::string_view mapFileName,
    const std::vector<std::string> &autonotes)
{
    std::ostringstream out;
    out << PromptIntroduction;

    out << "\n## 当前进度\n\n";
    out << "- 世界：" << worldId << '\n';
    out << "- 地图：" << mapFileName << '\n';
    out << "- 金币：" << party.gold() << '\n';
    out << "- 食物：" << party.food() << '\n';
    out << "- 队伍人数：" << party.memberCount() << '\n';

    if (party.memberCount() == 0)
    {
        out << "\n（队伍里还没有人。）\n";
    }

    size_t memberIndex = 0;

    for (const Character &member : party.members())
    {
        appendMember(out, member, ++memberIndex);
    }

    if (!autonotes.empty())
    {
        out << "\n## 已解锁的自动笔记\n\n";

        for (const std::string &note : autonotes)
        {
            out << "- " << singleLine(note) << '\n';
        }
    }

    return out.str();
}
}
