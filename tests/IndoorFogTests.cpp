#include "doctest/doctest.h"

#include "game/indoor/IndoorFog.h"
#include "game/maps/IndoorSceneYml.h"

#include <string>

using namespace OpenYAMM::Game;

TEST_CASE("indoor fog: disabled by default, and the uniforms are the old constants")
{
    const IndoorFog fog = {};
    CHECK_FALSE(fog.enabled);
    CHECK(indoorFogRatio(fog, 1.0e6f) == 0.0f);
    CHECK(indoorFogColorUniform(fog) == std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
    CHECK(indoorFogDensitiesUniform(fog) == std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
    CHECK(indoorFogDistancesUniform(fog) == std::array<float, 4>{4096.0f, 4096.0f, 4096.0f, 0.0f});
}

TEST_CASE("indoor fog: linear between near and far")
{
    IndoorFog fog = {};
    fog.enabled = true;
    fog.nearDistance = 100.0f;
    fog.farDistance = 300.0f;
    CHECK(indoorFogRatio(fog, 0.0f) == 0.0f);
    CHECK(indoorFogRatio(fog, 100.0f) == 0.0f);
    CHECK(indoorFogRatio(fog, 200.0f) == doctest::Approx(0.5f));
    CHECK(indoorFogRatio(fog, 300.0f) == 1.0f);
    CHECK(indoorFogRatio(fog, 5000.0f) == 1.0f);
    CHECK(indoorFogDensitiesUniform(fog)[3] == 1.0f);
    CHECK(indoorFogDistancesUniform(fog)[0] == 100.0f);
    CHECK(indoorFogDistancesUniform(fog)[1] == 300.0f);
}

TEST_CASE("indoor fog: scene parsing and refusals")
{
    IndoorFog fog = {};
    std::string error;
    REQUIRE(parseIndoorSceneFog("{color_rgb: [255, 0, 51], near_distance: 2.56, far_distance: 12800}", fog, error));
    CHECK(fog.enabled);
    CHECK(fog.color[0] == 1.0f);
    CHECK(fog.color[2] == doctest::Approx(0.2f));
    CHECK(fog.nearDistance == doctest::Approx(2.56f));
    CHECK(fog.farDistance == 12800.0f);

    const IndoorFog before = fog;
    CHECK_FALSE(parseIndoorSceneFog("{color_rgb: [0, 0], near_distance: 1, far_distance: 2}", fog, error));
    CHECK_FALSE(parseIndoorSceneFog("{color_rgb: [0, 0, 300], near_distance: 1, far_distance: 2}", fog, error));
    CHECK_FALSE(parseIndoorSceneFog("{color_rgb: [0, 0, 0], near_distance: 5, far_distance: 5}", fog, error));
    CHECK_FALSE(parseIndoorSceneFog("{color_rgb: [0, 0, 0], near_distance: -1, far_distance: 5}", fog, error));
    CHECK_FALSE(parseIndoorSceneFog("{color_rgb: [0, 0, 0], far_distance: 5}", fog, error));
    CHECK_FALSE(parseIndoorSceneFog("[1, 2, 3]", fog, error));
    CHECK(fog.farDistance == before.farDistance);
}
