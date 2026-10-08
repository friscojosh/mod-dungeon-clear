/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cmath>

#include "Ai/Dungeon/DungeonClear/Data/BossPullbackRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"

// Pull-back bosses (BossPullbackRegistry) — the "this boss must be fought
// somewhere else" table.
//
// The table's FIRST row (Ghaz'an) was retired in S1593 once the cause was fixed
// upstream: he sat in the lake only because a headless party never fired
// areatrigger 4302, the sole caller of his ACTION_MOVE_TO_PLATFORM. He climbs
// onto meshed ground now and is fought like any other boss. It holds one row
// today: Majordomo Executus, in Molten Core.
//
// So what these tests pin is (a) that a map with no row still pays nothing,
// (b) the opt-in DEFAULTS a new row inherits, (c) that Ghaz'an specifically is
// NOT pulled back and IS anchored on his platform — the regression guard:
// re-adding the row would silently restore a 150yd force-aggro and a
// teleport-summon on a boss that no longer needs either — and (d) the measured
// geometry each live row is only correct because of.

namespace
{
    constexpr uint32 kUnderbog = 546;
    constexpr uint32 kGhazan = 18105;

    // Measured facts (see BossPullbackRegistry.cpp / UnderbogEvents.cpp for how
    // they were obtained). Repeated here so a future edit has to consciously
    // break a documented number rather than silently drift.
    constexpr float kLakeSurfaceZ = 50.8f;      // water sheet over Ghaz'an's basin
    constexpr float kPlatformMeshZ = 81.45f;    // probed navmesh on his platform deck
    constexpr float kPlatformX = 256.28f;       // waypoint path 1383921, final node
    constexpr float kPlatformY = -458.73f;

    constexpr uint32 kMoltenCore = 409;
    constexpr uint32 kMajordomo = 12018;
}

TEST(DungeonClearBossPullbackTest, MajordomoIsPulledBackToTheChamberMouth)
{
    // The table was empty by design until a boss turned up whose room, not an
    // upstream defect, is what kills the party: Majordomo Executus is fought round
    // a permanent fire trap. The WHY is in BossPullbackRegistry.cpp.
    EXPECT_EQ(BossPullbackRegistry::Find(kUnderbog, kGhazan), nullptr);
    EXPECT_FALSE(BossPullbackRegistry::HasRows(kUnderbog));

    BossPullback const* row = BossPullbackRegistry::Find(kMoltenCore, kMajordomo);
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(BossPullbackRegistry::HasRows(kMoltenCore));
    EXPECT_EQ(BossPullbackRegistry::Find(kMoltenCore, 11988), nullptr);  // Golemagg is fought where he stands

    // An ordinary tag: no forced aggro, no relocation — and no tag window. He
    // stands where his script summons him, so the pull arms the moment the raid
    // is on the anchor, exactly as it did before that field existed.
    EXPECT_FLOAT_EQ(row->forceAggroRange, 0.0f);
    EXPECT_FALSE(row->summonWhenStuckBelow);
    EXPECT_FALSE(row->HasTagWindow());
    EXPECT_TRUE(BossPullbackRegistry::InTagWindow(*row, 759.5f, -1173.4f));
    EXPECT_TRUE(BossPullbackRegistry::InTagWindow(*row, 0.0f, 0.0f));

    // The anchor has to stand well clear of the Hot Coal trap (736.7,-1176.3) and
    // the straight tag leg to his summon point (759.5,-1173.4) has to miss it too.
    float const pitX = 736.7f, pitY = -1176.3f, majX = 759.5f, majY = -1173.4f;
    float const dx = row->campX - pitX, dy = row->campY - pitY;
    EXPECT_GT(dx * dx + dy * dy, 30.0f * 30.0f);

    float const lx = majX - row->campX, ly = majY - row->campY;
    float const t = ((pitX - row->campX) * lx + (pitY - row->campY) * ly) / (lx * lx + ly * ly);
    float const cx = row->campX + t * lx - pitX, cy = row->campY + t * ly - pitY;
    EXPECT_GT(cx * cx + cy * cy, 9.0f * 9.0f);   // the trap row's keep-out radius
}

