/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "gtest/gtest.h"

#include <cmath>

#include <utility>
#include <vector>

#include "Ai/Dungeon/DungeonClear/Data/BossPullbackRegistry.h"
#include "Ai/Dungeon/DungeonClear/Data/ScriptedPullRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/BossRosterRegistry.h"

// Pull-back bosses (BossPullbackRegistry) — the "this boss must be fought
// somewhere else" table.
//
// The table's FIRST row (Ghaz'an) was retired in S1593 once the cause was fixed
// upstream: he sat in the lake only because a headless party never fired
// areatrigger 4302, the sole caller of his ACTION_MOVE_TO_PLATFORM. He climbs
// onto meshed ground now and is fought like any other boss. It holds two rows
// today, both in Molten Core: Majordomo Executus and Baron Geddon.
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
    constexpr uint32 kGeddon = 12056;
    constexpr uint32 kShazzrah = 12264;

    // Baron Geddon's waypoint path 566550 (acore_world.waypoint_data), nodes 1-17
    // in order. One direction only, and a loop: 17 runs back to 1. Index = node - 1.
    std::vector<std::pair<float, float>> const& GeddonPath()
    {
        static std::vector<std::pair<float, float>> const kPath{
            {747.5f, -981.7f}, {740.0f, -953.2f}, {716.3f, -910.9f}, {701.4f, -887.8f},
            {675.8f, -847.8f}, {639.1f, -830.6f}, {611.1f, -828.0f}, {591.2f, -811.5f},
            {608.8f, -787.7f}, {643.6f, -778.0f}, {664.3f, -786.4f}, {661.7f, -816.7f},
            {678.6f, -847.1f}, {699.6f, -878.1f}, {718.3f, -907.5f}, {740.4f, -949.4f},
            {746.6f, -966.6f}};
        return kPath;
    }

    float Dist2d(float ax, float ay, float bx, float by)
    {
        float const dx = ax - bx;
        float const dy = ay - by;
        return std::sqrt(dx * dx + dy * dy);
    }
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

TEST(DungeonClearBossPullbackTest, GeddonIsFetchedOnlyFromTheStretchThatPassesTheAnchor)
{
    // Baron Geddon patrols; the WHY and the measurements are in
    // BossPullbackRegistry.cpp. What is pinned here is that the window means what
    // the row comment says it means, against his actual waypoints.
    BossPullback const* row = BossPullbackRegistry::Find(kMoltenCore, kGeddon);
    ASSERT_NE(row, nullptr);

    // An ordinary tag: no forced aggro, no relocation.
    EXPECT_FLOAT_EQ(row->forceAggroRange, 0.0f);
    EXPECT_FALSE(row->summonWhenStuckBelow);
    ASSERT_TRUE(row->HasTagWindow());

    // Nodes 10 and 11 are in; every other node of the loop is out — 9 and 12, the
    // ones either side, by a real margin, so the pull cannot arm with him merely
    // NEAR the window on his way in or out.
    std::vector<std::pair<float, float>> const& path = GeddonPath();
    ASSERT_EQ(path.size(), 17u);
    for (size_t i = 0; i < path.size(); ++i)
    {
        bool const in =
            BossPullbackRegistry::InTagWindow(*row, path[i].first, path[i].second);
        EXPECT_EQ(in, i == 9 || i == 10) << "node " << (i + 1);
    }
    EXPECT_GT(Dist2d(row->tagX, row->tagY, path[8].first, path[8].second),
              row->tagRadius + 10.0f) << "node 9 — the leg that passes the west pack";
    EXPECT_GT(Dist2d(row->tagX, row->tagY, path[11].first, path[11].second),
              row->tagRadius + 10.0f) << "node 12";

    // The anchor is nowhere on his path: he is never standing on the camp when
    // the raid arrives, and the camp is never inside the window it waits on.
    for (size_t i = 0; i < path.size(); ++i)
        EXPECT_GT(Dist2d(row->campX, row->campY, path[i].first, path[i].second), 60.0f)
            << "node " << (i + 1);
    EXPECT_FALSE(BossPullbackRegistry::InTagWindow(*row, row->campX, row->campY));

    // And the window is the near stretch, which is the whole point of it: the tag
    // leg from the anchor to anywhere inside it is bounded, where the leg to the
    // far end of the loop (node 1) is well over twice that.
    EXPECT_LT(Dist2d(row->campX, row->campY, row->tagX, row->tagY) + row->tagRadius,
              120.0f);
    EXPECT_GT(Dist2d(row->campX, row->campY, path[0].first, path[0].second), 250.0f);
}

