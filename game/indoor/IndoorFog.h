#pragma once

#include <algorithm>
#include <array>

namespace OpenYAMM::Game
{
// A map's indoor fog (scene `fog`, devdocs/INDOOR_FOG.md): every world pixel fades linearly into one colour between
// two distances from the eye. Disabled on every map without the scene block.
struct IndoorFog
{
    bool enabled = false;
    std::array<float, 3> color = {0.0f, 0.0f, 0.0f};   // display space, 0..1
    float nearDistance = 0.0f;
    float farDistance = 0.0f;
};

// How far a pixel `distance` units from the eye has faded into the fog colour: 0 up to near, 1 from far on.
inline float indoorFogRatio(const IndoorFog &fog, float distance)
{
    if (!fog.enabled)
    {
        return 0.0f;
    }
    return std::clamp((distance - fog.nearDistance) / std::max(fog.farDistance - fog.nearDistance, 1.0f), 0.0f, 1.0f);
}

// The fog as the billboard (u_fogColor, u_fogDensities, u_fogDistances) and model (u_modelFog) uniforms, whose shaders
// take a linear fog when the densities' w is 1. Disabled, these are the constants those draws always passed indoors.
inline std::array<float, 4> indoorFogColorUniform(const IndoorFog &fog)
{
    return fog.enabled ? std::array<float, 4>{fog.color[0], fog.color[1], fog.color[2], 1.0f}
                       : std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f};
}

inline std::array<float, 4> indoorFogDensitiesUniform(const IndoorFog &fog)
{
    return {0.0f, 0.0f, 0.0f, fog.enabled ? 1.0f : 0.0f};
}

inline std::array<float, 4> indoorFogDistancesUniform(const IndoorFog &fog)
{
    return fog.enabled ? std::array<float, 4>{fog.nearDistance, fog.farDistance, fog.farDistance + 1.0f, 0.0f}
                       : std::array<float, 4>{4096.0f, 4096.0f, 4096.0f, 0.0f};
}
}
