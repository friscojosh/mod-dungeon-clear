/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonEventTables.h"
#include "Ai/Dungeon/DungeonClear/Data/Events/DungeonRosterBuilders.h"
#include "Ai/Dungeon/DungeonClear/Data/DungeonWingRegistry.h"
#include "Ai/Dungeon/DungeonClear/Overrides/ObjectiveHookRegistry.h"
#include "Ai/Dungeon/DungeonClear/Util/DcTargeting.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <unordered_map>

// Blackrock Spire (map 229) is two dungeons on one map: Lower (LFG 32, ends at
// Overlord Wyrmthalak) and Upper (LFG 44, Pyroguard Emberseer -> General
// Drakkisath). This file is its definition unit: the wing split, and the
// Rend/Gyth stadium (event 1, OBJ(1), hook 900 — friscojosh fork). The other UBRS
// encounter gates (Dragonspine Door, hall rune packs, the Blackrock Altar) are
// still to come. The TU stays linked because the wing, event, roster and hook
// aggregators each call into it explicitly.

// --- the REND/GYTH STADIUM, ANCHORED + PERSISTENT (fork: friscojosh) ----------
// Warchief Rend Blackhand (10429) has a static spawn, but it is on the viewing
// balcony above Blackrock Stadium beside Lord Victor Nefarius, and the only mesh
// route to it runs through the stadium's exit portcullis (175186,
// GO_GYTH_EXIT_DOOR) — a PASSAGE door that opens only once Rend's encounter is
// DONE. A boss anchor on him therefore dead-ends at that door: live, the tank
// flagged it corridor-blocking and auto-paused ("door-blocked: can't open
// ... 175186").
//
// The encounter is started from the arena floor instead. Area trigger 2026
// (at_blackrock_stadium, centre (153.8,-419.8,110.5), radius 20) calls
// boss_rend_blackhand::SetData; one second later EVENT_START_1 sets his slot
// IN_PROGRESS and shuts the entrance portcullis (164726) behind the party. Five
// waves of summons come through the east portcullis (175185), each
// DoZoneInCombat(100y) — COME-TO-YOU, like BRD's arena, so the party holds the
// centre and fights reactively. Then Rend rides Gyth (10339) into the arena; at
// 25% Gyth summons a second, attackable Rend. That Rend's death sets the slot
// DONE, which opens the exit portcullis toward The Beast.
//
// So the balcony Rend's roster row is removed and OBJ(1) stands in for the
// encounter, at the arena centre, with Rend's DBC bit (11) and his boss-state
// slot (DATA_WARCHIEF_REND_BLACKHAND = 10; the slot, not the bit — the two
// diverge from 10 on, see the wing notes below) as doneBossStateIndex, so a
// re-entered instance does not replay a finished stadium.
//
//   1. walk onto the trigger spot. Arrival crosses AT 2026 (a human, or a
//      self-bot relayed from the master);
//   2. make sure it started: the hook forges AT 2026 from the leader until the
//      slot reads IN_PROGRESS (an all-bot party never sends CMSG_AREATRIGGER);
//   3. hold the centre until the slot reads DONE. A wipe or a reset reads FAIL
//      or NOT_STARTED and rewinds to step 1, which restarts the stadium.
//
// Step 3's rewind relies on the core recovering the encounter after a wipe (the
// balcony Rend requeued, the entrance reopened). Stock AzerothCore since the
// dynamic spawn port does not; the fix is ResetRendStadiumEvent in
// boss_rend_blackhand.cpp (friscojosh/azerothcore-wotlk). Without it the rewind
// holds the arena until the timeout.
//
// PERSISTENT because five waves and the Gyth fight are many combat gaps.
namespace
{
    constexpr uint32 UBRS_MAP = 229;
    constexpr uint32 UBRS_EV_STADIUM = 1;
    constexpr uint32 UBRS_STADIUM_OBJ_SEQ = 1;
    constexpr uint32 UBRS_BIT_REND = 11;          // DungeonEncounter bit (see wings)
    constexpr uint32 UBRS_STATE_REND = 10;        // DATA_WARCHIEF_REND_BLACKHAND slot
    constexpr uint32 UBRS_NPC_REND = 10429;
    constexpr uint32 UBRS_STADIUM_TRIGGER = 2026;