TEST(DungeonClearBossPullbackTest, GhazanIsNoLongerPulledBack)
{
    // The regression guard. He is an ordinary boss now — normal walk-in, normal
    // tag. A row here would re-enable the whole tag-and-drag maneuver, and with
    // his old row specifically, force-aggro from 150yd and a teleport-summon.
    EXPECT_EQ(BossPullbackRegistry::Find(kUnderbog, kGhazan), nullptr)
        << "Ghaz'an was re-added to BossPullbackRegistry. He walks onto his own "
           "platform now (areatrigger 4302 relay + Underbog event 2); check that "
           "the boss really is standing in the lake before restoring this.";
}

TEST(DungeonClearBossPullbackTest, NoUnderbogBossIsPulledBack)
{
    EXPECT_EQ(BossPullbackRegistry::Find(kUnderbog, 17770), nullptr);  // Hungarfen
    EXPECT_EQ(BossPullbackRegistry::Find(kUnderbog, 17826), nullptr);  // Swamplord Musel'ek
    EXPECT_EQ(BossPullbackRegistry::Find(kUnderbog, 17882), nullptr);  // The Black Stalker
}

TEST(DungeonClearBossPullbackTest, HasRowsGatesByMap)
{
    // The cheap early-out every map relies on. If this ever goes true for a map
    // with no row, every tick on that map starts paying for the cross-context
    // pull-back probes.
    EXPECT_FALSE(BossPullbackRegistry::HasRows(0));
    EXPECT_FALSE(BossPullbackRegistry::HasRows(545));   // The Slave Pens
    EXPECT_FALSE(BossPullbackRegistry::HasRows(585));   // Magisters' Terrace
}

// Force-aggro is a PER-ENCOUNTER opt-in, not a property of being a pull-back
// boss. Forcing bypasses the boss's own aggro logic — normal, tuned behaviour
// everywhere else — so the default has to be "off", and a new row must have to
// type the range out deliberately rather than inherit it.
TEST(DungeonClearBossPullbackTest, ForceAggroDefaultsOff)
{
    BossPullback const fresh;
    EXPECT_FLOAT_EQ(fresh.forceAggroRange, 0.0f);
}

// Same contract for the summon. Relocating a boss outright is a bigger hammer
// than forcing his aggro, so it has to be at least as hard to acquire by
// accident: off unless a row asks for it by name.
TEST(DungeonClearBossPullbackTest, SummonWhenStuckDefaultsOff)
{
    BossPullback const fresh;
    EXPECT_FALSE(fresh.summonWhenStuckBelow);
}

// The tag window is the third opt-in and keeps the same contract: off unless a
// row types a radius out. "Off" has to mean "always in the window", not "never"
// — a row that did not ask for one must keep arming the way it always has.
TEST(DungeonClearBossPullbackTest, TagWindowDefaultsOff)
{
    BossPullback const fresh;
    EXPECT_FLOAT_EQ(fresh.tagRadius, 0.0f);
    EXPECT_FALSE(fresh.HasTagWindow());
    EXPECT_TRUE(BossPullbackRegistry::InTagWindow(fresh, 0.0f, 0.0f));
    EXPECT_TRUE(BossPullbackRegistry::InTagWindow(fresh, 5000.0f, -5000.0f));

    // A centre with no radius is still no window: the radius is the switch.
    BossPullback centred;
    centred.tagX = 100.0f;
    centred.tagY = 200.0f;
    EXPECT_FALSE(centred.HasTagWindow());
    EXPECT_TRUE(BossPullbackRegistry::InTagWindow(centred, -900.0f, 900.0f));
}

