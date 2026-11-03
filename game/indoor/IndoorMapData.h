#pragma once

#include "game/indoor/IndoorFog.h"
#include "game/maps/MapPresentation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
struct IndoorBakedLighting;

struct IndoorVertex
{
    int x = 0;
    int y = 0;
    int z = 0;
};

struct IndoorFace
{
    uint32_t attributes = 0;
    // Imported 16.16 plane orientation remains defined when a closed door collapses its portal.
    // Procedurally authored faces without this field derive it from their vertex winding.
    std::optional<std::array<int32_t, 3>> planeNormal;
    std::vector<uint16_t> vertexIndices;
    std::vector<int16_t> textureUs;
    std::vector<int16_t> textureVs;
    int16_t textureDeltaU = 0;
    int16_t textureDeltaV = 0;
    // Face +0x3A: the engine's texture index. Animated faces store their texture frame table sequence here, which
    // the original engines use instead of the texture name.
    int16_t textureFrameTableIndex = -1;
    uint16_t textureFrameTableCog = 0;
    uint16_t cogNumber = 0;
    uint16_t cogTriggered = 0;
    uint16_t cogTriggerType = 0;
    uint16_t lightLevel = 0;
    uint16_t roomNumber = 0;
    uint16_t roomBehindNumber = 0;
    uint8_t facetType = 0;
    std::string textureName;
    bool isPortal = false;
};

struct IndoorSector
{
    int32_t flags = 0;
    int32_t unknown0 = 0;
    uint16_t floorCount = 0;
    uint16_t wallCount = 0;
    uint16_t ceilingCount = 0;
    uint16_t fluidCount = 0;
    int16_t portalCount = 0;
    uint16_t faceCount = 0;
    uint16_t nonBspFaceCount = 0;
    uint16_t cylinderFaceCount = 0;
    uint16_t cogCount = 0;
    uint16_t decorationCount = 0;
    uint16_t markerCount = 0;
    uint16_t lightCount = 0;
    int16_t waterLevel = 0;
    int16_t mistLevel = 0;
    int16_t lightDistanceMultiplier = 0;
    int16_t minAmbientLightLevel = 0;
    int16_t firstBspNode = 0;
    int16_t exitTag = 0;
    int16_t minX = 0;
    int16_t maxX = 0;
    int16_t minY = 0;
    int16_t maxY = 0;
    int16_t minZ = 0;
    int16_t maxZ = 0;
    std::vector<uint16_t> floorFaceIds;
    std::vector<uint16_t> wallFaceIds;
    std::vector<uint16_t> ceilingFaceIds;
    std::vector<uint16_t> faceIds;
    std::vector<uint16_t> nonBspFaceIds;
    std::vector<uint16_t> cylinderFaceIds;
    std::vector<uint16_t> lightIds;
    std::vector<uint16_t> decorationIds;
    std::vector<uint16_t> portalFaceIds;
};

struct IndoorLight
{
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
    int16_t radius = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t type = 0;
    int16_t attributes = 0;
    int16_t brightness = 0;
    int32_t id = 0;
};

struct IndoorBspNode
{
    int16_t front = 0;
    int16_t back = 0;
    int16_t faceIdOffset = 0;
    int16_t faceCount = 0;
};

struct IndoorOutline
{
    uint16_t vertex1Id = 0;
    uint16_t vertex2Id = 0;
    uint16_t face1Id = 0;
    uint16_t face2Id = 0;
    int16_t z = 0;
    uint16_t flags = 0;
};

struct IndoorEntity
{
    uint16_t decorationListId = 0;
    uint16_t aiAttributes = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int facing = 0;
    uint16_t eventIdPrimary = 0;
    uint16_t eventIdSecondary = 0;
    uint16_t variablePrimary = 0;
    uint16_t variableSecondary = 0;
    uint16_t specialTrigger = 0;
    std::string name;

    uint16_t scriptEventId() const
    {
        return eventIdSecondary;
    }

    uint32_t spriteOverrideKey(size_t entityIndex) const
    {
        return eventIdPrimary != 0 ? eventIdPrimary : static_cast<uint32_t>(entityIndex);
    }
};

struct IndoorSpawn
{
    int x = 0;
    int y = 0;
    int z = 0;
    uint16_t radius = 0;
    uint16_t typeId = 0;
    uint16_t index = 0;
    uint16_t attributes = 0;
    uint32_t group = 0;
};

// A translucent face (devdocs/INDOOR_TRANSLUCENT_FACES.md): drawn after the opaque scene, alpha-blended, its texture
// colour times `tint` at the texture's alpha times `opacity`. Opt-in per face through the map's scene file.
struct IndoorFaceTranslucency
{
    float opacity = 1.0f;
    std::array<float, 3> tint = {1.0f, 1.0f, 1.0f};
    // A single-sided face is drawn only from the side its plane normal faces.
    bool twoSided = false;

    bool operator==(const IndoorFaceTranslucency &other) const = default;
};

