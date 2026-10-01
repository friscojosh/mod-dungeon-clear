/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_DCMARKTARGETS_H
#define _PLAYERBOT_DCMARKTARGETS_H

#include "Action.h"
#include "Trigger.h"

class PlayerbotAI;

// The DC tank marks the fight (fork: friscojosh; the choices are in DcMarkPlan.h):
//   * the `rti` icon (skull) on the lowest-health attacker whenever no living
//     attacker carries it — the party's focus, re-placed as each one dies;
//   * on a pull of CC_MIN_ATTACKERS or more, each CC caster's OWN icon on a mob it can
//     hold — square for a warlock's Banish, diamond for a priest's Shackle, star for a
//     druid's Hibernate, moon for a mage's Polymorph, triangle for a hunter's Freezing
//     Trap — as many as the pull is worth.
// Followers run the same step for themselves: each CC-class bot points its own `rti
// cc` at its class's icon, and a priest who can be spared from healing gets the `cc`
// combat strategy. In combat during an active run. The trigger fires only when a mark
// or a setup is missing, so the action claims a tick only on the tick it places one.
bool DcMarkTargetsStep(PlayerbotAI* botAI, bool apply);

class DungeonClearMarkTargetsTrigger : public Trigger
{
public:
    DungeonClearMarkTargetsTrigger(PlayerbotAI* botAI) : Trigger(botAI, "dungeon clear mark targets", 1) {}
    bool IsActive() override;
};

class DungeonClearMarkTargetsAction : public Action
{
public:
    DungeonClearMarkTargetsAction(PlayerbotAI* botAI) : Action(botAI, "dungeon clear mark targets") {}
    bool Execute(Event event) override;
};

#endif