TEST(DungeonClearBossPullbackTest, TagWindowIsAFlatDiscRoundItsCentre)
{
    BossPullback row;
    row.tagX = 100.0f;
    row.tagY = -200.0f;
    row.tagRadius = 10.0f;
    ASSERT_TRUE(row.HasTagWindow());

    EXPECT_TRUE(BossPullbackRegistry::InTagWindow(row, 100.0f, -200.0f));   // centre
    EXPECT_TRUE(BossPullbackRegistry::InTagWindow(row, 106.0f, -208.0f));   // 10yd: the edge counts
    EXPECT_TRUE(BossPullbackRegistry::InTagWindow(row, 100.0f, -190.5f));
    EXPECT_FALSE(BossPullbackRegistry::InTagWindow(row, 100.0f, -189.5f));  // 10.5yd north
    EXPECT_FALSE(BossPullbackRegistry::InTagWindow(row, 108.0f, -208.0f));  // 11.3yd on the diagonal
    EXPECT_FALSE(BossPullbackRegistry::InTagWindow(row, 0.0f, 0.0f));

    // Measured from the window's own centre, never from the anchor.
    row.campX = 100.0f;
    row.campY = -100.0f;
    EXPECT_FALSE(BossPullbackRegistry::InTagWindow(row, 100.0f, -100.0f));
}

TEST(DungeonClearBossPullbackTest, GhazanIsAnchoredOnHisPlatform)
{
    // His DERIVED anchor is still his static spawn (193.68, -425.00, 43.54) —
    // open water — so the roster patch has to stay even though the pull-back row
    // is gone. It must now point at the platform his own script walks him to.
    bool found = false;
    for (BossRosterPatch const& patch : BossRosterRegistry::AllPatches())
    {
        if (patch.mapId != kUnderbog)
            continue;

        for (DungeonBossInfo const& b : patch.add)
        {
            if (b.entry != kGhazan)
                continue;
            found = true;
            EXPECT_EQ(b.kind, DungeonAnchorKind::Boss);
            EXPECT_FLOAT_EQ(b.x, kPlatformX);
            EXPECT_FLOAT_EQ(b.y, kPlatformY);
            EXPECT_FLOAT_EQ(b.z, kPlatformMeshZ);

            // The anchor must clear the lake by a real margin, not merely differ
            // from it: an anchor at or near the water sheet is a spot the party
            // can be knocked off into a ~47yd pit.
            EXPECT_GT(b.z, kLakeSurfaceZ + 25.0f);

            // Re-added rather than reordered, so it must inherit its own kill-bit
            // back off the base list — otherwise the boss would carry encounter
            // index 0 and be confused with Hungarfen's completion.
            EXPECT_EQ(b.inheritCompletionFrom, kGhazan);
        }

        // ...and the derived (in-water) anchor must actually be removed, else
        // both copies survive and the clear visits the lake one anyway.
        bool removed = false;
        for (uint32 e : patch.remove)
            if (e == kGhazan)
                removed = true;
        EXPECT_TRUE(removed);
    }
    EXPECT_TRUE(found) << "The Underbog roster patch no longer re-anchors Ghaz'an";
}

TEST(DungeonClearBossPullbackTest, GhazanAnchorIsOnTheDropDownDeck)
{
    // The platform and the "Drop down past Ghaz'an" ledge (274.72, -462.60,
    // 81.37) are the SAME walkable deck — that is what makes the anchor
    // reachable, and it is why boss-nav can route the party there at all. Pinned
    // as a distance + height agreement so moving either one alone fails here.
    constexpr float kLedgeX = 274.72f, kLedgeY = -462.60f, kLedgeZ = 81.37f;

    float const dx = kPlatformX - kLedgeX;
    float const dy = kPlatformY - kLedgeY;
    EXPECT_LT(std::sqrt(dx * dx + dy * dy), 30.0f)
        << "the boss anchor drifted off the ledge's deck";
    EXPECT_LT(std::fabs(kPlatformMeshZ - kLedgeZ), 2.0f)
        << "the boss anchor is no longer at deck height";
}