struct IndoorTranslucentFace
{
    uint16_t faceIndex = 0;
    IndoorFaceTranslucency translucency = {};
};

// A door's rotating and MM9 behaviours (devdocs/INDOOR_ROTATING_MOVERS_AND_LADDERS.md), opt-in per door through the
// scene's door `mover` block; no MM6-MM8 door has one.
enum class IndoorMoverEasing : uint8_t
{
    Linear = 0,
    Sine,
};

// What the mover does when a moving face meets the party or a monster.
enum class IndoorMoverBodyRule : uint8_t
{
    Ignore = 0,
    Hold,
    Reverse,
    Push,
};

struct IndoorMoverRotation
{
    // A point on the hinge line and its unit axis (native units); positive angles turn right-handed about the axis.
    std::array<float, 3> pivot = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> axis = {0.0f, 0.0f, 1.0f};
    // From the closed (stored) pose to the open pose.
    float openDegrees = 0.0f;
    // The sign is chosen when the door starts to open from closed, away from the party.
    bool openAway = false;
};

struct IndoorMoverDefinition
{
    uint32_t doorId = 0;
    std::optional<IndoorMoverRotation> rotation;
    IndoorMoverEasing easing = IndoorMoverEasing::Linear;
    IndoorMoverBodyRule bodies = IndoorMoverBodyRule::Ignore;
    // Grounded bodies standing on its faces move with it.
    bool carry = false;
    // An opened door closes itself after this long (0: never).
    float autoCloseSeconds = 0.0f;
};

// A climbable strip (devdocs/INDOOR_ROTATING_MOVERS_AND_LADDERS.md, "Ladders"), from the scene's `ladders`.
struct IndoorLadder
{
    std::string id;
    // The strip's centre line from the lower floor to where it ends.
    std::array<float, 3> bottom = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> top = {0.0f, 0.0f, 0.0f};
    // The direction a climber faces (toward the ladder), degrees about +z from +x.
    float facingDegrees = 0.0f;
    float width = 64.0f;
    // How far in front of the strip a body may stand and grab it.
    float reach = 48.0f;
    // Where the party stands after climbing off at the top.
    std::array<float, 3> topExit = {0.0f, 0.0f, 0.0f};
};

struct IndoorPartyStartPoint
{
    int x = 0;
    int y = 0;
    int z = 0;
    int facingDegrees = 0;
};

struct IndoorMapData
{
    int version = 0;
    int32_t faceDataSizeBytes = 0;
    int32_t sectorDataSizeBytes = 0;
    int32_t sectorLightDataSizeBytes = 0;
    int32_t doorsDataSizeBytes = 0;
    uint32_t rawFaceCount = 0;
    std::vector<IndoorVertex> vertices;
    std::vector<IndoorFace> faces;
    std::vector<IndoorSector> sectors;
    std::vector<uint16_t> sectorData;
    std::vector<uint16_t> sectorLightData;
    std::vector<IndoorEntity> entities;
    std::vector<IndoorLight> lights;
    std::vector<IndoorBspNode> bspNodes;
    std::vector<IndoorSpawn> spawns;
    std::vector<IndoorOutline> outlines;
    std::optional<IndoorPartyStartPoint> partyStartPoint;
    std::optional<MapPresentation> mapPresentation;
    // The map's baked lighting sidecar (IndoorBakedLighting.h) when it has a valid one; null renders unbaked.
    std::shared_ptr<const IndoorBakedLighting> bakedLighting;
    // The scene's translucent faces, sorted by face index (empty for every map without them).
    std::vector<IndoorTranslucentFace> translucentFaces;
    // The scene's door `mover` blocks, sorted by door id, and its ladders (both empty for every MM6-MM8 map).
    std::vector<IndoorMoverDefinition> moverDefinitions;
    std::vector<IndoorLadder> ladders;
    // The scene's fog (disabled for every map without it).
    IndoorFog fog;
    uint32_t doorCount = 0;
    size_t sectorCount = 0;
    size_t spriteCount = 0;
    size_t lightCount = 0;
    size_t spawnCount = 0;
};

std::vector<uint32_t> resolveIndoorLightReferenceIds(const IndoorMapData &indoorMapData, int32_t rawReferenceId);

// The face's translucency, or null when it is opaque (always null on a map without translucent faces).
const IndoorFaceTranslucency *findIndoorFaceTranslucency(const IndoorMapData &indoorMapData, size_t faceIndex);

// The door's mover definition, or null for an ordinary linear door (always null on a map without them).
const IndoorMoverDefinition *findIndoorMoverDefinition(const IndoorMapData &indoorMapData, uint32_t doorId);

class IndoorMapDataLoader
{
public:
    std::optional<IndoorMapData> loadFromBytes(const std::vector<uint8_t> &bytes) const;
};

class IndoorMapDataWriter
{
public:
    std::optional<std::vector<uint8_t>> buildBytes(const IndoorMapData &indoorMapData) const;
};
}
