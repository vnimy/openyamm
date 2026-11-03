#include "doctest/doctest.h"

#include "game/FaceEnums.h"
#include "game/indoor/IndoorBakedLighting.h"
#include "game/indoor/IndoorGeometryUtils.h"
#include "game/indoor/IndoorMovementController.h"
#include "game/indoor/IndoorMovers.h"
#include "game/maps/IndoorSceneYml.h"

#include <cmath>
#include <vector>

using namespace OpenYAMM::Game;

namespace
{
// A leaf 100 wide and 200 tall in the plane y = 0, hinged at the origin about +z: closed along +x.
MapDeltaDoor leafDoor()
{
    MapDeltaDoor door;
    door.doorId = 7;
    door.moveLength = 900;
    door.openSpeed = 900;
    door.closeSpeed = 900;
    door.vertexIds = {0, 1, 2, 3};
    door.faceIds = {0};
    door.xOffsets = {0, 100, 100, 0};
    door.yOffsets = {0, 0, 0, 0};
    door.zOffsets = {0, 0, 200, 200};
    return door;
}

IndoorMoverDefinition leafMover(float openDegrees = 90.0f, bool openAway = false)
{
    IndoorMoverDefinition mover;
    mover.doorId = 7;
    IndoorMoverRotation rotation;
    rotation.openDegrees = openDegrees;
    rotation.openAway = openAway;
    mover.rotation = rotation;
    return mover;
}

std::vector<IndoorVertex> leafVertices()
{
    return {{0, 0, 0}, {100, 0, 0}, {100, 0, 200}, {0, 0, 200}};
}

// A shaft: a lower room (sector 1, floor at 0, up to 512) under an upper room (sector 2) whose floor at 512 has a hole
// 256 across, closed by a portal in the floor's plane.
IndoorMapData shaftThroughFloorPortal(int32_t upperSectorFlags)
{
    IndoorMapData map;
    map.vertices = {
        {-128, -128, 0}, {128, -128, 0}, {128, 128, 0}, {-128, 128, 0},
        {-128, -128, 512}, {128, -128, 512}, {128, 128, 512}, {-128, 128, 512},
        {-512, -512, 512}, {512, -512, 512}, {512, 512, 512}, {-512, 512, 512}};
    const auto floorFace = [](std::vector<uint16_t> vertices, uint16_t room)
    {
        IndoorFace face;
        face.vertexIndices = std::move(vertices);
        face.facetType = 3;
        face.roomNumber = face.roomBehindNumber = room;
        return face;
    };
    map.faces = {floorFace({0, 1, 2, 3}, 1), floorFace({8, 9, 5, 4}, 2), floorFace({9, 10, 6, 5}, 2),
        floorFace({10, 11, 7, 6}, 2), floorFace({11, 8, 4, 7}, 2), floorFace({4, 5, 6, 7}, 2)};
    IndoorFace &portal = map.faces.back();
    portal.isPortal = true;
    portal.attributes = static_cast<uint32_t>(FaceAttribute::IsPortal);
    portal.roomBehindNumber = 1;
    map.sectors.resize(3);
    IndoorSector &lower = map.sectors[1];
    lower.minX = lower.minY = -128;
    lower.maxX = lower.maxY = 128;
    lower.minZ = 0;
    lower.maxZ = 512;
    lower.floorFaceIds = {0};
    lower.portalFaceIds = {5};
    IndoorSector &upper = map.sectors[2];
    upper.flags = upperSectorFlags;
    upper.minX = upper.minY = -512;
    upper.maxX = upper.maxY = 512;
    upper.minZ = 512;
    upper.maxZ = 1024;
    upper.floorFaceIds = {1, 2, 3, 4};
    upper.portalFaceIds = {5};
    return map;
}

// Climbs straight up the shaft as the ladder step does; returns the foot height it reaches.
float climbShaft(const IndoorMapData &map)
{
    const IndoorMovementController controller(map, nullptr, nullptr);
    const IndoorBodyDimensions body = {};
    IndoorMoveState state;
    state.footZ = 1.0f;
    state.sectorId = 1;
    state.eyeSectorId = 1;

    for (int step = 0; step < 300; ++step)
    {
        state.verticalVelocity = 288.0f;
        state = controller.resolveMove(state, body, 0.0f, 0.0f, false, 1.0f / 128.0f, nullptr, std::nullopt, true,
            nullptr, true);
    }

    return state.footZ;
}

std::vector<bx::Vec3> wallPolygon(float y)
{
    return {{-500.0f, y, 0.0f}, {500.0f, y, 0.0f}, {500.0f, y, 400.0f}, {-500.0f, y, 400.0f}};
}
}

