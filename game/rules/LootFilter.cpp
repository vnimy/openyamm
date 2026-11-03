#include "game/rules/LootFilter.h"

#include "game/ui/UiText.h"

#include <algorithm>

namespace OpenYAMM::Game
{
namespace
{
// The study's presets (level_generation/ui/maw_loot_progress_20261025/index.html PRESETS), by RulesetLootFilterPreset.
const std::array<LootFilterPresetDefinition, 6> Presets = {{
    {"Every drop stays an item.", 0, {}},
    {"Plain Common weapons and armour only. A first game.", 1, {{{1, 30}, {1, 30}, {0, 0}}}},
    {"Common and Uncommon gear; Rare once 20 levels behind.", 2, {{{2, 20}, {2, 20}, {1, 20}}}},
    {"Gear up to Rare, Epic once 20 levels behind; scrolls too.", 3, {{{3, 20}, {3, 20}, {2, 20}}}},
    {"Only Ancient and Primordial gear stays; wands and scrolls go. Late game.", 4, {{{4, 30}, {4, 30}, {3, 20}}}},
    {"Your own rules, from the preset you had.", -1, {}},
}};
}

const LootFilterPresetDefinition &lootFilterPreset(RulesetLootFilterPreset preset)
{
    return Presets[std::min<size_t>(size_t(preset), Presets.size() - 1)];
}

const char *lootFilterGroupName(LootFilterGroup group)
{
    constexpr std::array<const char *, LootFilterGroupCount> Names = {"Weapons", "Armour", "Jewellery"};
    static std::string translated;
    translated = UiText::translate(Names[size_t(group)]);
    return translated.c_str();
}

const char *lootFilterRarityName(int rarity)
{
    constexpr std::array<const char *, LootFilterRarityCount> Names = {
        "Common", "Uncommon", "Rare", "Epic", "Ancient", "Primordial"};
    static std::string translated;
    translated = UiText::translate(rarity >= 1 && rarity <= LootFilterRarityCount ? Names[size_t(rarity - 1)] : "");
    return translated.c_str();
}

const char *lootFilterKindName(RulesetLootKind kind)
{
    constexpr std::array<const char *, RulesetLootKindCount> Names = {"Wands", "Scrolls", "Potions", "Reagents", "Gems"};
    static std::string translated;
    translated = UiText::translate(Names[size_t(kind)]);
    return translated.c_str();
}

std::string lootFilterCellText(RulesetLootFilterPreset preset, LootFilterGroup group, int rarity)
{
    const LootFilterPresetColumn &column = lootFilterPreset(preset).columns[size_t(group)];

    if (rarity <= column.goldUpTo)
    {
        return UiText::translate("gold");
    }

    if (rarity == column.goldUpTo + 1 && column.levelGap > 0)
    {
        return UiText::translateTemplate("gold if {levels}+ levels below",
            {{"{levels}", std::to_string(column.levelGap)}});
    }

    return UiText::translate("keep");
}

LootFilterVerdict evaluateLootFilter(const RulesetLootFilter &filter, const LootFilterFacts &facts, int partyLevel)
{
    bool filtered = false;

    if (facts.alwaysKept)
    {
        return LootFilterVerdict::Keep;
    }

    if (facts.kind)
    {
        filtered = filter.turnsKindIntoGold(*facts.kind);
    }
    else if (facts.group && facts.rarity > 0)
    {
        const LootFilterPresetColumn &column = lootFilterPreset(filter.preset).columns[size_t(*facts.group)];
        filtered = facts.rarity <= column.goldUpTo
            || (facts.rarity == column.goldUpTo + 1 && column.levelGap > 0 && facts.itemLevel > 0
                && partyLevel - facts.itemLevel >= column.levelGap);
    }

    if (!filtered)
    {
        return LootFilterVerdict::Keep;
    }

    return filter.leaveOnGround ? LootFilterVerdict::Ground : LootFilterVerdict::Gold;
}

void RecentLootDrops::add(const RecentLootDrop &drop)
{
    m_drops.push_back(drop);

    while (m_drops.size() > MaxDrops)
    {
        m_drops.pop_front();
    }
}

const std::deque<RecentLootDrop> &RecentLootDrops::drops() const
{
    return m_drops;
}

LootFilterTrial tryLootFilter(const RulesetLootFilter &filter, const std::deque<RecentLootDrop> &drops)
{
    LootFilterTrial trial;
    trial.drops = drops.size();

    for (const RecentLootDrop &drop : drops)
    {
        if (evaluateLootFilter(filter, drop.facts, drop.partyLevel) != LootFilterVerdict::Keep)
        {
            ++trial.filtered;
            trial.gold += drop.gold;
        }
    }

    return trial;
}
}
