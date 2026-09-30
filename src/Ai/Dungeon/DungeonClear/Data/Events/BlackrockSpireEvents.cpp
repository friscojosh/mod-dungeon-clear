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
// Drakkisath). This file is its definition unit: the wing split and, on the
// friscojosh fork, the UBRS encounter gates — the Rend/Gyth stadium (event 1,
// OBJ(1), hook 900), the Dragonspire Hall pack registration (event 10, OBJ(10),
// hook 901), the seven hall rune packs (events 2-8, OBJ(2..8)) and the Blackrock
// Altar (event 9, OBJ(9)). The TU stays linked
// because the wing, event, roster and hook aggregators each call into it.

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

    // UBRS clear order. The picker advances to the lowest key STRICTLY greater
    // than the one just finished, so objectives sharing a key would be skipped
    // past; every UBRS anchor gets its own slot on this scale (all above LBRS's
    // bits 0-8, which a UBRS run filters out anyway).
    constexpr int32 UBRS_ORDER_HALL_REGISTER = 10;
    constexpr int32 UBRS_ORDER_FIRST_RUNE = 11;  // runes take 11..17
    constexpr int32 UBRS_ORDER_ALTAR = 18;
    constexpr int32 UBRS_ORDER_EMBERSEER = 19;
    constexpr int32 UBRS_ORDER_SOLAKAR = 20;
    constexpr int32 UBRS_ORDER_STADIUM = 21;
    constexpr int32 UBRS_ORDER_BEAST = 22;
    constexpr int32 UBRS_ORDER_DRAKKISATH = 23;

    constexpr uint32 UBRS_NPC_EMBERSEER = 9816;
    constexpr uint32 UBRS_NPC_SOLAKAR = 10264;
    constexpr uint32 UBRS_NPC_BEAST = 10430;
    constexpr uint32 UBRS_NPC_DRAKKISATH = 10363;
    constexpr uint32 UBRS_BIT_EMBERSEER = 9;
    constexpr uint32 UBRS_STATE_EMBERSEER = 9;    // DATA_PYROGAURD_EMBERSEER slot

    // --- the DRAGONSPIRE HALL runes ------------------------------------------
    // The Emberseer doors (175244, 175705) open only once all seven hall runes
    // are out. Crossing area trigger 2046 (just outside the Dragonspine Door,
    // which every party entering UBRS crosses) makes the instance record, per
    // rune, each Blackhand Dreadweaver / Summoner / Veteran (9817-9819) within
    // 15y of it; every 3s it puts out a rune whose recorded mobs are all dead
    // (slot DATA_HALL_RUNE_n = 16+n-1 -> DONE), and at seven the room slot
    // (DATA_DRAGONSPIRE_ROOM, 15) goes DONE and the doors open. The ordinary
    // clear only fights what stands on its path, so the tank reached Emberseer's
    // shut door with packs still up.
    //
    // One OBJECTIVE per rune (the Dire Maul pylon pattern): boss-nav carries the
    // tank to the rune, ClearRadius kills its pack, then a short hold waits for
    // the instance's 3s check to put the rune out. doneBossStateIndex is the
    // rune's own slot, so a re-entered instance skips runes already out.
    //
    // Measured on acore_world: each rune's pack is 3-5 mobs, the farthest 13.7y
    // (2D) from its rune, all on the rune's floor. The hall has two floors, z~71
    // (four runes) and z~77 (three); a 4y zBand keeps a clear on one floor from
    // pulling the other, and 18y keeps each circle clear of the next rune's pack
    // (the nearest two runes are 32y apart). Order walks the lower floor, then the
    // upper, ending beside the first Emberseer door (216,-286).
    //
    // The per-rune mob lists live only in the instance script's memory and its
    // 3s check is only scheduled by that trigger. After a server restart or an
    // instance reload both are gone, and nothing puts a rune out until someone
    // crosses AT 2046 again — a party logging back in inside the hall never does.
    // So the first UBRS stop (OBJ(10), event 10, hook 901) walks the tank to the
    // trigger and fires it, which re-registers whatever is still alive and
    // restarts the check. Done by the room slot, so a finished hall skips it.
    constexpr uint32 UBRS_EV_HALL_REGISTER = 10;
    constexpr uint32 UBRS_HALL_REGISTER_OBJ_SEQ = 10;
    constexpr uint32 UBRS_STATE_DRAGONSPIRE_ROOM = 15;  // DATA_DRAGONSPIRE_ROOM slot
    constexpr uint32 UBRS_HALL_TRIGGER = 2046;          // at_dragonspire_hall, radius 10
    constexpr float UBRS_HALL_TRIGGER_X = 102.4f;
    constexpr float UBRS_HALL_TRIGGER_Y = -319.2f;
    constexpr float UBRS_HALL_TRIGGER_Z = 65.5f;
    constexpr uint32 UBRS_REGISTER_HALL_PACKS_HOOK = 901;  // fork-local, like 900

    constexpr float UBRS_RUNE_CLEAR_RADIUS = 18.0f;
    constexpr float UBRS_RUNE_CLEAR_ZBAND = 4.0f;
    constexpr float UBRS_RUNE_ARRIVE = 20.0f;  // >= the clear, so the event owns the tick
    constexpr uint32 UBRS_RUNE_CLEAR_TIMEOUT_MS = 180000;
    constexpr uint32 UBRS_RUNE_OUT_TIMEOUT_MS = 30000;  // ten of the instance's 3s checks

    struct UbrsRune
    {
        uint32 eventId;
        uint32 objSeq;
        uint32 stateSlot;  // DATA_HALL_RUNE_n
        float x, y, z;
        char const* name;
    };
    constexpr UbrsRune kUbrsRunes[] = {
        { 2, 2, 18, 124.8f, -298.0f, 70.9f, "Dragonspire Hall: rune pack (lower west)" },       // 175195, rune 3
        { 3, 3, 16, 125.4f, -340.5f, 70.9f, "Dragonspire Hall: rune pack (lower south-west)" }, // 175197, rune 1
        { 4, 4, 17, 155.3f, -353.0f, 70.8f, "Dragonspire Hall: rune pack (lower south)" },      // 175199, rune 2
        { 5, 5, 19, 155.3f, -286.1f, 70.9f, "Dragonspire Hall: rune pack (lower north)" },      // 175200, rune 4
        { 6, 6, 22, 192.7f, -258.4f, 76.9f, "Dragonspire Hall: rune pack (upper north)" },      // 175194, rune 7
        { 7, 7, 21, 228.8f, -301.5f, 76.9f, "Dragonspire Hall: rune pack (upper east)" },       // 175196, rune 6
        { 8, 8, 20, 215.2f, -334.7f, 76.8f, "Dragonspire Hall: rune pack (upper south)" },      // 175198, rune 5
    };

    // --- the BLACKROCK ALTAR -------------------------------------------------
    // Pyroguard Emberseer is encaged and unattackable until a player uses the
    // Blackrock Altar (175706, a one-participant SUMMONING_RITUAL). Its spell
    // fires event 4884, which starts him (slot 9 IN_PROGRESS, both doors shut);
    // the Blackhand Incarcerators and then Emberseer himself come to the party.
    // So: walk to the altar, use it, and hold the room until slot 9 is DONE —
    // his Reset drops it to NOT_STARTED on a wipe, which rewinds to the click.
    constexpr uint32 UBRS_EV_ALTAR = 9;
    constexpr uint32 UBRS_ALTAR_OBJ_SEQ = 9;
    constexpr uint32 UBRS_GO_BLACKROCK_ALTAR = 175706;
    constexpr float UBRS_ALTAR_X = 144.4f;
    constexpr float UBRS_ALTAR_Y = -280.9f;
    constexpr float UBRS_ALTAR_Z = 91.5f;
    constexpr float UBRS_ALTAR_HOLD = 20.0f;  // the whole room, altar to cage
    constexpr uint32 UBRS_EMBERSEER_TIMEOUT_MS = 900000;

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

