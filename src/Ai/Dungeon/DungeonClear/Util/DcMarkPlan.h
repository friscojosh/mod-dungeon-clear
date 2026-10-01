/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCMARKPLAN_H
#define _PLAYERBOT_DCMARKPLAN_H

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// The DC tank's raid-icon choices (fork: friscojosh), pure so they are testable.
//
// Everything that ACTS on the icons already exists in mod-playerbots: DpsTargetValue
// returns the `rti` icon's unit (skull by default) before any other pick, so every
// DPS bot converges on it; and FindTargetForCcStrategy gives the bot's `rti cc` icon's
// unit first claim on its CC spell. In a 5-player dungeon that is the ONLY unit a bot
// will crowd-control (HasCcTargetTrigger refuses anything else there).
//
// `rti cc` is a per-bot value, moon for everyone by default. A shared moon is why only
// ever one kind of CC happened, and why a warlock's "fear on cc" went for the mob the
// mage was sheeping. So each CC class gets ITS OWN icon (CcClass below) and the tank
// marks, per pull, a target each available caster can actually hold: a demon for the
// warlock's Banish, an undead for the priest's Shackle, a humanoid or beast for the
// mage's Polymorph, a beast or dragonkin for the druid's Hibernate, and anything else
// eligible for the hunter's Freezing Trap.
namespace DcMarkPlan
{
    // A pull this size earns crowd control: past three, the party is tanking more than
    // it can burn, and taking one out of the fight is worth a caster's GCD.
    inline constexpr std::uint32_t CC_MIN_ATTACKERS = 4;

    // How many mobs to take out of a pull: one at 4-5, and one more for every two after,
    // so the party never fights more than about four at once while it has the casters.
    inline std::uint32_t CcSlots(std::uint32_t attackers)
    {
        return attackers < CC_MIN_ATTACKERS ? 0u : 1u + (attackers - CC_MIN_ATTACKERS) / 2u;
    }

    struct CcCandidate
    {
        bool  boss = false;          // never CC a boss
        bool  marked = false;        // already carries ANY raid icon (the skull included)
        bool  tankTarget = false;    // the tank's own victim: that one is being fought
        bool  controlled = false;    // already under someone's crowd control
        float healthPct = 100.0f;
        // Bit i set => caster i (index into the caster list) can CC this one: its
        // creature type fits the caster's spell and it is not immune to it.
        std::uint32_t castableBy = 0;
    };

    // Assign up to `slots` (caster, candidate) pairs. Casters are taken narrowest first —
    // the one with the fewest eligible targets — so a Banish that can only ever land on
    // the one demon gets it before a Freezing Trap that could have held anything. Each
    // caster takes the healthiest eligible candidate left (the one the party would spend
    // longest killing). `busy` marks casters whose icon is already on a live attacker.
    inline std::vector<std::pair<int, int>> PlanCc(std::vector<CcCandidate> const& cands,
                                                   std::uint32_t casterCount,
                                                   std::vector<bool> const& busy,
                                                   std::uint32_t slots)
    {
        auto eligible = [&](std::size_t i, std::uint32_t caster)
        {
            CcCandidate const& x = cands[i];
            return !x.boss && !x.marked && !x.tankTarget && !x.controlled &&
                   (x.castableBy & (1u << caster));
        };

        std::vector<std::pair<std::uint32_t, std::uint32_t>> order;  // (eligible count, caster)
        std::uint32_t inUse = 0;
        for (std::uint32_t k = 0; k < casterCount; ++k)
        {
            if (k < busy.size() && busy[k])
            {
                ++inUse;   // its CC is still holding something: that slot is spent
                continue;
            }
            std::uint32_t n = 0;
            for (std::size_t i = 0; i < cands.size(); ++i)
                if (eligible(i, k))
                    ++n;
            if (n)
                order.push_back({ n, k });
        }
        std::stable_sort(order.begin(), order.end(),
                         [](auto const& a, auto const& b) { return a.first < b.first; });

        std::vector<std::pair<int, int>> out;
        std::vector<bool> taken(cands.size(), false);
        for (auto const& [n, k] : order)
        {
            if (inUse + out.size() >= slots)
                break;
            int best = -1;
            for (std::size_t i = 0; i < cands.size(); ++i)
                if (!taken[i] && eligible(i, k) &&
                    (best < 0 || cands[i].healthPct > cands[best].healthPct))
                    best = static_cast<int>(i);
            if (best >= 0)
            {
                taken[best] = true;
                out.push_back({ static_cast<int>(k), best });
            }
        }
        return out;
    }
}

#endif
