#pragma once

#include "game/maps/MapRuntimeRestrictions.h"
#include "game/maps/MapArenaDefinition.h"
#include "game/maps/MapDeltaData.h"
#include "game/maps/MapItemSourceData.h"
#include "game/indoor/IndoorMapData.h"

#include <cstdint>
#include <optional>
#include <array>
#include <string>
#include <vector>

namespace OpenYAMM::Game
{
struct IndoorSceneEnvironment
{
    int64_t lastVisitTime = 0;
    std::string skyTexture;
    int32_t dayBitsRaw = 0;
    uint32_t mapExtraBitsRaw = 0;
    int32_t fogWeakDistance = 0;
    int32_t fogStrongDistance = 0;
    int32_t ceiling = 0;
    std::array<uint8_t, 24> mapExtraReserved = {};
};

struct IndoorSceneDecorationFlag
{
    size_t entityIndex = 0;
    uint16_t flag = 0;
};

struct IndoorSceneFaceAttributeOverride
{
    size_t faceIndex = 0;
    std::optional<uint32_t> legacyAttributes;
    std::optional<uint8_t> facetType;
    std::optional<std::string> textureName;
    std::optional<uint16_t> textureFrameTableCog;
    std::optional<int16_t> textureDeltaU;
    std::optional<int16_t> textureDeltaV;
    std::optional<std::vector<int16_t>> textureUs;
    std::optional<std::vector<int16_t>> textureVs;
    std::optional<uint16_t> cogNumber;
    std::optional<uint16_t> cogTriggered;
    std::optional<uint16_t> cogTriggerType;
};

struct IndoorSceneDoor
{
    size_t doorIndex = 0;
    MapDeltaDoor door = {};
    // The door's `mover` block (devdocs/INDOOR_ROTATING_MOVERS_AND_LADDERS.md); none for every MM6-MM8 door.
    std::optional<IndoorMoverDefinition> mover;
};

struct IndoorSceneSpawn
{
    size_t spawnIndex = 0;
    IndoorSpawn spawn = {};
};

struct IndoorScenePartyStartPoint
{
    size_t startIndex = 0;
    std::string sourceName;
    IndoorPartyStartPoint startPoint = {};
};

struct IndoorSceneInitialState
{
    MapDeltaLocationInfo locationInfo = {};
    std::vector<uint8_t> visibleOutlines;
    std::vector<IndoorSceneFaceAttributeOverride> faceAttributeOverrides;
    std::vector<IndoorSceneDecorationFlag> decorationFlags;
    std::vector<MapDeltaActor> actors;
    std::vector<MapDeltaSpriteObject> spriteObjects;
    std::vector<MapDeltaChest> chests;
    std::vector<IndoorSceneDoor> doors;
    MapDeltaPersistentVariables eventVariables = {};
};

// Scene `translucent_faces` entry (devdocs/INDOOR_TRANSLUCENT_FACES.md): faces drawn translucent, one setting each.
struct IndoorSceneTranslucentFaces
{
    std::vector<uint16_t> faceIds;
    IndoorFaceTranslucency translucency = {};
};

struct IndoorSceneData
{
    int formatVersion = 0;
    std::string geometryFile;
    std::optional<std::string> legacyCompanionFile;
    MapRuntimeRestrictions runtimeRestrictions = {};
    std::optional<MapArenaDefinition> arena;
    IndoorSceneEnvironment environment = {};
    bool hasSpawns = false;
    std::vector<IndoorSceneSpawn> spawns;
    std::vector<IndoorScenePartyStartPoint> partyStartPoints;
    IndoorSceneInitialState initialState = {};
    MapItemSourceData itemSources = {};
    std::vector<IndoorSceneTranslucentFaces> translucentFaces;
    // Scene `ladders` (devdocs/INDOOR_ROTATING_MOVERS_AND_LADDERS.md).
    std::vector<IndoorLadder> ladders;
    // Scene `fog` (devdocs/INDOOR_FOG.md).
    IndoorFog fog;
};

class IndoorSceneYmlLoader
{
public:
    std::optional<IndoorSceneData> loadFromText(const std::string &yamlText, std::string &errorMessage) const;
    bool applyOverlayFromText(
        IndoorSceneData &sceneData,
        const std::string &yamlText,
        std::string &errorMessage) const;
};

const IndoorSceneFaceAttributeOverride *findIndoorSceneFaceOverride(const IndoorSceneData &sceneData, size_t faceIndex);
IndoorSceneFaceAttributeOverride *findIndoorSceneFaceOverride(IndoorSceneData &sceneData, size_t faceIndex);
void applyIndoorSceneFaceOverride(const IndoorSceneFaceAttributeOverride &overrideEntry, IndoorFace &face);

// Parses a `translucent_faces` sequence (appended to `entries`).
bool parseIndoorSceneTranslucentFaces(
    const std::string &yamlText,
    std::vector<IndoorSceneTranslucentFaces> &entries,
    std::string &errorMessage);

// Parses a `fog` map: color_rgb (three numbers 0..255), near_distance, far_distance (0 <= near < far); false with the
// reason for anything else.
bool parseIndoorSceneFog(const std::string &yamlText, IndoorFog &fog, std::string &errorMessage);

// The map's per-face translucency from the scene entries, sorted by face index; false for a face out of range, one
// listed twice, an opacity outside 0..1 or a negative tint.
bool buildIndoorTranslucentFaces(
    const std::vector<IndoorSceneTranslucentFaces> &entries,
    size_t faceCount,
    std::vector<IndoorTranslucentFace> &faces,
    std::string &errorMessage);

// The map's mover definitions from the scene doors' `mover` blocks, sorted by door id; false (with the reason) for a
// rotating mover that is not rigid (it moves part of a face or a portal, or shares a vertex with another door), whose
// travel does not match its angle (move_length = tenths of a degree), whose axis is not a unit vector or that has a
// linear direction, or for easing on a linear door.
bool buildIndoorMoverDefinitions(
    const std::vector<IndoorSceneDoor> &doors,
    const IndoorMapData &indoorMapData,
    std::vector<IndoorMoverDefinition> &movers,
    std::string &errorMessage);

bool buildIndoorMapStateFromScene(
    const IndoorSceneData &sceneData,
    IndoorMapData &indoorMapData,
    MapDeltaData &mapDeltaData,
    std::string &errorMessage);
}
