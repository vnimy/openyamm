#include "doctest/doctest.h"

#include "game/debug/AiAdvicePrompt.h"
#include "game/party/Party.h"

#include <string>
#include <vector>

namespace
{
OpenYAMM::Game::Character makeAdviceTestMember(const std::string &name, const std::string &className, uint32_t level)
{
    OpenYAMM::Game::Character member = {};
    member.name = name;
    member.className = className;
    member.role = className;
    member.level = level;
    member.skillPoints = 7;
    member.might = 25;
    member.intellect = 8;
    member.personality = 9;
    member.endurance = 21;
    member.speed = 15;
    member.accuracy = 18;
    member.luck = 11;
    member.maxHealth = 150;
    member.health = 120;
    member.maxSpellPoints = 0;
    member.spellPoints = 0;
    return member;
}

void addTestSkill(OpenYAMM::Game::Character &member, const std::string &name, uint32_t level,
    OpenYAMM::Game::SkillMastery mastery)
{
    OpenYAMM::Game::CharacterSkill skill = {};
    skill.name = name;
    skill.level = level;
    skill.mastery = mastery;
    member.skills[name] = skill;
}
}

TEST_CASE("AI advice prompt carries the party and progress state")
{
    OpenYAMM::Game::Party party;
    OpenYAMM::Game::Character knight = makeAdviceTestMember("测试甲", "Knight", 12);
    addTestSkill(knight, "Sword", 12, OpenYAMM::Game::SkillMastery::Grandmaster);
    addTestSkill(knight, "Bow", 6, OpenYAMM::Game::SkillMastery::Expert);
    REQUIRE(party.recruitCharacter(knight));
    party.addGold(1200);
    party.addFood(30);

    const std::string prompt = OpenYAMM::Game::buildAiAdvicePrompt(
        party, "mm7", "7out01.odm", {"喷泉：可以恢复法力"});

    // The instruction the model reads first.
    CHECK(prompt.find("【这份数据的范围】") != std::string::npos);
    CHECK(prompt.find("数据如下：") != std::string::npos);

    // Progress state.
    CHECK(prompt.find("## 当前进度") != std::string::npos);
    CHECK(prompt.find("- 世界：mm7") != std::string::npos);
    CHECK(prompt.find("- 地图：7out01.odm") != std::string::npos);
    CHECK(prompt.find("- 金币：1200") != std::string::npos);
    CHECK(prompt.find("- 食物：30") != std::string::npos);
    CHECK(prompt.find("- 队伍人数：1") != std::string::npos);

    // The member's own numbers.
    CHECK(prompt.find("### 1. 测试甲") != std::string::npos);
    CHECK(prompt.find("属性：力量 25 / 智力 8") != std::string::npos);
    // Recruiting recomputes the base resources, so the expectation is read back from the recruited member.
    const OpenYAMM::Game::Character &recruited = party.members().front();
    CHECK(prompt.find("- 生命 " + std::to_string(recruited.health) + "/") != std::string::npos);
    CHECK(prompt.find("技能点 7") != std::string::npos);

    // Both skills are listed, in a stable (sorted) order.
    const size_t bowIndex = prompt.find("Bow 6");
    const size_t swordIndex = prompt.find("Sword 12");
    REQUIRE(bowIndex != std::string::npos);
    REQUIRE(swordIndex != std::string::npos);
    CHECK(bowIndex < swordIndex);

    // Unlocked autonotes are passed through as task clues.
    CHECK(prompt.find("## 已解锁的自动笔记") != std::string::npos);
    CHECK(prompt.find("- 喷泉：可以恢复法力") != std::string::npos);
}

TEST_CASE("AI advice prompt survives an empty party and multi-line autonotes")
{
    OpenYAMM::Game::Party party;

    const std::string prompt = OpenYAMM::Game::buildAiAdvicePrompt(
        party, "mm6", "6out01.odm", {"第一行\n第二行"});

    CHECK(prompt.find("- 队伍人数：0") != std::string::npos);
    CHECK(prompt.find("（队伍里还没有人。）") != std::string::npos);
    // A newline inside a note must not break the list: the note stays one line.
    CHECK(prompt.find("- 第一行 第二行") != std::string::npos);
}

TEST_CASE("AI advice prompt omits the autonote section when nothing is unlocked")
{
    OpenYAMM::Game::Party party;

    const std::string prompt = OpenYAMM::Game::buildAiAdvicePrompt(party, "mm8", "8out01.odm", {});

    CHECK(prompt.find("## 已解锁的自动笔记") == std::string::npos);
}
