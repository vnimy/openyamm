#include "game/gameplay/JournalArcomageRuntime.h"

#include "game/party/Party.h"
#include "game/tables/HouseTable.h"
#include "game/tables/MapStats.h"
#include "game/ui/UiText.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace OpenYAMM::Game
{
namespace
{
std::string continentName(uint32_t continentId)
{
    switch (continentId)
    {
        case 1:
            return "Jadame";
        case 2:
            return "Antagarich";
        case 3:
            return "Enroth";
        default:
            return "Elsewhere";
    }
}

struct ArcomageTavern
{
    std::string town;
    std::string name;
    bool won = false;
};
} // namespace

std::vector<std::string> buildArcomageTavernTexts(const HouseTable &houseTable, const MapStats &mapStats,
    const Party &party)
{
    std::map<uint32_t, std::vector<ArcomageTavern>> tavernsByContinent;

    for (const auto &[houseId, house] : houseTable.entries())
    {
        if (!house.arcomageRule)
        {
            continue;
        }

        const MapStatsEntry *pMap = mapStats.findById(house.mapId);
        ArcomageTavern tavern;
        tavern.town = pMap != nullptr ? pMap->name : std::string();
        tavern.name = house.name;
        tavern.won = party.hasArcomageWinAt(houseId);
        tavernsByContinent[pMap != nullptr ? pMap->mergedContinentId : 0].push_back(std::move(tavern));
    }

    std::vector<std::string> texts;

    for (auto &[continentId, taverns] : tavernsByContinent)
    {
        std::sort(taverns.begin(), taverns.end(), [](const ArcomageTavern &left, const ArcomageTavern &right)
            {
                return std::tie(left.town, left.name) < std::tie(right.town, right.name);
            });
        const size_t wonCount = size_t(std::count_if(taverns.begin(), taverns.end(),
            [](const ArcomageTavern &tavern) { return tavern.won; }));
        texts.push_back(UiText::translateTemplate("{continent}: {won} of {total} taverns won.",
            {{"{continent}", UiText::translate(continentName(continentId))},
             {"{won}", std::to_string(wonCount)},
             {"{total}", std::to_string(taverns.size())}}));

        for (const ArcomageTavern &tavern : taverns)
        {
            const std::string place = tavern.town.empty()
                ? UiText::translate(tavern.name)
                : UiText::translateTemplate("{tavern}, {town}",
                      {{"{tavern}", UiText::translate(tavern.name)}, {"{town}", UiText::translate(tavern.town)}});
            texts.push_back(UiText::translateTemplate(tavern.won ? "{place}: won" : "{place}: not won yet",
                {{"{place}", place}}));
        }
    }

    return texts;
}
} // namespace OpenYAMM::Game
