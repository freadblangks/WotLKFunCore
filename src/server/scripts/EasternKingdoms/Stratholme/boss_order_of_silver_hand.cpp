/* WotLKFunCore is a custom fun server built for
* World of Warcarft game build 3.3.5b. (12345)
*
* Copyright (C) 2008-2016 JustACore.
* Copyright (C) 2008-2016 TrinityCore.
* Copyright (C) 2006-2016 ScriptDev2.
*
* ==== Start of GNU GPL ====
*
* This file is free software; as a special exception the author gives
* unlimited permission to copy and/or distribute it, with or without
* modifications, as long as this notice is preserved.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
* implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* ==== End of GNU GPL ====
*/

/* ScriptData
SDName: Boss_Silver_Hand_Bosses
SD%Complete: 40
SDComment: Basic script to have support for Horde paladin epic mount (quest 9737). All 5 members of Order of the Silver Hand running this script (least for now)
SDCategory: Stratholme
EndScriptData */

#include "ScriptPCH.h"
#include "stratholme.h"

/*#####
# Additional:
# Although this is a working solution, the correct would be in addition to check if Aurius is dead.
# Once player extinguish the eternal flame (cast spell 31497->start event 11206) Aurius should become hostile.
# Once Aurius is defeated, he should be the one summoning the ghosts.
#####*/

#define SH_GREGOR           17910
#define SH_CATHELA          17911
#define SH_NEMAS            17912
#define SH_AELMAR           17913
#define SH_VICAR            17914
#define SH_QUEST_CREDIT     17915

#define SPELL_HOLY_LIGHT    25263
#define SPELL_DIVINE_SHIELD 13874
class boss_silver_hand_bosses : public CreatureScript
{
public:
    boss_silver_hand_bosses() : CreatureScript("boss_silver_hand_bosses") { }

    CreatureAI* GetAI(Creature* creature) const
    {
        return new boss_silver_hand_bossesAI (creature);
    }

    struct boss_silver_hand_bossesAI : public ScriptedAI
    {
        boss_silver_hand_bossesAI(Creature* creature) : ScriptedAI(creature)
        {
            instance = creature->GetInstanceScript();
        }

        InstanceScript* instance;

        uint32 HolyLight_Timer;
        uint32 DivineShield_Timer;

        void Reset()
        {
            HolyLight_Timer = 20000;
            DivineShield_Timer = 20000;

            if (instance)
            {
                switch (me->GetEntry())
                {
                    case SH_AELMAR:
                        instance->SetData(TYPE_SH_AELMAR, 0);
                        break;
                    case SH_CATHELA:
                        instance->SetData(TYPE_SH_CATHELA, 0);
                        break;
                    case SH_GREGOR:
                        instance->SetData(TYPE_SH_GREGOR, 0);
                        break;
                    case SH_NEMAS:
                        instance->SetData(TYPE_SH_NEMAS, 0);
                        break;
                    case SH_VICAR:
                        instance->SetData(TYPE_SH_VICAR, 0);
                        break;
                }
            }
        }

        void EnterCombat(Unit* /*who*/)
        {
        }

        void JustDied(Unit* killer)
        {
            if (!instance)
                return;

            switch (me->GetEntry())
            {
                case SH_AELMAR:
                    instance->SetData(TYPE_SH_AELMAR, 2);
                    break;
                case SH_CATHELA:
                    instance->SetData(TYPE_SH_CATHELA, 2);
                    break;
                case SH_GREGOR:
                    instance->SetData(TYPE_SH_GREGOR, 2);
                    break;
                case SH_NEMAS:
                    instance->SetData(TYPE_SH_NEMAS, 2);
                    break;
                case SH_VICAR:
                    instance->SetData(TYPE_SH_VICAR, 2);
                    break;
            }

            if (instance->GetData(TYPE_SH_QUEST))
            {
                if (Player* player = killer->ToPlayer())
                    player->KilledMonsterCredit(SH_QUEST_CREDIT, 0);
            }
        }

        void UpdateAI(const uint32 diff)
        {
            //Return since we have no target
            if (!UpdateVictim())
                return;

            if (HolyLight_Timer <= diff)
            {
                if (HealthBelowPct(20))
                {
                    DoCast(me, SPELL_HOLY_LIGHT);
                    HolyLight_Timer = 20000;
                }
            } else HolyLight_Timer -= diff;

            if (DivineShield_Timer <= diff)
            {
                if (HealthBelowPct(5))
                {
                    DoCast(me, SPELL_DIVINE_SHIELD);
                    DivineShield_Timer = 40000;
                }
            } else DivineShield_Timer -= diff;

            DoMeleeAttackIfReady();
        }
    };
};

void AddSC_boss_order_of_silver_hand()
{
    new boss_silver_hand_bosses();
}