TEST_CASE("rotating mover: closed at move_length, turned by the full angle at zero distance")
{
    const MapDeltaDoor door = leafDoor();
    const IndoorMoverDefinition mover = leafMover();
    std::vector<IndoorVertex> vertices = leafVertices();
    applyIndoorMechanismDoorToVertices(door, &mover, 900.0f, 1, vertices);
    CHECK(vertices[1].x == 100);
    CHECK(vertices[1].y == 0);
    applyIndoorMechanismDoorToVertices(door, &mover, 0.0f, 1, vertices);
    CHECK(vertices[1].x == 0);
    CHECK(vertices[1].y == 100);
    CHECK(vertices[2].z == 200);
    applyIndoorMechanismDoorToVertices(door, &mover, 450.0f, 1, vertices);
    CHECK(vertices[1].x == 71);
    CHECK(vertices[1].y == 71);
}

TEST_CASE("rotating mover: the sign follows the axis and open-away direction only when asked")
{
    const MapDeltaDoor door = leafDoor();
    IndoorMoverDefinition mover = leafMover(-90.0f);
    std::vector<IndoorVertex> vertices = leafVertices();
    applyIndoorMechanismDoorToVertices(door, &mover, 0.0f, 1, vertices);
    CHECK(vertices[1].y == -100);
    applyIndoorMechanismDoorToVertices(door, &mover, 0.0f, -1, vertices);
    CHECK(vertices[1].y == -100);
    mover.rotation->openAway = true;
    applyIndoorMechanismDoorToVertices(door, &mover, 0.0f, -1, vertices);
    CHECK(vertices[1].y == 100);
}

TEST_CASE("rotating mover: a linear door is unchanged by the shared pose function")
{
    MapDeltaDoor door = leafDoor();
    door.directionZ = -65536;
    door.moveLength = 256;
    std::vector<IndoorVertex> vertices = leafVertices();
    applyIndoorMechanismDoorToVertices(door, nullptr, 256.0f, 1, vertices);
    CHECK(vertices[0].z == -256);
    CHECK(vertices[2].z == -56);
    applyIndoorMechanismDoorToVertices(door, nullptr, 0.0f, 1, vertices);
    CHECK(vertices[2].z == 200);
}

TEST_CASE("rotating mover: sine easing is symmetric and continuous")
{
    CHECK(indoorMoverEase(IndoorMoverEasing::Sine, 0.0f) == doctest::Approx(0.0f));
    CHECK(indoorMoverEase(IndoorMoverEasing::Sine, 1.0f) == doctest::Approx(1.0f));
    CHECK(indoorMoverEase(IndoorMoverEasing::Sine, 0.5f) == doctest::Approx(0.5f));
    CHECK(indoorMoverEase(IndoorMoverEasing::Sine, 0.25f)
        == doctest::Approx(1.0f - indoorMoverEase(IndoorMoverEasing::Sine, 0.75f)));
    CHECK(indoorMoverEase(IndoorMoverEasing::Sine, 0.1f) < 0.1f);
    CHECK(indoorMoverEase(IndoorMoverEasing::Linear, 0.3f) == doctest::Approx(0.3f));
}

