/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCMARKPLAN_H
#define _PLAYERBOT_DCMARKPLAN_H

#include <cstdint>
#include <vector>

// The DC tank's raid-icon choices (fork: friscojosh), pure so they are testable.
//
// Everything that ACTS on the icons already exists in mod-playerbots: DpsTargetValue
// returns the `rti` icon's unit (skull by default) before any other pick, so every
// DPS bot converges on it; FindTargetForCcStrategy gives the `rti cc` icon's unit
// (moon by default) first claim on a CC spell, so the mage sheeps it; and the
// group-cc-aware strategy keeps the party off a sheep. What never existed was a DC
// tank that PUTS THE ICONS DOWN. Stock `mark rti` (lowest-health attacker) is not
// enabled for anyone by default, and nothing marks a CC target at all — so on UBRS's
// 8-10 elite Captain pulls the party spread its damage and nobody sheeped.
namespace DcMarkPlan
{
    // A pull this size earns a crowd-control mark: past three, the party is tanking
    // more than it can burn, and taking one out of the fight is worth a caster's GCD.
    inline constexpr std::uint32_t CC_MIN_ATTACKERS = 4;

    inline bool WantCc(std::uint32_t attackers, bool partyHasCcCaster)
    {
        return partyHasCcCaster && attackers >= CC_MIN_ATTACKERS;
    }

    struct CcCandidate
    {
        bool  boss = false;          // never CC a boss
        bool  marked = false;        // already carries ANY raid icon (the skull included)
        bool  tankTarget = false;    // the tank's own victim: that one is being fought
        bool  sheepableType = false; // humanoid or beast (Polymorph's creature types)
        bool  immune = false;        // immune to Polymorph (mechanic mask)
        float healthPct = 100.0f;
    };

    // The mob to CC: an eligible candidate at the HIGHEST health — the one the party
    // would spend longest killing, so the one worth taking out. -1 when none is.
    inline int PickCc(std::vector<CcCandidate> const& c)
    {
        int best = -1;
        for (std::size_t i = 0; i < c.size(); ++i)
        {
            CcCandidate const& x = c[i];
            if (x.boss || x.marked || x.tankTarget || !x.sheepableType || x.immune)
                continue;
            if (best < 0 || x.healthPct > c[best].healthPct)
                best = static_cast<int>(i);
        }
        return best;
    }
}

#endif
