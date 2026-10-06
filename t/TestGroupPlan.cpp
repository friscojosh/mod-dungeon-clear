/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "Ai/Dungeon/DungeonClear/Util/DcGroupPlan.h"

using DcGroupPlan::Member;
using DcGroupPlan::Plan;

namespace
{
    Member Bot(std::uint8_t group) { Member m; m.group = group; m.movable = true; return m; }
    Member Tank(std::uint8_t group, bool main = false) { Member m = Bot(group); m.tank = true; m.mainTank = main; return m; }
    Member Melee(std::uint8_t group) { Member m = Bot(group); m.melee = true; return m; }
    Member Healer(std::uint8_t group) { Member m = Bot(group); m.healer = true; return m; }
    Member Shaman(std::uint8_t group) { Member m = Bot(group); m.shaman = true; return m; }
    Member Human(std::uint8_t group) { Member m; m.group = group; m.melee = true; return m; }

    std::vector<Member> Apply(std::vector<Member> members)
    {
        for (auto const& [a, b] : Plan(members))
            std::swap(members[a].group, members[b].group);
        return members;
    }

    int ShamansIn(std::vector<Member> const& members, std::uint8_t group)
    {
        int n = 0;
        for (Member const& m : members)
            if (m.group == group && m.shaman)
                ++n;
        return n;
    }

    // The raid as it stood on 2026-10-06: built class by class, both shamans with the hunters.
    std::vector<Member> LiveRaid()
    {
        return {
            Tank(0), Tank(0, true), Tank(0), Healer(0), Human(0),          // 1: three warriors, a paladin, Josh
            Healer(1), Healer(1), Healer(1), Healer(1), Healer(1),         // 2: paladins and a priest
            Healer(2), Healer(2), Healer(2), Healer(2), Healer(2),         // 3: priests and druids
            Bot(3), Bot(3), Bot(3), Shaman(3), Shaman(3),                  // 4: hunters and BOTH shamans
            Bot(4), Bot(4), Bot(4), Bot(4), Bot(4),                        // 5: casters
            Bot(5), Bot(5), Bot(5), Bot(5), Bot(5),                        // 6: casters
            Melee(6), Melee(6), Melee(6), Melee(6), Bot(6),                // 7: rogues and a warlock
            Tank(7), Melee(7), Melee(7), Melee(7), Melee(7),               // 8: warriors, one of them a tank
        };
    }
}

TEST(DcGroupPlanTest, TheLiveRaidGetsAShamanWithEachTankGroup)
{
    std::vector<Member> const after = Apply(LiveRaid());
    EXPECT_EQ(ShamansIn(after, 0), 1) << "the main tank's group";
    EXPECT_EQ(ShamansIn(after, 7), 1) << "the other tank's group";
    EXPECT_EQ(ShamansIn(after, 3), 0) << "the hunters no longer hold both";
}

TEST(DcGroupPlanTest, GroupsKeepTheirSizeAndNobodyProtectedMoves)
{
    std::vector<Member> const before = LiveRaid();
    std::vector<Member> const after = Apply(before);
    for (std::uint8_t g = 0; g < 8; ++g)
    {
        int nb = 0, na = 0;
        for (Member const& m : before) nb += m.group == g;
        for (Member const& m : after) na += m.group == g;
        EXPECT_EQ(nb, na) << "group " << int(g);
    }
    for (std::size_t i = 0; i < before.size(); ++i)
        if (before[i].tank || !before[i].movable)
            EXPECT_EQ(before[i].group, after[i].group) << "member " << i;
}

TEST(DcGroupPlanTest, TheHealerLeavesTheTankGroupOnlyWhenNobodyElseCan)
{
    // Group 1 is three tanks, a healer and a real player: the healer is the only one who can
    // make room, and the shaman's totem is worth more there than the healer's seat.
    std::vector<Member> const before = LiveRaid();
    std::vector<Member> const after = Apply(before);
    EXPECT_NE(after[3].group, 0);
    // Group 8 has melee to spare: one of them goes, not the tank.
    EXPECT_EQ(after[35].group, 7);
}

TEST(DcGroupPlanTest, AlreadySpreadIsLeftAlone)
{
    std::vector<Member> const once = Apply(LiveRaid());
    EXPECT_TRUE(Plan(once).empty());
}

TEST(DcGroupPlanTest, NoShamansNoSwaps)
{
    EXPECT_TRUE(Plan({ Tank(0, true), Melee(0), Bot(1), Bot(1) }).empty());
}

TEST(DcGroupPlanTest, ALoneShamanGoesToTheMainTank)
{
    std::vector<Member> const after = Apply({ Tank(0, true), Melee(0), Bot(0), Shaman(1), Bot(1), Bot(1) });
    EXPECT_EQ(ShamansIn(after, 0), 1);
    EXPECT_EQ(ShamansIn(after, 1), 0);
}

TEST(DcGroupPlanTest, AShamanAlreadyWithTheMainTankStays)
{
    std::vector<Member> const before = { Tank(0, true), Shaman(0), Bot(0), Melee(1), Bot(1), Bot(1) };
    EXPECT_TRUE(Plan(before).empty());
}

TEST(DcGroupPlanTest, AFullHouseOfShamansCoversEveryGroupOnce)
{
    std::vector<Member> members;
    for (std::uint8_t g = 0; g < 4; ++g)
        for (int i = 0; i < 4; ++i)
            members.push_back(g == 0 && i == 0 ? Tank(g, true) : Melee(g));
    // four shamans, all dropped into group 3
    for (int i = 0; i < 4; ++i)
        members[12 + i] = Shaman(3);
    std::vector<Member> const after = Apply(members);
    for (std::uint8_t g = 0; g < 4; ++g)
        EXPECT_EQ(ShamansIn(after, g), 1) << "group " << int(g);
}

TEST(DcGroupPlanTest, AMovablePlayerMakesRoomBeforeAHealerDoes)
{
    // The live arranger marks real players movable. In the main tank's group of three tanks,
    // a healer and a melee player, the melee player is the one who goes: the healer stays.
    std::vector<Member> members = LiveRaid();
    members[4].movable = true;
    std::vector<Member> const after = Apply(members);
    EXPECT_EQ(ShamansIn(after, 0), 1);
    EXPECT_EQ(after[3].group, 0) << "the healer stays with the tanks";
    EXPECT_NE(after[4].group, 0) << "the player made room";
}

TEST(DcGroupPlanTest, AnImmovableShamanIsNeverMoved)
{
    Member human = Shaman(1);
    human.movable = false;
    std::vector<Member> const before = { Tank(0, true), Melee(0), human, Bot(1) };
    EXPECT_TRUE(Plan(before).empty());
}