namespace
{
    // RegisterHallPacks (hook 901): fire AT 2046 from the leader standing in it.
    // at_dragonspire_hall -> SetData(DATA_DRAGONSPIRE_ROOM) schedules the rune
    // pack registration 1s later unless the room is already DONE. Idempotent:
    // a repeat just re-registers the packs still alive. (It also re-asserts the
    // Dragonspine Door for a Seal of Ascension holder, which is harmless.)
    ObjectiveArriveResult RegisterHallPacks(Player* bot, AiObjectContext* /*context*/,
                                            DungeonBossInfo const& /*info*/)
    {
        if (!DcTargeting::GetInstanceScript(bot))
            return ObjectiveArriveResult::Running;  // not in the instance yet

        WorldPacket p(CMSG_AREATRIGGER);
        p << uint32(UBRS_HALL_TRIGGER);
        p.rpos(0);
        bot->GetSession()->HandleAreaTriggerOpcode(p);
        LOG_INFO("playerbots.dungeonclear", "[DC:{}] Dragonspire Hall packs registered via areatrigger {}",
                 bot->GetName(), UBRS_HALL_TRIGGER);
        return ObjectiveArriveResult::Done;
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

    out.push_back(
        EventBuilder(UBRS_MAP, UBRS_EV_HALL_REGISTER, "Dragonspire Hall: register the rune packs")
            .Anchored(/*orderIndex, doc-only*/ UBRS_EV_HALL_REGISTER)
            // Well inside the 10y trigger sphere; the core range-checks the fire.
            .MoveTo(UBRS_HALL_TRIGGER_X, UBRS_HALL_TRIGGER_Y, UBRS_HALL_TRIGGER_Z, /*radius*/ 4.0f)
            .Custom(UBRS_REGISTER_HALL_PACKS_HOOK)
            .Wait(/*the instance schedules registration 1s out*/ 2000)
            .Build());

    for (UbrsRune const& rune : kUbrsRunes)
        out.push_back(
            EventBuilder(UBRS_MAP, rune.eventId, rune.name)
                .Anchored(/*orderIndex, doc-only*/ rune.eventId)
                .Persistent()
                // 1. Kill the rune's pack.
                .ClearRadius(rune.x, rune.y, rune.z, UBRS_RUNE_CLEAR_RADIUS, UBRS_RUNE_CLEAR_ZBAND)
                    .Timeout(UBRS_RUNE_CLEAR_TIMEOUT_MS)
                // 2. Wait for the instance to put the rune out.
                .MoveToHoldUntilBossState(rune.x, rune.y, rune.z, UBRS_RUNE_CLEAR_RADIUS,
                                          rune.stateSlot, DcBossStateBit(DONE))
                    .Timeout(UBRS_RUNE_OUT_TIMEOUT_MS)
                .Build());

    out.push_back(
        EventBuilder(UBRS_MAP, UBRS_EV_ALTAR, "Pyroguard Emberseer: Blackrock Altar")
            .Anchored(/*encounterIndex*/ UBRS_BIT_EMBERSEER)
            .Persistent()  // the Incarcerators and Emberseer are several combat gaps
            // 1. Use the altar; UseGO early-returns Done once the ritual is spent.
            .UseGO(UBRS_GO_BLACKROCK_ALTAR, /*searchRadius*/ 15.0f,
                   UBRS_ALTAR_X, UBRS_ALTAR_Y, UBRS_ALTAR_Z)
                .Timeout(60000)
            // 2. Wait for the ritual to land (event 4884 -> slot 9 IN_PROGRESS).
            //    No restart here: the slot is still NOT_STARTED while it casts.
            .MoveToHoldUntilBossState(UBRS_ALTAR_X, UBRS_ALTAR_Y, UBRS_ALTAR_Z, UBRS_ALTAR_HOLD,
                                      UBRS_STATE_EMBERSEER,
                                      DcBossStateBit(IN_PROGRESS) | DcBossStateBit(DONE))
                .Timeout(60000)
            // 3. Hold the room until Emberseer is DONE; a reset rewinds to the click.
            .MoveToHoldUntilBossState(UBRS_ALTAR_X, UBRS_ALTAR_Y, UBRS_ALTAR_Z, UBRS_ALTAR_HOLD,
                                      UBRS_STATE_EMBERSEER, DcBossStateBit(DONE))
                .RestartOnBossState(DcBossStateBit(FAIL) | DcBossStateBit(NOT_STARTED))
                .Timeout(UBRS_EMBERSEER_TIMEOUT_MS)
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
                                            UBRS_EV_STADIUM, UBRS_ORDER_STADIUM);
    stadium.doneBossStateIndex = UBRS_STATE_REND;
    p.add.push_back(stadium);

    DungeonBossInfo reg = MakeObjective(OBJ(UBRS_HALL_REGISTER_OBJ_SEQ), UBRS_BIT_EMBERSEER, UBRS_MAP,
                                        "Dragonspire Hall: register the rune packs",
                                        UBRS_HALL_TRIGGER_X, UBRS_HALL_TRIGGER_Y, UBRS_HALL_TRIGGER_Z,
                                        /*arriveRadius*/ 6.0f, /*gateEntry*/ 0, /*hook*/ 0,
                                        UBRS_EV_HALL_REGISTER, UBRS_ORDER_HALL_REGISTER);
    reg.doneBossStateIndex = UBRS_STATE_DRAGONSPIRE_ROOM;
    p.add.push_back(reg);

    // The rune packs, then the altar, ahead of Emberseer. Their encounterIndex is
    // Emberseer's bit as an ordering hint only (objectives complete by their
    // events and doneBossStateIndex, never by the kill mask); orderOverride
    // places them.
    int32 order = UBRS_ORDER_FIRST_RUNE;
    for (UbrsRune const& rune : kUbrsRunes)
    {
        DungeonBossInfo o = MakeObjective(OBJ(rune.objSeq), UBRS_BIT_EMBERSEER, UBRS_MAP, rune.name,
                                          rune.x, rune.y, rune.z, UBRS_RUNE_ARRIVE,
                                          /*gateEntry*/ 0, /*hook*/ 0, rune.eventId, order++);
        o.doneBossStateIndex = static_cast<int32>(rune.stateSlot);
        p.add.push_back(o);
    }

    DungeonBossInfo altar = MakeObjective(OBJ(UBRS_ALTAR_OBJ_SEQ), UBRS_BIT_EMBERSEER, UBRS_MAP,
                                          "Pyroguard Emberseer: Blackrock Altar",
                                          UBRS_ALTAR_X, UBRS_ALTAR_Y, UBRS_ALTAR_Z,
                                          /*arriveRadius*/ 12.0f, /*gateEntry*/ 0, /*hook*/ 0,
                                          UBRS_EV_ALTAR, UBRS_ORDER_ALTAR);
    altar.doneBossStateIndex = UBRS_STATE_EMBERSEER;
    p.add.push_back(altar);

    // The real bosses keep their DBC kill-bits; only their place in the order moves.
    p.reorder = {
        { UBRS_NPC_EMBERSEER, UBRS_ORDER_EMBERSEER },
        { UBRS_NPC_SOLAKAR, UBRS_ORDER_SOLAKAR },
        { UBRS_NPC_BEAST, UBRS_ORDER_BEAST },
        { UBRS_NPC_DRAKKISATH, UBRS_ORDER_DRAKKISATH },
    };
    t.push_back(std::move(p));
}

void RegisterBlackrockSpireHooks(ObjectiveHookRegistry::HookTable& out)
{
    ObjectiveHookRegistry::AddHook(out, UBRS_ENSURE_STADIUM_STARTED_HOOK, &EnsureStadiumStarted);
    ObjectiveHookRegistry::AddHook(out, UBRS_REGISTER_HALL_PACKS_HOOK, &RegisterHallPacks);
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
            DcRoster::OBJ(2), DcRoster::OBJ(3), DcRoster::OBJ(4), DcRoster::OBJ(5),  // Dragonspire
            DcRoster::OBJ(6), DcRoster::OBJ(7), DcRoster::OBJ(8),                    // Hall runes (events 2-8)
            DcRoster::OBJ(UBRS_ALTAR_OBJ_SEQ),     // Blackrock Altar objective (event 9)
            DcRoster::OBJ(UBRS_HALL_REGISTER_OBJ_SEQ),  // Dragonspire Hall pack registration (event 10)
            10430,  // The Beast
            10363,  // General Drakkisath
        }, "ubrs", /*lfgDungeonId*/ 44, /*terminalBossEntry*/ 10363, /*encounterMask*/ 0x3E00u},
    }, WingSelect::Explicit, /*defaultWing*/ "lbrs"};
}
