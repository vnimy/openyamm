#include "engine/TextLayout.h"

namespace OpenYAMM::Engine
{
bool isWideBreakable(uint32_t codePoint)
{
    return (codePoint >= 0x2e80 && codePoint <= 0x9fff) || (codePoint >= 0xf900 && codePoint <= 0xfaff)
        || (codePoint >= 0xff00 && codePoint <= 0xff60) || (codePoint >= 0xffe0 && codePoint <= 0xffe6);
}

bool isLineLeadingProhibited(uint32_t codePoint)
{
    switch (codePoint)
    {
    case 0x00b7: // middle dot
    case 0x2014: // em dash
    case 0x2026: // ellipsis
    case 0x3001: // ideographic comma
    case 0x3002: // ideographic full stop
    case 0x3009:
    case 0x300b:
    case 0x300d:
    case 0x300f:
    case 0x3011:
    case 0x3015:
    case 0x3017:
    case 0xff01: // full-width exclamation mark
    case 0xff09:
    case 0xff0c: // full-width comma
    case 0xff0e:
    case 0xff1a:
    case 0xff1b:
    case 0xff1f:
    case 0xff3d:
    case 0xff5d:
        return true;
    default:
        return false;
    }
}
}
