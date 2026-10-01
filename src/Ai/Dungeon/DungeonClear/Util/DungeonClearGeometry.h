/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DUNGEONCLEARGEOMETRY_H
#define _PLAYERBOT_DUNGEONCLEARGEOMETRY_H

#include <cstddef>
#include <vector>

#include "Define.h"
#include "G3D/Vector3.h"
#include "MapDefines.h"

class Player;
class dtQueryFilterExt;

// Shared geometry / line-of-sight primitives for the dungeon-clear route
// producers and the corridor-centering pass. These were previously copy-pasted
// byte-for-byte across StridedPathfinder, LongRangePathfinder and CorridorCenter
// (with comments admitting "keep the two in sync"); this is the single source of
// truth so a tuning change can't silently desync the producers.
namespace DungeonClearGeometry
{
    // Plain Euclidean distance helpers (no engine state).
    float Dist2D(float ax, float ay, float bx, float by);
    float Dist3D(float ax, float ay, float az, float bx, float by, float bz);

    // True iff the bot has a clear static-VMAP straight line between two world
    // points. Sub-3yd hops are treated as clear (they're Detour smoothing
    // artifacts the producers also skip). A null map fails open (clear) so the
    // check never blocks routing when geometry can't be queried. The raycast is
    // bumped to ~eye height so floor-touching points don't false-fail on
    // slope/stair transitions.
    bool ChordClear(Player* bot, G3D::Vector3 const& a, G3D::Vector3 const& b);

    // Walks the smoothed polyline's consecutive chords with ChordClear and
    // returns how many *leading* points form a usable corridor (counting from
    // index 0, inclusive). Returns pts.size() when the whole corridor is clean
    // (the common case).
    //
    // On a sustained obstruction it returns the length of the clean prefix so
    // the caller can still use the verified part and re-probe from its end.
    // Isolated blocked chords (corner grazes on sharp convex bends) are tolerated
    // up to a small consecutive-failure bridge; a clear chord resets the run. A
    // genuinely bad poly bridging two rooms through solid geometry fails for a
    // longer continuous run and truncates the corridor there.
    //
    // Static-VMAP-only (no game-object checks): doors and dynamic obstacles are
    // handled elsewhere (DungeonClearBlockingDoorValue / engage triggers), and
    // including them here would reject good corridors with a transient door.
    std::size_t LosCleanPrefixCount(Player* bot, std::vector<G3D::Vector3> const& pts);

    // Every poly type a player can stand on — the include flags for every DC
    // navmesh filter. NAV_GROUND_STEEP is the 50-60 degree band the mmap generator
    // tags (modAlmostUnwalkableTriangles): still walkable, so a route or a snap
    // must accept it, or a party on a ramp could neither route nor locate itself.
    // ApplyTerrainAreaCosts makes it expensive instead.
    inline constexpr uint16 WALKABLE_NAV_FLAGS =
        NAV_GROUND | NAV_GROUND_STEEP | NAV_WATER | NAV_MAGMA;

    // Apply the terrain-preference Detour area-cost multipliers (WaterPathCost,
    // MagmaPathCost, SteepPathCost) to a freshly-built filter so the A* corridor
    // search and the string-pulled smooth path prefer level, dry ground. The
    // costed polys stay in the include flags, so a route still takes a steep ramp
    // or crosses liquid when no cheaper-enough alternative exists (the only ramp
    // up, water caves, mandatory swims). Costs are server-only conf values, so
    // this is safe to call from the off-map-thread route producers.
    void ApplyTerrainAreaCosts(dtQueryFilterExt& filter);
}

#endif
