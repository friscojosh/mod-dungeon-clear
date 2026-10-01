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
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace
{
    // One crowd-control kind per class: the icon that class's bots CC (their own `rti
    // cc`, so two classes never queue different CCs on one mob), the rank-1 spell whose
    // immunity answers for every rank (immunity is by mechanic), and the creature types
    // it can hold. Rogues (Sap is out-of-combat only) and paladins (Turn Undead sends
    // the mob running into the next pack) are left out on purpose.
    struct CcKind
    {
        uint8       cls;
        char const* icon;
        uint32      spell;
        uint32      typeMask;   // bit (1 << CREATURE_TYPE_x); 0 = any type
    };
    constexpr uint32 T(uint32 type) { return 1u << type; }
    constexpr CcKind kCcKinds[] = {
        { CLASS_WARLOCK, "square",   710,  T(CREATURE_TYPE_DEMON) | T(CREATURE_TYPE_ELEMENTAL) },   // Banish
        { CLASS_PRIEST,  "diamond",  9484, T(CREATURE_TYPE_UNDEAD) },                               // Shackle Undead
        { CLASS_DRUID,   "star",     2637, T(CREATURE_TYPE_BEAST) | T(CREATURE_TYPE_DRAGONKIN) },   // Hibernate
        { CLASS_MAGE,    "moon",     118,  T(CREATURE_TYPE_HUMANOID) | T(CREATURE_TYPE_BEAST) |
                                           T(CREATURE_TYPE_CRITTER) },                              // Polymorph
        { CLASS_HUNTER,  "triangle", 3355, 0 },                                                     // Freezing Trap
    };

    CcKind const* KindFor(uint8 cls)
    {
        for (CcKind const& k : kCcKinds)
            if (k.cls == cls)
                return &k;
        return nullptr;
    }

    // Already held by someone's crowd control: a second CC on it is wasted or breaks it.
    constexpr uint32 kCcMechanics =
        (1u << MECHANIC_POLYMORPH) | (1u << MECHANIC_BANISH) | (1u << MECHANIC_SHACKLE) |
        (1u << MECHANIC_SLEEP) | (1u << MECHANIC_FREEZE) | (1u << MECHANIC_FEAR) |
        (1u << MECHANIC_SAPPED) | (1u << MECHANIC_HORROR) | (1u << MECHANIC_TURN);

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

    // A priest spends heals on Shackle only when someone else can heal meanwhile.
    bool PriestMayCc(Player* priest, Group* group)
    {
        if (!PlayerbotAI::IsHeal(priest))
            return true;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* m = ref->GetSource();
            if (m && m != priest && m->IsAlive() && m->GetMapId() == priest->GetMapId() &&
                PlayerbotAI::IsHeal(m))
                return true;
        }
        return false;
    }

    // Follower half, run by each bot for itself (never written cross-bot): point its own
    // `rti cc` at its class's icon, and give an eligible priest the `cc` combat strategy
    // (off by default for every priest spec). True when something needed changing.
    bool EnsureOwnCcSetup(PlayerbotAI* botAI, Player* bot, Group* group, bool apply)
    {
        CcKind const* kind = KindFor(bot->getClass());
        if (!kind)
            return false;
        bool changed = false;
        AiObjectContext* context = botAI->GetAiObjectContext();
        Value<std::string>* rtiCc = context->GetValue<std::string>("rti cc");
        if (rtiCc->Get() != kind->icon)
        {
            if (!apply)
                return true;
            rtiCc->Set(kind->icon);
            changed = true;
        }
        if (kind->cls == CLASS_PRIEST && PriestMayCc(bot, group) && !botAI->HasStrategy("cc", BOT_STATE_COMBAT))
        {
            if (!apply)
                return true;
            botAI->ChangeStrategy("+cc", BOT_STATE_COMBAT);
            changed = true;
        }
        return changed;
    }

    struct Caster
    {
        CcKind const* kind;
        SpellInfo const* spell;
        int32 iconIdx;
    };

    // Party members who will act on their CC icon: a bot of a CC class with the `cc`
    // combat strategy, alive, here. One caster per icon — a second mage would only
    // share the first one's moon.
    std::vector<Caster> CcCasters(Player* bot, Group* group)
    {
        std::vector<Caster> out;
        for (CcKind const& k : kCcKinds)
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* m = ref->GetSource();
                if (!m || m == bot || !m->IsAlive() || m->GetMapId() != bot->GetMapId() || m->getClass() != k.cls)
                    continue;
                PlayerbotAI* ai = GET_PLAYERBOT_AI(m);
                if (!ai || !ai->HasStrategy("cc", BOT_STATE_COMBAT))
                    continue;
                if (k.cls == CLASS_PRIEST && !PriestMayCc(m, group))
                    continue;
                out.push_back({ &k, sSpellMgr->GetSpellInfo(k.spell), RtiTargetValue::GetRtiIndex(k.icon) });
                break;
            }
        }
        return out;
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
    Player* const leader = context->GetValue<Player*>(DcKey::PartyTank)->Get();
    if (!leader)
        return false;   // no active run
    // Every bot of a CC class sets itself up; only the run's leader marks.
    if (leader != bot)
        return EnsureOwnCcSetup(botAI, bot, group, apply);

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

    // Crowd control: each available caster's own icon on a mob it can hold, as many as
    // the pull is worth (DcMarkPlan::CcSlots), narrowest caster first.
    uint32 const slots = DcMarkPlan::CcSlots(static_cast<uint32>(attackers.size()));
    std::vector<Caster> const casters = slots ? CcCasters(bot, group) : std::vector<Caster>{};
    if (!casters.empty())
    {
        std::vector<bool> busy;
        for (Caster const& c : casters)
            busy.push_back(c.iconIdx == skullIdx || IconOnLiveAttacker(group, c.iconIdx, attackers));
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
            x.controlled = u->HasAuraWithMechanic(kCcMechanics);
            x.healthPct = u->GetHealthPct();
            uint32 const typeBit = T(u->GetCreatureType());
            for (std::size_t k = 0; k < casters.size(); ++k)
            {
                CcKind const* kind = casters[k].kind;
                bool const typeOk = !kind->typeMask || (kind->typeMask & typeBit);
                if (typeOk && casters[k].spell && !u->IsImmunedToSpell(casters[k].spell))
                    x.castableBy |= 1u << k;
            }
            cands.push_back(x);
        }
        for (auto const& [k, i] : DcMarkPlan::PlanCc(cands, static_cast<uint32>(casters.size()), busy, slots))
        {
            if (!apply)
                return true;
            group->SetTargetIcon(casters[k].iconIdx, bot->GetGUID(), attackers[i]->GetGUID());
            changed = true;
        }
    }

    return changed;
}

bool DungeonClearMarkTargetsTrigger::IsActive() { return DcMarkTargetsStep(botAI, false); }

bool DungeonClearMarkTargetsAction::Execute(Event /*event*/) { return DcMarkTargetsStep(botAI, true); }
