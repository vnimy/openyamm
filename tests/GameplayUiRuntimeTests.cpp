#include "engine/AssetFileSystem.h"
#include "engine/ImageAssetLoader.h"
#include "game/app/GameSession.h"
#include "game/data/GameDataLoader.h"
#include "game/gameplay/GameplayInputFrame.h"
#include "game/gameplay/GameplayOverlayInputController.h"
#include "game/gameplay/GameplayScreenController.h"
#include "game/items/InventoryBags.h"
#include "game/items/ItemRuntime.h"
#include "game/party/Party.h"
#include "game/rules/GameRules.h"
#include "game/rules/maw/MawRules.h"
#include "game/ui/GameplayHudCommon.h"
#include "game/ui/GameplayUiRuntime.h"

#include "tests/RegressionGameData.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>

TEST_CASE("dimensional travel uses original artwork and matching continent hover regions")
{
    using namespace OpenYAMM;
    const std::filesystem::path sourceRoot = OPENYAMM_SOURCE_DIR;
    Engine::AssetFileSystem assets;
    REQUIRE(assets.initialize(sourceRoot, sourceRoot / "assets_dev", Engine::AssetScaleTier::X1));
    Game::GameDataLoader loader;
    REQUIRE(loader.loadForHeadlessGameplay(assets));
    Game::GameDataRepository data;
    data.bind(loader);
    Game::GameplayUiRuntime ui;
    ui.bindDataRepository(&data);
    CHECK_FALSE(ui.ensureDimensionDoorDestinationsLoaded(1, false));
    REQUIRE(ui.ensureDimensionDoorDestinationsLoaded(1, true));
    REQUIRE_EQ(ui.townPortalDestinations().size(), 3);

    Engine::BinaryAssetCache images;
    const std::optional<Engine::ImagePixelsBgra> background =
        Engine::loadImageAssetPixelsBgra(assets, ui.townPortalBackgroundTextureName() + ".bmp", images);
    REQUIRE(background.has_value());
    CHECK_EQ(background->width, 640);
    CHECK_EQ(background->height, 480);
    CHECK(background->assetScaleTier == Engine::AssetScaleTier::X1);

    struct ExpectedDestination
    {
        const char *pLabel;
        const char *pMapName;
        int32_t hitX;
        uint32_t hitWidth;
        int iconWidth;
        int iconHeight;
    };
    const std::array<ExpectedDestination, 3> expected = {{
        {"Jadame", "out01.odm", 0, 213, 450, 360},
        {"Enroth", "oute3.odm", 213, 214, 270, 400},
        {"Antagarich", "7out01.odm", 427, 213, 460, 360},
    }};
    for (const ExpectedDestination &entry : expected)
    {
        const auto destination = std::find_if(ui.townPortalDestinations().begin(), ui.townPortalDestinations().end(),
            [&entry](const Game::GameplayTownPortalDestination &candidate) { return candidate.label == entry.pLabel; });
        REQUIRE(destination != ui.townPortalDestinations().end());
        CHECK_EQ(destination->mapName, entry.pMapName);
        CHECK_EQ(destination->hitX, entry.hitX);
        CHECK_EQ(destination->hitWidth, entry.hitWidth);
        CHECK_EQ(destination->hitY, 0);
        CHECK_EQ(destination->hitHeight, 480);
        const std::optional<Engine::ImagePixelsBgra> icon =
            Engine::loadImageAssetPixelsBgra(assets, destination->iconTextureName + ".bmp", images);
        REQUIRE(icon.has_value());
        CHECK_EQ(icon->width, entry.iconWidth);
        CHECK_EQ(icon->height, entry.iconHeight);
        CHECK(icon->assetScaleTier == Engine::AssetScaleTier::X1);
    }
}

TEST_CASE("the press that opens a chest or dialogue is consumed before the overlay reacts to it")
{
    using namespace OpenYAMM;
    Game::GameSession session;
    Game::GameplayScreenRuntime &runtime = session.gameplayScreenRuntime();
    Game::GameplayOverlayInteractionState &state = runtime.interactionState();
    state.chestClickLatch = false;
    state.chestItemClickLatch = false;
    state.lootChestItemLatch = false;
    state.dialogueClickLatch = false;

    // A frame without a loot view or dialogue: the button that is about to open one is still held.
    Game::GameplayInputFrame input;
    input.leftMouseButton.held = true;
    Game::GameplayOverlayInputController::handleLootOverlayInput(runtime, input, false);
    runtime.resetDialogueOverlayInteractionState();

    // The overlay's first frame sees these latches set, so it neither grabs the item under the cursor nor arms a
    // button or topic for the release; each latch clears once the button is up.
    CHECK(state.chestClickLatch);
    CHECK(state.chestItemClickLatch);
    CHECK(state.lootChestItemLatch);
    CHECK(state.chestPressedTarget.type == Game::GameplayChestPointerTargetType::None);
    CHECK(state.dialogueClickLatch);
    CHECK(state.dialoguePressedTarget.type == Game::GameplayDialoguePointerTargetType::None);
}

