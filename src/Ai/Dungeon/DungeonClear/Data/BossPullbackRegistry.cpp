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
        //
        // Baron Geddon (12056, Molten Core 409). Nothing about the ground he stands
        // on kills anyone; what kills here is that he does not stand on any. He
        // walks waypoint path 566550, a 554yd one-way loop of 17 nodes around the
        // cavern below Garr's tunnel, and it brings him through the three trash
        // packs at the cavern's north end. Live (reported 2026-10-07): "late
        // joiner: Baron Geddon (entry 12056) engaged ... at 21.6yd | camp none |
        // party fighting: 40" — he walked into the raid while it fought trash, the
        // fight happened where he arrived, and on Inferno the raid ran 20yd off
        // him, into packs nobody had pulled.
        //
        // There is no upstream defect to fix instead. The patrol and Inferno are the
        // encounter's own design, and the 20yd scatter and its direction belong to
        // mod-playerbots' Molten Core strategy, not to this module. What this module
        // owns is WHERE the fight is, so that is the fix: somewhere a 20yd scatter
        // lands on ground that is already empty.
        //
        // Anchor (712.0,-710.0,-209.2): probed navmesh, in the last yards of the
        // tunnel that comes south from Garr's room, 14yd up it from where it opens
        // into the cavern's north-east corner. The tunnel is about 20yd wide there
        // and this is the southernmost point on its centre line with unbroken mesh
        // under an 8yd disc (z -209.3..-208.7); the mouth itself (701,-718) does not
        // have one — rock stands either side of the opening. It is 90yd from the nearest
        // node of his path (11) and 65yd from the nearest trash spawn (91289), and
        // the three packs between it and him are emptied first, in order, by the
        // ScriptedPullRegistry stages that share this anchor as their camp. He comes
        // AFTER Garr and BEFORE Shazzrah (see the roster patch), so the tunnel
        // behind the raid is cleared ground by the time anyone stands here.
        //
        // Tag window (650,-781) r22: holds nodes 10 (643.6,-778.0; 7.1yd from the
        // centre) and 11 (664.3,-786.4; 15.3yd) and excludes 9 (608.8,-787.7;
        // 41.7yd) and 12 (661.7,-816.7; 37.6yd) — 51yd of his path, entered at about
        // (628,-782) and left at about (663,-799), the closest he ever comes to the
        // anchor. So the tag leg is 90-111yd over the three stages' ground, never
        // the 250yd through live packs it could be without the window, and never
        // toward node 9, which passes 15yd from a fourth pack (91290-91292) that no
        // stage clears. He is somewhere else on the loop most of the time; the raid
        // waits on the anchor for him to come round.
        //
        // Ordinary tag, like Majordomo: no force-aggro, no summon.
        //
        // Not yet run live.
        static std::vector<BossPullback> const rows = {
            // map  entry  anchor x  y  z  forceAggro  summonIfStuck  tag x  y  radius
            { 409, 12018, 714.7f, -1205.3f, -119.6f },
            { 409, 12056, 712.0f, -710.0f, -209.2f, 0.0f, false, 650.0f, -781.0f, 22.0f },
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

bool BossPullbackRegistry::InTagWindow(BossPullback const& row, float x, float y)
{
    // No window is not "an empty window": a row that never asked for one must keep
    // arming exactly as it did before the field existed.
    if (!row.HasTagWindow())
        return true;
    float const dx = x - row.tagX;
    float const dy = y - row.tagY;
    return (dx * dx + dy * dy) <= (row.tagRadius * row.tagRadius);
}
