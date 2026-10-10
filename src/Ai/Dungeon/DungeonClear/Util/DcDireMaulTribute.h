/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef DC_DIRE_MAUL_TRIBUTE_H
#define DC_DIRE_MAUL_TRIBUTE_H

#include <cstdint>
#include <string>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/DungeonBossInfo.h"

class Player;

namespace DcDireMaulTribute
{
    constexpr std::uint32_t MAP = 429;
    constexpr std::uint32_t KING = 11501;
    constexpr std::uint32_t MIZZLE = 14353;
    constexpr std::uint32_t CHEST = 179564;
    constexpr std::uint32_t CLAIM_HOOK = 44;
    // Dire Maul normal events occupy 1..17, including West sweep events.
    constexpr std::uint32_t CLAIM_EVENT = 18;
    constexpr std::uint32_t DISGUISE_EVENT = 19;
    constexpr std::uint32_t TRAP_EVENT = 20;
    constexpr std::uint32_t FENGUS_EVENT = 21;
    constexpr std::uint32_t FENGUS_HOOK = 47;
    constexpr std::uint32_t SLIPKIK_WAIT_HOOK = 48;
    constexpr std::uint32_t FENGUS = 14321;
    constexpr std::uint32_t DISGUISE_HOOK = 45;
    constexpr std::uint32_t TRAP_HOOK = 46;
    constexpr std::uint32_t OGRE_SUIT = 18258;
    constexpr std::uint32_t OGRE_DISGUISE = 22736;
    constexpr std::uint32_t KROMCRUSH = 14325;
    constexpr std::uint32_t SLIPKIK = 14323;
    constexpr std::uint32_t BROKEN_TRAP = 179485;
    constexpr std::uint32_t FIXED_TRAP = 179512;
    constexpr std::uint32_t TRAP_QUEST = 1193;
    constexpr std::uint32_t THORIUM_WIDGET = 15994;
    constexpr std::uint32_t FROST_OIL = 3829;
    constexpr std::uint32_t ICE_LOCK = 22856;
    constexpr std::uint32_t NORTH_PROGRESS = 3;
    constexpr std::uint32_t KING_DEFEATED = 3;
    constexpr std::uint16_t FULL_LOOT_MODE = 0x3F;

    // The spawned object's native loot mode is authoritative. Merely killing
    // the King or seeing Mizzle does not prove that a full tribute was earned.
    constexpr bool IsFullTribute(std::uint16_t lootMode)
    {
        return lootMode == FULL_LOOT_MODE;
    }

    // Kreeg is also spared: the tribute route must not turn a bypass into an
    // optional boss pull. Ordinary trash and King Gordok remain attackable.
    constexpr bool IsProtectedEntry(std::uint32_t entry)
    {
        switch (entry)
        {
            case 14326: // Guard Mol'dar
            case 14321: // Guard Fengus
            case 14323: // Guard Slip'kik
            case 14325: // Captain Kromcrush
            case 14324: // Cho'Rush the Observer
            case 14322: // Stomper Kreeg
                return true;
            default:
                return false;
        }
    }

    // Native patrol 2480790 doubles back after its easternmost north-rim
    // point (19). Follow behind that westbound pass: the southern circuit
    // then keeps him away long enough to clear the trash at the north exit.
    // Position alone also matches his approaching eastbound pass.
    constexpr bool IsFengusInBypassWindow(float x, float y, float z, float orientation)
    {
        return x >= 425.0f && x <= 445.0f && y >= 334.0f && y <= 345.0f &&
               z >= 0.0f && z < 6.0f && orientation >= 2.5f && orientation <= 3.7f;
    }

    constexpr bool IsSlipkikInTrapWindow(float x, float y, float z)
    {
        return x >= 470.0f && x <= 500.0f && y >= 595.0f && y <= 612.0f &&
               z >= -28.0f && z <= -22.0f;
    }

    constexpr bool InteractionHoldPoint(std::uint32_t eventId, float& x, float& y, float& z)
    {
        if (eventId == TRAP_EVENT)
        {
            x = 515.0f; y = 535.0f; z = -25.2951f;
            return true;
        }
        if (eventId == DISGUISE_EVENT)
        {
            x = 578.0f; y = 481.721f; z = 29.6382f;
            return true;
        }
        return false;
    }

    bool Enabled(Player* bot);
    bool FollowerHoldPoint(Player* bot, float& x, float& y, float& z);
    std::string BlockedReason(Player* bot, std::uint32_t eventId);
    bool SelectForRun(Player* leader, std::string const& option, std::string& error);
    std::vector<DungeonBossInfo> BuildRoster(std::vector<DungeonBossInfo> const& northBosses);
}

#endif
