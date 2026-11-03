#include "game/ui/UiText.h"
#include "game/party/SkillData.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>

namespace OpenYAMM::Game
{
namespace
{
std::string trimCopy(const std::string &value)
{
    size_t start = 0;

    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
    {
        ++start;
    }

    size_t end = value.size();

    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
    {
        --end;
    }

    return value.substr(start, end - start);
}

std::string canonicalLookupKey(const std::string &value)
{
    const std::string trimmed = trimCopy(value);
    std::string result;

    for (unsigned char character : trimmed)
    {
        if (std::isalnum(character))
        {
            result.push_back(static_cast<char>(std::tolower(character)));
        }
    }

    return result;
}

// MMMerge's classes, then MAW's four class lines (ids 53-64), which only a ruleset's class tables make playable.
const std::array<std::string, 65> CanonicalClassNames = {
    "Archer",
    "WarriorMage",
    "MasterArcher",
    "Sniper",
    "Cleric",
    "Priest",
    "PriestLight",
    "PriestDark",
    "DarkElf",
    "Patriarch",
    "Dragon",
    "GreatWyrm",
    "Druid",
    "GreatDruid",
    "Warlock",
    "ArchDruid",
    "Knight",
    "Cavalier",
    "BlackKnight",
    "Champion",
    "Minotaur",
    "MinotaurLord",
    "Monk",
    "Initiate",
    "Master",
    "Ninja",
    "Paladin",
    "Crusader",
    "Hero",
    "Villain",
    "Ranger",
    "Hunter",
    "BountyHunter",
    "RangerLord",
    "Thief",
    "Rogue",
    "Assassin",
    "Spy",
    "Troll",
    "WarTroll",
    "Vampire",
    "Nosferatu",
    "Sorcerer",
    "Wizard",
    "Necromancer",
    "Lich",
    "ArchMage",
    "DarkArchMage",
    "Peasant",
    "n/u",
    "HighPriest",
    "MasterWizard",
    "None",
    "Seraphim",
    "Angel",
    "Archangel",
    "DeathKnight",
    "DeathLord",
    "LichKing",
    "Shaman",
    "Sekhem",
    "Marabou",
    "Elementalist",
    "ElementalAdept",
    "MasterOfElements",
};

// Skill ids are positions in this list. The 40 classic skills keep their MMerge ids (teacher topics name skills by
// id); the technology skills are appended from FirstTechnologySkillId and never renumber the classic ones, and the
// ruleset skills follow from FirstRulesetSkillId.
const std::array<std::string, 49> CanonicalSkillNames = {
    "Staff",
    "Sword",
    "Dagger",
    "Axe",
    "Spear",
    "Bow",
    "Mace",
    "Blaster",
    "Shield",
    "LeatherArmor",
    "ChainArmor",
    "PlateArmor",
    "FireMagic",
    "AirMagic",
    "WaterMagic",
    "EarthMagic",
    "SpiritMagic",
    "MindMagic",
    "BodyMagic",
    "LightMagic",
    "DarkMagic",
    "DarkElfAbility",
    "VampireAbility",
    "DragonAbility",
    "IdentifyItem",
    "Merchant",
    "RepairItem",
    "Bodybuilding",
    "Meditation",
    "Perception",
    "Regeneration",
    "DisarmTraps",
    "Dodging",
    "Unarmed",
    "IdentifyMonster",
    "Armsmaster",
    "Stealing",
    "Alchemy",
    "Learning",
    "Throwing",
    "EnergyGuns",
    "EnergyBlades",
    "ShieldTech",
    "Hacking",
    "Cover",
    "ManaShield",
    "Enlightenment",
    "Retaliation",
    "Cybernetics",
};

const std::vector<std::string> WeaponSheetSkillNames = {
    "Axe", "Bow", "Throwing", "Dagger", "Mace", "Spear", "Staff", "Sword", "Unarmed", "Blaster",
};

// The racial abilities are cast like spells and follow the schools, as in MM8.
const std::vector<std::string> MagicSheetSkillNames = {
    "FireMagic", "AirMagic", "WaterMagic", "EarthMagic", "SpiritMagic", "MindMagic", "BodyMagic", "LightMagic",
    "DarkMagic", "DarkElfAbility", "VampireAbility", "DragonAbility",
};

// A ruleset's Cover, Retaliation and Mana Shield follow the armour skills, as in MAW (zzMAW-Skills.lua:2185-2214).
const std::vector<std::string> ArmorSheetSkillNames = {
    "LeatherArmor", "ChainArmor", "PlateArmor", "Shield", "Dodging", "Cover", "Retaliation", "ManaShield",
};

const std::vector<std::string> MiscSheetSkillNames = {
    "Alchemy", "Armsmaster", "Bodybuilding", "IdentifyItem", "IdentifyMonster", "Learning", "DisarmTraps",
    "Meditation", "Merchant", "Perception", "RepairItem", "Stealing",
};

const std::vector<std::string> TechnologySheetSkillNames = {
    "EnergyGuns", "EnergyBlades", "ShieldTech", "Hacking", "Cybernetics",
};

const std::unordered_map<std::string, std::string> ClassAliases = {
    {"cleric2", "Priest"},
    {"priestoflight", "PriestLight"},
    {"priestofthelight", "PriestLight"},
    {"priestofdark", "PriestDark"},
    {"priestofthedark", "PriestDark"},
    {"darkelf2", "Patriarch"},
    {"dragon2", "GreatWyrm"},
    {"knight2", "Champion"},
    {"minotaur2", "MinotaurLord"},
    {"troll2", "WarTroll"},
    {"vampire2", "Nosferatu"},
    {"necromancer2", "Lich"},
};

const std::unordered_map<std::string, std::string> SkillAliases = {
    {"thrown", "Throwing"},
    {"throwingweapon", "Throwing"},
    {"throwingweapons", "Throwing"},
    {"leather", "LeatherArmor"},
    {"chain", "ChainArmor"},
    {"plate", "PlateArmor"},
    {"fire", "FireMagic"},
    {"air", "AirMagic"},
    {"water", "WaterMagic"},
    {"earth", "EarthMagic"},
    {"spirit", "SpiritMagic"},
    {"mind", "MindMagic"},
    {"body", "BodyMagic"},
    {"light", "LightMagic"},
    {"dark", "DarkMagic"},
    {"darkelf", "DarkElfAbility"},
    {"vampire", "VampireAbility"},
    {"dragon", "DragonAbility"},
    {"iditem", "IdentifyItem"},
    {"identifyitem", "IdentifyItem"},
    {"repair", "RepairItem"},
    {"repairitem", "RepairItem"},
    {"bbuilding", "Bodybuilding"},
    {"bodybuilding", "Bodybuilding"},
    {"meditate", "Meditation"},
    {"meditation", "Meditation"},
    {"disarm", "DisarmTraps"},
    {"disarmtrap", "DisarmTraps"},
    {"disarmtraps", "DisarmTraps"},
    {"identmon", "IdentifyMonster"},
    {"identifymonster", "IdentifyMonster"},
    {"lightmagic", "LightMagic"},
    {"darkmagic", "DarkMagic"},
};

const std::unordered_map<std::string, std::string> DisplayClassNames = {
    {"WarriorMage", "Warrior Mage"},
    {"MasterArcher", "Master Archer"},
    {"PriestLight", "Priest of the Light"},
    {"PriestDark", "Priest of the Dark"},
    {"DeathKnight", "Death Knight"},
    {"DeathLord", "Death Lord"},
    {"LichKing", "Lich King"},
    {"ElementalAdept", "Elemental Adept"},
    {"MasterOfElements", "Master of Elements"},
    {"DarkElf", "Dark Elf"},
    {"GreatWyrm", "Great Wyrm"},
    {"GreatDruid", "Great Druid"},
    {"ArchDruid", "Arch Druid"},
    {"BlackKnight", "Black Knight"},
    {"MinotaurLord", "Minotaur Lord"},
    {"BountyHunter", "Bounty Hunter"},
    {"RangerLord", "Ranger Lord"},
    {"WarTroll", "War Troll"},
    {"ArchMage", "Arch Mage"},
    {"DarkArchMage", "Dark ArchMage"},
    {"HighPriest", "High Priest"},
    {"MasterWizard", "Master Wizard"},
};

const std::unordered_map<std::string, std::string> DisplaySkillNames = {
    {"Throwing", "Throwing Weapons"},
    {"LeatherArmor", "Leather Armor"},
    {"ChainArmor", "Chain Armor"},
    {"PlateArmor", "Plate Armor"},
    {"FireMagic", "Fire Magic"},
    {"AirMagic", "Air Magic"},
    {"WaterMagic", "Water Magic"},
    {"EarthMagic", "Earth Magic"},
    {"SpiritMagic", "Spirit Magic"},
    {"MindMagic", "Mind Magic"},
    {"BodyMagic", "Body Magic"},
    {"LightMagic", "Light Magic"},
    {"DarkMagic", "Dark Magic"},
    {"DarkElfAbility", "Dark Elf Ability"},
    {"VampireAbility", "Vampire Ability"},
    {"DragonAbility", "Dragon Ability"},
    {"IdentifyItem", "Identify Item"},
    {"RepairItem", "Repair Item"},
    {"Bodybuilding", "Body Building"},
    {"IdentifyMonster", "Identify Monster"},
    {"DisarmTraps", "Disarm Traps"},
    {"EnergyGuns", "Energy Guns"},
    {"EnergyBlades", "Energy Blades"},
    {"ShieldTech", "Shield Tech"},
    {"ManaShield", "Mana Shield"},
};

std::map<std::string, std::string> RulesetDisplaySkillNames;

std::unordered_map<std::string, std::string> buildCanonicalClassLookup()
{
    std::unordered_map<std::string, std::string> lookup = ClassAliases;

    for (const std::string &candidate : CanonicalClassNames)
    {
        lookup.try_emplace(canonicalLookupKey(candidate), candidate);
    }

    return lookup;
}

std::unordered_map<std::string, std::string> buildCanonicalSkillLookup()
{
    std::unordered_map<std::string, std::string> lookup = SkillAliases;

    for (const std::string &candidate : CanonicalSkillNames)
    {
        lookup.try_emplace(canonicalLookupKey(candidate), candidate);
    }

    return lookup;
}

const std::unordered_map<std::string, std::string> CanonicalClassLookup = buildCanonicalClassLookup();
const std::unordered_map<std::string, std::string> CanonicalSkillLookup = buildCanonicalSkillLookup();
}

std::string canonicalClassName(const std::string &name)
{
    const std::string lookupKey = canonicalLookupKey(name);

    if (lookupKey.empty())
    {
        return {};
    }

    const std::unordered_map<std::string, std::string>::const_iterator lookupIt = CanonicalClassLookup.find(lookupKey);

    if (lookupIt != CanonicalClassLookup.end())
    {
        return lookupIt->second;
    }

    return trimCopy(name);
}

std::string canonicalSkillName(const std::string &name)
{
    const std::string lookupKey = canonicalLookupKey(name);

    if (lookupKey.empty())
    {
        return {};
    }

    const std::unordered_map<std::string, std::string>::const_iterator lookupIt = CanonicalSkillLookup.find(lookupKey);

    if (lookupIt != CanonicalSkillLookup.end())
    {
        return lookupIt->second;
    }

    return trimCopy(name);
}

std::string displayClassName(const std::string &className)
{
    const std::string canonicalName = canonicalClassName(className);
    const std::unordered_map<std::string, std::string>::const_iterator displayIt =
        DisplayClassNames.find(canonicalName);

    if (displayIt != DisplayClassNames.end())
    {
        return displayIt->second;
    }

    return canonicalName;
}

std::string displaySkillName(const std::string &skillName)
{
    const std::string canonicalName = canonicalSkillName(skillName);
    const std::map<std::string, std::string>::const_iterator rulesetIt = RulesetDisplaySkillNames.find(canonicalName);

    if (rulesetIt != RulesetDisplaySkillNames.end())
    {
        return UiText::translate(rulesetIt->second);
    }
    const std::unordered_map<std::string, std::string>::const_iterator displayIt =
        DisplaySkillNames.find(canonicalName);

    if (displayIt != DisplaySkillNames.end())
    {
        return UiText::translate(displayIt->second);
    }

    return UiText::translate(canonicalName);
}

void setSkillDisplayNameOverrides(const std::map<std::string, std::string> &displayNames)
{
    RulesetDisplaySkillNames = displayNames;
}

std::string masteryDisplayName(SkillMastery mastery)
{
    switch (mastery)
    {
        case SkillMastery::Normal:
            return "Normal";
        case SkillMastery::Expert:
            return "Expert";
        case SkillMastery::Master:
            return "Master";
        case SkillMastery::Grandmaster:
            return "Grandmaster";
        default:
            return "None";
    }
}

std::string localizedMasteryName(SkillMastery mastery)
{
    const std::string raw = masteryDisplayName(mastery);

    // A skill level and the monk's final class are both spelled "Master" in English; sharing one key would
    // translate the class name as a level. The level looks up its own prefixed key first.
    const std::string prefixed = UiText::translate("SkillMastery: " + raw);
    return prefixed == "SkillMastery: " + raw ? UiText::translate(raw) : prefixed;
}

std::string localizedSkillPageMasteryName(SkillMastery mastery)
{
    // The sheet abbreviates the top level to "Grand" so it fits the narrow column; the level keeps its own key
    // so the translation of the abbreviation is independent of the full name.
    const std::string raw = masteryDisplayName(mastery) == "Grandmaster" ? "Grand" : masteryDisplayName(mastery);
    const std::string prefixed = UiText::translate("SkillMastery: " + raw);

    return prefixed == "SkillMastery: " + raw ? UiText::translate(raw) : prefixed;
}

std::vector<std::string> allCanonicalSkillNames()
{
    return {CanonicalSkillNames.begin(), CanonicalSkillNames.end()};
}

std::optional<std::string> skillNameForSkillId(uint32_t skillId)
{
    if (skillId >= CanonicalSkillNames.size())
    {
        return std::nullopt;
    }

    return CanonicalSkillNames[skillId];
}

std::optional<uint32_t> skillIdForSkillName(const std::string &skillName)
{
    const std::string canonicalName = canonicalSkillName(skillName);
    const auto skillIt = std::find(CanonicalSkillNames.begin(), CanonicalSkillNames.end(), canonicalName);

    if (skillIt == CanonicalSkillNames.end())
    {
        return std::nullopt;
    }

    return static_cast<uint32_t>(skillIt - CanonicalSkillNames.begin());
}

bool isTechnologySkill(const std::string &skillName)
{
    const std::optional<uint32_t> skillId = skillIdForSkillName(skillName);
    return skillId
        && ((*skillId >= FirstTechnologySkillId && *skillId < FirstRulesetSkillId) || *skillId == CyberneticsSkillId);
}

bool isRulesetSkill(const std::string &skillName)
{
    const std::optional<uint32_t> skillId = skillIdForSkillName(skillName);
    return skillId && *skillId >= FirstRulesetSkillId && *skillId < CyberneticsSkillId;
}

const std::vector<std::string> &skillSheetGroupSkillNames(SkillSheetGroup group)
{
    switch (group)
    {
        case SkillSheetGroup::Weapons:
            return WeaponSheetSkillNames;
        case SkillSheetGroup::Magic:
            return MagicSheetSkillNames;
        case SkillSheetGroup::Armor:
            return ArmorSheetSkillNames;
        case SkillSheetGroup::Misc:
            return MiscSheetSkillNames;
        case SkillSheetGroup::Technology:
            return TechnologySheetSkillNames;
    }

    return MiscSheetSkillNames;
}

std::optional<std::string> nextPromotionClassName(const std::string &className)
{
    const std::string canonicalName = canonicalClassName(className);

    if (canonicalName == "Cleric")
    {
        return "Priest";
    }

    if (canonicalName == "Archer")
    {
        return "WarriorMage";
    }

    if (canonicalName == "WarriorMage")
    {
        return "MasterArcher";
    }

    if (canonicalName == "DarkElf")
    {
        return "Patriarch";
    }

    if (canonicalName == "Dragon")
    {
        return "GreatWyrm";
    }

    if (canonicalName == "Knight")
    {
        return "Cavalier";
    }

    if (canonicalName == "Cavalier")
    {
        return "BlackKnight";
    }

    if (canonicalName == "Minotaur")
    {
        return "MinotaurLord";
    }

    if (canonicalName == "Troll")
    {
        return "WarTroll";
    }

    if (canonicalName == "Vampire")
    {
        return "Nosferatu";
    }

    if (canonicalName == "Necromancer")
    {
        return "Lich";
    }

    if (canonicalName == "Druid")
    {
        return "GreatDruid";
    }

    if (canonicalName == "GreatDruid")
    {
        return "Warlock";
    }

    if (canonicalName == "Monk")
    {
        return "Initiate";
    }

    if (canonicalName == "Initiate")
    {
        return "Master";
    }

    if (canonicalName == "Paladin")
    {
        return "Crusader";
    }

    if (canonicalName == "Crusader")
    {
        return "Hero";
    }

    if (canonicalName == "Ranger")
    {
        return "Hunter";
    }

    if (canonicalName == "Hunter")
    {
        return "BountyHunter";
    }

    if (canonicalName == "Thief")
    {
        return "Rogue";
    }

    if (canonicalName == "Rogue")
    {
        return "Assassin";
    }

    if (canonicalName == "Sorcerer")
    {
        return "Wizard";
    }

    if (canonicalName == "Wizard")
    {
        return "ArchMage";
    }

    return std::nullopt;
}

std::vector<std::string> promotionClassNames(const std::string &className)
{
    const std::string canonicalName = canonicalClassName(className);

    if (canonicalName == "Archer")
    {
        return {"WarriorMage"};
    }

    if (canonicalName == "WarriorMage")
    {
        return {"MasterArcher", "Sniper"};
    }

    if (canonicalName == "Cleric")
    {
        return {"Priest"};
    }

    if (canonicalName == "Priest")
    {
        return {"PriestLight", "PriestDark", "HighPriest"};
    }

    if (canonicalName == "DarkElf")
    {
        return {"Patriarch"};
    }

    if (canonicalName == "Dragon")
    {
        return {"GreatWyrm"};
    }

    if (canonicalName == "Druid")
    {
        return {"GreatDruid"};
    }

    if (canonicalName == "GreatDruid")
    {
        return {"Warlock", "ArchDruid"};
    }

    if (canonicalName == "Knight")
    {
        return {"Cavalier"};
    }

    if (canonicalName == "Cavalier")
    {
        return {"BlackKnight", "Champion"};
    }

    if (canonicalName == "Minotaur")
    {
        return {"MinotaurLord"};
    }

    if (canonicalName == "Monk")
    {
        return {"Initiate"};
    }

    if (canonicalName == "Initiate")
    {
        return {"Master", "Ninja"};
    }

    if (canonicalName == "Paladin")
    {
        return {"Crusader"};
    }

    if (canonicalName == "Crusader")
    {
        return {"Hero", "Villain"};
    }

    if (canonicalName == "Ranger")
    {
        return {"Hunter"};
    }

    if (canonicalName == "Hunter")
    {
        return {"BountyHunter", "RangerLord"};
    }

    if (canonicalName == "Thief")
    {
        return {"Rogue"};
    }

    if (canonicalName == "Rogue")
    {
        return {"Assassin", "Spy"};
    }

    if (canonicalName == "Troll")
    {
        return {"WarTroll"};
    }

    if (canonicalName == "Vampire")
    {
        return {"Nosferatu"};
    }

    if (canonicalName == "Sorcerer")
    {
        return {"Wizard"};
    }

    if (canonicalName == "Wizard")
    {
        return {"ArchMage", "DarkArchMage", "MasterWizard"};
    }

    if (canonicalName == "Necromancer")
    {
        return {"Lich"};
    }

    return {};
}

std::optional<uint32_t> mm8ClassIdForClassName(const std::string &className)
{
    const std::string canonicalName = canonicalClassName(className);

    if (canonicalName == "Necromancer")
    {
        return 0;
    }

    if (canonicalName == "Lich")
    {
        return 1;
    }

    if (canonicalName == "Cleric")
    {
        return 2;
    }

    if (canonicalName == "Priest")
    {
        return 3;
    }

    if (canonicalName == "Knight")
    {
        return 4;
    }

    if (canonicalName == "Champion")
    {
        return 5;
    }

    if (canonicalName == "Troll")
    {
        return 6;
    }

    if (canonicalName == "WarTroll")
    {
        return 7;
    }

    if (canonicalName == "Minotaur")
    {
        return 8;
    }

    if (canonicalName == "MinotaurLord")
    {
        return 9;
    }

    if (canonicalName == "DarkElf")
    {
        return 10;
    }

    if (canonicalName == "Patriarch")
    {
        return 11;
    }

    if (canonicalName == "Vampire")
    {
        return 12;
    }

    if (canonicalName == "Nosferatu")
    {
        return 13;
    }

    if (canonicalName == "Dragon")
    {
        return 14;
    }

    if (canonicalName == "GreatWyrm")
    {
        return 15;
    }

    return std::nullopt;
}

std::optional<std::string> classNameForMm8ClassId(uint32_t classId)
{
    switch (classId)
    {
        case 0: return "Necromancer";
        case 1: return "Lich";
        case 2: return "Cleric";
        case 3: return "Priest";
        case 4: return "Knight";
        case 5: return "Champion";
        case 6: return "Troll";
        case 7: return "WarTroll";
        case 8: return "Minotaur";
        case 9: return "MinotaurLord";
        case 10: return "DarkElf";
        case 11: return "Patriarch";
        case 12: return "Vampire";
        case 13: return "Nosferatu";
        case 14: return "Dragon";
        case 15: return "GreatWyrm";
        default: break;
    }

    return std::nullopt;
}

SkillMastery parseSkillMasteryToken(const std::string &token)
{
    const std::string trimmed = trimCopy(token);

    if (trimmed == "B")
    {
        return SkillMastery::Normal;
    }

    if (trimmed == "E")
    {
        return SkillMastery::Expert;
    }

    if (trimmed == "M")
    {
        return SkillMastery::Master;
    }

    if (trimmed == "G")
    {
        return SkillMastery::Grandmaster;
    }

    return SkillMastery::None;
}

StartingSkillAvailability parseStartingSkillAvailabilityToken(const std::string &token)
{
    const std::string trimmed = trimCopy(token);

    if (trimmed == "C")
    {
        return StartingSkillAvailability::CanLearn;
    }

    if (trimmed == "F")
    {
        return StartingSkillAvailability::HasByDefault;
    }

    return StartingSkillAvailability::None;
}
}
