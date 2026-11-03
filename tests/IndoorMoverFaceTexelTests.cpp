#include <doctest/doctest.h>

#include "game/indoor/IndoorGeometryUtils.h"

#include <vector>

using namespace OpenYAMM::Game;

namespace
{
// A 128 x 256 door leaf in the original engines' door projection (one texel per unit).
const std::vector<IndoorProjectedFacePoint> DoorLeafProjection = {{-64, -512}, {64, -512}, {64, -256}, {-64, -256}};
}

TEST_CASE("a door face whose stored texels are the original door mapping keeps the geometric rule")
{
    // MM6-MM8: the projection up to a constant offset, rounded to integers (a texel either way).
    const std::vector<IndoorProjectedFacePoint> stored = {{37, 12}, {166, 12}, {165, 268}, {37, 267}};
    CHECK_FALSE(indoorMoverFaceKeepsStoredTexels(DoorLeafProjection, stored));
    CHECK_FALSE(indoorMoverFaceKeepsStoredTexels(DoorLeafProjection, DoorLeafProjection));
}

TEST_CASE("a door face that stores its own texel density or orientation keeps its stored texels")
{
    // The MM9 rework's doors: about 0.52 texels per unit.
    const std::vector<IndoorProjectedFacePoint> halfDensity = {{0, 0}, {67, 0}, {67, 133}, {0, 133}};
    CHECK(indoorMoverFaceKeepsStoredTexels(DoorLeafProjection, halfDensity));

    // One texel per unit but turned a quarter: u runs down the leaf.
    const std::vector<IndoorProjectedFacePoint> turned = {{0, 0}, {0, 128}, {256, 128}, {256, 0}};
    CHECK(indoorMoverFaceKeepsStoredTexels(DoorLeafProjection, turned));

    // A thin side face (8 units deep) at half density across it differs by 4 texels: beyond rounding.
    const std::vector<IndoorProjectedFacePoint> sideProjection = {{0, 0}, {8, 0}, {8, 256}, {0, 256}};
    const std::vector<IndoorProjectedFacePoint> sideStored = {{0, 0}, {4, 0}, {4, 256}, {0, 256}};
    CHECK(indoorMoverFaceKeepsStoredTexels(sideProjection, sideStored));
}

TEST_CASE("a door face without matching texels keeps the geometric rule")
{
    const std::vector<IndoorProjectedFacePoint> tooFew = {{0, 0}, {67, 0}, {67, 133}};
    CHECK_FALSE(indoorMoverFaceKeepsStoredTexels(DoorLeafProjection, tooFew));
    CHECK_FALSE(indoorMoverFaceKeepsStoredTexels({}, {}));
}
