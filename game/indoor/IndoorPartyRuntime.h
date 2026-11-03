#pragma once

#include "game/indoor/IndoorMovementController.h"
#include "game/party/Party.h"

#include <optional>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
class ItemTable;

class IndoorPartyRuntime
{
public:
    struct Snapshot
    {
        IndoorMoveState movementState = {};
        float movementAccumulatorSeconds = 0.0f;
        bool pendingJumpRequested = false;
        float pendingImpulseVelocityX = 0.0f;
        float pendingImpulseVelocityY = 0.0f;
        float pendingImpulseVelocityZ = 0.0f;
        bool alwaysRunEnabled = true;
    };

    IndoorPartyRuntime(IndoorMovementController movementController, const ItemTable &itemTable);

    void initializeEyePosition(float x, float y, float z, bool resetParty = true);
    void initializePartyPosition(float x, float y, float z, bool resetParty = true);
    void teleportEyePosition(float x, float y, float z);
    void teleportPartyPosition(float x, float y, float z);
    void translatePartyPosition(float deltaX, float deltaY, float deltaZ);
    // A moving face pushes the party (a rotating mover's push rule): moved, no longer supported, falling from there.
    void pushPartyPosition(float deltaX, float deltaY, float deltaZ);
    // Raises the party in place; it falls back from there (MAW's unstuck key).
    void lift(float height);
    void update(
        float desiredVelocityX,
        float desiredVelocityY,
        bool jumpRequested,
        bool runningMovement,
        float deltaSeconds,
        bool turnBasedMovementStep = false,
        bool turnBasedPhysicsStep = false);
    void setActorColliders(const std::vector<IndoorActorCollision> &actorColliders);
    void setDecorationColliders(const std::vector<IndoorCylinderCollision> &decorationColliders);
    void setSpriteObjectColliders(const std::vector<IndoorCylinderCollision> &spriteObjectColliders);
    void applyMechanismGeometryUpdate(const std::vector<uint32_t> &changedDoorIds);
    void invalidateRuntimeGeometryCache();

    const IndoorMoveState &movementState() const;
    // Fly Up/Fly Down while on a ladder: +1, -1 or 0 (devdocs/INDOOR_ROTATING_MOVERS_AND_LADDERS.md, "Ladders").
    void setLadderVerticalInput(int verticalInput);
    bool climbingLadder() const;
    // Moves out the floor faces stood on during movement steps since the last call, in order, so pressure plates
    // see every face crossed instead of only the frame's final one.
    void takeSteppedFloorFaces(std::vector<size_t> &faces);
    const Party &party() const;
    Party &party();
    const std::string &movementStatusText() const;
    float partyX() const;
    float partyY() const;
    float partyFootZ() const;
    void setParty(const Party &party);
    Snapshot snapshot() const;
    void restoreSnapshot(const Snapshot &snapshot);
    void setMovementSpeedMultiplier(float multiplier);
    // Per face: the speed share while the party stands on it (shallow liquids it wades through); empty: none.
    void setFaceWadeSpeeds(std::vector<float> speeds);
    void setAlwaysRunEnabled(bool enabled);
    void setCollisionTraceEnabled(bool enabled, std::string mapName);
    bool alwaysRunEnabled() const;
    void syncSpellMovementStatesFromPartyBuffs();
    void requestJump(std::optional<float> verticalVelocity = std::nullopt, float lift = 0.0f);
    void requestSpecialJump(float velocityX, float velocityY, float velocityZ);
    // Adds this horizontal velocity to every movement step for `seconds`, with the world's collision (a charge).
    void requestDash(float velocityX, float velocityY, float seconds);

private:
    // One movement step on a ladder (grabbing it first when the party walks into one); false: walk normally.
    bool stepOnLadder(
        float desiredVelocityX,
        float desiredVelocityY,
        bool runningMovement,
        const IndoorBodyDimensions &body,
        std::vector<size_t> &contactedActorIndices);

    IndoorMovementController m_movementController;
    // Runtime only: a save made on a ladder restores the party falling from where it was.
    std::optional<size_t> m_ladderIndex;
    int m_ladderVerticalInput = 0;
    Party m_party;
    IndoorMoveState m_movementState = {};
    float m_movementSpeedMultiplier = 1.0f;
    std::vector<float> m_faceWadeSpeeds;
    bool m_alwaysRunEnabled = true;
    bool m_collisionTraceEnabled = false;
    std::string m_collisionTraceMapName;
    float m_collisionTraceClockSeconds = 0.0f;
    float m_nextCollisionTraceSeconds = 0.0f;
    float m_movementAccumulatorSeconds = 0.0f;
    bool m_pendingJumpRequested = false;
    float m_pendingImpulseVelocityX = 0.0f;
    float m_pendingImpulseVelocityY = 0.0f;
    float m_pendingImpulseVelocityZ = 0.0f;
    // A special jump's horizontal speed, kept until the party is back on the ground.
    float m_jumpCarryVelocityX = 0.0f;
    float m_jumpCarryVelocityY = 0.0f;
    float m_dashVelocityX = 0.0f;
    float m_dashVelocityY = 0.0f;
    float m_dashSeconds = 0.0f;
    std::optional<float> m_pendingJumpVelocity;
    float m_pendingJumpLift = 0.0f;
    float m_burningDamageTimerSeconds = 0.0f;
    std::string m_movementStatusText;
    std::vector<size_t> m_steppedFloorFaces;
};
}
