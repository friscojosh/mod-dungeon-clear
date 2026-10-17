/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCGAPCLOSER_H
#define _PLAYERBOT_DCGAPCLOSER_H

#include <cstring>
#include <string>

// Pure name classifier for the follower gap-closer veto (fork: friscojosh; see
// FollowerGapCloserBanned in DungeonClearMultiplier.cpp). A playerbots action
// whose name is one of these spells, bare or with a " ..." suffix ("intercept
// on enemy healer", "feral charge - bear"), moves the bot 25-30yd at an enemy
// (or, Death Grip, the enemy to the bot). Intervene targets an ally and is not
// listed.
namespace DcGapCloser
{
    inline bool IsGapCloser(std::string const& name)
    {
        static char const* const kSpells[] = { "charge", "intercept", "feral charge", "death grip" };
        for (char const* s : kSpells)
        {
            std::size_t const n = std::strlen(s);
            if (name.compare(0, n, s) == 0 && (name.size() == n || name[n] == ' '))
                return true;
        }
        return false;
    }
}

#endif
