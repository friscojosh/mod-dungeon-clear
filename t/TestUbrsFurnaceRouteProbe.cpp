/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

// Upper Blackrock Spire (map 229) — THE FURNACE route probe (fork: friscojosh).
//
// Not a committed regression: reads the FULL mmaps dir from env DC_PROBE_MMAPS
// and GTEST_SKIPs when unset, same contract as the other route probes. It
// answers one authoring question: which of the Furnace's unavoidable pull groups
// (assist range at spawn + creature_formations, measured off acore_world) does
// the walking route from Blackrock Stadium to The Beast pass within reach of?
//
//   DC_PROBE_MMAPS=<dir holding mmaps/> ./dungeon_clear_tests --gtest_filter='DcUbrsFurnace*'

#include "gtest/gtest.h"
#include "NavHarness.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace
{
    constexpr uint32_t UBRS = 229;

    struct Group { float x, y, z; char const* name; };
    // Unavoidable pull groups (centre of their spawns). Patrollers excluded.
    std::vector<Group> const GROUPS = {
        {  94.1f, -481.5f, 116.9f, "east ledge 4 (Elite/Assassin/FireTongue/DragonGuard)" },
        {  71.5f, -474.6f, 115.8f, "lone Assassin 137979" },
        { 124.1f, -474.6f, 116.9f, "Iron Guard pair 137983/4" },
        { 137.4f, -458.2f, 121.9f, "lone Iron Guard 137986" },
        {  40.5f, -451.3f, 111.0f, "formation 137977 (DragonGuard/Elite/Assassin)" },
        {  15.3f, -462.6f, 111.0f, "WEST 9 (Captain formation + Elite/2 Assassin/FireTongue)" },
        {  32.9f, -493.6f, 111.0f, "south-west 3 (Assassin/Elite/DragonGuard)" },
        { 172.1f, -474.6f, 116.9f, "Iron Guard pair 137987/8" },
        {   9.8f, -489.8f, 111.0f, "Dragon Guard pair 137854/48" },
        { 121.2f, -564.7f, 107.4f, "The Beast + 2 Elites" },
    };

    std::shared_ptr<dtNavMesh> LoadOrSkip()
    {
        char const* dir = std::getenv("DC_PROBE_MMAPS");
        if (!dir || !*dir)
            return nullptr;
        return DcNavHarness::LoadMap(dir, UBRS);
    }
}

TEST(DcUbrsFurnaceRouteProbe, StadiumToTheBeast)
{
    std::shared_ptr<dtNavMesh> mesh = LoadOrSkip();
    if (!mesh)
        GTEST_SKIP() << "DC_PROBE_MMAPS unset or map 229 mmaps missing";

    struct P { float x, y, z; char const* name; };
    std::vector<P> const legs = {
        { 153.8f, -419.8f, 110.9f, "stadium centre" },
        { 124.2f, -563.8f, 107.4f, "The Beast" },
    };
    for (P const& p : legs)
    {
        G3D::Vector3 s;
        bool const on = DcNavHarness::NearestPoint(mesh.get(), p.x, p.y, p.z, 10.0f, 20.0f, s);
        std::printf("  snap %-16s on=%d -> (%.1f, %.1f, %.1f)\n", p.name, on, s.x, s.y, s.z);
    }

    DcNavHarness::RouteResult const r =
        DcNavHarness::Route(mesh.get(), UBRS, legs[0].x, legs[0].y, legs[0].z, legs[1].x, legs[1].y, legs[1].z);
    std::printf("route reachable=%d complete=%d len2d=%.1f pts=%zu %s\n", r.reachable, r.corridorComplete,
                r.routeLength2d, r.points.size(), r.failureReason.c_str());
    ASSERT_TRUE(r.reachable);
    for (auto const& p : r.points)
        std::printf("    (%.1f, %.1f, %.1f)\n", p.x, p.y, p.z);

    // Closest approach of the densified route to each group (3D; aggro is 3D).
    std::vector<G3D::Vector3> dense;
    for (std::size_t i = 0; i + 1 < r.points.size(); ++i)
    {
        G3D::Vector3 const d = r.points[i + 1] - r.points[i];
        int const steps = std::max(1, static_cast<int>(d.length() / 1.0f));
        for (int k = 0; k < steps; ++k)
            dense.push_back(r.points[i] + d * (static_cast<float>(k) / steps));
    }
    if (!r.points.empty())
        dense.push_back(r.points.back());
    std::printf("closest approach per group:\n");
    for (Group const& g : GROUPS)
    {
        float best = 1e9f;
        G3D::Vector3 at;
        for (auto const& p : dense)
        {
            float const d = std::sqrt((p.x - g.x) * (p.x - g.x) + (p.y - g.y) * (p.y - g.y) + (p.z - g.z) * (p.z - g.z));
            if (d < best) { best = d; at = p; }
        }
        std::printf("  %6.1fyd  at (%.1f, %.1f, %.1f)  %s\n", best, at.x, at.y, at.z, g.name);
    }
}
