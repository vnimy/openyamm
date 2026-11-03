#include "game/maps/IndoorSceneYml.h"

#include "game/StringUtils.h"
#include "game/maps/MapItemSourceYml.h"
#include "game/maps/SceneOverlayOperations.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <sstream>
#include <vector>

namespace OpenYAMM::Game
{
namespace
{
constexpr size_t FaceAttributeMapVariableCount = 75;
constexpr size_t FaceAttributeDecorVariableCount = 125;
constexpr size_t IndoorVisibleOutlinesBytes = 875;
constexpr size_t SpriteObjectContainingItemSize = 0x24;
constexpr size_t ChestItemPayloadSize = 140 * 36;

template <typename ValueType>
bool readScalarNode(
    const YAML::Node &parentNode,
    const char *key,
    ValueType &value,
    std::string &errorMessage,
    bool required = true)
{
    const YAML::Node childNode = parentNode[key];

    if (!childNode)
    {
        if (required)
        {
            errorMessage = std::string("missing required field: ") + key;
            return false;
        }

        return true;
    }

    if (!childNode.IsScalar())
    {
        errorMessage = std::string("field must be a scalar: ") + key;
        return false;
    }

    try
    {
        value = childNode.as<ValueType>();
    }
    catch (const std::exception &exception)
    {
        errorMessage = std::string("could not parse field ") + key + ": " + exception.what();
        return false;
    }

    return true;
}

template <typename ValueType>
bool readOptionalScalarNode(
    const YAML::Node &parentNode,
    const char *key,
    std::optional<ValueType> &value,
    std::string &errorMessage)
{
    ValueType parsedValue = {};

    if (!readScalarNode(parentNode, key, parsedValue, errorMessage, false))
    {
        return false;
    }

    if (parentNode[key])
    {
        value = parsedValue;
    }
    else
    {
        value.reset();
    }

    return true;
}

template <typename CoordinateType>
bool parsePositionNode(
    const YAML::Node &node,
    CoordinateType &x,
    CoordinateType &y,
    CoordinateType &z,
    std::string &errorMessage)
{
    if (!node)
    {
        errorMessage = "position must be a map or 3-value sequence";
        return false;
    }

    if (node.IsMap())
    {
        return readScalarNode(node, "x", x, errorMessage)
            && readScalarNode(node, "y", y, errorMessage)
            && readScalarNode(node, "z", z, errorMessage);
    }

    if (node.IsSequence() && node.size() == 3)
    {
        try
        {
            x = node[0].as<CoordinateType>();
            y = node[1].as<CoordinateType>();
            z = node[2].as<CoordinateType>();
            return true;
        }
        catch (const std::exception &exception)
        {
            errorMessage = std::string("could not parse position sequence: ") + exception.what();
            return false;
        }
    }

    errorMessage = "position must be a map or 3-value sequence";
    return false;
}

// A placed actor's patrol (scene patrol): a sequence of [x, y, z] waypoints; absent is none.
bool parsePatrolNode(const YAML::Node &node, std::vector<std::array<int32_t, 3>> &points, std::string &errorMessage)
{
    points.clear();

    if (!node)
    {
        return true;
    }

    if (!node.IsSequence())
    {
        errorMessage = "patrol must be a list of [x, y, z] waypoints";
        return false;
    }

    for (const YAML::Node &point : node)
    {
        if (!point.IsSequence() || point.size() != 3)
        {
            errorMessage = "patrol must be a list of [x, y, z] waypoints";
            return false;
        }

        points.push_back({point[0].as<int32_t>(), point[1].as<int32_t>(), point[2].as<int32_t>()});
    }

    return true;
}

bool parseArenaDefinition(
    const YAML::Node &rootNode,
    std::optional<MapArenaDefinition> &definition,
    std::string &errorMessage)
{
    const YAML::Node node = rootNode["arena"];
    if (!node)
    {
        return true;
    }
    if (!node.IsMap())
    {
        errorMessage = "arena must be a map";
        return false;
    }
    MapArenaDefinition arena = {};
    if (!readScalarNode(node, "minimum_monster_id", arena.minimumMonsterId, errorMessage)
        || !readScalarNode(node, "maximum_monster_id", arena.maximumMonsterId, errorMessage)
        || !parsePositionNode(node["party_position"], arena.partyPosition.x,
            arena.partyPosition.y, arena.partyPosition.z, errorMessage)
        || !readScalarNode(node, "party_direction_degrees", arena.partyDirectionDegrees, errorMessage))
    {
        return false;
    }
    if (arena.minimumMonsterId <= 0 || arena.maximumMonsterId < arena.minimumMonsterId)
    {
        errorMessage = "arena monster id range is invalid";
        return false;
    }
    const YAML::Node positions = node["monster_positions"];
    if (!positions || !positions.IsSequence() || positions.size() < 20)
    {
        errorMessage = "arena.monster_positions must contain at least 20 positions";
        return false;
    }
    for (const YAML::Node &position : positions)
    {
        ArenaPosition parsed = {};
        if (!parsePositionNode(position, parsed.x, parsed.y, parsed.z, errorMessage))
        {
            return false;
        }
        arena.monsterPositions.push_back(parsed);
    }
    definition = std::move(arena);
    return true;
}

bool parseBoolFlagConsistency(
    const YAML::Node &flagsNode,
    const char *key,
    bool expectedValue,
    std::string &errorMessage)
{
    bool actualValue = false;

    if (!readScalarNode(flagsNode, key, actualValue, errorMessage))
    {
        return false;
    }

    if (actualValue != expectedValue)
    {
        errorMessage = std::string("flag does not match raw legacy bits: ") + key;
        return false;
    }

    return true;
}

bool parseHexBytes(
    const std::string &text,
    size_t expectedSize,
    std::vector<uint8_t> &bytes,
    std::string &errorMessage)
{
    bytes.clear();

    if (text.size() != expectedSize * 2)
    {
        std::ostringstream stream;
        stream << "hex payload has wrong length, expected " << (expectedSize * 2)
               << " characters, got " << text.size();
        errorMessage = stream.str();
        return false;
    }

    bytes.reserve(expectedSize);

    const auto decodeNibble = [](char character) -> int
    {
        if (character >= '0' && character <= '9')
        {
            return character - '0';
        }

        const char lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

        if (lowered >= 'a' && lowered <= 'f')
        {
            return 10 + lowered - 'a';
        }

        return -1;
    };

    for (size_t offset = 0; offset < text.size(); offset += 2)
    {
        const int highNibble = decodeNibble(text[offset]);
        const int lowNibble = decodeNibble(text[offset + 1]);

        if (highNibble < 0 || lowNibble < 0)
        {
            errorMessage = "hex payload contains non-hex characters";
            return false;
        }

        bytes.push_back(uint8_t((highNibble << 4) | lowNibble));
    }

    return true;
}

bool parseHexByteArray(
    const std::string &text,
    std::array<uint8_t, 24> &bytes,
    std::string &errorMessage)
{
    std::vector<uint8_t> parsedBytes;

    if (!parseHexBytes(text, bytes.size(), parsedBytes, errorMessage))
    {
        return false;
    }

    std::copy(parsedBytes.begin(), parsedBytes.end(), bytes.begin());
    return true;
}

template <typename ValueType>
bool parseIntegerSequence(
    const YAML::Node &node,
    std::vector<ValueType> &values,
    std::string &errorMessage)
{
    values.clear();

    if (!node || !node.IsSequence())
    {
        errorMessage = "sequence field must be a YAML sequence";
        return false;
    }

    values.reserve(node.size());

    for (const YAML::Node &entryNode : node)
    {
        if (!entryNode.IsScalar())
        {
            errorMessage = "sequence entry must be scalar";
            return false;
        }

        try
        {
            values.push_back(entryNode.as<ValueType>());
        }
        catch (const std::exception &exception)
        {
            errorMessage = std::string("could not parse sequence entry: ") + exception.what();
            return false;
        }
    }

    return true;
}

bool parseFixedIntSequence(
    const YAML::Node &node,
    size_t expectedSize,
    std::vector<int> &values,
    std::string &errorMessage)
{
    if (!parseIntegerSequence(node, values, errorMessage))
    {
        return false;
    }

    if (values.size() != expectedSize)
    {
        std::ostringstream stream;
        stream << "sequence field has wrong length, expected " << expectedSize
               << ", got " << values.size();
        errorMessage = stream.str();
        return false;
    }

    return true;
}

template <typename ValueType>
bool parseOptionalIntegerSequence(
    const YAML::Node &parentNode,
    const char *key,
    std::optional<std::vector<ValueType>> &values,
    std::string &errorMessage)
{
    const YAML::Node sequenceNode = parentNode[key];

    if (!sequenceNode)
    {
        values.reset();
        return true;
    }

    std::vector<ValueType> parsedValues;

    if (!parseIntegerSequence(sequenceNode, parsedValues, errorMessage))
    {
        errorMessage = std::string(key) + ": " + errorMessage;
        return false;
    }

    values = std::move(parsedValues);
    return true;
}

void encodeSceneMapExtra(uint32_t mapExtraBitsRaw, int32_t ceiling, std::array<uint8_t, 24> &reservedBytes)
{
    reservedBytes.fill(0);
    std::memcpy(reservedBytes.data(), &mapExtraBitsRaw, sizeof(mapExtraBitsRaw));
    std::memcpy(reservedBytes.data() + sizeof(mapExtraBitsRaw), &ceiling, sizeof(ceiling));
}

size_t totalIndoorFaceCount(const IndoorMapData &indoorMapData)
{
    return indoorMapData.rawFaceCount != 0 ? indoorMapData.rawFaceCount : indoorMapData.faces.size();
}

int normalizeDegrees(int degrees)
{
    int normalizedDegrees = degrees % 360;

    if (normalizedDegrees < 0)
    {
        normalizedDegrees += 360;
    }

    return normalizedDegrees;
}

int yawUnitsToDegrees(int yawUnits)
{
    const int normalizedYawUnits = ((yawUnits % 2048) + 2048) % 2048;
    return normalizeDegrees(normalizedYawUnits * 360 / 2048);
}

bool parseIndoorScenePartyStartPoint(
    const YAML::Node &startNode,
    IndoorScenePartyStartPoint &partyStartPoint,
    std::string &errorMessage)
{
    if (!startNode.IsMap())
    {
        errorMessage = "party start point entry must be a map";
        return false;
    }

    if (!readScalarNode(startNode, "start_index", partyStartPoint.startIndex, errorMessage)
        || !readScalarNode(startNode, "source_name", partyStartPoint.sourceName, errorMessage, false)
        || !parsePositionNode(
            startNode["position"],
            partyStartPoint.startPoint.x,
            partyStartPoint.startPoint.y,
            partyStartPoint.startPoint.z,
            errorMessage))
    {
        return false;
    }

    double directionDegrees = 0.0;

    if (readScalarNode(startNode, "direction_degrees", directionDegrees, errorMessage, false))
    {
        if (startNode["direction_degrees"])
        {
            partyStartPoint.startPoint.facingDegrees =
                normalizeDegrees(static_cast<int>(std::lround(directionDegrees)));
            return true;
        }
    }
    else
    {
        return false;
    }

    int directionYawUnits = 0;

    if (!readScalarNode(startNode, "direction_yaw_units", directionYawUnits, errorMessage, false))
    {
        return false;
    }

    partyStartPoint.startPoint.facingDegrees = yawUnitsToDegrees(directionYawUnits);
    return true;
}

const IndoorScenePartyStartPoint *defaultIndoorPartyStartPoint(const IndoorSceneData &sceneData)
{
    if (sceneData.partyStartPoints.empty())
    {
        return nullptr;
    }

    for (const IndoorScenePartyStartPoint &partyStartPoint : sceneData.partyStartPoints)
    {
        if (toLowerCopy(partyStartPoint.sourceName) == "startpoint0")
        {
            return &partyStartPoint;
        }
    }

    return &sceneData.partyStartPoints.front();
}

bool parseIndoorSceneFaceAttributeOverride(
    const YAML::Node &overrideNode,
    IndoorSceneFaceAttributeOverride &faceOverride,
    std::string &errorMessage)
{
    if (!overrideNode.IsMap())
    {
        errorMessage = "face attribute override entry must be a map";
        return false;
    }

    if (!readScalarNode(overrideNode, "face_index", faceOverride.faceIndex, errorMessage)
        || !readOptionalScalarNode(overrideNode, "legacy_attributes", faceOverride.legacyAttributes, errorMessage)
        || !readOptionalScalarNode(overrideNode, "facet_type", faceOverride.facetType, errorMessage)
        || !readOptionalScalarNode(overrideNode, "texture_name", faceOverride.textureName, errorMessage)
        || !readOptionalScalarNode(
            overrideNode,
            "texture_frame_table_cog",
            faceOverride.textureFrameTableCog,
            errorMessage)
        || !readOptionalScalarNode(overrideNode, "texture_delta_u", faceOverride.textureDeltaU, errorMessage)
        || !readOptionalScalarNode(overrideNode, "texture_delta_v", faceOverride.textureDeltaV, errorMessage)
        || !parseOptionalIntegerSequence(overrideNode, "texture_us", faceOverride.textureUs, errorMessage)
        || !parseOptionalIntegerSequence(overrideNode, "texture_vs", faceOverride.textureVs, errorMessage)
        || !readOptionalScalarNode(overrideNode, "cog_number", faceOverride.cogNumber, errorMessage)
        || !readOptionalScalarNode(overrideNode, "cog_triggered", faceOverride.cogTriggered, errorMessage)
        || !readOptionalScalarNode(
            overrideNode,
            "cog_trigger_type",
            faceOverride.cogTriggerType,
            errorMessage))
    {
        return false;
    }

    if (!faceOverride.legacyAttributes.has_value()
        && !faceOverride.facetType.has_value()
        && !faceOverride.textureName.has_value()
        && !faceOverride.textureFrameTableCog.has_value()
        && !faceOverride.textureDeltaU.has_value()
        && !faceOverride.textureDeltaV.has_value()
        && !faceOverride.textureUs.has_value()
        && !faceOverride.textureVs.has_value()
        && !faceOverride.cogNumber.has_value()
        && !faceOverride.cogTriggered.has_value()
        && !faceOverride.cogTriggerType.has_value())
    {
        errorMessage = "face attribute override must define at least one override field";
        return false;
    }

    return true;
}

void mergeIndoorSceneFaceAttributeOverride(
    IndoorSceneData &sceneData,
    const IndoorSceneFaceAttributeOverride &sourceOverride)
{
    IndoorSceneFaceAttributeOverride *pTargetOverride =
        findIndoorSceneFaceOverride(sceneData, sourceOverride.faceIndex);

    if (pTargetOverride == nullptr)
    {
        sceneData.initialState.faceAttributeOverrides.push_back(sourceOverride);
        return;
    }

    if (sourceOverride.legacyAttributes.has_value())
    {
        pTargetOverride->legacyAttributes = sourceOverride.legacyAttributes;
    }

    if (sourceOverride.facetType.has_value())
    {
        pTargetOverride->facetType = sourceOverride.facetType;
    }

    if (sourceOverride.textureName.has_value())
    {
        pTargetOverride->textureName = sourceOverride.textureName;
    }

    if (sourceOverride.textureFrameTableCog.has_value())
    {
        pTargetOverride->textureFrameTableCog = sourceOverride.textureFrameTableCog;
    }

    if (sourceOverride.textureDeltaU.has_value())
    {
        pTargetOverride->textureDeltaU = sourceOverride.textureDeltaU;
    }

    if (sourceOverride.textureDeltaV.has_value())
    {
        pTargetOverride->textureDeltaV = sourceOverride.textureDeltaV;
    }

    if (sourceOverride.textureUs.has_value())
    {
        pTargetOverride->textureUs = sourceOverride.textureUs;
    }

    if (sourceOverride.textureVs.has_value())
    {
        pTargetOverride->textureVs = sourceOverride.textureVs;
    }

    if (sourceOverride.cogNumber.has_value())
    {
        pTargetOverride->cogNumber = sourceOverride.cogNumber;
    }

    if (sourceOverride.cogTriggered.has_value())
    {
        pTargetOverride->cogTriggered = sourceOverride.cogTriggered;
    }

    if (sourceOverride.cogTriggerType.has_value())
    {
        pTargetOverride->cogTriggerType = sourceOverride.cogTriggerType;
    }
}

// `translucent_faces`: a sequence of {faces: [ids], opacity, tint: [r, g, b], two_sided}; opacity, tint and
// two_sided are optional (1, white, false).
bool parseFloatTriple(const YAML::Node &node, std::array<float, 3> &value, std::string &errorMessage)
{
    return parsePositionNode(node, value[0], value[1], value[2], errorMessage);
}

// A door's `mover` block: rotation {pivot, axis, open_degrees, open_away}, easing, bodies, carry, auto_close_seconds.
bool parseIndoorMoverNode(
    const YAML::Node &node,
    uint32_t doorId,
    IndoorMoverDefinition &mover,
    std::string &errorMessage)
{
    if (!node.IsMap())
    {
        errorMessage = "door mover must be a map";
        return false;
    }

    mover.doorId = doorId;
    std::string easing = "linear";
    std::string bodies = "ignore";

    if (!readScalarNode(node, "easing", easing, errorMessage, false)
        || !readScalarNode(node, "bodies", bodies, errorMessage, false)
        || !readScalarNode(node, "carry", mover.carry, errorMessage, false)
        || !readScalarNode(node, "auto_close_seconds", mover.autoCloseSeconds, errorMessage, false))
    {
        return false;
    }

    if (easing == "linear")
    {
        mover.easing = IndoorMoverEasing::Linear;
    }
    else if (easing == "sine")
    {
        mover.easing = IndoorMoverEasing::Sine;
    }
    else
    {
        errorMessage = "door mover easing must be linear or sine: " + easing;
        return false;
    }

    if (bodies == "ignore")
    {
        mover.bodies = IndoorMoverBodyRule::Ignore;
    }
    else if (bodies == "hold")
    {
        mover.bodies = IndoorMoverBodyRule::Hold;
    }
    else if (bodies == "reverse")
    {
        mover.bodies = IndoorMoverBodyRule::Reverse;
    }
    else if (bodies == "push")
    {
        mover.bodies = IndoorMoverBodyRule::Push;
    }
    else
    {
        errorMessage = "door mover bodies must be ignore, hold, reverse or push: " + bodies;
        return false;
    }

    if (!std::isfinite(mover.autoCloseSeconds) || mover.autoCloseSeconds < 0.0f)
    {
        errorMessage = "door mover auto_close_seconds must be zero or positive";
        return false;
    }

    if (const YAML::Node rotationNode = node["rotation"]; rotationNode)
    {
        if (!rotationNode.IsMap())
        {
            errorMessage = "door mover rotation must be a map";
            return false;
        }

        IndoorMoverRotation rotation = {};

        if (!parseFloatTriple(rotationNode["pivot"], rotation.pivot, errorMessage)
            || !parseFloatTriple(rotationNode["axis"], rotation.axis, errorMessage)
            || !readScalarNode(rotationNode, "open_degrees", rotation.openDegrees, errorMessage)
            || !readScalarNode(rotationNode, "open_away", rotation.openAway, errorMessage, false))
        {
            return false;
        }

        mover.rotation = rotation;
    }

    return true;
}

// Scene `ladders`: a sequence of {id, bottom, top, facing_degrees, width, reach, top_exit}.
bool parseIndoorLaddersNode(const YAML::Node &node, std::vector<IndoorLadder> &ladders, std::string &errorMessage)
{
    if (!node.IsSequence())
    {
        errorMessage = "ladders must be a sequence";
        return false;
    }

    for (const YAML::Node &ladderNode : node)
    {
        IndoorLadder ladder = {};

        if (!ladderNode.IsMap()
            || !readScalarNode(ladderNode, "id", ladder.id, errorMessage)
            || !parseFloatTriple(ladderNode["bottom"], ladder.bottom, errorMessage)
            || !parseFloatTriple(ladderNode["top"], ladder.top, errorMessage)
            || !readScalarNode(ladderNode, "facing_degrees", ladder.facingDegrees, errorMessage)
            || !readScalarNode(ladderNode, "width", ladder.width, errorMessage, false)
            || !readScalarNode(ladderNode, "reach", ladder.reach, errorMessage, false)
            || !parseFloatTriple(ladderNode["top_exit"], ladder.topExit, errorMessage))
        {
            errorMessage = errorMessage.empty() ? "ladder entry must be a map" : "ladder: " + errorMessage;
            return false;
        }

        if (ladder.top[2] <= ladder.bottom[2] || ladder.width <= 0.0f || ladder.reach <= 0.0f)
        {
            errorMessage = "ladder " + ladder.id + " needs top above bottom and a positive width and reach";
            return false;
        }

        ladders.push_back(std::move(ladder));
    }

    return true;
}

bool parseTranslucentFacesNode(
    const YAML::Node &node,
    std::vector<IndoorSceneTranslucentFaces> &entries,
    std::string &errorMessage)
{
    if (!node.IsSequence())
    {
        errorMessage = "translucent_faces must be a sequence";
        return false;
    }

    for (const YAML::Node &entryNode : node)
    {
        if (!entryNode.IsMap())
        {
            errorMessage = "translucent_faces entry must be a map";
            return false;
        }

        IndoorSceneTranslucentFaces entry = {};
        const YAML::Node facesNode = entryNode["faces"];
        const YAML::Node tintNode = entryNode["tint"];

        if (!facesNode || !facesNode.IsSequence() || facesNode.size() == 0)
        {
            errorMessage = "translucent_faces entry needs a non-empty faces sequence";
            return false;
        }

        if (tintNode && (!tintNode.IsSequence() || tintNode.size() != 3))
        {
            errorMessage = "translucent_faces tint must be three numbers";
            return false;
        }

        try
        {
            for (const YAML::Node &faceNode : facesNode)
            {
                entry.faceIds.push_back(faceNode.as<uint16_t>());
            }

            for (size_t channel = 0; tintNode && channel < 3; ++channel)
            {
                entry.translucency.tint[channel] = tintNode[channel].as<float>();
            }
        }
        catch (const std::exception &exception)
        {
            errorMessage = std::string("could not parse translucent_faces entry: ") + exception.what();
            return false;
        }

        if (!readScalarNode(entryNode, "opacity", entry.translucency.opacity, errorMessage, false)
            || !readScalarNode(entryNode, "two_sided", entry.translucency.twoSided, errorMessage, false))
        {
            return false;
        }

        entries.push_back(std::move(entry));
    }

    return true;
}
}

// `fog`: {color_rgb: [r, g, b], near_distance, far_distance} in native units (devdocs/INDOOR_FOG.md).
bool parseFogNode(const YAML::Node &node, IndoorFog &fog, std::string &errorMessage)
{
    const YAML::Node colorNode = node.IsMap() ? node["color_rgb"] : YAML::Node();

    if (!colorNode || !colorNode.IsSequence() || colorNode.size() != 3 || !node["near_distance"]
        || !node["far_distance"])
    {
        errorMessage = "fog must be a map of color_rgb (three numbers), near_distance and far_distance";
        return false;
    }

    IndoorFog parsed = {};

    try
    {
        for (size_t channel = 0; channel < 3; ++channel)
        {
            const float value = colorNode[channel].as<float>();

            if (!(value >= 0.0f && value <= 255.0f))
            {
                errorMessage = "fog color_rgb must be within 0..255";
                return false;
            }

            parsed.color[channel] = value / 255.0f;
        }

        parsed.nearDistance = node["near_distance"].as<float>();
        parsed.farDistance = node["far_distance"].as<float>();
    }
    catch (const std::exception &exception)
    {
        errorMessage = std::string("could not parse fog: ") + exception.what();
        return false;
    }

    if (!(parsed.nearDistance >= 0.0f && parsed.nearDistance < parsed.farDistance))
    {
        errorMessage = "fog requires 0 <= near_distance < far_distance";
        return false;
    }

    parsed.enabled = true;
    fog = parsed;
    return true;
}

bool parseIndoorSceneFog(const std::string &yamlText, IndoorFog &fog, std::string &errorMessage)
{
    try
    {
        return parseFogNode(YAML::Load(yamlText), fog, errorMessage);
    }
    catch (const std::exception &exception)
    {
        errorMessage = std::string("could not parse fog: ") + exception.what();
        return false;
    }
}

bool parseIndoorSceneTranslucentFaces(
    const std::string &yamlText,
    std::vector<IndoorSceneTranslucentFaces> &entries,
    std::string &errorMessage)
{
    try
    {
        return parseTranslucentFacesNode(YAML::Load(yamlText), entries, errorMessage);
    }
    catch (const std::exception &exception)
    {
        errorMessage = std::string("could not parse translucent_faces: ") + exception.what();
        return false;
    }
}

bool buildIndoorTranslucentFaces(
    const std::vector<IndoorSceneTranslucentFaces> &entries,
    size_t faceCount,
    std::vector<IndoorTranslucentFace> &faces,
    std::string &errorMessage)
{
    faces.clear();

    for (const IndoorSceneTranslucentFaces &entry : entries)
    {
        const IndoorFaceTranslucency &translucency = entry.translucency;

        if (!(translucency.opacity >= 0.0f && translucency.opacity <= 1.0f)
            || !std::all_of(translucency.tint.begin(), translucency.tint.end(),
                [](float value) { return std::isfinite(value) && value >= 0.0f; }))
        {
            errorMessage = "translucent_faces opacity must be within 0..1 and tint non-negative";
            return false;
        }

        for (uint16_t faceIndex : entry.faceIds)
        {
            if (faceIndex >= faceCount)
            {
                errorMessage = "translucent_faces face " + std::to_string(faceIndex) + " is out of range";
                return false;
            }

            faces.push_back({faceIndex, translucency});
        }
    }

    std::sort(faces.begin(), faces.end(),
        [](const IndoorTranslucentFace &left, const IndoorTranslucentFace &right)
        {
            return left.faceIndex < right.faceIndex;
        });

    for (size_t index = 1; index < faces.size(); ++index)
    {
        if (faces[index].faceIndex == faces[index - 1].faceIndex)
        {
            errorMessage = "translucent_faces lists face " + std::to_string(faces[index].faceIndex) + " twice";
            return false;
        }
    }

    return true;
}

const IndoorSceneFaceAttributeOverride *findIndoorSceneFaceOverride(const IndoorSceneData &sceneData, size_t faceIndex)
{
    for (const IndoorSceneFaceAttributeOverride &overrideEntry : sceneData.initialState.faceAttributeOverrides)
    {
        if (overrideEntry.faceIndex == faceIndex)
        {
            return &overrideEntry;
        }
    }

    return nullptr;
}

IndoorSceneFaceAttributeOverride *findIndoorSceneFaceOverride(IndoorSceneData &sceneData, size_t faceIndex)
{
    for (IndoorSceneFaceAttributeOverride &overrideEntry : sceneData.initialState.faceAttributeOverrides)
    {
        if (overrideEntry.faceIndex == faceIndex)
        {
            return &overrideEntry;
        }
    }

    return nullptr;
}

void applyIndoorSceneFaceOverride(const IndoorSceneFaceAttributeOverride &overrideEntry, IndoorFace &face)
{
    if (overrideEntry.legacyAttributes.has_value())
    {
        face.attributes = *overrideEntry.legacyAttributes;
    }

    if (overrideEntry.facetType.has_value())
    {
        face.facetType = *overrideEntry.facetType;
    }

    if (overrideEntry.textureName.has_value())
    {
        face.textureName = *overrideEntry.textureName;
    }

    if (overrideEntry.textureFrameTableCog.has_value())
    {
        face.textureFrameTableCog = *overrideEntry.textureFrameTableCog;
    }

    if (overrideEntry.textureDeltaU.has_value())
    {
        face.textureDeltaU = *overrideEntry.textureDeltaU;
    }

    if (overrideEntry.textureDeltaV.has_value())
    {
        face.textureDeltaV = *overrideEntry.textureDeltaV;
    }

    if (overrideEntry.textureUs.has_value())
    {
        face.textureUs = *overrideEntry.textureUs;
    }

    if (overrideEntry.textureVs.has_value())
    {
        face.textureVs = *overrideEntry.textureVs;
    }

    if (overrideEntry.cogNumber.has_value())
    {
        face.cogNumber = *overrideEntry.cogNumber;
    }

    if (overrideEntry.cogTriggered.has_value())
    {
        face.cogTriggered = *overrideEntry.cogTriggered;
    }

    if (overrideEntry.cogTriggerType.has_value())
    {
        face.cogTriggerType = *overrideEntry.cogTriggerType;
    }
}

std::optional<IndoorSceneData> IndoorSceneYmlLoader::loadFromText(
    const std::string &yamlText,
    std::string &errorMessage) const
{
    IndoorSceneData sceneData = {};
    YAML::Node rootNode;

    try
    {
        rootNode = YAML::Load(yamlText);
    }
    catch (const std::exception &exception)
    {
        errorMessage = exception.what();
        return std::nullopt;
    }

    if (!rootNode || !rootNode.IsMap())
    {
        errorMessage = "scene yaml root must be a map";
        return std::nullopt;
    }

    if (!readScalarNode(rootNode, "format_version", sceneData.formatVersion, errorMessage))
    {
        return std::nullopt;
    }

    if (sceneData.formatVersion != 1)
    {
        errorMessage = "unsupported indoor scene format_version";
        return std::nullopt;
    }

    std::string kind;

    if (!readScalarNode(rootNode, "kind", kind, errorMessage) || toLowerCopy(kind) != "indoor_scene")
    {
        errorMessage = "kind must be \"indoor_scene\"";
        return std::nullopt;
    }

    if (!parseArenaDefinition(rootNode, sceneData.arena, errorMessage)
        || !parseMapItemSourceData(rootNode, sceneData.itemSources, errorMessage))
    {
        return std::nullopt;
    }

    const YAML::Node sourceNode = rootNode["source"];

    if (!sourceNode || !sourceNode.IsMap())
    {
        errorMessage = "source must be a map";
        return std::nullopt;
    }

    if (!readScalarNode(sourceNode, "geometry_file", sceneData.geometryFile, errorMessage))
    {
        return std::nullopt;
    }

    std::string legacyCompanionFile;

    if (readScalarNode(sourceNode, "legacy_companion_file", legacyCompanionFile, errorMessage, false)
        && !legacyCompanionFile.empty())
    {
        sceneData.legacyCompanionFile = legacyCompanionFile;
    }

    const YAML::Node runtimeRestrictionsNode = rootNode["runtime_restrictions"];

    if (runtimeRestrictionsNode)
    {
        if (!runtimeRestrictionsNode.IsMap())
        {
            errorMessage = "runtime_restrictions must be a map";
            return std::nullopt;
        }

        if (!readScalarNode(
                runtimeRestrictionsNode,
                "allow_save_game",
                sceneData.runtimeRestrictions.allowSaveGame,
                errorMessage,
                false)
            || !readScalarNode(
                runtimeRestrictionsNode,
                "allow_lloyds_beacon",
                sceneData.runtimeRestrictions.allowLloydsBeacon,
                errorMessage,
                false)
            || !readScalarNode(
                runtimeRestrictionsNode,
                "allow_rest",
                sceneData.runtimeRestrictions.allowRest,
                errorMessage,
                false)
            || !readScalarNode(
                runtimeRestrictionsNode,
                "arena",
                sceneData.runtimeRestrictions.isArena,
                errorMessage,
                false))
        {
            return std::nullopt;
        }
    }

    const YAML::Node environmentNode = rootNode["environment"];

    if (!environmentNode || !environmentNode.IsMap())
    {
        errorMessage = "environment must be a map";
        return std::nullopt;
    }

    if (!readScalarNode(environmentNode, "last_visit_time", sceneData.environment.lastVisitTime, errorMessage, false)
        || !readScalarNode(environmentNode, "sky_texture", sceneData.environment.skyTexture, errorMessage)
        || !readScalarNode(environmentNode, "day_bits_raw", sceneData.environment.dayBitsRaw, errorMessage)
        || !readScalarNode(
            environmentNode,
            "map_extra_bits_raw",
            sceneData.environment.mapExtraBitsRaw,
            errorMessage)
        || !readScalarNode(environmentNode, "ceiling", sceneData.environment.ceiling, errorMessage))
    {
        return std::nullopt;
    }

    const YAML::Node mapExtraReservedNode = environmentNode["map_extra_reserved_hex"];

    if (mapExtraReservedNode)
    {
        std::string mapExtraReservedHex;

        if (!readScalarNode(environmentNode, "map_extra_reserved_hex", mapExtraReservedHex, errorMessage)
            || !parseHexByteArray(
                mapExtraReservedHex,
                sceneData.environment.mapExtraReserved,
                errorMessage))
        {
            return std::nullopt;
        }
    }
    else
    {
        encodeSceneMapExtra(
            sceneData.environment.mapExtraBitsRaw,
            sceneData.environment.ceiling,
            sceneData.environment.mapExtraReserved);
    }

    const YAML::Node flagsNode = environmentNode["flags"];
    const YAML::Node fogNode = environmentNode["fog"];

    if (!flagsNode || !flagsNode.IsMap())
    {
        errorMessage = "environment.flags must be a map";
        return std::nullopt;
    }

    if (!fogNode || !fogNode.IsMap())
    {
        errorMessage = "environment.fog must be a map";
        return std::nullopt;
    }

    if (!readScalarNode(fogNode, "weak_distance", sceneData.environment.fogWeakDistance, errorMessage)
        || !readScalarNode(fogNode, "strong_distance", sceneData.environment.fogStrongDistance, errorMessage))
    {
        return std::nullopt;
    }

    if (!parseBoolFlagConsistency(flagsNode, "foggy", (sceneData.environment.dayBitsRaw & 0x1) != 0, errorMessage)
        || !parseBoolFlagConsistency(
            flagsNode,
            "raining",
            (sceneData.environment.mapExtraBitsRaw & 0x1) != 0,
            errorMessage)
        || !parseBoolFlagConsistency(
            flagsNode,
            "snowing",
            (sceneData.environment.mapExtraBitsRaw & 0x2) != 0,
            errorMessage)
        || !parseBoolFlagConsistency(
            flagsNode,
            "underwater",
            (sceneData.environment.mapExtraBitsRaw & 0x4) != 0,
            errorMessage)
        || !parseBoolFlagConsistency(
            flagsNode,
            "no_terrain",
            (sceneData.environment.mapExtraBitsRaw & 0x8) != 0,
            errorMessage)
        || !parseBoolFlagConsistency(
            flagsNode,
            "always_dark",
            (sceneData.environment.mapExtraBitsRaw & 0x10) != 0,
            errorMessage)
        || !parseBoolFlagConsistency(
            flagsNode,
            "always_light",
            (sceneData.environment.mapExtraBitsRaw & 0x20) != 0,
            errorMessage)
        || !parseBoolFlagConsistency(
            flagsNode,
            "always_foggy",
            (sceneData.environment.mapExtraBitsRaw & 0x40) != 0,
            errorMessage)
        || !parseBoolFlagConsistency(
            flagsNode,
            "red_fog",
            (sceneData.environment.mapExtraBitsRaw & 0x80) != 0,
            errorMessage))
    {
        return std::nullopt;
    }

    const YAML::Node spawnsNode = rootNode["spawns"];
    const YAML::Node partyStartPointsNode = rootNode["party_start_points"];

    if (spawnsNode)
    {
        if (!spawnsNode.IsSequence())
        {
            errorMessage = "spawns must be a sequence";
            return std::nullopt;
        }

        sceneData.hasSpawns = true;
        sceneData.spawns.reserve(spawnsNode.size());

        for (const YAML::Node &spawnNode : spawnsNode)
        {
            if (!spawnNode.IsMap())
            {
                errorMessage = "spawn entry must be a map";
                return std::nullopt;
            }

            IndoorSceneSpawn spawn = {};

            if (!readScalarNode(spawnNode, "spawn_index", spawn.spawnIndex, errorMessage)
                || !readScalarNode(spawnNode, "radius", spawn.spawn.radius, errorMessage)
                || !readScalarNode(spawnNode, "type_id", spawn.spawn.typeId, errorMessage)
                || !readScalarNode(spawnNode, "index", spawn.spawn.index, errorMessage)
                || !readScalarNode(spawnNode, "attributes", spawn.spawn.attributes, errorMessage)
                || !readScalarNode(spawnNode, "group", spawn.spawn.group, errorMessage)
                || !parsePositionNode(spawnNode["position"], spawn.spawn.x, spawn.spawn.y, spawn.spawn.z, errorMessage))
            {
                return std::nullopt;
            }

            sceneData.spawns.push_back(std::move(spawn));
        }
    }

    if (partyStartPointsNode)
    {
        if (!partyStartPointsNode.IsSequence())
        {
            errorMessage = "party_start_points must be a sequence";
            return std::nullopt;
        }

        sceneData.partyStartPoints.reserve(partyStartPointsNode.size());

        for (const YAML::Node &startNode : partyStartPointsNode)
        {
            IndoorScenePartyStartPoint partyStartPoint = {};

            if (!parseIndoorScenePartyStartPoint(startNode, partyStartPoint, errorMessage))
            {
                return std::nullopt;
            }

            sceneData.partyStartPoints.push_back(std::move(partyStartPoint));
        }
    }

    if (const YAML::Node translucentFacesNode = rootNode["translucent_faces"];
        translucentFacesNode
        && !parseTranslucentFacesNode(translucentFacesNode, sceneData.translucentFaces, errorMessage))
    {
        return std::nullopt;
    }

    if (const YAML::Node fogNode = rootNode["fog"]; fogNode && !parseFogNode(fogNode, sceneData.fog, errorMessage))
    {
        return std::nullopt;
    }

    if (const YAML::Node laddersNode = rootNode["ladders"];
        laddersNode && !parseIndoorLaddersNode(laddersNode, sceneData.ladders, errorMessage))
    {
        return std::nullopt;
    }

    const YAML::Node initialStateNode = rootNode["initial_state"];
    const YAML::Node locationNode = initialStateNode ? initialStateNode["location"] : YAML::Node();
    const YAML::Node visibleOutlinesNode = initialStateNode ? initialStateNode["visible_outlines"] : YAML::Node();
    const YAML::Node faceAttributeOverridesNode =
        initialStateNode ? initialStateNode["face_attribute_overrides"] : YAML::Node();
    const YAML::Node decorationFlagsNode = initialStateNode ? initialStateNode["decoration_flags"] : YAML::Node();
    const YAML::Node actorsNode = initialStateNode ? initialStateNode["actors"] : YAML::Node();
    const YAML::Node spriteObjectsNode = initialStateNode ? initialStateNode["sprite_objects"] : YAML::Node();
    const YAML::Node chestsNode = initialStateNode ? initialStateNode["chests"] : YAML::Node();
    const YAML::Node doorsNode = initialStateNode ? initialStateNode["doors"] : YAML::Node();
    const YAML::Node variablesNode = initialStateNode ? initialStateNode["variables"] : YAML::Node();

    if (!initialStateNode || !initialStateNode.IsMap()
        || !locationNode || !locationNode.IsMap()
        || !visibleOutlinesNode || !visibleOutlinesNode.IsMap()
        || !faceAttributeOverridesNode || !faceAttributeOverridesNode.IsSequence()
        || !decorationFlagsNode || !decorationFlagsNode.IsSequence()
        || !actorsNode || !actorsNode.IsSequence()
        || !spriteObjectsNode || !spriteObjectsNode.IsSequence()
        || !chestsNode || !chestsNode.IsSequence()
        || !doorsNode || !doorsNode.IsSequence()
        || !variablesNode || !variablesNode.IsMap())
    {
        errorMessage = "initial_state has invalid structure";
        return std::nullopt;
    }

    if (!readScalarNode(locationNode, "respawn_count", sceneData.initialState.locationInfo.respawnCount, errorMessage)
        || !readScalarNode(
            locationNode,
            "last_respawn_day",
            sceneData.initialState.locationInfo.lastRespawnDay,
            errorMessage)
        || !readScalarNode(locationNode, "reputation", sceneData.initialState.locationInfo.reputation, errorMessage)
        || !readScalarNode(
            locationNode,
            "alert_status",
            sceneData.initialState.locationInfo.alertStatus,
            errorMessage))
    {
        return std::nullopt;
    }

    std::string visibleOutlinesHex;

    if (!readScalarNode(visibleOutlinesNode, "bitset_hex", visibleOutlinesHex, errorMessage)
        || !parseHexBytes(
            visibleOutlinesHex,
            IndoorVisibleOutlinesBytes,
            sceneData.initialState.visibleOutlines,
            errorMessage))
    {
        return std::nullopt;
    }

    sceneData.initialState.faceAttributeOverrides.reserve(faceAttributeOverridesNode.size());

    for (const YAML::Node &overrideNode : faceAttributeOverridesNode)
    {
        IndoorSceneFaceAttributeOverride faceOverride = {};

        if (!parseIndoorSceneFaceAttributeOverride(overrideNode, faceOverride, errorMessage))
        {
            return std::nullopt;
        }

        sceneData.initialState.faceAttributeOverrides.push_back(faceOverride);
    }

    sceneData.initialState.decorationFlags.reserve(decorationFlagsNode.size());

    for (const YAML::Node &flagNode : decorationFlagsNode)
    {
        if (!flagNode.IsMap())
        {
            errorMessage = "decoration flag entry must be a map";
            return std::nullopt;
        }

        IndoorSceneDecorationFlag decorationFlag = {};

        if (!readScalarNode(flagNode, "entity_index", decorationFlag.entityIndex, errorMessage)
            || !readScalarNode(flagNode, "flag", decorationFlag.flag, errorMessage))
        {
            return std::nullopt;
        }

        sceneData.initialState.decorationFlags.push_back(decorationFlag);
    }

    sceneData.initialState.actors.reserve(actorsNode.size());

    for (size_t actorIndex = 0; actorIndex < actorsNode.size(); ++actorIndex)
    {
        const YAML::Node actorNode = actorsNode[actorIndex];

        if (!actorNode.IsMap())
        {
            errorMessage = "actor entry must be a map";
            return std::nullopt;
        }

        MapDeltaActor actor = {};
        actor.diagnosticSourceActorIndex = actorIndex;

        if (!readScalarNode(actorNode, "name", actor.name, errorMessage)
            || !readScalarNode(actorNode, "npc_id", actor.npcId, errorMessage)
            || !readScalarNode(actorNode, "mm9_rude_id", actor.mm9RudeId, errorMessage, false)
            || !readScalarNode(
                actorNode,
                "mm9_source_object_index",
                actor.mm9SourceObjectIndex,
                errorMessage,
                false)
            || !readScalarNode(
                actorNode,
                "mm9_can_receive_damage",
                actor.mm9CanReceiveDamage,
                errorMessage,
                false)
            || !readScalarNode(actorNode, "mm9_civilian", actor.mm9Civilian, errorMessage, false)
            || !readScalarNode(actorNode, "mm9_guard", actor.mm9Guard, errorMessage, false)
            || !readScalarNode(actorNode, "initial_yaw_units", actor.initialYawUnits, errorMessage, false)
            || !readScalarNode(actorNode, "immobile", actor.immobile, errorMessage, false)
            || !readScalarNode(actorNode, "leash_radius", actor.leashRadius, errorMessage, false)
            || !readScalarNode(actorNode, "dormant", actor.dormant, errorMessage, false)
            || !parsePatrolNode(actorNode["patrol"], actor.patrolPoints, errorMessage)
            || !readScalarNode(actorNode, "keeps_height", actor.keepsHeight, errorMessage, false)
            || !readScalarNode(actorNode, "attributes", actor.attributes, errorMessage)
            || !readScalarNode(actorNode, "hp", actor.hp, errorMessage)
            || !readScalarNode(actorNode, "hostility_type", actor.hostilityType, errorMessage)
            || !readScalarNode(actorNode, "monster_info_id", actor.monsterInfoId, errorMessage)
            || !readScalarNode(actorNode, "monster_id", actor.monsterId, errorMessage)
            || !readScalarNode(actorNode, "radius", actor.radius, errorMessage)
            || !readScalarNode(actorNode, "height", actor.height, errorMessage)
            || !readScalarNode(actorNode, "move_speed", actor.moveSpeed, errorMessage)
            || !readScalarNode(actorNode, "sector_id", actor.sectorId, errorMessage)
            || !readScalarNode(
                actorNode,
                "current_action_animation",
                actor.currentActionAnimation,
                errorMessage)
            || !readScalarNode(actorNode, "group", actor.group, errorMessage)
            || !readScalarNode(actorNode, "ally", actor.ally, errorMessage)
            || !readScalarNode(actorNode, "unique_name_index", actor.uniqueNameIndex, errorMessage)
            || !parsePositionNode(actorNode["position"], actor.x, actor.y, actor.z, errorMessage))
        {
            return std::nullopt;
        }

        if (!readScalarNode(actorNode, "carried_item_id", actor.carriedItemId, errorMessage, false))
        {
            actor.carriedItemId = 0;
        }

        const YAML::Node spriteIdsNode = actorNode["sprite_ids"];

        if (!spriteIdsNode || !spriteIdsNode.IsSequence() || spriteIdsNode.size() != actor.spriteIds.size())
        {
            errorMessage = "actor.sprite_ids must have exactly 4 entries";
            return std::nullopt;
        }

        for (size_t spriteIndex = 0; spriteIndex < actor.spriteIds.size(); ++spriteIndex)
        {
            if (!spriteIdsNode[spriteIndex].IsScalar())
            {
                errorMessage = "actor sprite_ids entries must be scalar";
                return std::nullopt;
            }

            try
            {
                actor.spriteIds[spriteIndex] = spriteIdsNode[spriteIndex].as<uint16_t>();
            }
            catch (const std::exception &exception)
            {
                errorMessage = std::string("could not parse actor sprite id: ") + exception.what();
                return std::nullopt;
            }
        }

        sceneData.initialState.actors.push_back(std::move(actor));
    }

    sceneData.initialState.spriteObjects.reserve(spriteObjectsNode.size());

    for (const YAML::Node &spriteObjectNode : spriteObjectsNode)
    {
        if (!spriteObjectNode.IsMap())
        {
            errorMessage = "sprite object entry must be a map";
            return std::nullopt;
        }

        MapDeltaSpriteObject spriteObject = {};
        std::string rawContainingItemHex;

        if (!readScalarNode(spriteObjectNode, "sprite_id", spriteObject.spriteId, errorMessage)
            || !readScalarNode(
                spriteObjectNode,
                "object_description_id",
                spriteObject.objectDescriptionId,
                errorMessage)
            || !readScalarNode(spriteObjectNode, "yaw_angle", spriteObject.yawAngle, errorMessage)
            || !readScalarNode(spriteObjectNode, "sound_id", spriteObject.soundId, errorMessage)
            || !readScalarNode(spriteObjectNode, "attributes", spriteObject.attributes, errorMessage)
            || !readScalarNode(spriteObjectNode, "sector_id", spriteObject.sectorId, errorMessage)
            || !readScalarNode(
                spriteObjectNode,
                "time_since_created",
                spriteObject.timeSinceCreated,
                errorMessage)
            || !readScalarNode(
                spriteObjectNode,
                "temporary_lifetime",
                spriteObject.temporaryLifetime,
                errorMessage)
            || !readScalarNode(
                spriteObjectNode,
                "glow_radius_multiplier",
                spriteObject.glowRadiusMultiplier,
                errorMessage)
            || !readScalarNode(spriteObjectNode, "spell_id", spriteObject.spellId, errorMessage)
            || !readScalarNode(spriteObjectNode, "spell_level", spriteObject.spellLevel, errorMessage)
            || !readScalarNode(spriteObjectNode, "spell_skill", spriteObject.spellSkill, errorMessage)
            || !readScalarNode(spriteObjectNode, "field54", spriteObject.field54, errorMessage)
            || !readScalarNode(
                spriteObjectNode,
                "spell_caster_pid",
                spriteObject.spellCasterPid,
                errorMessage)
            || !readScalarNode(
                spriteObjectNode,
                "spell_target_pid",
                spriteObject.spellTargetPid,
                errorMessage)
            || !readScalarNode(spriteObjectNode, "lod_distance", spriteObject.lodDistance, errorMessage)
            || !readScalarNode(
                spriteObjectNode,
                "spell_caster_ability",
                spriteObject.spellCasterAbility,
                errorMessage)
            || !readScalarNode(
                spriteObjectNode,
                "raw_containing_item_hex",
                rawContainingItemHex,
                errorMessage)
            || !parsePositionNode(
                spriteObjectNode["position"],
                spriteObject.x,
                spriteObject.y,
                spriteObject.z,
                errorMessage)
            || !parsePositionNode(
                spriteObjectNode["velocity"],
                spriteObject.velocityX,
                spriteObject.velocityY,
                spriteObject.velocityZ,
                errorMessage)
            || !parsePositionNode(
                spriteObjectNode["initial_position"],
                spriteObject.initialX,
                spriteObject.initialY,
                spriteObject.initialZ,
                errorMessage))
        {
            return std::nullopt;
        }

        if (!parseHexBytes(
                rawContainingItemHex,
                SpriteObjectContainingItemSize,
                spriteObject.rawContainingItem,
                errorMessage))
        {
            return std::nullopt;
        }

        sceneData.initialState.spriteObjects.push_back(std::move(spriteObject));
    }

    sceneData.initialState.chests.reserve(chestsNode.size());

    for (const YAML::Node &chestNode : chestsNode)
    {
        if (!chestNode.IsMap())
        {
            errorMessage = "chest entry must be a map";
            return std::nullopt;
        }

        MapDeltaChest chest = {};
        std::string rawItemsHex;
        std::vector<uint8_t> rawItems;
        std::vector<int> inventoryMatrixValues;

        if (!readScalarNode(chestNode, "chest_type_id", chest.chestTypeId, errorMessage)
            || !readScalarNode(chestNode, "flags", chest.flags, errorMessage)
            || !readScalarNode(chestNode, "raw_items_hex", rawItemsHex, errorMessage)
            || !parseFixedIntSequence(
                chestNode["inventory_matrix"],
                140,
                inventoryMatrixValues,
                errorMessage))
        {
            return std::nullopt;
        }

        if (!parseHexBytes(rawItemsHex, ChestItemPayloadSize, rawItems, errorMessage))
        {
            return std::nullopt;
        }

        chest.rawItems = std::move(rawItems);
        chest.inventoryMatrix.reserve(inventoryMatrixValues.size());

        for (int value : inventoryMatrixValues)
        {
            chest.inventoryMatrix.push_back(static_cast<int16_t>(value));
        }

        sceneData.initialState.chests.push_back(std::move(chest));
    }

    sceneData.initialState.doors.reserve(doorsNode.size());

    for (const YAML::Node &doorNode : doorsNode)
    {
        if (!doorNode.IsMap())
        {
            errorMessage = "door entry must be a map";
            return std::nullopt;
        }

        IndoorSceneDoor sceneDoor = {};

        if (!readScalarNode(doorNode, "door_index", sceneDoor.doorIndex, errorMessage)
            || !readScalarNode(doorNode, "legacy_attributes", sceneDoor.door.attributes, errorMessage)
            || !readScalarNode(doorNode, "door_id", sceneDoor.door.doorId, errorMessage)
            || !readScalarNode(
                doorNode,
                "time_since_triggered_ms",
                sceneDoor.door.timeSinceTriggered,
                errorMessage)
            || !readScalarNode(doorNode, "move_length", sceneDoor.door.moveLength, errorMessage)
            || !readScalarNode(doorNode, "open_speed", sceneDoor.door.openSpeed, errorMessage)
            || !readScalarNode(doorNode, "close_speed", sceneDoor.door.closeSpeed, errorMessage)
            || !readScalarNode(doorNode, "state", sceneDoor.door.state, errorMessage)
            || !parsePositionNode(
                doorNode["direction"],
                sceneDoor.door.directionX,
                sceneDoor.door.directionY,
                sceneDoor.door.directionZ,
                errorMessage)
            || !parseIntegerSequence(doorNode["vertex_ids"], sceneDoor.door.vertexIds, errorMessage)
            || !parseIntegerSequence(doorNode["face_ids"], sceneDoor.door.faceIds, errorMessage)
            || !parseIntegerSequence(doorNode["sector_ids"], sceneDoor.door.sectorIds, errorMessage)
            || !parseIntegerSequence(doorNode["delta_us"], sceneDoor.door.deltaUs, errorMessage)
            || !parseIntegerSequence(doorNode["delta_vs"], sceneDoor.door.deltaVs, errorMessage)
            || !parseIntegerSequence(doorNode["x_offsets"], sceneDoor.door.xOffsets, errorMessage)
            || !parseIntegerSequence(doorNode["y_offsets"], sceneDoor.door.yOffsets, errorMessage)
            || !parseIntegerSequence(doorNode["z_offsets"], sceneDoor.door.zOffsets, errorMessage))
        {
            return std::nullopt;
        }

        if (const YAML::Node moverNode = doorNode["mover"]; moverNode)
        {
            IndoorMoverDefinition mover = {};

            if (!parseIndoorMoverNode(moverNode, sceneDoor.door.doorId, mover, errorMessage))
            {
                return std::nullopt;
            }

            sceneDoor.mover = mover;
        }

        sceneDoor.door.slotIndex = sceneDoor.doorIndex;
        sceneDoor.door.numVertices = sceneDoor.door.vertexIds.size();
        sceneDoor.door.numFaces = sceneDoor.door.faceIds.size();
        sceneDoor.door.numSectors = sceneDoor.door.sectorIds.size();
        sceneDoor.door.numOffsets = sceneDoor.door.xOffsets.size();
        sceneData.initialState.doors.push_back(std::move(sceneDoor));
    }

    std::vector<int> mapVariableValues;
    std::vector<int> decorVariableValues;

    if (!parseFixedIntSequence(
            variablesNode["map"],
            FaceAttributeMapVariableCount,
            mapVariableValues,
            errorMessage)
        || !parseFixedIntSequence(
            variablesNode["decor"],
            FaceAttributeDecorVariableCount,
            decorVariableValues,
            errorMessage))
    {
        return std::nullopt;
    }

    for (size_t index = 0; index < sceneData.initialState.eventVariables.mapVars.size(); ++index)
    {
        sceneData.initialState.eventVariables.mapVars[index] = static_cast<uint8_t>(mapVariableValues[index]);
    }

    for (size_t index = 0; index < sceneData.initialState.eventVariables.decorVars.size(); ++index)
    {
        sceneData.initialState.eventVariables.decorVars[index] = static_cast<uint8_t>(decorVariableValues[index]);
    }

    return sceneData;
}

bool IndoorSceneYmlLoader::applyOverlayFromText(
    IndoorSceneData &sceneData,
    const std::string &yamlText,
    std::string &errorMessage) const
{
    YAML::Node rootNode;

    try
    {
        rootNode = YAML::Load(yamlText);
    }
    catch (const std::exception &exception)
    {
        errorMessage = exception.what();
        return false;
    }

    if (!rootNode || !rootNode.IsMap())
    {
        errorMessage = "scene overlay yaml root must be a map";
        return false;
    }

    int formatVersion = 0;

    if (!readScalarNode(rootNode, "format_version", formatVersion, errorMessage))
    {
        return false;
    }

    if (formatVersion != 1)
    {
        errorMessage = "unsupported indoor scene overlay format_version";
        return false;
    }

    std::string kind;

    if (!readScalarNode(rootNode, "kind", kind, errorMessage) || toLowerCopy(kind) != "indoor_scene_overlay")
    {
        errorMessage = "kind must be \"indoor_scene_overlay\"";
        return false;
    }

    if (!parseArenaDefinition(rootNode, sceneData.arena, errorMessage))
    {
        return false;
    }

    const YAML::Node sourceNode = rootNode["source"];

    if (sourceNode)
    {
        if (!sourceNode.IsMap())
        {
            errorMessage = "source must be a map";
            return false;
        }

        std::string geometryFile;

        if (!readScalarNode(sourceNode, "geometry_file", geometryFile, errorMessage, false))
        {
            return false;
        }

        if (!geometryFile.empty() && toLowerCopy(geometryFile) != toLowerCopy(sceneData.geometryFile))
        {
            errorMessage = "scene overlay geometry_file does not match base scene";
            return false;
        }
    }

    const YAML::Node runtimeRestrictionsNode = rootNode["runtime_restrictions"];

    if (runtimeRestrictionsNode)
    {
        if (!runtimeRestrictionsNode.IsMap())
        {
            errorMessage = "runtime_restrictions must be a map";
            return false;
        }

        if (!readScalarNode(
                runtimeRestrictionsNode,
                "allow_save_game",
                sceneData.runtimeRestrictions.allowSaveGame,
                errorMessage,
                false)
            || !readScalarNode(
                runtimeRestrictionsNode,
                "allow_lloyds_beacon",
                sceneData.runtimeRestrictions.allowLloydsBeacon,
                errorMessage,
                false)
            || !readScalarNode(
                runtimeRestrictionsNode,
                "allow_rest",
                sceneData.runtimeRestrictions.allowRest,
                errorMessage,
                false)
            || !readScalarNode(
                runtimeRestrictionsNode,
                "arena",
                sceneData.runtimeRestrictions.isArena,
                errorMessage,
                false))
        {
            return false;
        }
    }

    // Spawn points a ruleset adds (the index may be left out to append) or disables.
    if (const YAML::Node authoredContentNode = rootNode["authored_content"])
    {
        if (const YAML::Node spawnsNode = authoredContentNode["spawns"])
        {
            if (!spawnsNode.IsSequence())
            {
                errorMessage = "authored_content.spawns must be a sequence";
                return false;
            }

            for (const YAML::Node &spawnNode : spawnsNode)
            {
                IndoorSceneSpawn spawn = {};
                spawn.spawnIndex = sceneData.spawns.size();

                if (!spawnNode.IsMap()
                    || !readScalarNode(spawnNode, "spawn_index", spawn.spawnIndex, errorMessage, false)
                    || !readScalarNode(spawnNode, "radius", spawn.spawn.radius, errorMessage)
                    || !readScalarNode(spawnNode, "type_id", spawn.spawn.typeId, errorMessage)
                    || !readScalarNode(spawnNode, "index", spawn.spawn.index, errorMessage)
                    || !readScalarNode(spawnNode, "attributes", spawn.spawn.attributes, errorMessage, false)
                    || !readScalarNode(spawnNode, "group", spawn.spawn.group, errorMessage, false)
                    || !parsePositionNode(spawnNode["position"], spawn.spawn.x, spawn.spawn.y, spawn.spawn.z,
                        errorMessage))
                {
                    errorMessage = errorMessage.empty() ? "spawn entry must be a map" : errorMessage;
                    return false;
                }

                sceneData.spawns.push_back(std::move(spawn));
                sceneData.hasSpawns = true;
            }
        }
    }

    if (const YAML::Node removalsNode = rootNode["removals"])
    {
        if (const YAML::Node spawnRemovals = removalsNode["spawns"];
            spawnRemovals && !applySceneSpawnRemovals(spawnRemovals, sceneData.spawns, errorMessage))
        {
            return false;
        }
    }

    const YAML::Node initialStateNode = rootNode["initial_state"];

    if (initialStateNode)
    {
        if (!initialStateNode.IsMap())
        {
            errorMessage = "initial_state must be a map";
            return false;
        }

        if (const YAML::Node actorOverridesNode = initialStateNode["actor_overrides"];
            actorOverridesNode
            && !applySceneActorOverrides(actorOverridesNode, sceneData.initialState.actors, errorMessage))
        {
            return false;
        }

        const YAML::Node faceAttributeOverridesNode = initialStateNode["face_attribute_overrides"];

        if (faceAttributeOverridesNode)
        {
            if (!faceAttributeOverridesNode.IsSequence())
            {
                errorMessage = "initial_state.face_attribute_overrides must be a sequence";
                return false;
            }

            for (const YAML::Node &overrideNode : faceAttributeOverridesNode)
            {
                IndoorSceneFaceAttributeOverride faceOverride = {};

                if (!parseIndoorSceneFaceAttributeOverride(overrideNode, faceOverride, errorMessage))
                {
                    return false;
                }

                mergeIndoorSceneFaceAttributeOverride(sceneData, faceOverride);
            }
        }
    }

    // An overlay's translucent faces add to the base scene's.
    if (const YAML::Node translucentFacesNode = rootNode["translucent_faces"];
        translucentFacesNode
        && !parseTranslucentFacesNode(translucentFacesNode, sceneData.translucentFaces, errorMessage))
    {
        return false;
    }

    // An overlay's fog replaces the base scene's.
    if (const YAML::Node fogNode = rootNode["fog"]; fogNode && !parseFogNode(fogNode, sceneData.fog, errorMessage))
    {
        return false;
    }

    return true;
}

bool buildIndoorMoverDefinitions(
    const std::vector<IndoorSceneDoor> &doors,
    const IndoorMapData &indoorMapData,
    std::vector<IndoorMoverDefinition> &movers,
    std::string &errorMessage)
{
    movers.clear();
    std::vector<uint32_t> vertexDoor;

    for (const IndoorSceneDoor &sceneDoor : doors)
    {
        if (!sceneDoor.mover)
        {
            continue;
        }

        const MapDeltaDoor &door = sceneDoor.door;
        const IndoorMoverDefinition &mover = *sceneDoor.mover;
        const std::string name = "door " + std::to_string(door.doorId) + " mover: ";

        if (!mover.rotation)
        {
            if (mover.easing != IndoorMoverEasing::Linear)
            {
                errorMessage = name + "easing needs a rotation";
                return false;
            }

            movers.push_back(mover);
            continue;
        }

        const IndoorMoverRotation &rotation = *mover.rotation;
        const float axisLength = std::sqrt(rotation.axis[0] * rotation.axis[0] + rotation.axis[1] * rotation.axis[1]
            + rotation.axis[2] * rotation.axis[2]);
        const float degrees = std::fabs(rotation.openDegrees);

        if (std::fabs(axisLength - 1.0f) > 1.0e-3f || !(degrees > 0.0f && degrees <= 360.0f))
        {
            errorMessage = name + "the axis must be a unit vector and open_degrees within (0, 360]";
            return false;
        }

        if (door.directionX != 0 || door.directionY != 0 || door.directionZ != 0
            || door.moveLength != static_cast<uint32_t>(std::lround(degrees * 10.0f)))
        {
            errorMessage = name + "a rotating door has direction zero and move_length = open_degrees x 10";
            return false;
        }

        if (vertexDoor.empty())
        {
            vertexDoor.assign(indoorMapData.vertices.size(), 0);
            for (const IndoorSceneDoor &otherDoor : doors)
            {
                for (uint16_t vertexId : otherDoor.door.vertexIds)
                {
                    if (vertexId < vertexDoor.size())
                    {
                        const uint32_t owner = vertexDoor[vertexId];
                        vertexDoor[vertexId] = owner == 0 || owner == otherDoor.door.doorId
                            ? otherDoor.door.doorId
                            : std::numeric_limits<uint32_t>::max();
                    }
                }
            }
        }

        for (uint16_t faceId : door.faceIds)
        {
            if (faceId >= indoorMapData.faces.size() || indoorMapData.faces[faceId].isPortal)
            {
                errorMessage = name + "moves a portal or a missing face";
                return false;
            }

            for (uint16_t vertexId : indoorMapData.faces[faceId].vertexIndices)
            {
                if (vertexId >= vertexDoor.size() || vertexDoor[vertexId] != door.doorId)
                {
                    errorMessage = name + "is not rigid: a face has a vertex it does not move or another door moves";
                    return false;
                }
            }
        }

        movers.push_back(mover);
    }

    std::sort(movers.begin(), movers.end(),
        [](const IndoorMoverDefinition &left, const IndoorMoverDefinition &right)
        {
            return left.doorId < right.doorId;
        });

    for (size_t index = 1; index < movers.size(); ++index)
    {
        if (movers[index].doorId == movers[index - 1].doorId)
        {
            errorMessage = "door " + std::to_string(movers[index].doorId) + " has two mover blocks";
            return false;
        }
    }

    return true;
}

bool buildIndoorMapStateFromScene(
    const IndoorSceneData &sceneData,
    IndoorMapData &indoorMapData,
    MapDeltaData &mapDeltaData,
    std::string &errorMessage)
{
    mapDeltaData = {};
    mapDeltaData.locationInfo = sceneData.initialState.locationInfo;
    mapDeltaData.locationInfo.totalFacesCount = totalIndoorFaceCount(indoorMapData);
    mapDeltaData.locationInfo.decorationCount = indoorMapData.entities.size();
    mapDeltaData.locationInfo.bmodelCount = 0;

    if (sceneData.hasSpawns)
    {
        indoorMapData.spawns.assign(sceneData.spawns.size(), {});

        for (const IndoorSceneSpawn &spawn : sceneData.spawns)
        {
            if (spawn.spawnIndex >= indoorMapData.spawns.size())
            {
                errorMessage = "scene spawn_index is out of bounds";
                return false;
            }

            indoorMapData.spawns[spawn.spawnIndex] = spawn.spawn;
        }

        indoorMapData.spawnCount = indoorMapData.spawns.size();
    }

    if (const IndoorScenePartyStartPoint *pPartyStartPoint = defaultIndoorPartyStartPoint(sceneData))
    {
        indoorMapData.partyStartPoint = pPartyStartPoint->startPoint;
    }

    if (sceneData.initialState.visibleOutlines.size() != IndoorVisibleOutlinesBytes)
    {
        errorMessage = "scene visible outline bitset has wrong size";
        return false;
    }

    mapDeltaData.visibleOutlines = sceneData.initialState.visibleOutlines;
    const size_t faceCount = totalIndoorFaceCount(indoorMapData);
    mapDeltaData.faceAttributes.reserve(faceCount);

    for (const IndoorFace &face : indoorMapData.faces)
    {
        mapDeltaData.faceAttributes.push_back(face.attributes);
    }

    if (mapDeltaData.faceAttributes.size() != faceCount)
    {
        errorMessage = "indoor geometry face count does not match indoor scene expectations";
        return false;
    }

    for (const IndoorSceneFaceAttributeOverride &faceOverride : sceneData.initialState.faceAttributeOverrides)
    {
        if (faceOverride.faceIndex >= mapDeltaData.faceAttributes.size())
        {
            errorMessage = "scene initial face attribute override is out of bounds";
            return false;
        }

        if (faceOverride.legacyAttributes.has_value())
        {
            mapDeltaData.faceAttributes[faceOverride.faceIndex] = *faceOverride.legacyAttributes;
        }

        const IndoorFace &targetFace = indoorMapData.faces[faceOverride.faceIndex];

        if (faceOverride.textureUs.has_value() && faceOverride.textureUs->size() != targetFace.vertexIndices.size())
        {
            errorMessage = "scene initial face texture_us override has wrong size";
            return false;
        }

        if (faceOverride.textureVs.has_value() && faceOverride.textureVs->size() != targetFace.vertexIndices.size())
        {
            errorMessage = "scene initial face texture_vs override has wrong size";
            return false;
        }

        applyIndoorSceneFaceOverride(faceOverride, indoorMapData.faces[faceOverride.faceIndex]);
    }

    if (!buildIndoorTranslucentFaces(
            sceneData.translucentFaces, indoorMapData.faces.size(), indoorMapData.translucentFaces, errorMessage))
    {
        return false;
    }

    if (!buildIndoorMoverDefinitions(
            sceneData.initialState.doors, indoorMapData, indoorMapData.moverDefinitions, errorMessage))
    {
        return false;
    }

    indoorMapData.ladders = sceneData.ladders;
    indoorMapData.fog = sceneData.fog;

    mapDeltaData.decorationFlags.assign(indoorMapData.entities.size(), 0);

    for (const IndoorSceneDecorationFlag &decorationFlag : sceneData.initialState.decorationFlags)
    {
        if (decorationFlag.entityIndex >= mapDeltaData.decorationFlags.size())
        {
            errorMessage = "scene decoration flag index is out of bounds";
            return false;
        }

        mapDeltaData.decorationFlags[decorationFlag.entityIndex] = decorationFlag.flag;
    }

    mapDeltaData.actors = sceneData.initialState.actors;
    mapDeltaData.spriteObjects = sceneData.initialState.spriteObjects;
    mapDeltaData.chests = sceneData.initialState.chests;
    mapDeltaData.eventVariables = sceneData.initialState.eventVariables;
    mapDeltaData.doorSlotCount = indoorMapData.doorCount;

    std::vector<IndoorSceneDoor> sortedDoors = sceneData.initialState.doors;
    std::sort(
        sortedDoors.begin(),
        sortedDoors.end(),
        [](const IndoorSceneDoor &left, const IndoorSceneDoor &right)
        {
            return left.doorIndex < right.doorIndex;
        });

    std::vector<uint8_t> seenDoorSlots(indoorMapData.doorCount, 0);
    mapDeltaData.doors.reserve(sortedDoors.size());

    for (const IndoorSceneDoor &sceneDoor : sortedDoors)
    {
        if (sceneDoor.doorIndex >= indoorMapData.doorCount)
        {
            errorMessage = "scene door_index is out of bounds";
            return false;
        }

        if (seenDoorSlots[sceneDoor.doorIndex] != 0)
        {
            errorMessage = "scene contains duplicate door_index entries";
            return false;
        }

        seenDoorSlots[sceneDoor.doorIndex] = 1;
        MapDeltaDoor door = sceneDoor.door;
        door.slotIndex = sceneDoor.doorIndex;
        door.numVertices = door.vertexIds.size();
        door.numFaces = door.faceIds.size();
        door.numSectors = door.sectorIds.size();
        door.numOffsets = door.xOffsets.size();

        if (door.deltaUs.size() != door.faceIds.size() || door.deltaVs.size() != door.faceIds.size())
        {
            errorMessage = "scene door texture delta arrays do not match face_ids";
            return false;
        }

        if (door.yOffsets.size() != door.xOffsets.size() || door.zOffsets.size() != door.xOffsets.size())
        {
            errorMessage = "scene door offset arrays must have the same length";
            return false;
        }

        for (uint16_t vertexId : door.vertexIds)
        {
            if (vertexId >= indoorMapData.vertices.size())
            {
                errorMessage = "scene door vertex id is out of bounds";
                return false;
            }
        }

        for (uint16_t faceId : door.faceIds)
        {
            if (faceId >= indoorMapData.faces.size())
            {
                errorMessage = "scene door face id is out of bounds";
                return false;
            }
        }

        for (uint16_t sectorId : door.sectorIds)
        {
            if (sectorId >= indoorMapData.sectors.size())
            {
                errorMessage = "scene door sector id is out of bounds";
                return false;
            }
        }

        mapDeltaData.doors.push_back(std::move(door));
    }

    normalizeIndoorDoorTextureDeltas(mapDeltaData, indoorMapData);

    for (const MapDeltaDoor &door : mapDeltaData.doors)
    {
        for (uint16_t value : door.vertexIds)
        {
            mapDeltaData.doorsData.push_back(int16_t(value));
        }

        for (uint16_t value : door.faceIds)
        {
            mapDeltaData.doorsData.push_back(int16_t(value));
        }

        for (uint16_t value : door.sectorIds)
        {
            mapDeltaData.doorsData.push_back(int16_t(value));
        }

        mapDeltaData.doorsData.insert(mapDeltaData.doorsData.end(), door.deltaUs.begin(), door.deltaUs.end());
        mapDeltaData.doorsData.insert(mapDeltaData.doorsData.end(), door.deltaVs.begin(), door.deltaVs.end());
        mapDeltaData.doorsData.insert(mapDeltaData.doorsData.end(), door.xOffsets.begin(), door.xOffsets.end());
        mapDeltaData.doorsData.insert(mapDeltaData.doorsData.end(), door.yOffsets.begin(), door.yOffsets.end());
        mapDeltaData.doorsData.insert(mapDeltaData.doorsData.end(), door.zOffsets.begin(), door.zOffsets.end());
    }

    mapDeltaData.locationTime.lastVisitTime = sceneData.environment.lastVisitTime;
    mapDeltaData.locationTime.skyTextureName = sceneData.environment.skyTexture;
    mapDeltaData.locationTime.weatherFlags = sceneData.environment.dayBitsRaw;
    mapDeltaData.locationTime.fogWeakDistance = sceneData.environment.fogWeakDistance;
    mapDeltaData.locationTime.fogStrongDistance = sceneData.environment.fogStrongDistance;
    mapDeltaData.locationTime.reserved = sceneData.environment.mapExtraReserved;
    std::memcpy(
        mapDeltaData.locationTime.reserved.data(),
        &sceneData.environment.mapExtraBitsRaw,
        sizeof(sceneData.environment.mapExtraBitsRaw));
    std::memcpy(
        mapDeltaData.locationTime.reserved.data() + sizeof(sceneData.environment.mapExtraBitsRaw),
        &sceneData.environment.ceiling,
        sizeof(sceneData.environment.ceiling));
    return true;
}
}
