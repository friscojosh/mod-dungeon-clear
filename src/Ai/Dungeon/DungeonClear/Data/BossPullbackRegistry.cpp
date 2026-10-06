/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "BossPullbackRegistry.h"

#include <vector>

namespace
{
    // ---- the table ------------------------------------------------------
    //
    // CURRENTLY EMPTY. It has held exactly one row in its life — Ghaz'an
    // (18105, The Underbog 546) — and that row was retired in S1593. The
    // machinery is kept because the facility is general and the invariants are
    // tested; adding a boss is still a single entry here plus a matching
    // BossRosterPatch anchor.
    //
    // WHY GHAZ'AN'S ROW WENT AWAY, so nobody re-adds it from the old reasoning:
    //
    // The row existed because Ghaz'an was always found swimming in his lake, and
    // the party had to be walked ~147yd away to fight him on dry ground — with
    // force-aggro at 150yd and a teleport-if-stuck, because there was no
    // reachable spot inside his aggro bubble and no reliable way for him to
    // climb out. Every one of those was compensation for a single upstream
    // cause: `at_underbog_ghazan` (areatrigger 4302) is the ONLY caller of his
    // ACTION_MOVE_TO_PLATFORM, and a headless bot party never sent the
    // CMSG_AREATRIGGER that runs it. So he never left the water, and everything
    // downstream was built around meeting him there.
    //
    // DcTestAreaTriggers now sends that packet (and the Underbog "Send Ghaz'an
    // to his platform" event covers a route that misses the volume), so he
    // climbs waypoint path 1383921 onto his platform like he does for a real
    // party. Probed against the live mmaps rather than assumed:
    //
    //   * his platform end (256.28, -458.73) has walkable navmesh at z 81.45 —
    //     the SAME surface as (274.72, -462.60), the ledge boss-nav already
    //     drives the tank to for the drop-down objective;
    //   * his whole 12yd MoveRandom circle is on it (probed E/W/N/S/NE/SE, all
    //     z 81.45), so he cannot wander off the deck;
    //   * the deck connects to the old anchor by a continuous walkable ramp —
    //     (154.16,-452.03) z 73.58 -> (170,-460) 73.58 -> (190,-468) 75.98 ->
    //     (205,-472) 79.67 -> (215,-475) 81.08 -> the deck.
    //
    // The "his platform and the pipe are missing from the extracted navmesh"
    // finding this row was justified with was measured around his WATER HOME
    // (193.74, -423.40, 43.58) and never around the platform itself. The
    // platform is meshed. On dry, connected ground he is an ordinary boss, and
    // an ordinary pull is strictly better than a forced one.
    //
    // A row belongs here only when the ground a boss stands on genuinely kills
    // the party AND no upstream cause can be fixed instead. Check the second
    // half first — this row spent a long time treating a symptom.
    std::vector<BossPullback> const& Rows()
    {
        // Majordomo Executus (12018, Molten Core 409). The ground that kills here is
        // not his own spot but the room's middle: gameobject 178164 "Hot Coal", a
        // permanent fire trap (5yd trigger) at (736.7,-1176.3,-118.1), 23yd from his
        // summon point (759.5,-1173.4) and inside the ring his eight adds stand in.
        // Walked in, the tank took the pull 14yd from the pit and the raid fought,
        // rested and resurrected on top of it (live 2026-10-06; the DcTrapHazard row
        // now drives bots OFF it, but that only empties the pit, it does not move the
        // fight). There is no upstream defect to fix instead: the pit is the
        // encounter's own mechanic, where his two Teleports drop people.
        //
        // Anchor (714.7,-1205.3,-119.6): probed navmesh (poly centroid, flat, slope
        // 4 deg) on the chamber floor by the south-west mouth the raid enters
        // through — 38yd from the pit, 57yd from his summon point, in a floor about
        // 60yd wide. The straight leg from the anchor to him passes 13yd south of
        // the pit's centre, outside the trap row's 9yd keep-out, so the tag does not
        // cross the coals either. He chases like any boss (no leash in
        // boss_majordomo_executus.cpp) and the adds come with him, so Separation
        // Anxiety (adds too far from HIM) is not provoked by the drag.
        //
        // Ordinary tag: his adds notice the tank at ~35yd, which is how the fight
        // started on the live run. No force-aggro, no summon.
        static std::vector<BossPullback> const rows = {
            // map  entry  anchor x  y  z  forceAggro  summonIfStuck
            { 409, 12018, 714.7f, -1205.3f, -119.6f },
        };
        return rows;
    }
}

BossPullback const* BossPullbackRegistry::Find(uint32 mapId, uint32 bossEntry)
{
    for (BossPullback const& r : Rows())
        if (r.mapId == mapId && r.bossEntry == bossEntry)
            return &r;
    return nullptr;
}

bool BossPullbackRegistry::HasRows(uint32 mapId)
{
    for (BossPullback const& r : Rows())
        if (r.mapId == mapId)
            return true;
    return false;
}
