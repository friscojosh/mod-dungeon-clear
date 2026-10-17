/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Measurement, not a committed regression: how much of a bot's route crosses steep ground
// (NAV_GROUND_STEEP, the 50-60 degree band the regenerated navmesh tags) under different
// steep-ground policies. Reads the FULL mmaps dir from env DC_PROBE_MMAPS (a dir holding mmaps/)
// and GTEST_SKIPs without it.
//
// Per map: a fixed-seed set of random start/end pairs on the mesh, each routed with the bot filter
// (ground + water, water cost 20 — PathGenerator's bot branch) under four steep policies:
//   cost 1   — what the July navmesh effectively did (nothing was tagged, so no avoidance);
//   cost 4 / cost 20 — steep included but expensive;
//   excluded — what mod-playerbots' 65018ac shipped.
// For each: share of the walked route on steep polys (the route is sampled every 0.5yd), the
// length increase over cost 1, and how many pairs find no complete path at all.

#include "gtest/gtest.h"
#include "NavHarness.h"

#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"
#include "MapDefines.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

namespace
{
    struct Policy { char const* name; bool includeSteep; float steepCost; };
    Policy const POLICIES[] = {
        { "cost 1 (July mesh)", true, 1.0f },
        { "cost 4",             true, 4.0f },
        { "cost 20",            true, 20.0f },
        { "excluded",           false, 1.0f },
    };

    struct MapCase { uint32_t id; char const* name; };
    MapCase const MAPS[] = {
        { 229, "Blackrock Spire" }, { 230, "Blackrock Depths" }, { 289, "Scholomance" },
        { 329, "Stratholme" },      { 429, "Dire Maul" },        { 349, "Maraudon" },
        { 209, "Zul'Farrak" },      { 109, "Sunken Temple" },    { 47,  "Razorfen Kraul" },
        { 409, "Molten Core" },     { 469, "Blackwing Lair" },   { 531, "Temple of Ahn'Qiraj" },
        { 603, "Ulduar" },          { 574, "Utgarde Keep" },     { 575, "Utgarde Pinnacle" },
    };

    constexpr int PAIRS = 300;
    constexpr int MAX_POLYS = 2048;
    constexpr int MAX_STRAIGHT = 512;

    // Detour's findRandomPoint takes a plain function pointer, so the seeded generator is reached
    // through a file-local pointer set before each map's draw.
    std::mt19937* gRng = nullptr;
    float FrandStatic() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(*gRng); }

    struct Stats { int found = 0, failed = 0; double len = 0, steep = 0; };

    // Walk the straight path in 0.5yd steps and add up the length that lies on steep polys.
    void Measure(dtNavMeshQuery& q, dtQueryFilter const& any, float const* pts, int n, double& len, double& steep)
    {
        float const ext[3] = { 1.0f, 3.0f, 1.0f };
        for (int i = 1; i < n; ++i)
        {
            float const* a = &pts[(i - 1) * 3];
            float const* b = &pts[i * 3];
            float const segLen = dtVdist(a, b);
            len += segLen;
            int const steps = std::max(1, int(segLen / 0.5f));
            for (int s = 0; s < steps; ++s)
            {
                float p[3];
                dtVlerp(p, a, b, (s + 0.5f) / steps);
                dtPolyRef ref = 0;
                float nearest[3];
                if (dtStatusFailed(q.findNearestPoly(p, ext, &any, &ref, nearest)) || !ref)
                    continue;
                dtMeshTile const* tile = nullptr;
                dtPoly const* poly = nullptr;
                if (dtStatusSucceed(q.getAttachedNavMesh()->getTileAndPolyByRef(ref, &tile, &poly)) &&
                    poly->getArea() == NAV_GROUND_STEEP)
                    steep += segLen / steps;
            }
        }
    }
}

TEST(SteepAvoidanceProbe, ShareOfBotRoutesOnSteepGround)
{
    char const* dir = std::getenv("DC_PROBE_MMAPS");
    if (!dir)
        GTEST_SKIP() << "set DC_PROBE_MMAPS to a dir holding mmaps/";

    std::printf("\n%-22s %-20s %6s %6s %9s %9s\n", "map", "policy", "found", "nopath", "steep%", "len+%");
    for (MapCase const& m : MAPS)
    {
        std::shared_ptr<dtNavMesh> mesh = DcNavHarness::LoadMap(dir, m.id);
        if (!mesh)
        {
            std::printf("%-22s (no mesh)\n", m.name);
            continue;
        }
        dtNavMeshQuery q;
        ASSERT_TRUE(dtStatusSucceed(q.init(mesh.get(), 65535)));

        // Pairs are drawn once, over every walkable poly, so all policies route the same pairs.
        dtQueryFilter any;
        any.setIncludeFlags(NAV_GROUND | NAV_GROUND_STEEP | NAV_WATER);
        any.setExcludeFlags(0);
        std::mt19937 rng(0xC0FFEEu ^ m.id);
        gRng = &rng;
        std::vector<std::pair<std::array<float, 3>, std::array<float, 3>>> pairs;
        for (int i = 0; i < PAIRS; ++i)
        {
            dtPolyRef ra = 0, rb = 0;
            std::array<float, 3> a{}, b{};
            if (dtStatusFailed(q.findRandomPoint(&any, FrandStatic, &ra, a.data())) ||
                dtStatusFailed(q.findRandomPoint(&any, FrandStatic, &rb, b.data())))
                continue;
            pairs.push_back({ a, b });
        }

        double baseLen = 0;
        for (Policy const& pol : POLICIES)
        {
            dtQueryFilter f;
            f.setIncludeFlags(NAV_GROUND | NAV_WATER | (pol.includeSteep ? NAV_GROUND_STEEP : 0));
            f.setExcludeFlags(NAV_MAGMA | NAV_SLIME | (pol.includeSteep ? 0 : NAV_GROUND_STEEP));
            f.setAreaCost(NAV_WATER, 20.0f);
            f.setAreaCost(NAV_GROUND_STEEP, pol.steepCost);

            Stats st;
            float const ext[3] = { 3.0f, 5.0f, 3.0f };
            for (auto const& [a, b] : pairs)
            {
                dtPolyRef sa = 0, sb = 0;
                float na[3], nb[3];
                q.findNearestPoly(a.data(), ext, &f, &sa, na);
                q.findNearestPoly(b.data(), ext, &f, &sb, nb);
                dtPolyRef path[MAX_POLYS];
                int npath = 0;
                if (!sa || !sb || dtStatusFailed(q.findPath(sa, sb, na, nb, &f, path, &npath, MAX_POLYS)) ||
                    npath == 0 || path[npath - 1] != sb)
                {
                    ++st.failed;
                    continue;
                }
                float straight[MAX_STRAIGHT * 3];
                int nstraight = 0;
                q.findStraightPath(na, nb, path, npath, straight, nullptr, nullptr, &nstraight, MAX_STRAIGHT);
                ++st.found;
                Measure(q, any, straight, nstraight, st.len, st.steep);
            }
            if (pol.steepCost == 1.0f && pol.includeSteep)
                baseLen = st.len / std::max(1, st.found);
            double const avg = st.len / std::max(1, st.found);
            std::printf("%-22s %-20s %6d %6d %8.1f%% %8.1f%%\n", m.name, pol.name, st.found, st.failed,
                        st.len > 0 ? 100.0 * st.steep / st.len : 0.0,
                        baseLen > 0 ? 100.0 * (avg - baseLen) / baseLen : 0.0);
        }
    }
}
