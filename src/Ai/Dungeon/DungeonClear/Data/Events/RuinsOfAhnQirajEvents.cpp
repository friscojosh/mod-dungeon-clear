/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"

#include "Creature.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "Playerbots.h"

// --- Ruins of Ahn'Qiraj (map 509) — start General Rajaxx's event ------------
//
// Verified against the server, 2026-10-01:
//
//   * Kurinnaxx's JustDied (boss_kurinnaxx.cpp) has his killer's player summon
//     Lieutenant General Andorov (15471) at (-8538.18, 1486.10); the instance
//     re-summons him from OnPlayerEnter while Kurinnaxx is DONE, Rajaxx is not
//     and the wave counter is still 0 (instance_ruins_of_ahnqiraj.cpp).
//   * npc_general_andorov (boss_rajaxx.cpp) is an escort that STARTS WALKING ON
//     ITS OWN (InitializeAI -> Start). At waypoint 10 (script_waypoint 15471/10,
//     -8870.72, 1648.40, 21.51) it pauses and sets UNIT_NPC_FLAG_GOSSIP.
//   * sGossipSelect resumes the escort for any non-zero gossipListId and strips
//     the gossip flag. The menu is 7048 (creature_template.gossip_menu_id);
//     OptionID 1 "Let's find out." is the one conditions row 15/7048/1 shows
//     while Rajaxx is not DONE, and OptionID 0 (the vendor) is only shown once
//     he is. So before Rajaxx the menu holds exactly one item: ordinal 0, which
//     SelectGossip resolves to OptionID 1 — the non-zero action the script needs.
//   * At waypoint 14 (-8939.95, 1551.13) he and his four Kaldorei Elites become
//     attackable and Captain Qeez's wave is sent at him 5s later. Nothing else is
//     clicked: ONE gossip starts the whole event.
//
// "Talk to him twice" is the trap this event avoids, not a second step: the
// template already carries npcflag 129 (gossip | vendor), so Andorov offers the
// same option from the moment he spawns, while he is still walking in. Picking
// it then only strips the flag (SetEscortPaused(false) on a running escort does
// nothing); waypoint 10 re-sets the flag and waits for a SECOND talk. The
// condition below therefore only fires once he is standing at waypoint 10, so
// the one gossip DC sends is the one that counts.
//
// CONDITIONAL, not Anchored: Andorov is not an encounter and this map has no
// roster objective to hang an anchored event on. Not Repeatable: the click
// strips the gossip flag, which makes the condition false, and an unfinished
// step list is not latched, so a missed click simply re-fires. Optional: if the
// gossip cannot be driven, the waves can still be started the way a raid without
// Andorov would — the IP SmartAI on every wave leader reports its own aggro
// (Set Instance Data 1 -> DATA_RAJAXX_WAVE_ENGAGED), which arms the 2-minute
// next-wave timer — so a stall here must not block the clear.
namespace
{
    constexpr uint32 MAP_ID = 509;
    constexpr uint32 EVENT_ANDOROV_START = 1;

    constexpr uint32 NPC_KURINNAXX = 15348;
    constexpr uint32 NPC_ANDOROV = 15471;

    // ruins_of_ahnqiraj.h DataTypes.
    constexpr uint32 DATA_KURINNAXX = 0;
    constexpr uint32 DATA_RAJAXX = 1;
    constexpr uint32 DATA_ANDOROV = 15;

    // script_waypoint (15471, 10): where the escort pauses for the gossip.
    constexpr float ANDOROV_PAUSE_X = -8870.72f;
    constexpr float ANDOROV_PAUSE_Y = 1648.40f;
    constexpr float ANDOROV_PAUSE_TOLERANCE = 3.0f;

    // The leader has to be in the neighbourhood (Kurinnaxx died 53y from the
    // pause point); the Gossip step walks the rest (DC_EVENT_GOSSIP_APPROACH).
    constexpr float ANDOROV_DUE_RANGE = 90.0f;
    constexpr float ANDOROV_SCAN = 40.0f;

    bool AndorovAwaitsGossip(Player* bot, AiObjectContext* /*context*/)
    {
        if (!bot || bot->GetMapId() != MAP_ID)
            return false;
        if (bot->GetExactDist2d(ANDOROV_PAUSE_X, ANDOROV_PAUSE_Y) > ANDOROV_DUE_RANGE)
            return false;

        InstanceScript* inst = bot->GetInstanceScript();
        if (!inst || inst->GetBossState(DATA_KURINNAXX) != DONE || inst->GetBossState(DATA_RAJAXX) == DONE)
            return false;

        Creature* andorov = bot->GetMap()->GetCreature(inst->GetGuidData(DATA_ANDOROV));
        if (!andorov || !andorov->IsAlive() || !andorov->HasNpcFlag(UNIT_NPC_FLAG_GOSSIP))
            return false;

        return andorov->GetExactDist2d(ANDOROV_PAUSE_X, ANDOROV_PAUSE_Y) <= ANDOROV_PAUSE_TOLERANCE;
    }
}

void RegisterRuinsOfAhnQirajEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(EventBuilder(MAP_ID, EVENT_ANDOROV_START, "Andorov — start General Rajaxx's waves")
                      .Conditional(&AndorovAwaitsGossip)
                      .PanelAfterBoss(NPC_KURINNAXX)
                      .Optional()
                      .Gossip(NPC_ANDOROV, /*option*/ 0, /*searchRadius*/ ANDOROV_SCAN)
                      .WaitTargetStill()
                      .Build());
}
