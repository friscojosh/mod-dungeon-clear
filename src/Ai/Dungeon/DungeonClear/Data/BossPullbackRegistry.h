/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_BOSSPULLBACKREGISTRY_H
#define _PLAYERBOT_BOSSPULLBACKREGISTRY_H

#include "Define.h"

// Static registry of PULL-BACK bosses: bosses that must never be fought where
// they stand, because the ground they stand on kills the party.
//
// THE TABLE IS CURRENTLY EMPTY — read BossPullbackRegistry.cpp before adding to
// it. Its one and only row (Ghaz'an, 18105, The Underbog 546) was retired in
// S1593 once the actual cause was fixed upstream: he was only ever in the water
// because a headless party never fired areatrigger 4302, the sole caller of his
// ACTION_MOVE_TO_PLATFORM. He climbs onto meshed, connected ground now and is
// fought like any other boss. The machinery stays because the facility is
// general and its invariants are pinned by tests.
//
// That history is the main thing to take from this file: a pull-back row is a
// POSITIONAL WORKAROUND, and the first question for a new one is always whether
// the boss is standing in a bad place because of an upstream defect that can be
// fixed instead. This row spent a long time treating a symptom.
//
// The fix is positional and hand-authored, in the same spirit as
// FightInPlaceRegistry (which forbids a pull) — this is its mirror image: it
// MANDATES one. A row says "the party fights this boss HERE, not where he lives".
// The tank walks the party to `camp`, goes out ALONE to tag the boss, drags him
// back, and the whole party fights on the anchor.
//
// Everything after that is the EXISTING advanced-pull machinery, unchanged: the
// Forming/Advancing/Returning/Engage FSM, the follower hold-at-camp, and the
// drag-back action all run exactly as they do for a trash pull. A row only
// changes WHICH target the pull is aimed at and WHERE the camp is.
//
// Why a registry and not geometry: "is this spot lethal" is not derivable. The
// navmesh happily reports the water sheet as walkable (it IS — you can swim it),
// the mob is reachable, and there is no aura or hazard emitter to detect. Only a
// human who has watched the encounter knows the party has to stand somewhere
// else. Mirrors RoomAggroRegistry / BossRosterRegistry: adding a fix is a single
// table edit inside DungeonClear/, never a core change or an mmap regen.
struct BossPullback
{
    uint32 mapId{0};
    uint32 bossEntry{0};
    // Party fight anchor: hand-authored, on safe ground, verified against the
    // navmesh. This is ALSO the boss's roster anchor (see the matching
    // BossRosterPatch), so boss navigation walks the party here instead of
    // routing them at the boss's live position.
    float  campX{0.0f}, campY{0.0f}, campZ{0.0f};

    // FORCE-AGGRO opt-in. 0 (the default) means "tag this boss the normal way" —
    // the tag leg walks inside his aggro bubble and lets him notice the tank, the
    // same pull every other boss in every other dungeon gets. A positive value is
    // the range, in yards, within which the tank instead forces him into combat
    // outright (DcForcePullbackAggro).
    //
    // DEFAULT OFF ON PURPOSE, and it should stay the exception. Forcing bypasses
    // the boss's own aggro logic, which is normal, tuned behaviour we want almost
    // everywhere: it can start an encounter from outside the range the script
    // expects, skip a script's own aggro hooks, and it is indiscriminate about
    // where the boss is standing when it lands. Setting this is a statement that a
    // SPECIFIC boss cannot be tagged normally at all — not a shortcut for one that
    // is merely awkward.
    //
    // Ghaz'an was the case it was built for, on the belief that his platform was
    // off-mesh and he never finished his lap — so no reachable spot existed
    // inside his aggro bubble. Both were consequences of him being stuck in the
    // water, and both went away with the areatrigger fix (S1593). No row uses
    // this today, and "the tag leg keeps timing out" is a reason to find out WHY
    // before it is a reason to set this.
    float  forceAggroRange{0.0f};