TEST_CASE("rotating mover: rigid motions invert and compose")
{
    IndoorMoverDefinition mover = leafMover();
    mover.rotation->pivot = {10.0f, 20.0f, 30.0f};
    mover.rotation->axis = {1.0f, 0.0f, 0.0f};
    const MapDeltaDoor door = leafDoor();
    const IndoorRigidMotion quarter = *indoorRotatingMoverMotion(door, &mover, 450.0f, 1);
    const IndoorRigidMotion half = *indoorRotatingMoverMotion(door, &mover, 0.0f, 1);
    const bx::Vec3 point = {50.0f, 60.0f, 70.0f};
    const bx::Vec3 moved = half.apply(point);
    const bx::Vec3 back = half.inverse().apply(moved);
    CHECK(back.x == doctest::Approx(point.x));
    CHECK(back.y == doctest::Approx(point.y));
    CHECK(back.z == doctest::Approx(point.z));
    const IndoorRigidMotion step = composeIndoorRigidMotions(half, quarter.inverse());
    const bx::Vec3 viaStep = step.apply(quarter.apply(point));
    CHECK(viaStep.x == doctest::Approx(moved.x));
    CHECK(viaStep.y == doctest::Approx(moved.y));
    CHECK(viaStep.z == doctest::Approx(moved.z));
}

TEST_CASE("rotating mover: open away swings from the party, a chord measures its travel")
{
    const MapDeltaDoor door = leafDoor();
    const IndoorMoverDefinition mover = leafMover(90.0f, true);
    // Positive angles swing the leaf toward +y.
    CHECK(indoorMoverOpenAwayDirection(door, mover, {50.0f, 300.0f, 80.0f}) == -1);
    CHECK(indoorMoverOpenAwayDirection(door, mover, {50.0f, -300.0f, 80.0f}) == 1);
    CHECK(indoorRotatingMoverMaxTravel(door, *mover.rotation) == doctest::Approx(141.42f).epsilon(0.01));
}

TEST_CASE("rotating mover: a body touches a polygon by its capsule and is moved clear of it")
{
    const IndoorMoverBody body = {0.0f, 30.0f, 0.0f, 37.0f, 160.0f};
    CHECK(indoorBodyTouchesPolygon(body, wallPolygon(0.0f)));
    CHECK_FALSE(indoorBodyTouchesPolygon(body, wallPolygon(100.0f)));
    const bx::Vec3 clear = indoorBodyPolygonClearance(body, wallPolygon(0.0f), {0.0f, 1.0f, 0.0f});
    CHECK(clear.y > 6.9f);
    CHECK(clear.y < 8.5f);
    CHECK(clear.x == doctest::Approx(0.0f));
    // An arm coming down over the body's head pushes it down, off its side of the plate.
    const std::vector<bx::Vec3> arm = {{-100.0f, -100.0f, 120.0f}, {100.0f, -100.0f, 120.0f}, {100.0f, 100.0f, 120.0f},
        {-100.0f, 100.0f, 120.0f}};
    const IndoorMoverBody under = {0.0f, 0.0f, 0.0f, 37.0f, 160.0f};
    const bx::Vec3 down = indoorBodyPolygonClearance(under, arm, {0.0f, 0.0f, -1.0f});
    CHECK(down.z < -39.0f);
    // A polygon cutting the axis is left on the hinted side.
    const IndoorMoverBody cut = {0.0f, 0.0f, 0.0f, 37.0f, 160.0f};
    const std::vector<bx::Vec3> plate = {{-100.0f, -100.0f, 80.0f}, {100.0f, -100.0f, 80.0f}, {100.0f, 100.0f, 80.0f},
        {-100.0f, 100.0f, 80.0f}};
    const bx::Vec3 up = indoorBodyPolygonClearance(cut, plate, {0.0f, 0.0f, 1.0f});
    CHECK(up.z > 79.0f);
}

