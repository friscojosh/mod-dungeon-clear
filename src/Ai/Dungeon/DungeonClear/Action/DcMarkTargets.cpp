/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "DcMarkTargets.h"

#include "Ai/Dungeon/DungeonClear/DcValueKeys.h"
#include "Ai/Dungeon/DungeonClear/Util/DcMarkPlan.h"
#include "Creature.h"
#include "Group.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RtiTargetValue.h"
#include "SpellMgr.h"

namespace
{
    constexpr uint32 SPELL_POLYMORPH_R1 = 118;  // immunity is by mechanic, so rank 1 answers for all

    bool HasIcon(Group* group, ObjectGuid guid)
    {
        for (uint8 i = 0; i < TARGETICONCOUNT; ++i)
            if (group->GetTargetIcon(i) == guid)
                return true;
        return false;
    }

    // Is the icon at `index` on a unit that is alive and fighting us?
    bool IconOnLiveAttacker(Group* group, int32 index, std::vector<Unit*> const& attackers)
    {
        if (index < 0)
            return false;
        ObjectGuid const g = group->GetTargetIcon(index);
        if (g.IsEmpty())
            return false;
        for (Unit* u : attackers)
            if (u->GetGUID() == g)
                return true;
        return false;
    }

    bool PartyHasMage(Player* bot, Group* group)
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (m && m != bot && m->IsAlive() && m->GetMapId() == bot->GetMapId() &&
                m->getClass() == CLASS_MAGE)
                return true;
        }
        return false;
    }
}

bool DcMarkTargetsStep(PlayerbotAI* botAI, bool apply)
{
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    if (!bot || !bot->IsAlive() || !bot->IsInCombat() || bot->InBattleground())
        return false;
    Group* group = bot->GetGroup();
    if (!group)
        return false;
    AiObjectContext* context = botAI->GetAiObjectContext();
    // The run's leader only: PartyTank resolves to the leader (itself, for the leader)
    // while an unpaused run is active.
    if (context->GetValue<Player*>(DcKey::PartyTank)->Get() != bot)
        return false;

    std::vector<Unit*> attackers;
    for (ObjectGuid const guid : context->GetValue<GuidVector>("attackers")->Get())
    {
        Unit* u = botAI->GetUnit(guid);
        if (u && u->IsAlive() && !u->IsPlayer() && u->GetMapId() == bot->GetMapId())
            attackers.push_back(u);
    }
    if (attackers.empty())
        return false;

    int32 const skullIdx = RtiTargetValue::GetRtiIndex(context->GetValue<std::string>("rti")->Get());
    int32 const ccIdx = RtiTargetValue::GetRtiIndex(context->GetValue<std::string>("rti cc")->Get());
    bool changed = false;

    // Focus: the lowest-health attacker not already carrying an icon (stock mark
    // rti's rule), re-placed as each focus dies.
    if (skullIdx >= 0 && !IconOnLiveAttacker(group, skullIdx, attackers))
    {
        Unit* pick = nullptr;
        for (Unit* u : attackers)
            if (!HasIcon(group, u->GetGUID()) && (!pick || u->GetHealth() < pick->GetHealth()))
                pick = u;
        if (pick)
        {
            if (!apply)
                return true;
            group->SetTargetIcon(skullIdx, bot->GetGUID(), pick->GetGUID());
            changed = true;
        }
    }

    // Crowd control: a big pull, a mage to cast it, and no live moon yet.
    if (ccIdx >= 0 && ccIdx != skullIdx &&
        DcMarkPlan::WantCc(static_cast<uint32>(attackers.size()), PartyHasMage(bot, group)) &&
        !IconOnLiveAttacker(group, ccIdx, attackers))
    {
        SpellInfo const* poly = sSpellMgr->GetSpellInfo(SPELL_POLYMORPH_R1);
        Unit* const victim = bot->GetVictim();
        std::vector<DcMarkPlan::CcCandidate> cands;
        cands.reserve(attackers.size());
        for (Unit* u : attackers)
        {
            Creature* c = u->ToCreature();
            DcMarkPlan::CcCandidate x;
            x.boss = c && (c->isWorldBoss() || c->IsDungeonBoss() ||
                           c->GetCreatureTemplate()->rank == CREATURE_ELITE_WORLDBOSS);
            x.marked = HasIcon(group, u->GetGUID());
            x.tankTarget = (u == victim);
            uint32 const type = u->GetCreatureType();
            x.sheepableType = (type == CREATURE_TYPE_HUMANOID || type == CREATURE_TYPE_BEAST);
            x.immune = !poly || u->IsImmunedToSpell(poly);
            x.healthPct = u->GetHealthPct();
            cands.push_back(x);
        }
        int const pick = DcMarkPlan::PickCc(cands);
        if (pick >= 0)
        {
            if (!apply)
                return true;
            group->SetTargetIcon(ccIdx, bot->GetGUID(), attackers[pick]->GetGUID());
            changed = true;
        }
    }

    return changed;
}

bool DungeonClearMarkTargetsTrigger::IsActive() { return DcMarkTargetsStep(botAI, false); }

bool DungeonClearMarkTargetsAction::Execute(Event /*event*/) { return DcMarkTargetsStep(botAI, true); }
