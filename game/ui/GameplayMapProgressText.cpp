#include "game/ui/UiText.h"
#include "game/ui/GameplayMapProgressBar.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// The map progress bar's texts, apart from its drawing so the unit tests need no HUD.
namespace OpenYAMM::Game
{
std::string GameplayMapProgressBar::percentText(double percent)
{
    const double tenths = std::round(std::clamp(percent, 0.0, 100.0) * 10);
    char text[16] = {};

    if (std::fmod(tenths, 10.0) == 0)
    {
        std::snprintf(text, sizeof(text), "%d %%", int(tenths / 10));
    }
    else
    {
        std::snprintf(text, sizeof(text), "%.1f %%", tenths / 10);
    }

    return text;
}

std::string GameplayMapProgressBar::tabText(const RulesetMapProgress &progress, bool percentOnly)
{
    // The HUD fonts are single-byte: the middle dot is 0xB7.
    const std::string dot = " \xB7 ";
    const std::string percent = percentText(progress.percent);

    if (percentOnly)
    {
        return percent;
    }

    if (progress.kind == RulesetMapProgress::Kind::Arena)
    {
        return "Endless Arena" + dot + "Level " + std::to_string(progress.arenaLevel) + dot + percent;
    }

    if (progress.complete)
    {
        return "Cleared" + dot + std::to_string(progress.slain) + " / " + std::to_string(progress.counted);
    }

    return percent + dot + std::to_string(progress.slain) + " / " + std::to_string(progress.needed);
}

std::string GameplayMapProgressBar::popupBody(const RulesetMapProgress &progress)
{
    std::vector<std::string> lines;

    if (progress.kind == RulesetMapProgress::Kind::Arena)
    {
        lines.push_back(UiText::translateTemplate(
            "Wave level: {level}", {{"{level}", std::to_string(progress.arenaLevel)}}));
        lines.push_back(UiText::translateTemplate(
            "This wave: {percent}", {{"{percent}", percentText(progress.percent)}}));
        lines.push_back(UiText::translate("The Arena is never completed"));
    }
    else
    {
        if (progress.kind == RulesetMapProgress::Kind::DimensionRun)
        {
            lines.push_back(UiText::translateTemplate(
                "Dimension run: power {power}, {affixes}",
                {{"{power}", std::to_string(progress.dimensionPower)},
                 {"{affixes}", UiText::translateTemplate(
                      progress.dimensionAffixes == 1 ? "{count} affix" : "{count} affixes",
                      {{"{count}", std::to_string(progress.dimensionAffixes)}})}}));
        }

        lines.push_back(UiText::translateTemplate(
            "Slain: {slain} of {counted}",
            {{"{slain}", std::to_string(progress.slain)}, {"{counted}", std::to_string(progress.counted)}}));
        lines.push_back(UiText::translateTemplate(
            "Needed to complete: {needed} ({share})",
            {{"{needed}", std::to_string(progress.needed)},
             {"{share}", percentText(progress.requiredShare * 100)}}));
        std::string alive = UiText::translateTemplate(
            "Alive: {alive}", {{"{alive}", std::to_string(std::max(0, progress.counted - progress.slain))}});

        if (progress.hidden > 0)
        {
            alive += ", " + std::to_string(progress.hidden) + " hidden, not counted";
        }

        lines.push_back(alive);

        if (progress.revival)
        {
            lines.push_back(UiText::translateTemplate(
                "Dead rise again on leaving: {revival}",
                {{"{revival}", progress.revivalPassed
                     ? UiText::translate("no longer")
                     : UiText::translateTemplate("until {count} slain",
                           {{"{count}", std::to_string(progress.revivalSlain)}})}}));
        }

        lines.push_back(UiText::translateTemplate(
            "Reward: {reward}",
            {{"{reward}", progress.rewardPaid ? UiText::translate("paid") : progress.reward}}));
    }

    std::string body;

    for (const std::string &line : lines)
    {
        body += (body.empty() ? "" : "\n") + line;
    }

    return body;
}
}