TEST_CASE("HUD text colour codes take no room, carry across wrapped lines and tint only the main layer")
{
    using namespace OpenYAMM::Game;
    GameplayHudFontData font;
    font.fontHeight = 10;
    font.atlasCellWidth = 6;
    font.atlasWidth = 256;
    font.atlasHeight = 256;
    font.atlasScale = 1;

    for (uint32_t codePoint = 32; codePoint <= 126; ++codePoint)
    {
        OpenYAMM::Engine::FontGlyph glyph;
        glyph.metrics = {0, 5, 1, 0};
        glyph.cellWidth = 5;
        glyph.cellHeight = 10;
        font.glyphs[codePoint] = glyph;
    }

    font.mainTextureHandle = bgfx::TextureHandle {1};
    font.shadowTextureHandle = bgfx::TextureHandle {2};
    font.tintTextureHandle = bgfx::TextureHandle {3};

    // "\f02016" is Might and Magic's green; "\f00000" returns to the line's own colour.
    CHECK(GameplayHudCommon::measureHudTextWidth(font, "ab") == doctest::Approx(12));
    CHECK(GameplayHudCommon::measureHudTextWidth(font, "a\f02016b\f00000") == doctest::Approx(12));

    const std::vector<std::string> lines = GameplayHudCommon::wrapHudTextToWidth(font, "x \f02016aaaa bbbb\f00000 c", 30);
    REQUIRE_EQ(lines.size(), 4u);
    CHECK_EQ(lines[1], "\f02016aaaa");
    CHECK_EQ(lines[2], "\f02016bbbb\f00000");
    CHECK_EQ(lines[3], "c");

    std::vector<std::pair<uint16_t, uint32_t>> quads;
    const auto collect = [&quads](bgfx::TextureHandle texture, float, float, float, float, float, float, float, float,
                             TextureFilterProfile, uint32_t tint)
    {
        quads.emplace_back(texture.idx, tint);
    };
    GameplayHudCommon::renderHudFontLayer(
        font, GameplayHudFontLayer(font.mainTextureHandle), "a\f02016b\f00000c", 0, 0, 1, collect);
    REQUIRE_EQ(quads.size(), 3u);
    CHECK_EQ(quads[0].first, 1);
    CHECK_EQ(quads[1].first, 3);
    CHECK_EQ(quads[1].second, 0xff00ff00u);
    CHECK_EQ(quads[2].first, 1);

    quads.clear();
    GameplayHudCommon::renderHudFontLayer(
        font, GameplayHudFontLayer(font.shadowTextureHandle), "a\f02016b", 0, 0, 1, collect);
    REQUIRE_EQ(quads.size(), 2u);
    CHECK_EQ(quads[1].first, 2);
}

TEST_CASE("MAW bag overview: identify and repair act on the inspected item's bag, not the open one")
{
    using namespace OpenYAMM::Game;
    REQUIRE_MESSAGE(OpenYAMM::Tests::regressionGameDataLoaded(), OpenYAMM::Tests::regressionGameDataFailure().c_str());
    const ItemTable &itemTable = OpenYAMM::Tests::regressionGameData().itemTable;
    const ItemDefinition *pDefinition = nullptr;

    for (const ItemDefinition &definition : itemTable.entries())
    {
        if (definition.itemId != 0 && ItemRuntime::requiresIdentification(definition))
        {
            pDefinition = &definition;
            break;
        }
    }

    REQUIRE(pDefinition != nullptr);

    MawRules rules(RulesetOptions{}, MawMapLevels{});
    Character member = {};
    member.name = "Member";
    member.skills["IdentifyItem"] = {"IdentifyItem", 60, SkillMastery::Grandmaster};
    member.skills["RepairItem"] = {"RepairItem", 60, SkillMastery::Grandmaster};
    PartySeed seed = {};
    seed.members.assign(2, member);
    Party party = {};
    party.seed(seed);
    party.setRules(&rules.gameRules());
    party.setItemTable(&itemTable);
    const int bagCount = rules.gameRules().items.bagCount(party);
    REQUIRE(bagCount > 2);

    // The same cell holds an unidentified, broken item in the open bag 0 and in bag 2.
    InventoryItem item = {};
    item.objectDescriptionId = pDefinition->itemId;
    item.width = std::max<uint8_t>(1, pDefinition->inventoryWidth);
    item.height = std::max<uint8_t>(1, pDefinition->inventoryHeight);
    item.identified = false;
    item.broken = true;
    Character &holder = *party.member(0);
    REQUIRE(holder.addInventoryItemAt(item, 0, 0));
    REQUIRE(InventoryBags::openBag(holder, 2, bagCount));
    REQUIRE(holder.addInventoryItemAt(item, 0, 0));
    REQUIRE(InventoryBags::openBag(holder, 0, bagCount));

    std::string statusText;
    CHECK(InventoryBags::withBagOpen(party, 0, 2, bagCount,
        [&]() { return party.tryIdentifyMemberInventoryItem(0, 0, 0, 1, statusText); }));
    CHECK(InventoryBags::withBagOpen(party, 0, 2, bagCount,
        [&]() { return party.tryRepairMemberInventoryItem(0, 0, 0, 1, statusText); }));

    CHECK_EQ(holder.rulesetState.currentBag, 0);
    const InventoryItem *pOtherBagItem = InventoryBags::itemInBagAt(holder, 2, 0, 0);
    REQUIRE(pOtherBagItem != nullptr);
    CHECK(pOtherBagItem->identified);
    CHECK_FALSE(pOtherBagItem->broken);
    const InventoryItem *pOpenBagItem = holder.inventoryItemAt(0, 0);
    REQUIRE(pOpenBagItem != nullptr);
    CHECK_FALSE(pOpenBagItem->identified);
    CHECK(pOpenBagItem->broken);

    // Held right button moving onto the same cell of another bag tries that item too.
    GameplayUiController::ItemInspectOverlayState openBagOverlay = {};
    openBagOverlay.objectDescriptionId = pDefinition->itemId;
    openBagOverlay.sourceType = GameplayUiController::ItemInspectSourceType::Inventory;
    GameplayUiController::ItemInspectOverlayState otherBagOverlay = openBagOverlay;
    otherBagOverlay.sourceBag = 2;
    CHECK_NE(GameplayScreenController::itemInspectInteractionKey(openBagOverlay),
        GameplayScreenController::itemInspectInteractionKey(otherBagOverlay));
}