TEST_CASE("rotating mover: its faces sample the stored pose's lightmap in any pose")
{
    IndoorBakedLighting lighting;
    lighting.pages.resize(1);
    IndoorBakedLightingFace face;
    face.page = 0;
    face.uAxis = {0.01f, 0.0f, 0.0f, 0.25f};
    face.vAxis = {0.0f, 0.0f, 0.02f, 0.0f};
    lighting.faces = {face};
    IndoorFace mapFace;
    mapFace.vertexIndices = {0, 1, 2, 3};
    const MapDeltaDoor door = leafDoor();
    const IndoorMoverDefinition mover = leafMover();
    const IndoorRigidMotion motion = *indoorRotatingMoverMotion(door, &mover, 0.0f, 1);
    std::vector<IndoorVertex> vertices = leafVertices();
    const std::vector<IndoorVertex> stored = leafVertices();
    applyIndoorMechanismDoorToVertices(door, &mover, 0.0f, 1, vertices);
    const IndoorBakedFaceLightmap lightmap =
        IndoorBakedLighting::faceLightmap(&lighting, true, 0, mapFace, true, vertices, stored, &motion);
    REQUIRE(lightmap.page == 0);
    const std::array<float, 2> uv = lightmap.uvAt(face, 0.0f, 100.0f, 200.0f);
    const std::array<float, 2> storedUv = face.uvAt(100.0f, 0.0f, 200.0f);
    CHECK(uv[0] == doctest::Approx(storedUv[0]));
    CHECK(uv[1] == doctest::Approx(storedUv[1]));
    const std::array<const MapDeltaDoor *, 1> doors = {&door};
    CHECK(indoorMoverFaceBakeable(mapFace, doors, &mover));
    MapDeltaDoor wide = door;
    wide.xOffsets = {0, 2000, 2000, 0};
    const std::array<const MapDeltaDoor *, 1> wideDoors = {&wide};
    CHECK_FALSE(indoorMoverFaceBakeable(mapFace, wideDoors, &mover));
}

TEST_CASE("rotating mover: the scene loader refuses a mover that is not rigid or whose numbers disagree")
{
    IndoorMapData map;
    map.vertices = leafVertices();
    map.vertices.push_back({0, 0, 400});
    IndoorFace face;
    face.vertexIndices = {0, 1, 2, 3};
    map.faces = {face};
    IndoorSceneDoor sceneDoor;
    sceneDoor.door = leafDoor();
    sceneDoor.mover = leafMover();
    std::vector<IndoorMoverDefinition> movers;
    std::string error;
    REQUIRE(buildIndoorMoverDefinitions({sceneDoor}, map, movers, error));
    REQUIRE(movers.size() == 1);
    map.moverDefinitions = movers;
    CHECK(findIndoorMoverDefinition(map, 7) != nullptr);
    CHECK(findIndoorMoverDefinition(map, 8) == nullptr);

    IndoorSceneDoor badLength = sceneDoor;
    badLength.door.moveLength = 800;
    CHECK_FALSE(buildIndoorMoverDefinitions({badLength}, map, movers, error));

    IndoorSceneDoor linearDirection = sceneDoor;
    linearDirection.door.directionZ = 65536;
    CHECK_FALSE(buildIndoorMoverDefinitions({linearDirection}, map, movers, error));

    IndoorSceneDoor notRigid = sceneDoor;
    notRigid.door.vertexIds = {0, 1, 2};
    notRigid.door.xOffsets = {0, 100, 100};
    notRigid.door.yOffsets = {0, 0, 0};
    notRigid.door.zOffsets = {0, 0, 200};
    CHECK_FALSE(buildIndoorMoverDefinitions({notRigid}, map, movers, error));

    IndoorSceneDoor badAxis = sceneDoor;
    badAxis.mover->rotation->axis = {0.0f, 0.0f, 2.0f};
    CHECK_FALSE(buildIndoorMoverDefinitions({badAxis}, map, movers, error));

    map.faces[0].isPortal = true;
    CHECK_FALSE(buildIndoorMoverDefinitions({sceneDoor}, map, movers, error));
}