    // Fork-local hook id, well clear of upstream's 1-43 so a future upstream hook
    // cannot collide with it (AddHook would LOG_ERROR and the registry gtest fail).
    constexpr uint32 UBRS_ENSURE_STADIUM_STARTED_HOOK = 900;

    // Arena centre = AT 2026's x,y; z on the arena floor (Rend's own
    // NearTeleportTo into the arena lands at z 110.888).
    constexpr float UBRS_STADIUM_X = 153.8f;
    constexpr float UBRS_STADIUM_Y = -419.8f;
    constexpr float UBRS_STADIUM_Z = 110.9f;

    // Walk-in tolerance: well inside the 20y trigger sphere.
    constexpr float UBRS_STADIUM_ARRIVE = 6.0f;
    // Hold leash: the waves and Gyth come to the party anywhere on the floor, so
    // this only stops a chase from dragging the tank out of the arena.
    constexpr float UBRS_STADIUM_HOLD = 15.0f;

    // Five waves, the Rend walk-down and the Gyth + Rend fight.
    constexpr uint32 UBRS_STADIUM_TIMEOUT_MS = 1200000;

    // EnsureStadiumStarted (hook 900): fires the real area trigger from the
    // leader until Rend's slot is at least IN_PROGRESS. The core range-checks the
    // bot against the trigger sphere and runs at_blackrock_stadium, whose own
    // guard (_gythEvent) makes a repeat a no-op; the slot flips one second after
    // the first fire takes (EVENT_START_1), so this returns Running meanwhile.
    ObjectiveArriveResult EnsureStadiumStarted(Player* bot, AiObjectContext* /*context*/,
                                               DungeonBossInfo const& /*info*/)
    {
        InstanceScript* inst = DcTargeting::GetInstanceScript(bot);
        if (!inst)
            return ObjectiveArriveResult::Running;  // not in the instance yet

        uint32 const state = inst->GetBossState(UBRS_STATE_REND);
        if (state == IN_PROGRESS || state == DONE)
        {
            LOG_INFO("playerbots.dungeonclear", "[DC:{}] Blackrock Stadium running (Rend slot {})",
                     bot->GetName(), state);
            return ObjectiveArriveResult::Done;
        }

        WorldPacket p(CMSG_AREATRIGGER);
        p << uint32(UBRS_STADIUM_TRIGGER);
        p.rpos(0);
        bot->GetSession()->HandleAreaTriggerOpcode(p);
        return ObjectiveArriveResult::Running;
    }
}

void RegisterBlackrockSpireEvents(std::vector<DungeonEvent>& out)
{
    out.push_back(
        EventBuilder(UBRS_MAP, UBRS_EV_STADIUM, "Rend: Blackrock Stadium")
            .Anchored(/*encounterIndex*/ UBRS_BIT_REND)
            .Persistent()
            // 1. Onto the trigger spot.
            .MoveTo(UBRS_STADIUM_X, UBRS_STADIUM_Y, UBRS_STADIUM_Z, UBRS_STADIUM_ARRIVE)
            // 2. Started, or start it.
            .Custom(UBRS_ENSURE_STADIUM_STARTED_HOOK)
                .Timeout(60000)
            // 3. Hold the floor until Rend's slot is DONE; a wipe/reset rewinds.
            .MoveToHoldUntilBossState(UBRS_STADIUM_X, UBRS_STADIUM_Y, UBRS_STADIUM_Z,
                                      UBRS_STADIUM_HOLD, UBRS_STATE_REND, DcBossStateBit(DONE))
                .RestartOnBossState(DcBossStateBit(FAIL) | DcBossStateBit(NOT_STARTED))
                .Timeout(UBRS_STADIUM_TIMEOUT_MS)
            .Build());
}

void RegisterBlackrockSpireRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;

    // The balcony Rend is unreachable until his encounter is DONE (see the
    // stadium notes above); OBJ(1) at the arena centre owns the encounter.
    BossRosterPatch p;
    p.mapId = UBRS_MAP;
    p.remove = { UBRS_NPC_REND };
    DungeonBossInfo stadium = MakeObjective(OBJ(UBRS_STADIUM_OBJ_SEQ), UBRS_BIT_REND, UBRS_MAP,
                                            "Rend: Blackrock Stadium",
                                            UBRS_STADIUM_X, UBRS_STADIUM_Y, UBRS_STADIUM_Z,
                                            /*arriveRadius*/ 12.0f, /*gateEntry*/ 0, /*hook*/ 0,
                                            UBRS_EV_STADIUM);
    stadium.doneBossStateIndex = UBRS_STATE_REND;
    p.add.push_back(stadium);
    t.push_back(std::move(p));
}

void RegisterBlackrockSpireHooks(ObjectiveHookRegistry::HookTable& out)
{
    ObjectiveHookRegistry::AddHook(out, UBRS_ENSURE_STADIUM_STARTED_HOOK, &EnsureStadiumStarted);
}

// --- wing layout ---------------------------------------------------------
void RegisterBlackrockSpireWings(std::unordered_map<uint32, DungeonWingLayout>& store)
{
    // --- Blackrock Spire (map 229) -------------------------------
    // Both wings are entered through the SAME portal and are stacked: from the
    // portal (78.5, -225.0, 49.8) the nearest bosses are UBRS's (Emberseer 87yd,
    // Drakkisath 96yd; Omokk, LBRS's first, is 128yd). Nearest-boss detection
    // would put an LBRS party on the UBRS list at the door, and the last brs test
    // runs show the other half of the problem: clear LBRS, then walk back up to
    // the Dragonspine Door for Emberseer. So the wing is chosen per RUN
    // (WingSelect::Explicit, see DcRunWing), defaulting to LBRS.
    //
    // Encounter bits from DungeonEncounter.dbc, verified against acore_world
    // (blackrock_spire.h's DATA_* indices diverge from bit 10 on — use these):
    //   LBRS bits 0-8: Omokk, Vosh'gajin, Voone, Smolderweb, Urok (GO summon),
    //                  Zigris, Gizrul (spawns on Halycon's death), Halycon,
    //                  Wyrmthalak (lastEncounterDungeon = 32)
    //   UBRS bits 9-13: Emberseer, Solakar (Father Flame summon), Rend, The
    //                  Beast, Drakkisath (lastEncounterDungeon = 44)
    // Urok, Gizrul and Solakar have no static spawn, so BossSpawnIndex never
    // lists them; they are here so credit and counting still resolve.
    store[229] = {true, {
        {"Blackrock Spire (Lower)", {
            9196,   // Highlord Omokk
            9236,   // Shadow Hunter Vosh'gajin
            9237,   // War Master Voone
            10596,  // Mother Smolderweb
            10584,  // Urok Doomhowl
            9736,   // Quartermaster Zigris
            10268,  // Gizrul the Slavener
            10220,  // Halycon
            9568,   // Overlord Wyrmthalak
        }, "lbrs", /*lfgDungeonId*/ 32, /*terminalBossEntry*/ 9568, /*encounterMask*/ 0x01FFu},
        {"Blackrock Spire (Upper)", {
            9816,   // Pyroguard Emberseer
            10264,  // Solakar Flamewreath
            10429,  // Warchief Rend Blackhand
            DcRoster::OBJ(UBRS_STADIUM_OBJ_SEQ),  // Rend: Blackrock Stadium objective (event 1)
            10430,  // The Beast
            10363,  // General Drakkisath
        }, "ubrs", /*lfgDungeonId*/ 44, /*terminalBossEntry*/ 10363, /*encounterMask*/ 0x3E00u},
    }, WingSelect::Explicit, /*defaultWing*/ "lbrs"};
}
