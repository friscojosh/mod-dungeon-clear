/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCGROUPPLAN_H
#define _PLAYERBOT_DCGROUPPLAN_H

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// Which raid sub-group each shaman belongs in (fork: friscojosh), pure so it is testable.
//
// Why shamans, and only shamans. On this realm the raid-wide group effects really are
// raid-wide -- paladin auras, the buff totems, Battle Shout and Gift of the Wild all apply
// to the raid (Spell.dbc: APPLY_AREA_AURA_RAID / TARGET_UNIT_CASTER_AREA_RAID), and Prayer of
// Healing heals the party of whoever it is cast on, so a priest's own group does not matter.
// What is left is Tremor Totem: its pulse (8146) is TARGET_UNIT_SRC_AREA_PARTY, 30yd. A
// Tremor Totem frees the shaman's OWN party from fear and nobody else. A raid assembled class
// by class puts every shaman in one group, usually with the hunters, where the totem does
// nothing for the tank Magmadar is about to fear.
//
// So: no two shamans share a group while another group has none, and the groups that get one
// are chosen by who is in them -- the main tank's group first, then groups by how many tanks
// and then how many melee they hold (those are the ones standing in a fear's reach).
//
// The plan only ever SWAPS a shaman with one member of the group it is going to, so every
// group keeps its size, and it never moves a tank or anyone who is not a bot.
namespace DcGroupPlan
{
    struct Member
    {
        std::uint8_t group = 0;     // raid sub-group, 0-based
        bool shaman = false;
        bool tank = false;
        bool mainTank = false;
        bool melee = false;         // fights in melee and is not a tank
        bool healer = false;
        bool movable = false;       // a bot; a real player is never moved
    };

    // A swap of two members, by index into the input.
    using Swap = std::pair<int, int>;

    // How much a group wants a Tremor Totem. Shamans are left out of the count so the answer
    // does not change as they are moved around.
    inline int GroupNeed(std::vector<Member> const& members, std::uint8_t group)
    {
        int need = 0;
        for (Member const& m : members)
        {
            if (m.group != group || m.shaman)
                continue;
            if (m.mainTank)
                need += 1000;
            if (m.tank)
                need += 100;
            else if (m.melee)
                need += 10;
            else
                need += 1;
        }
        return need;
    }

    // Who a shaman takes the place of in `group`: never a tank, a real player or another
    // shaman; a ranged damage dealer before a melee one before a healer. -1 if nobody can go.
    inline int Displaceable(std::vector<Member> const& members, std::uint8_t group)
    {
        int best = -1;
        int bestRank = 0;
        for (std::size_t i = 0; i < members.size(); ++i)
        {
            Member const& m = members[i];
            if (m.group != group || !m.movable || m.tank || m.shaman)
                continue;
            int const rank = m.healer ? 1 : (m.melee ? 2 : 3);
            if (rank > bestRank)
            {
                best = static_cast<int>(i);
                bestRank = rank;
            }
        }
        return best;
    }

    inline std::vector<Swap> Plan(std::vector<Member> members)
    {
        std::vector<Swap> swaps;

        std::vector<std::uint8_t> groups;
        for (Member const& m : members)
            if (std::find(groups.begin(), groups.end(), m.group) == groups.end())
                groups.push_back(m.group);
        std::sort(groups.begin(), groups.end());
        std::stable_sort(groups.begin(), groups.end(), [&](std::uint8_t a, std::uint8_t b)
        {
            return GroupNeed(members, a) > GroupNeed(members, b);
        });

        auto shamansIn = [&](std::uint8_t group)
        {
            int n = 0;
            for (Member const& m : members)
                if (m.group == group && m.shaman)
                    ++n;
            return n;
        };

        // Walk the groups from neediest down. One that has no shaman takes a spare from a
        // group that is either less needy or holds more than one.
        for (std::size_t g = 0; g < groups.size(); ++g)
        {
            std::uint8_t const want = groups[g];
            if (shamansIn(want) > 0)
                continue;

            int donor = -1;
            // A second shaman from any group first (that one is wasted where it is), taken
            // from the least needy such group; failing that, the only shaman of a group
            // further down the list.
            for (std::size_t d = groups.size(); d-- > 0 && donor < 0;)
            {
                if (groups[d] == want || shamansIn(groups[d]) < 2)
                    continue;
                for (std::size_t i = 0; i < members.size(); ++i)
                    if (members[i].group == groups[d] && members[i].shaman && members[i].movable)
                        donor = static_cast<int>(i);
            }
            for (std::size_t d = groups.size(); d-- > g + 1 && donor < 0;)
            {
                if (shamansIn(groups[d]) != 1)
                    continue;
                for (std::size_t i = 0; i < members.size(); ++i)
                    if (members[i].group == groups[d] && members[i].shaman && members[i].movable)
                        donor = static_cast<int>(i);
            }
            if (donor < 0)
                break;   // no shaman left to hand out: every group below goes without too

            int const out = Displaceable(members, want);
            if (out < 0)
                continue;   // a group of tanks and players only: nobody can make room

            std::swap(members[donor].group, members[out].group);
            swaps.push_back({ donor, out });
        }

        return swaps;
    }
}

#endif