TEST_CASE("ladder: grabbing needs the body in front of the strip and moving toward it")
{
    IndoorLadder ladder;
    ladder.bottom = {0.0f, 0.0f, 0.0f};
    ladder.top = {0.0f, 0.0f, 600.0f};
    ladder.facingDegrees = 0.0f;
    ladder.width = 120.0f;
    ladder.reach = 48.0f;
    const IndoorMoverBody body = {-41.0f, 0.0f, 0.0f, 37.0f, 160.0f};
    CHECK(indoorLadderCanGrab(ladder, body, 300.0f, 0.0f));
    CHECK_FALSE(indoorLadderCanGrab(ladder, body, -300.0f, 0.0f));
    CHECK_FALSE(indoorLadderCanGrab(ladder, body, 0.0f, 0.0f));
    CHECK_FALSE(indoorLadderCanGrab(ladder, {-400.0f, 0.0f, 0.0f, 37.0f, 160.0f}, 300.0f, 0.0f));
    CHECK_FALSE(indoorLadderCanGrab(ladder, {-41.0f, 100.0f, 0.0f, 37.0f, 160.0f}, 300.0f, 0.0f));
    CHECK_FALSE(indoorLadderCanGrab(ladder, {-41.0f, 0.0f, 700.0f, 37.0f, 160.0f}, 300.0f, 0.0f));
}

TEST_CASE("ladder: forward and the fly keys climb, the top exits, strafing past the strip lets go")
{
    IndoorLadder ladder;
    ladder.bottom = {0.0f, 0.0f, 0.0f};
    ladder.top = {0.0f, 0.0f, 600.0f};
    ladder.width = 120.0f;
    const float step = 1.0f / 128.0f;
    const IndoorMoverBody body = {-41.0f, 0.0f, 100.0f, 37.0f, 160.0f};
    IndoorLadderClimbStep climb = indoorLadderClimbStep(ladder, body, 300.0f, 0.0f, 0, false, step);
    CHECK(climb.verticalVelocity == doctest::Approx(IndoorLadderClimbSpeed));
    climb = indoorLadderClimbStep(ladder, body, -300.0f, 0.0f, 0, true, step);
    CHECK(climb.verticalVelocity == doctest::Approx(-IndoorLadderRunClimbSpeed));
    climb = indoorLadderClimbStep(ladder, body, 0.0f, 0.0f, 1, false, step);
    CHECK(climb.verticalVelocity > 0.0f);
    climb = indoorLadderClimbStep(ladder, body, 0.0f, 0.0f, 0, false, step);
    CHECK(climb.verticalVelocity == 0.0f);
    climb = indoorLadderClimbStep(ladder, {-41.0f, 0.0f, 598.0f, 37.0f, 160.0f}, 300.0f, 0.0f, 0, false, step);
    CHECK(climb.exitTop);
    climb = indoorLadderClimbStep(ladder, {-41.0f, 110.0f, 100.0f, 37.0f, 160.0f}, 0.0f, 300.0f, 0, false, step);
    CHECK(climb.detach);
    // A body off its hold distance is pulled back to it.
    climb = indoorLadderClimbStep(ladder, {-60.0f, 0.0f, 100.0f, 37.0f, 160.0f}, 0.0f, 0.0f, 0, false, step);
    CHECK(climb.velocityX > 0.0f);
}

TEST_CASE("ladder: a climb passes a floor portal when the room above is marked for it")
{
    // The bug: over a hole the upper room has no floor face and the lower room ends at the portal, so a rising eye had
    // no sector and the move was refused about a body height under the portal.
    CHECK(climbShaft(shaftThroughFloorPortal(0)) < 512.0f - 64.0f);
    CHECK(climbShaft(shaftThroughFloorPortal(0x8)) > 512.0f + 100.0f);
}
