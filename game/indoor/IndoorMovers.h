#pragma once

#include "game/indoor/IndoorMapData.h"
#include "game/maps/MapDeltaData.h"

#include <bx/math.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace OpenYAMM::Game
{
// Rotating movers, MM9 door behaviours and ladders: the pure part (devdocs/INDOOR_ROTATING_MOVERS_AND_LADDERS.md).

// p' = R (p - pivot) + pivot + translation.
struct IndoorRigidMotion
{
    std::array<float, 9> rotation = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 3> pivot = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> translation = {0.0f, 0.0f, 0.0f};

    bx::Vec3 apply(const bx::Vec3 &point) const;
    // The rotation alone, for directions.
    bx::Vec3 rotate(const bx::Vec3 &direction) const;
    IndoorRigidMotion inverse() const;
};

// `second` after `first`.
IndoorRigidMotion composeIndoorRigidMotions(const IndoorRigidMotion &second, const IndoorRigidMotion &first);

float indoorMoverEase(IndoorMoverEasing easing, float fraction);
// How open the door is at `distance` (0 closed, 1 open; distance runs from move_length closed to 0 open), eased.
float indoorMoverOpenFraction(const MapDeltaDoor &door, const IndoorMoverDefinition *pMover, float distance);
// The motion of a rotating mover from its stored (closed) pose to its pose at `distance`; nullopt for a door without
// a rotation.
std::optional<IndoorRigidMotion> indoorRotatingMoverMotion(
    const MapDeltaDoor &door,
    const IndoorMoverDefinition *pMover,
    float distance,
    int8_t rotationDirection);
// A rotating mover's vertices at `distance`: each stored vertex (the door's offsets) moved by the motion, rounded.
void applyIndoorRotatingMoverToVertices(
    const MapDeltaDoor &door,
    const IndoorRigidMotion &motion,
    std::vector<IndoorVertex> &vertices);
// The sign that swings the door's centre away from `activator`.
int8_t indoorMoverOpenAwayDirection(
    const MapDeltaDoor &door,
    const IndoorMoverDefinition &mover,
    const bx::Vec3 &activator);
// The farthest any vertex travels between the closed and the open pose (a chord for a rotation).
float indoorRotatingMoverMaxTravel(const MapDeltaDoor &door, const IndoorMoverRotation &rotation);

// A body for the mover rules: a vertical capsule standing at (x, y, footZ).
struct IndoorMoverBody
{
    float x = 0.0f;
    float y = 0.0f;
    float footZ = 0.0f;
    float radius = 37.0f;
    float height = 160.0f;
};

// Shortest distance between the body's capsule axis and a planar convex polygon.
float indoorBodyAxisPolygonDistance(
    const IndoorMoverBody &body,
    std::span<const bx::Vec3> polygon,
    bx::Vec3 *pClosestOnAxis = nullptr,
    bx::Vec3 *pClosestOnPolygon = nullptr);
bool indoorBodyTouchesPolygon(const IndoorMoverBody &body, std::span<const bx::Vec3> polygon);
// The move that takes the body clear of the polygon (zero when it does not touch it). `sideHint` points to the side
// of the polygon the body came from; it decides the direction when the polygon cuts through the body's axis.
bx::Vec3 indoorBodyPolygonClearance(
    const IndoorMoverBody &body,
    std::span<const bx::Vec3> polygon,
    const bx::Vec3 &sideHint);

// Ladders. The climber faces `facing` (toward the strip); `side` runs along the strip's width.
struct IndoorLadderFrame
{
    bx::Vec3 facing = {1.0f, 0.0f, 0.0f};
    bx::Vec3 side = {0.0f, 1.0f, 0.0f};
};

IndoorLadderFrame indoorLadderFrame(const IndoorLadder &ladder);
// The body may grab the ladder: in front of the strip within reach, within its width and height, moving toward it.
bool indoorLadderCanGrab(
    const IndoorLadder &ladder,
    const IndoorMoverBody &body,
    float desiredVelocityX,
    float desiredVelocityY);

inline constexpr float IndoorLadderClimbSpeed = 192.0f;
inline constexpr float IndoorLadderRunClimbSpeed = 288.0f;

struct IndoorLadderClimbStep
{
    // Velocity for the step: horizontal along the strip plus the pull that holds the body in front of it.
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    float verticalVelocity = 0.0f;
    bool exitTop = false;
    bool detach = false;
};

// One climbing step from the player's movement (world velocity) and the Fly Up/Down input (+1, -1, 0).
IndoorLadderClimbStep indoorLadderClimbStep(
    const IndoorLadder &ladder,
    const IndoorMoverBody &body,
    float desiredVelocityX,
    float desiredVelocityY,
    int verticalInput,
    bool running,
    float deltaSeconds);
}