TEST(DungeonClearBossPullbackTest, GeddonsAnchorIsOnePointInThreeTables)
{
    // The pull-back row's camp, the roster anchor boss navigation walks the raid
    // to, and the camp of every scripted stage that clears the ground in front of
    // it are the same point by design. Any one of them drifting leaves the raid
    // waiting somewhere the tank does not drag him to.
    BossPullback const* row = BossPullbackRegistry::Find(kMoltenCore, kGeddon);
    ASSERT_NE(row, nullptr);

    bool found = false;
    for (BossRosterPatch const& patch : BossRosterRegistry::AllPatches())
    {
        if (patch.mapId != kMoltenCore)
            continue;

        for (DungeonBossInfo const& b : patch.add)
        {
            if (b.entry != kGeddon)
                continue;
            found = true;
            EXPECT_EQ(b.kind, DungeonAnchorKind::Boss);
            EXPECT_FLOAT_EQ(b.x, row->campX);
            EXPECT_FLOAT_EQ(b.y, row->campY);
            EXPECT_FLOAT_EQ(b.z, row->campZ);
            // Re-added, so it must inherit its own kill-bit back off the base list,
            // and it takes the order slot Shazzrah gives up.
            EXPECT_EQ(b.inheritCompletionFrom, kGeddon);
            EXPECT_EQ(b.orderOverride, 4);

            // ...and the derived (spawn) anchor must actually be removed, else both
            // copies survive and the clear walks to his spawn anyway.
            bool removed = false;
            for (uint32 e : patch.remove)
                if (e == kGeddon)
                    removed = true;
            EXPECT_TRUE(removed);

            // Shazzrah moves behind him, as a KEPT entry (so his anchor and bit
            // stay derived).
            bool shazzrahAfter = false;
            for (auto const& r : patch.reorder)
                if (r.first == kShazzrah && r.second == 5)
                    shazzrahAfter = true;
            EXPECT_TRUE(shazzrahAfter);
            for (uint32 e : patch.remove)
                EXPECT_NE(e, kShazzrah);
        }
    }
    EXPECT_TRUE(found) << "the Molten Core roster patch no longer re-anchors Baron Geddon";

    std::vector<ScriptedPullStage const*> const stages =
        ScriptedPullRegistry::Rows(kMoltenCore);
    ASSERT_EQ(stages.size(), 3u);
    for (ScriptedPullStage const* s : stages)
    {
        EXPECT_EQ(s->bossEntry, kGeddon) << "stage " << s->order;
        EXPECT_FLOAT_EQ(s->campX, row->campX) << "stage " << s->order;
        EXPECT_FLOAT_EQ(s->campY, row->campY) << "stage " << s->order;
        EXPECT_FLOAT_EQ(s->campZ, row->campZ) << "stage " << s->order;

        // The anchor stands well back from the packs it is the camp for...
        EXPECT_GT(Dist2d(row->campX, row->campY, s->packX, s->packY), 45.0f + s->packRadius)
            << "stage " << s->order;
        // ...and no pack is left standing between it and the window: every staged
        // cylinder is nearer the anchor than the far edge of the window is.
        EXPECT_LT(Dist2d(row->campX, row->campY, s->packX, s->packY),
                  Dist2d(row->campX, row->campY, row->tagX, row->tagY) + row->tagRadius)
            << "stage " << s->order;
    }
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
