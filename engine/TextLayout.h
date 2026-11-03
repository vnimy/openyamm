#pragma once

#include <cstdint>

namespace OpenYAMM::Engine
{
// Line breaking for the UTF-8 text every face now renders. Latin words break at spaces; Han characters and full-width
// forms break between any two characters, which is how the translated tables are laid out.

// True for a Han character or a full-width form: a break may follow it without a space.
bool isWideBreakable(uint32_t codePoint);

// True for closing punctuation and small forms that may not open a line (，。！？、）】 etc.).
bool isLineLeadingProhibited(uint32_t codePoint);
}
