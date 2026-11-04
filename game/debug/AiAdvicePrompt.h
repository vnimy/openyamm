#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace OpenYAMM::Game
{
class Party;

// The console's AI advice text: a copy-ready prompt holding the live party and progress state plus the instruction
// that turns it into advice. The AI advice button copies this text, `aiadvice` prints it for scripting and checks.
std::string buildAiAdvicePrompt(
    const Party &party,
    std::string_view worldId,
    std::string_view mapFileName,
    const std::vector<std::string> &autonotes);
}
