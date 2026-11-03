#include "game/ui/GameplayQuickItems.h"

#include "game/app/GameSettings.h"
#include "game/app/KeyboardBindings.h"
#include "game/gameplay/GameMechanics.h"
#include "game/gameplay/GameplayInputFrame.h"
#include "game/gameplay/GameplayItemService.h"
#include "game/gameplay/GameplayScreenRuntime.h"
#include "game/items/InventoryBags.h"
#include "game/items/InventoryItemUseRuntime.h"
#include "game/items/QuickItemSlots.h"
#include "game/party/Party.h"
#include "game/rules/GameRules.h"
#include "game/rules/ItemRules.h"
#include "game/tables/ItemTable.h"
#include "game/ui/GameplayHudCommon.h"
#include "game/ui/GameplayUiRuntime.h"
#include "game/ui/GameplayUiSkin.h"
#include "game/ui/UiText.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace OpenYAMM::Game
{
namespace
{
// The study's colours (maw_potion_radial_20261025 index.html), as ABGR.
constexpr uint32_t abgr(uint32_t rgb, uint32_t alpha = 0xff)
{
    return alpha << 24 | (rgb & 0xffu) << 16 | (rgb & 0xff00u) | (rgb >> 16 & 0xffu);
}

constexpr uint32_t Ivory = abgr(0xefe0bc);
constexpr uint32_t Label = abgr(0xe6d1a8);
constexpr uint32_t Body = abgr(0xc8c7a7);
constexpr uint32_t Muted = abgr(0x9fa594);
constexpr uint32_t Gold = abgr(0xe3c185);
constexpr uint32_t Warn = abgr(0xffb347);
constexpr uint32_t Bad = abgr(0xff6a5a);
constexpr uint32_t Disc = abgr(0x13201e);
constexpr uint32_t Dim = abgr(0x0a1312, 0xd8);
constexpr uint32_t Badge = abgr(0x0b1212);
constexpr uint32_t BadgeBorder = abgr(0x8c815f);
constexpr uint32_t Socket = abgr(0x050a0a);
constexpr uint32_t PanelFill = abgr(0x070e0e, 0xf0);
constexpr uint32_t PanelBorder = abgr(0x8c815f);
constexpr uint32_t ChipFill = abgr(0x0d1716, 0xcc);
constexpr uint32_t ChipBorder = abgr(0x5d6449);
constexpr uint32_t ChipOnFill = abgr(0x2b2a1a, 0xcc);
constexpr uint32_t ChipOnBorder = abgr(0xd6bb80);
constexpr uint32_t RowHighlight = abgr(0xc9b07a, 0x26);
constexpr uint32_t RowRule = abgr(0xffffff, 0x10);
constexpr uint32_t Track = abgr(0x2a3829);
constexpr uint32_t Thumb = abgr(0xac8d6a);

// The picker's text: upright and heavier than the bitmap Lucida (the enemy labels' face).
constexpr char TextFont[] = "alegreya_semibold";

// A tap of the wheel key opens the ring and leaves it open; a longer hold uses the slot it is released over.
constexpr uint64_t WheelHoldTicks = 250;

struct Circle
{
    float x = 0;
    float y = 0;
    float radius = 0;

    bool contains(float pointX, float pointY) const
    {
        return std::hypot(pointX - x, pointY - y) <= radius;
    }
};

struct Box
{
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    bool contains(float pointX, float pointY) const
    {
        return pointX >= x && pointX < x + width && pointY >= y && pointY < y + height;
    }
};

// The ring above a portrait: the hub, the six slots (slot 1 on top, clockwise) and the dimmed disc behind them, in
// the portrait's face units.
struct RingLayout
{
    Circle hub;
    Circle backdrop;
    std::array<Circle, QuickItemSlotCount> slots = {};
    float unit = 1;
};

std::optional<GameplayResolvedHudLayoutElement> faceRect(GameplayScreenRuntime &context, int width, int height,
    size_t member)
{
    return context.resolvePartyPortraitRect(width, height, member);
}

// The round button at a portrait's upper right, outside the mana arc: about 40 degrees above the horizontal from the
// face's centre, one radius past the arc's outer edge, and short of the next portrait's HP arc. None while the
// Potion buttons setting hides them.
std::optional<Circle> buttonCircle(GameplayScreenRuntime &context, int width, int height, size_t member)
{
    const std::optional<GameplayResolvedHudLayoutElement> face = faceRect(context, width, height, member);

    if (!face || !context.settingsSnapshot().potionButtons)
    {
        return std::nullopt;
    }

    return Circle{face->x + face->width * 1.234f, face->y + face->height * 0.043f, face->height * 0.13f};
}

std::optional<RingLayout> ringLayout(GameplayScreenRuntime &context, int width, int height, size_t member)
{
    const std::optional<GameplayResolvedHudLayoutElement> face = faceRect(context, width, height, member);

    if (!face)
    {
        return std::nullopt;
    }

    RingLayout layout;
    const float faceHeight = face->height;
    const float radius = faceHeight * 0.88f;
    const float slotRadius = faceHeight * 0.29f;
    layout.unit = faceHeight / 128.0f;
    const float reach = radius + slotRadius + 6 * layout.unit;
    const float x = std::clamp(face->x + face->width / 2, reach, float(width) - reach);
    const float y = face->y - faceHeight * 1.46f;
    layout.hub = {x, y, faceHeight * 0.39f};
    layout.backdrop = {x, y, reach + 4 * layout.unit};

    for (size_t slot = 0; slot < QuickItemSlotCount; ++slot)
    {
        const float angle = (-90.0f + 60.0f * float(slot)) * 3.14159265f / 180.0f;
        layout.slots[slot] = {x + radius * std::cos(angle), y + radius * std::sin(angle), slotRadius};
    }

    return layout;
}

// The picker beside the ring, on the side with more room: the slot's title and bottle rule, the list of what the
// party carries, its scrollbar, and Clear slot and Close.
struct PickerLayout
{
    Box panel;
    std::array<Box, 2> rules = {};
    Box list;
    float rowHeight = 0;
    size_t visibleRows = 0;
    Box track;
    Box clear;
    Box close;
    float unit = 1;
};

std::optional<PickerLayout> pickerLayout(GameplayScreenRuntime &context, int width, int height, size_t member)
{
    const std::optional<RingLayout> ring = ringLayout(context, width, height, member);

    if (!ring)
    {
        return std::nullopt;
    }

    PickerLayout layout;
    const float u = ring->unit;
    layout.unit = u;
    const float panelWidth = 300 * u;
    const float panelHeight = 2 * ring->backdrop.radius;
    const bool right = ring->backdrop.x < float(width) / 2;
    const float x = right ? ring->backdrop.x + ring->backdrop.radius + 8 * u
                          : ring->backdrop.x - ring->backdrop.radius - 8 * u - panelWidth;
    const float y = std::clamp(ring->backdrop.y - ring->backdrop.radius, 4 * u, float(height) - panelHeight - 4 * u);
    layout.panel = {std::clamp(x, 4 * u, float(width) - panelWidth - 4 * u), y, panelWidth, panelHeight};
    const Box &panel = layout.panel;
    layout.rules[1] = {panel.x + panel.width - 10 * u - 84 * u, panel.y + 10 * u, 84 * u, 22 * u};
    layout.rules[0] = {layout.rules[1].x - 4 * u - 84 * u, panel.y + 10 * u, 84 * u, 22 * u};
    const float footerHeight = 30 * u;
    layout.list = {panel.x + 8 * u, panel.y + 40 * u, panel.width - 24 * u, panel.height - 40 * u - footerHeight - 6 * u};
    layout.rowHeight = 38 * u;
    layout.visibleRows = size_t(std::max(1.0f, std::floor(layout.list.height / layout.rowHeight)));
    layout.track = {layout.list.x + layout.list.width + 4 * u, layout.list.y, 5 * u, layout.list.height};
    layout.close = {panel.x + panel.width - 10 * u - 76 * u, panel.y + panel.height - footerHeight, 76 * u, 22 * u};
    layout.clear = {layout.close.x - 6 * u - 90 * u, layout.close.y, 90 * u, 22 * u};
    return layout;
}

int bagCount(const Party &party)
{
    return std::max(1, party.rules().items.bagCount(party));
}

// One row of the picker: a kind of potion or scroll at one strength, how many the party carries and who.
struct PickerRow
{
    uint32_t itemId = 0;
    uint16_t power = 0;
    size_t count = 0;
    bool scroll = false;
    std::string name;
    std::string icon;
    std::vector<std::string> carriers;
};

// Every potion and scroll a slot may hold, from every member's bags: potions first, then scrolls, by name and
// strength.
std::vector<PickerRow> pickerRows(GameplayScreenRuntime &context, const Party &party)
{
    std::vector<PickerRow> rows;
    const ItemTable *pItemTable = context.itemTable();

    if (pItemTable == nullptr)
    {
        return rows;
    }

    const int bags = bagCount(party);

    for (const Character &member : party.members())
    {
        for (int bag = 0; bag < bags; ++bag)
        {
            for (const InventoryItem &item : InventoryBags::itemsInBag(member, uint8_t(bag)))
            {
                const ItemDefinition *pDefinition = pItemTable->get(item.objectDescriptionId);

                if (pDefinition == nullptr
                    || !QuickItemSlots::canHold(item, *pItemTable, context.mergedPotionSettingTable(),
                        context.mergedReagentSettingTable()))
                {
                    continue;
                }

                auto row = std::find_if(rows.begin(), rows.end(), [&item](const PickerRow &candidate)
                    {
                        return candidate.itemId == item.objectDescriptionId
                            && candidate.power == item.standardEnchantPower;
                    });

                if (row == rows.end())
                {
                    PickerRow added;
                    added.itemId = item.objectDescriptionId;
                    added.power = item.standardEnchantPower;
                    added.scroll = pDefinition->equipStat != "Bottle";
                    added.name = pDefinition->name;
                    added.icon = pDefinition->iconName;
                    rows.push_back(std::move(added));
                    row = rows.end() - 1;
                }

                ++row->count;

                if (std::find(row->carriers.begin(), row->carriers.end(), member.name) == row->carriers.end())
                {
                    row->carriers.push_back(member.name);
                }
            }
        }
    }

    std::sort(rows.begin(), rows.end(), [](const PickerRow &left, const PickerRow &right)
        {
            return std::tie(left.scroll, left.name, left.power) < std::tie(right.scroll, right.name, right.power);
        });
    return rows;
}

class Painter
{
public:
    explicit Painter(GameplayScreenRuntime &context)
        : m_context(context)
    {
    }

    void fill(const Box &box, uint32_t colour) const
    {
        if (const std::optional<GameplayHudTextureHandle> texture = solid(colour))
        {
            m_context.submitHudTexturedQuad(*texture, box.x, box.y, box.width, box.height);
        }
    }

    void frame(const Box &box, float line, uint32_t colour) const
    {
        fill({box.x, box.y, box.width, line}, colour);
        fill({box.x, box.y + box.height - line, box.width, line}, colour);
        fill({box.x, box.y, line, box.height}, colour);
        fill({box.x + box.width - line, box.y, line, box.height}, colour);
    }

    void disc(const Circle &circle, uint32_t colour) const
    {
        if (const std::optional<GameplayHudTextureHandle> texture = solid(colour))
        {
            m_context.gameplayUiRuntime().submitHudTexturedEllipse(texture->textureHandle, circle.x - circle.radius,
                circle.y - circle.radius, 2 * circle.radius, 2 * circle.radius);
        }
    }

    void ring(const Circle &circle, float stroke, bool bright) const
    {
        const GameplayResolvedHudLayoutElement rect = {circle.x - circle.radius, circle.y - circle.radius,
            2 * circle.radius, 2 * circle.radius, circle.radius / 37.0f};
        GameplayUiSkin::renderArc(m_context, rect, stroke, 0, 360,
            bright ? GameplayUiSkin::ArcMaterial::SelectedRim : GameplayUiSkin::ArcMaterial::NormalRim);
    }

    // An item's icon fitted in a box, centred, greyed when asked.
    void icon(const std::string &iconName, const Box &box, bool grey) const
    {
        const std::optional<GameplayHudTextureHandle> texture =
            m_context.gameplayUiRuntime().ensureItemIconTextureLoaded(iconName);

        if (!texture || texture->width <= 0 || texture->height <= 0)
        {
            return;
        }

        const float scale = std::min(box.width / float(texture->width), box.height / float(texture->height));
        const float iconWidth = texture->width * scale;
        const float iconHeight = texture->height * scale;
        const bgfx::TextureHandle handle = grey
            ? m_context.gameplayUiRuntime().ensureHudTextureColorModulated(*texture, 0xff606060u)
            : texture->textureHandle;
        m_context.gameplayUiRuntime().submitHudTexturedQuad(handle, box.x + (box.width - iconWidth) / 2,
            box.y + (box.height - iconHeight) / 2, iconWidth, iconHeight);
    }

    // Text of a pixel height; align 0 left, 1 centred on x, 2 ending at x.
    void text(const std::string &font, float height, uint32_t colour, const std::string &value, float x, float y,
        int align = 0) const
    {
        const float scale = fontScale(font, height);
        const float textWidth = m_context.measureHudTextWidth(font, value) * scale;
        x -= align == 1 ? textWidth / 2 : align == 2 ? textWidth : 0;
        m_context.renderHudTextLine(font, colour, value, std::round(x), std::round(y), scale);
    }

    float width(const std::string &font, float height, const std::string &value) const
    {
        return m_context.measureHudTextWidth(font, value) * fontScale(font, height);
    }

    // The value shortened with an ellipsis to fit the width.
    std::string fitted(const std::string &font, float height, const std::string &value, float maxWidth) const
    {
        std::string result = value;

        while (result.size() > 1 && width(font, height, result) > maxWidth)
        {
            result.pop_back();

            if (width(font, height, result + "...") <= maxWidth)
            {
                return result + "...";
            }
        }

        return result;
    }

private:
    std::optional<GameplayHudTextureHandle> solid(uint32_t colour) const
    {
        char name[40] = {};
        std::snprintf(name, sizeof(name), "__quick_item_%08x", colour);
        return m_context.gameplayUiRuntime().ensureSolidHudTextureLoaded(name, colour);
    }

    float fontScale(const std::string &font, float height) const
    {
        const float wanted = height / float(std::max(1, m_context.hudFontHeight(font)));
        return wanted >= 1.0f ? GameplayHudCommon::snappedHudFontScale(wanted) : std::max(0.5f, wanted);
    }

    GameplayScreenRuntime &m_context;
};

// The member's state as the hub says it: ready, recovering (and who gives the next drink, where drinks cost
// recovery: MAW), or nobody ready.
std::string hubState(GameplayScreenRuntime &context, const Party &party, size_t member)
{
    const Character *pMember = party.member(member);

    if (pMember == nullptr)
    {
        return {};
    }

    if (GameMechanics::canTakeGameplayAction(*pMember))
    {
        return UiText::translate("ready");
    }

    if (!InventoryItemUseRuntime::drinkRecoveryApplies(context.mergedPotionSettingTable()))
    {
        return UiText::translate("recovering");
    }

    const std::optional<size_t> payer = InventoryItemUseRuntime::drinkRecoveryPayer(party, member);
    const Character *pPayer = payer ? party.member(*payer) : nullptr;
    return pPayer != nullptr
        ? UiText::translateTemplate("next fed by {name}", {{"{name}", pPayer->name}})
        : UiText::translate("nobody ready");
}

void closeRing(GameplayScreenRuntime &context)
{
    GameplayOverlayInteractionState &state = context.interactionState();
    state.quickItemRingOpen = false;
    state.quickItemHoveredSlot.reset();
    state.quickItemPickerSlot.reset();
}

void openPicker(GameplayScreenRuntime &context, size_t slot)
{
    context.interactionState().quickItemPickerSlot = slot;
    context.interactionState().quickItemPickerScroll = 0;
}

void renderPicker(GameplayScreenRuntime &context, const Party &party, const Character &member, size_t slot,
    const PickerLayout &layout)
{
    const Painter paint(context);
    const float u = layout.unit;
    const QuickItemSlot &quickSlot = member.quickItemSlots[slot];
    paint.fill(layout.panel, PanelFill);
    paint.frame(layout.panel, std::max(1.0f, u), PanelBorder);
    paint.text("Fondamento", 18 * u, Label,
        UiText::translateTemplate("Slot {slot}", {{"{slot}", std::to_string(slot + 1)}}), layout.panel.x + 12 * u,
        layout.panel.y + 9 * u);
    const std::array<const char *, 2> rules = {"Weakest first", "Strongest first"};

    for (size_t index = 0; index < rules.size(); ++index)
    {
        const bool on = size_t(quickSlot.bottleRule) == index;
        paint.fill(layout.rules[index], on ? ChipOnFill : ChipFill);
        paint.frame(layout.rules[index], std::max(1.0f, u), on ? ChipOnBorder : ChipBorder);
        paint.text(TextFont, 11 * u, on ? Ivory : Body, UiText::translate(rules[index]),
            layout.rules[index].x + layout.rules[index].width / 2, layout.rules[index].y + 4 * u, 1);
    }

    const std::vector<PickerRow> rows = pickerRows(context, party);
    const size_t scroll = std::min(context.interactionState().quickItemPickerScroll,
        rows.size() > layout.visibleRows ? rows.size() - layout.visibleRows : size_t(0));

    for (size_t index = 0; index < layout.visibleRows && scroll + index < rows.size(); ++index)
    {
        const PickerRow &row = rows[scroll + index];
        const Box box = {layout.list.x, layout.list.y + index * layout.rowHeight, layout.list.width, layout.rowHeight};

        if (row.itemId == quickSlot.itemId)
        {
            paint.fill(box, RowHighlight);
        }

        paint.fill({box.x, box.y + box.height - std::max(1.0f, u), box.width, std::max(1.0f, u)}, RowRule);
        paint.icon(row.icon, {box.x + 2 * u, box.y + 3 * u, 30 * u, box.height - 6 * u}, false);
        const std::string countText =
            UiText::translateTemplate("x{count}", {{"{count}", std::to_string(row.count)}});
        const float countWidth = paint.width(TextFont, 13 * u, countText);
        paint.text(TextFont, 13 * u, Ivory,
            paint.fitted(TextFont, 13 * u, row.name, box.width - 46 * u - countWidth - 8 * u), box.x + 38 * u,
            box.y + 4 * u);
        paint.text(TextFont, 13 * u, Ivory, countText, box.x + box.width - 6 * u, box.y + 4 * u, 2);
        std::string carriers;

        for (const std::string &name : row.carriers)
        {
            carriers += (carriers.empty() ? "" : ", ") + name;
        }

        // The HUD fonts are single-byte: the middle dot is 0xB7.
        const std::string note = (row.power > 0
                ? UiText::translateTemplate("strength {power}", {{"{power}", std::to_string(row.power)}}) + "  \xB7  "
                : std::string())
            + carriers;
        paint.text(TextFont, 10 * u, Muted, paint.fitted(TextFont, 10 * u, note, box.width - 44 * u), box.x + 38 * u,
            box.y + 21 * u);
    }

    if (rows.empty())
    {
        paint.text(TextFont, 12 * u, Muted, UiText::translate("Nobody carries a potion or a scroll"),
            layout.list.x + 6 * u,
            layout.list.y + 8 * u);
    }

    if (rows.size() > layout.visibleRows)
    {
        paint.fill(layout.track, Track);
        const float thumb = layout.track.height * float(layout.visibleRows) / float(rows.size());
        const float offset = (layout.track.height - thumb) * float(scroll) / float(rows.size() - layout.visibleRows);
        paint.fill({layout.track.x, layout.track.y + offset, layout.track.width, thumb}, Thumb);
    }

    const auto button = [&](const Box &box, const char *pLabel)
    {
        paint.fill(box, ChipFill);
        paint.frame(box, std::max(1.0f, u), ChipOnBorder);
        paint.text("Fondamento", 14 * u, abgr(0xe8d1a4), pLabel, box.x + box.width / 2, box.y + 3 * u, 1);
    };
    button(layout.clear, UiText::translate("Clear slot").c_str());
    button(layout.close, UiText::translate("Close").c_str());
}

// The picker's clicks and wheel: true when the pointer was over it.
bool handlePickerInput(GameplayScreenRuntime &context, const GameplayInputFrame &input, const Party &party,
    Character &member, size_t slot, const PickerLayout &layout, bool left)
{
    GameplayOverlayInteractionState &state = context.interactionState();
    const float x = input.pointerX;
    const float y = input.pointerY;

    if (!layout.panel.contains(x, y))
    {
        return false;
    }

    const std::vector<PickerRow> rows = pickerRows(context, party);
    const size_t maxScroll = rows.size() > layout.visibleRows ? rows.size() - layout.visibleRows : 0;
    state.quickItemPickerScroll = std::min(state.quickItemPickerScroll, maxScroll);

    if (input.mouseWheelDelta > 0)
    {
        state.quickItemPickerScroll = state.quickItemPickerScroll > 0 ? state.quickItemPickerScroll - 1 : 0;
    }
    else if (input.mouseWheelDelta < 0)
    {
        state.quickItemPickerScroll = std::min(state.quickItemPickerScroll + 1, maxScroll);
    }

    if (!left)
    {
        return true;
    }

    QuickItemSlot &quickSlot = member.quickItemSlots[slot];

    for (size_t index = 0; index < layout.rules.size(); ++index)
    {
        if (layout.rules[index].contains(x, y))
        {
            quickSlot.bottleRule = QuickItemBottleRule(index);
        }
    }

    if (layout.clear.contains(x, y))
    {
        quickSlot.itemId = 0;
        state.quickItemPickerSlot.reset();
    }
    else if (layout.close.contains(x, y))
    {
        state.quickItemPickerSlot.reset();
    }
    else if (layout.track.contains(x, y) && maxScroll > 0)
    {
        // A click on the track scrolls to that point of the list.
        const float position = (y - layout.track.y) / std::max(1.0f, layout.track.height);
        state.quickItemPickerScroll = std::min(size_t(std::lround(position * float(maxScroll))), maxScroll);
    }
    else if (layout.list.contains(x, y))
    {
        const size_t index = state.quickItemPickerScroll + size_t((y - layout.list.y) / layout.rowHeight);

        if (index < rows.size())
        {
            quickSlot.itemId = rows[index].itemId;
            state.quickItemPickerSlot.reset();
        }
    }

    return true;
}
}

bool GameplayQuickItems::ringOpen(GameplayScreenRuntime &context)
{
    return context.interactionState().quickItemRingOpen;
}

bool GameplayQuickItems::takesPointer(GameplayScreenRuntime &context)
{
    const GameplayOverlayInteractionState &state = context.interactionState();
    return state.quickItemRingOpen || state.quickItemClickLatch;
}

bool GameplayQuickItems::takesPointerAction(GameplayScreenRuntime &context, KeyboardAction action)
{
    return takesPointer(context)
        && context.settingsSnapshot().keyboard.binding(action).kind != InputBindingKind::Keyboard;
}

void GameplayQuickItems::renderHud(GameplayScreenRuntime &context, int width, int height)
{
    const Party *pParty = context.partyReadOnly();
    const ItemTable *pItemTable = context.itemTable();

    if (pParty == nullptr || pItemTable == nullptr)
    {
        return;
    }

    const Painter paint(context);
    const GameplayOverlayInteractionState &state = context.interactionState();
    const size_t members = std::min<size_t>(5, pParty->members().size());

    // The buttons: a small round obsidian plate showing a miniature of slot 1's item (user, 2026-10-25), greyed with
    // its party-wide count when nobody carries one; an empty black socket when slot 1 is empty (user).
    for (size_t member = 0; member < members; ++member)
    {
        const std::optional<Circle> button = buttonCircle(context, width, height, member);
        const Character *pMember = pParty->member(member);

        if (!button || pMember == nullptr)
        {
            continue;
        }

        const bool on = state.quickItemRingOpen && state.quickItemRingMember == member;
        const QuickItemSlot &first = pMember->quickItemSlots[0];
        const ItemDefinition *pItem = first.itemId != 0 ? pItemTable->get(first.itemId) : nullptr;
        paint.disc(*button, Disc);

        if (pItem == nullptr)
        {
            paint.disc({button->x, button->y, button->radius * 0.62f}, Socket);
        }

        paint.ring(*button, 3.2f, on);

        if (pItem != nullptr)
        {
            const size_t count = QuickItemSlots::countPartyItems(*pParty, first, bagCount(*pParty));
            const float iconHeight = button->radius * 1.5f;
            paint.icon(pItem->iconName, {button->x - button->radius, button->y - iconHeight / 2, 2 * button->radius,
                iconHeight}, count == 0);
            const float textHeight = std::max(9.0f, button->radius * 0.62f);
            paint.text("Lucida", textHeight, count == 0 ? Bad : count <= 2 ? Warn : Ivory, std::to_string(count),
                button->x + button->radius * 1.05f, button->y + button->radius - textHeight * 0.55f, 2);
        }
    }

    if (!state.quickItemRingOpen || state.quickItemRingMember >= members)
    {
        return;
    }

    const std::optional<RingLayout> layout = ringLayout(context, width, height, state.quickItemRingMember);
    const Character *pMember = pParty->member(state.quickItemRingMember);

    if (!layout || pMember == nullptr)
    {
        return;
    }

    const float unit = layout->unit;
    paint.disc(layout->backdrop, Dim);

    for (size_t slot = 0; slot < QuickItemSlotCount; ++slot)
    {
        const Circle &circle = layout->slots[slot];
        const QuickItemSlot &quickSlot = pMember->quickItemSlots[slot];
        const ItemDefinition *pItem = quickSlot.itemId != 0 ? pItemTable->get(quickSlot.itemId) : nullptr;
        const bool hot = state.quickItemHoveredSlot == slot || state.quickItemPickerSlot == slot;
        paint.disc(circle, Disc);
        paint.ring(circle, hot ? 4.0f : 3.2f, hot);

        if (pItem != nullptr)
        {
            // The slot's kind across the party: the owner's bags first, then the others' (useQuickItemSlot).
            const size_t count = QuickItemSlots::countPartyItems(*pParty, quickSlot, bagCount(*pParty));
            const float iconHeight = circle.radius * 1.55f;
            paint.icon(pItem->iconName, {circle.x - circle.radius, circle.y - iconHeight / 2, 2 * circle.radius,
                iconHeight}, count == 0);
            // The count: amber at two or fewer, red when none is left.
            const std::string countText = std::to_string(count);
            const float badgeHeight = 20 * unit;
            const float badgeWidth = std::max(26 * unit, paint.width("Lucida", 13 * unit, countText) + 10 * unit);
            const Box badge = {circle.x + circle.radius - badgeWidth + 4 * unit,
                circle.y + circle.radius - badgeHeight + 2 * unit, badgeWidth, badgeHeight};
            paint.fill(badge, Badge);
            paint.frame(badge, std::max(1.0f, unit), count == 0 ? Bad : count <= 2 ? Warn : BadgeBorder);
            paint.text("Lucida", 13 * unit, count == 0 ? Bad : count <= 2 ? Warn : Ivory, countText,
                badge.x + badge.width / 2, badge.y + 3 * unit, 1);
            paint.text("Lucida", 12 * unit, Body, pItem->name, circle.x, circle.y + circle.radius + 4 * unit, 1);
        }
        else
        {
            paint.text("Fondamento", 34 * unit, abgr(0x9a8d64), "+", circle.x, circle.y - 20 * unit, 1);
        }

        // The slot's key, only when one is bound (slots 2-6 are unbound by default).
        const InputBinding binding = context.settingsSnapshot().keyboard.binding(
            KeyboardAction(size_t(KeyboardAction::QuickItem1) + slot));

        if (binding.kind != InputBindingKind::None)
        {
            const std::string keyText = inputBindingDisplayName(binding);
            const float keyWidth = std::max(22 * unit, paint.width("Lucida", 14 * unit, keyText) + 10 * unit);
            const Box key = {circle.x - circle.radius - 6 * unit, circle.y - circle.radius - 4 * unit, keyWidth,
                22 * unit};
            paint.fill(key, abgr(0x1c2524));
            paint.frame(key, std::max(1.0f, unit), abgr(0x6b6a4e));
            paint.text("Lucida", 14 * unit, Gold, keyText, key.x + key.width / 2, key.y + 3 * unit, 1);
        }
    }

    paint.disc(layout->hub, abgr(0x0d1716));
    paint.ring(layout->hub, 3.0f, false);
    paint.text("Fondamento", 19 * unit, Label, pMember->name, layout->hub.x, layout->hub.y - 16 * unit, 1);
    const std::string stateText = hubState(context, *pParty, state.quickItemRingMember);
    paint.text("Lucida", 11 * unit, stateText == UiText::translate("nobody ready") ? Warn : Muted, stateText,
        layout->hub.x,
        layout->hub.y + 8 * unit, 1);

    if (state.quickItemPickerSlot && *state.quickItemPickerSlot < QuickItemSlotCount)
    {
        if (const std::optional<PickerLayout> picker = pickerLayout(context, width, height, state.quickItemRingMember))
        {
            renderPicker(context, *pParty, *pMember, *state.quickItemPickerSlot, *picker);
        }
    }
}

bool GameplayQuickItems::handleHudInput(GameplayScreenRuntime &context, const GameplayInputFrame &input, int width,
    int height, bool allowInput, bool pointerAllowed)
{
    GameplayOverlayInteractionState &state = context.interactionState();
    Party *pParty = context.party();

    if (!allowInput || pParty == nullptr)
    {
        closeRing(context);
        state.quickItemWheelHeld = false;
        state.quickItemClickLatch = false;
        return false;
    }

    // The latch keeps a click on the ring from reaching the world until its button is let go.
    if (!input.leftMouseButton.held && !input.rightMouseButton.held)
    {
        state.quickItemClickLatch = false;
    }

    const size_t members = std::min<size_t>(5, pParty->members().size());
    const auto hoveredSlot = [&]() -> std::optional<size_t>
    {
        const std::optional<RingLayout> layout = ringLayout(context, width, height, state.quickItemRingMember);

        for (size_t slot = 0; layout && slot < QuickItemSlotCount; ++slot)
        {
            if (layout->slots[slot].contains(input.pointerX, input.pointerY))
            {
                return slot;
            }
        }

        return std::nullopt;
    };

    // The wheel key: a tap opens the active member's ring (or closes it), a hold uses the slot it is released over.
    const GameplayButtonInputState &wheel = input.action(KeyboardAction::QuickItemWheel);

    if (wheel.pressed && !state.quickItemWheelHeld)
    {
        state.quickItemWheelHeld = true;
        state.quickItemWheelPressedTicks = SDL_GetTicks();
        const size_t active = pParty->activeMemberIndex();
        const bool same = state.quickItemRingOpen && state.quickItemRingMember == active;
        closeRing(context);

        if (!same)
        {
            state.quickItemRingOpen = true;
            state.quickItemRingMember = active;
        }
    }
    else if (!wheel.held && state.quickItemWheelHeld)
    {
        state.quickItemWheelHeld = false;

        if (state.quickItemRingOpen && !state.quickItemPickerSlot
            && SDL_GetTicks() - state.quickItemWheelPressedTicks >= WheelHoldTicks)
        {
            const std::optional<size_t> slot = pointerAllowed ? hoveredSlot() : std::nullopt;
            closeRing(context);

            if (slot)
            {
                context.itemService().useQuickItemSlot(context, state.quickItemRingMember, *slot);
            }
        }
    }

    state.quickItemHoveredSlot =
        state.quickItemRingOpen && pointerAllowed && !state.quickItemPickerSlot ? hoveredSlot() : std::nullopt;

    // Escape closes the picker first, then the ring.
    const bool escape = input.isScancodeHeld(SDL_SCANCODE_ESCAPE);
    const bool escapePressed = escape && !state.quickItemEscapeHeld;
    state.quickItemEscapeHeld = escape;

    if (state.quickItemRingOpen && escapePressed)
    {
        if (state.quickItemPickerSlot)
        {
            state.quickItemPickerSlot.reset();
        }
        else
        {
            closeRing(context);
        }

        return true;
    }

    const bool left = pointerAllowed && input.leftMouseButton.pressed;
    const bool right = pointerAllowed && input.rightMouseButton.pressed;
    Character *pMember = state.quickItemRingOpen ? pParty->member(state.quickItemRingMember) : nullptr;

    // The picker takes its own clicks and wheel.
    if (pMember != nullptr && state.quickItemPickerSlot && *state.quickItemPickerSlot < QuickItemSlotCount)
    {
        const std::optional<PickerLayout> picker = pickerLayout(context, width, height, state.quickItemRingMember);

        if (picker && pointerAllowed
            && handlePickerInput(context, input, *pParty, *pMember, *state.quickItemPickerSlot, *picker, left))
        {
            state.quickItemClickLatch = left || state.quickItemClickLatch;
            return true;
        }
    }

    if (!left && !right)
    {
        return state.quickItemRingOpen
            && (input.leftMouseButton.held || input.rightMouseButton.held) && state.quickItemClickLatch;
    }

    // A button opens or closes its member's ring.
    for (size_t member = 0; member < members && left; ++member)
    {
        const std::optional<Circle> button = buttonCircle(context, width, height, member);

        if (button && button->contains(input.pointerX, input.pointerY))
        {
            const bool same = state.quickItemRingOpen && state.quickItemRingMember == member;
            closeRing(context);

            if (!same)
            {
                state.quickItemRingOpen = true;
                state.quickItemRingMember = member;
            }

            state.quickItemClickLatch = true;
            return true;
        }
    }

    if (!state.quickItemRingOpen || pMember == nullptr)
    {
        return false;
    }

    state.quickItemClickLatch = true;
    const std::optional<RingLayout> layout = ringLayout(context, width, height, state.quickItemRingMember);
    std::optional<size_t> slot;

    for (size_t index = 0; layout && index < QuickItemSlotCount; ++index)
    {
        if (layout->slots[index].contains(input.pointerX, input.pointerY))
        {
            slot = index;
        }
    }

    if (slot)
    {
        // A click uses the slot; an empty one, or a right click, opens the picker for it.
        if (right || pMember->quickItemSlots[*slot].itemId == 0)
        {
            openPicker(context, *slot);
        }
        else
        {
            state.quickItemPickerSlot.reset();
            context.itemService().useQuickItemSlot(context, state.quickItemRingMember, *slot);
        }

        return true;
    }

    // A click away closes the picker first, then the ring; one on the dimmed disc does nothing.
    if (state.quickItemPickerSlot)
    {
        state.quickItemPickerSlot.reset();
    }
    else if (!layout || !layout->backdrop.contains(input.pointerX, input.pointerY)
        || layout->hub.contains(input.pointerX, input.pointerY))
    {
        closeRing(context);
    }

    return true;
}
}