    // SUMMON-IF-STUCK opt-in, and like forceAggroRange it defaults OFF and should
    // stay rare — this one more so, because it relocates a boss outright.
    //
    // It fires in exactly one situation: the tank is home at the anchor, the boss
    // is engaged and coming, and he is STILL IN THE WATER BELOW the anchor. Then he
    // is teleported to the anchor rather than waited on. It is not a shortcut for a
    // slow boss — the water-and-below test is what keeps it to the failure it is
    // for, and a boss that has climbed out onto dry ground is left alone to walk
    // the rest of the way himself.
    //
    // Ghaz'an was the case for this too: engaged from the water he had to chase
    // the party up a climb his aggro path could not follow, and he would hang at
    // the water's edge indefinitely. He is not engaged from the water any more —
    // he walks up under his own script, before the fight — so nothing needs it.
    // Kept because "the boss physically cannot reach the party" is a real class
    // of failure, but a boss that is merely SLOW to arrive is not it.
    bool   summonWhenStuckBelow{false};

    // TAG WINDOW opt-in, for a boss that PATROLS. (tagX,tagY) and a 2D radius in
    // yards; a radius of 0 (the default) means "no window", and the row behaves
    // exactly as it always has — the pull arms the moment the tank is on the anchor
    // and the boss is alive anywhere on the map.
    //
    // DEFAULT OFF, and for a boss that stands still it should stay off: there the
    // distance from the anchor to the boss is a fact about the room, measured once
    // when the row was authored. A patroller breaks that. The tag leg walks to his
    // LIVE position with the chase leash skipped, so "alive anywhere on the map" can
    // mean a tag taken from the far end of a 250yd loop, through every pack between
    // here and there — the anchor was chosen for the ground around IT, and says
    // nothing about the ground he happens to be crossing when the tank arrives.
    //
    // So a row with a window waits for him. The raid holds on the anchor — the same
    // hold it already makes whenever the pull is not due — and the pull arms only on
    // a tick his live position is inside the window: the stretch of his path that
    // passes the anchor, over ground the plan has already emptied.
    //
    // IT ONLY DELAYS THE START. Once the pull has committed
    // (DcPullContext::bossPullback) the window is not consulted again: he keeps
    // walking, he will be outside it within seconds, and a maneuver that dissolved
    // when he stepped over the line would strand the party passive at the camp with
    // nothing left to release them. An in-flight pull finishes on the existing leg
    // watchdogs, exactly like a row with no window.
    //
    // Size it from his waypoints, not by eye: it should hold the nodes nearest the
    // anchor and exclude the ones either side, and the excluded ones are the
    // statement of what the tank must never be sent past. 2D on purpose — a patrol
    // path is a line on a floor, and the anchor's own Z already says which floor.
    float  tagX{0.0f}, tagY{0.0f};
    float  tagRadius{0.0f};

    // True once a row names a tag window.
    bool HasTagWindow() const { return tagRadius > 0.0f; }
};

class BossPullbackRegistry
{
public:
    // The pull-back row for (mapId, bossEntry), or nullptr when the boss is
    // fought normally. Pure (no game state) so it is unit-testable on its own.
    // Linear scan; the table is tiny.
    static BossPullback const* Find(uint32 mapId, uint32 bossEntry);

    // True iff `mapId` has any pull-back boss. Cheap early-out for the per-tick
    // callers (the engage gate and the camp guard) so a map with no rows pays one
    // bool and nothing else.
    static bool HasRows(uint32 mapId);

    // May a pull of `row`'s boss START with him standing at (x,y)? Always true for
    // a row with no tag window; otherwise true iff the point is inside it (2D, the
    // edge counts as inside). Pure — the caller supplies his live position — so the
    // geometry is unit-tested without a boss to stand in it. See BossPullback::tagX.
    static bool InTagWindow(BossPullback const& row, float x, float y);
};

#endif  // _PLAYERBOT_BOSSPULLBACKREGISTRY_H
