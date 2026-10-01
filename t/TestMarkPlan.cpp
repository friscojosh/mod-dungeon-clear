/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"
#include "Ai/Dungeon/DungeonClear/Util/DcMarkPlan.h"

using DcMarkPlan::CcCandidate;
using DcMarkPlan::CcSlots;
using DcMarkPlan::PlanCc;

namespace
{
    CcCandidate Mob(float hp, std::uint32_t castableBy)
    {
        CcCandidate c;
        c.healthPct = hp;
        c.castableBy = castableBy;
        return c;
    }
}

TEST(DcMarkPlanTest, PullSizeDecidesHowManyMobsLeaveTheFight)
{
    EXPECT_EQ(CcSlots(3), 0u);   // small pulls are burned down
    EXPECT_EQ(CcSlots(4), 1u);
    EXPECT_EQ(CcSlots(5), 1u);
    EXPECT_EQ(CcSlots(6), 2u);
    EXPECT_EQ(CcSlots(8), 3u);
    EXPECT_EQ(CcSlots(10), 4u);  // a UBRS 10-elite pack: four held, six fought
}

TEST(DcMarkPlanTest, NarrowestCasterChoosesFirst)
{
    // Caster 0 = mage (can hold mobs 0 and 1), caster 1 = warlock (only the demon, mob 1).
    // Greedy by caster order would sheep the demon and leave the warlock nothing.
    std::vector<CcCandidate> mobs = { Mob(100, 0b01), Mob(100, 0b11), Mob(100, 0), Mob(100, 0) };
    auto plan = PlanCc(mobs, 2, { false, false }, 2);
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0], std::make_pair(1, 1));   // warlock banishes the demon
    EXPECT_EQ(plan[1], std::make_pair(0, 0));   // mage sheeps the humanoid
}

TEST(DcMarkPlanTest, TakesTheHealthiestEligibleMob)
{
    std::vector<CcCandidate> mobs = { Mob(40, 1), Mob(90, 1), Mob(70, 1), Mob(100, 0) };
    auto plan = PlanCc(mobs, 1, { false }, 1);
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0].second, 1);
}

TEST(DcMarkPlanTest, NeverBossTankTargetMarkedOrAlreadyHeld)
{
    std::vector<CcCandidate> mobs(5, Mob(100, 1));
    mobs[0].boss = true;
    mobs[1].tankTarget = true;
    mobs[2].marked = true;     // carries the skull
    mobs[3].controlled = true; // someone's Fear already holds it
    auto plan = PlanCc(mobs, 1, { false }, 1);
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0].second, 4);

    mobs[4].controlled = true;
    EXPECT_TRUE(PlanCc(mobs, 1, { false }, 1).empty());
}

TEST(DcMarkPlanTest, ABusyCasterSpendsItsSlotAndIsNotReassigned)
{
    std::vector<CcCandidate> mobs = { Mob(100, 0b11), Mob(100, 0b11), Mob(100, 0), Mob(100, 0) };
    // One slot, and the mage's moon is still on a live sheep: nothing new.
    EXPECT_TRUE(PlanCc(mobs, 2, { true, false }, 1).empty());
    // Two slots: the warlock takes the second; the busy mage gets nothing.
    auto plan = PlanCc(mobs, 2, { true, false }, 2);
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0].first, 1);
}

TEST(DcMarkPlanTest, SlotsCapTheCount)
{
    std::vector<CcCandidate> mobs(6, Mob(100, 0b111));
    EXPECT_EQ(PlanCc(mobs, 3, { false, false, false }, 2).size(), 2u);
    EXPECT_EQ(PlanCc(mobs, 3, { false, false, false }, 9).size(), 3u);   // one per caster
}

TEST(DcMarkPlanTest, NoCasterCanHoldAnything)
{
    std::vector<CcCandidate> mobs(6, Mob(100, 0));   // e.g. all dragonkin, only a mage
    EXPECT_TRUE(PlanCc(mobs, 1, { false }, 3).empty());
}
