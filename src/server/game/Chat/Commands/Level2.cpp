/* WotLKFunCore is a custom fun server built for
* World of Warcarft game build 3.3.5b. (12345)
*
* Copyright (C) 2008-2016 JustACore.
* Copyright (C) 2008-2016 TrinityCore.
* Copyright (C) 2005-2016 MaNGOS.
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

#include "Common.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Item.h"
#include "GameObject.h"
#include "Opcodes.h"
#include "Chat.h"
#include "MapManager.h"
#include "Language.h"
#include "World.h"
#include "GameEventMgr.h"
#include "SpellMgr.h"
#include "PoolMgr.h"
#include "AccountMgr.h"
#include "WaypointManager.h"
#include "Util.h"
#include <cctype>
#include <iostream>
#include <fstream>
#include <map>
#include "OutdoorPvPMgr.h"
#include "Transport.h"
#include "TargetedMovementGenerator.h"                      // for HandleNpcUnFollowCommand
#include "CreatureGroups.h"
#include "ace/INET_Addr.h"

/// Triggering corpses expire check in world
bool ChatHandler::HandleServerCorpsesCommand(const char* /*args*/)
{
    sObjectAccessor->RemoveOldCorpses();
    return true;
}

//mute player for some times
bool ChatHandler::HandleMuteCommand(const char* args)
{
    std::string announce;

    char* nameStr;
    char* delayStr;
    extractOptFirstArg((char*)args, &nameStr, &delayStr);
    if (!delayStr)
        return false;

    char *mutereason = strtok(NULL, "\r");
    std::string mutereasonstr = "No reason";
    if (mutereason != NULL)
         mutereasonstr = mutereason;

    if(!mutereason)
    {
        PSendSysMessage("You must enter a reason of mute");
        SetSentErrorMessage(true);
        return false;
    }

    Player* target;
    uint64 target_guid;
    std::string target_name;
    if (!extractPlayerTarget(nameStr, &target, &target_guid, &target_name))
        return false;

    uint32 accountId = target ? target->GetSession()->GetAccountId() : sObjectMgr->GetPlayerAccountIdByGUID(target_guid);

    // find only player from same account if any
    if (!target)
        if (WorldSession* session = sWorld->FindSession(accountId))
            target = session->GetPlayer();

    uint32 notspeaktime = (uint32) atoi(delayStr);

    // must have strong lesser security level
    if (HasLowerSecurity (target, target_guid, true))
        return false;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_MUTE_TIME);

    if (target)
    {
        // Target is online, mute will be in effect right away.
        int64 muteTime = time(NULL) + notspeaktime * MINUTE;
        target->GetSession()->m_muteTime = muteTime;

        stmt->setInt64(0, muteTime);

        ChatHandler(target).PSendSysMessage(LANG_YOUR_CHAT_DISABLED, notspeaktime, mutereasonstr.c_str());
    }
    else
    {
        // Target is offline, mute will be in effect starting from the next login.
        int32 muteTime = -int32(notspeaktime * MINUTE);

        stmt->setInt64(0, muteTime);
    }

    stmt->setUInt32(1, accountId);

    LoginDatabase.Execute(stmt);

    std::string nameLink = playerLink(target_name);

    PSendSysMessage(target ? LANG_YOU_DISABLE_CHAT : LANG_COMMAND_DISABLE_CHAT_DELAYED, nameLink.c_str(), notspeaktime, mutereasonstr.c_str());

    announce = "The character '";
    announce += nameStr;
    announce += "' was muted for ";
    announce += delayStr;
    announce += " minutes by the character '";
    announce += m_session->GetPlayerName();
    announce += "'. The reason is: ";
    announce += mutereason;
    HandleAnnounceCommand(announce.c_str());

    return true;
}

//unmute player
bool ChatHandler::HandleUnmuteCommand(const char* args)
{
    Player* target;
    uint64 target_guid;
    std::string target_name;
    if (!extractPlayerTarget((char*)args, &target, &target_guid, &target_name))
        return false;

    uint32 accountId = target ? target->GetSession()->GetAccountId() : sObjectMgr->GetPlayerAccountIdByGUID(target_guid);

    // find only player from same account if any
    if (!target)
        if (WorldSession* session = sWorld->FindSession(accountId))
            target = session->GetPlayer();

    // must have strong lesser security level
    if (HasLowerSecurity (target, target_guid, true))
        return false;

    if (target)
    {
        if (target->CanSpeak())
        {
            SendSysMessage(LANG_CHAT_ALREADY_ENABLED);
            SetSentErrorMessage(true);
            return false;
        }

        target->GetSession()->m_muteTime = 0;
    }

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_MUTE_TIME);

    stmt->setInt64(0, 0);
    stmt->setUInt32(1, accountId);

    LoginDatabase.Execute(stmt);

    if (target)
        ChatHandler(target).PSendSysMessage(LANG_YOUR_CHAT_ENABLED);

    std::string nameLink = playerLink(target_name);

    PSendSysMessage(LANG_YOU_ENABLE_CHAT, nameLink.c_str());
    return true;
}

bool ChatHandler::HandleGUIDCommand(const char* /*args*/)
{
    uint64 guid = m_session->GetPlayer()->GetSelection();

    if (guid == 0)
    {
        SendSysMessage(LANG_NO_SELECTION);
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage(LANG_OBJECT_GUID, GUID_LOPART(guid), GUID_HIPART(guid));
    return true;
}

 //move item to other slot
bool ChatHandler::HandleItemMoveCommand(const char* args)
{
    if (!*args)
        return false;

    char* pParam1 = strtok((char*)args, " ");
    if (!pParam1)
        return false;

    char* pParam2 = strtok(NULL, " ");
    if (!pParam2)
        return false;

    uint8 srcslot = (uint8)atoi(pParam1);
    uint8 dstslot = (uint8)atoi(pParam2);

    if (srcslot == dstslot)
        return true;

    if (!m_session->GetPlayer()->IsValidPos(INVENTORY_SLOT_BAG_0, srcslot, true))
        return false;

    if (!m_session->GetPlayer()->IsValidPos(INVENTORY_SLOT_BAG_0, dstslot, false))
        return false;

    uint16 src = ((INVENTORY_SLOT_BAG_0 << 8) | srcslot);
    uint16 dst = ((INVENTORY_SLOT_BAG_0 << 8) | dstslot);

    m_session->GetPlayer()->SwapItem(src, dst);

    return true;
}

//kick player
bool ChatHandler::HandleKickPlayerCommand(const char *args)
{
    Player* target = NULL;
    std::string playerName;
    if (!extractPlayerTarget((char*)args, &target, NULL, &playerName))
        return false;

    if (m_session && target == m_session->GetPlayer())
    {
        SendSysMessage(LANG_COMMAND_KICKSELF);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    char const* kickReason = strtok(NULL, "\r");
    std::string kickReasonStr = "No reason";
    if (kickReason != NULL)
        kickReasonStr = kickReason;
    if (sWorld->getBoolConfig(CONFIG_SHOW_KICK_IN_WORLD))
        sWorld->SendWorldText(LANG_COMMAND_KICKMESSAGE, playerName.c_str(), kickReasonStr.c_str());
    else
        PSendSysMessage(LANG_COMMAND_KICKMESSAGE, playerName.c_str(), kickReasonStr.c_str());

    ChatHandler(target).PSendSysMessage("You will be kicked in 3 seconds. The reason is: |cffFF0000%s |r", kickReasonStr.c_str());
    target->GetSession()->SendNotification("You will be kicked in 3 seconds.");
    target->m_Events.AddEvent(new kick_player_delay_event(target), target->m_Events.CalculateTime(3000));
    return true;
}

//show info of player
bool ChatHandler::HandlePInfoCommand(const char* args)
{
    Player* target;
    uint64 target_guid;
    std::string target_name;

    uint32 parseGUID = MAKE_NEW_GUID(atol((char*)args), 0, HIGHGUID_PLAYER);

    if (sObjectMgr->GetPlayerNameByGUID(parseGUID, target_name))
    {
        target = sObjectMgr->GetPlayerByLowGUID(parseGUID);
        target_guid = parseGUID;
    }
    else if (!extractPlayerTarget((char*)args, &target, &target_guid, &target_name))
        return false;

    uint32 accId             = 0;
    uint32 money             = 0;
    uint32 total_player_time = 0;
    uint8 level              = 0;
    uint32 latency           = 0;
    uint8 race;
    uint8 Class;
    int64 muteTime           = 0;
    int64 banTime            = -1;
    uint32 mapId;
    uint32 areaId;
    uint32 phase             = 0;
    uint32 votepoints = 0;
    uint32 donationpoints = 0;

    std::string cheatcooldown = "";
    std::string cheatcasttime = "";
    std::string cheatpower = "";

    // get additional information from Player object
    if (target)
    {
        // check online security
        if (HasLowerSecurity(target, 0))
            return false;

        QueryResult resultTwo = LoginDatabase.PQuery("SELECT vp, dp FROM fusion.account_data WHERE id = '%u' AND vp >= '0'", target->GetSession()->GetAccountId());
        if (!resultTwo) // check
            return false;

        Field *fields = resultTwo->Fetch();
        votepoints = fields[0].GetUInt32();
        donationpoints = fields[1].GetUInt32();

        accId             = target->GetSession()->GetAccountId();
        money             = target->GetMoney();
        total_player_time = target->GetTotalPlayedTime();
        level             = target->getLevel();
        latency           = target->GetSession()->GetLatency();
        race              = target->getRace();
        Class             = target->getClass();
        muteTime          = target->GetSession()->m_muteTime;
        mapId             = target->GetMapId();
        areaId            = target->GetAreaId();
        phase             = target->GetPhaseMask();
	 if(target->GetCommandStatus(CHEAT_COOLDOWN))
	    cheatcooldown = "ON";
	 else
	    cheatcooldown = "OFF";
	 if(target->GetCommandStatus(CHEAT_CASTTIME))
	    cheatcasttime = "ON";
	 else
	    cheatcasttime = "OFF";
	 if(target->GetCommandStatus(CHEAT_POWER))
	    cheatpower = "ON";
	 else
	    cheatpower = "OFF";
    }
    // get additional information from DB
    else
    {
        // check offline security
        if (HasLowerSecurity(NULL, target_guid))
            return false;

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_PINFO);
        stmt->setUInt32(0, GUID_LOPART(target_guid));
        PreparedQueryResult result = CharacterDatabase.Query(stmt);

        if (!result)
            return false;

        Field* fields     = result->Fetch();
        total_player_time = fields[0].GetUInt32();
        level             = fields[1].GetUInt8();
        money             = fields[2].GetUInt32();
        accId             = fields[3].GetUInt32();
        race              = fields[4].GetUInt8();
        Class             = fields[5].GetUInt8();
        mapId             = fields[6].GetUInt16();
        areaId            = fields[7].GetUInt16();
    }

    std::string username   = GetTrinityString(LANG_ERROR);
    std::string email      = GetTrinityString(LANG_ERROR);
    std::string last_ip    = GetTrinityString(LANG_ERROR);
    uint32 security        = 0;
    std::string last_login = GetTrinityString(LANG_ERROR);

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_PINFO);
    stmt->setInt32(0, int32(realmID));
    stmt->setUInt32(1, accId);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    if (result)
    {
        Field* fields = result->Fetch();
        username      = fields[0].GetString();
        security      = fields[1].GetUInt8();
        email         = fields[2].GetString();
        muteTime      = fields[5].GetUInt64();

        if (email.empty())
            email = "-";

        if (!m_session || m_session->GetSecurity() >= AccountTypes(security))
        {
            last_ip = fields[3].GetString();
            last_login = fields[4].GetString();

            uint32 ip = inet_addr(last_ip.c_str());
#if TRINITY_ENDIAN == BIGENDIAN
            EndianConvertReverse(ip);
#endif

            PreparedStatement* stmt = WorldDatabase.GetPreparedStatement(WORLD_SEL_IP2NATION_COUNTRY);

            stmt->setUInt32(0, ip);

            PreparedQueryResult result2 = WorldDatabase.Query(stmt);

            if (result2)
            {
                Field* fields2 = result2->Fetch();
                last_ip.append(" (");
                last_ip.append(fields2[0].GetString());
                last_ip.append(")");
            }
        }
        else
        {
            last_ip = "-";
            last_login = "-";
        }
    }

    std::string nameLink = playerLink(target_name);

    PSendSysMessage(LANG_PINFO_ACCOUNT, (target?"":GetTrinityString(LANG_OFFLINE)), nameLink.c_str(), GUID_LOPART(target_guid), username.c_str(), accId, email.c_str(), security, last_ip.c_str(), last_login.c_str(), latency, donationpoints, votepoints);

    std::string bannedby = "unknown";
    std::string banreason = "";

    stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_PINFO_BANS);
    stmt->setUInt32(0, accId);
    PreparedQueryResult result2 = LoginDatabase.Query(stmt);
    if (!result2)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_PINFO_BANS);
        stmt->setUInt32(0, GUID_LOPART(target_guid));
        result2 = CharacterDatabase.Query(stmt);
    }

    if (result2)
    {
        Field* fields = result2->Fetch();
        banTime       = int64(fields[1].GetBool() ? 0 : fields[0].GetUInt32());
        bannedby      = fields[2].GetString();
        banreason     = fields[3].GetString();
    }

    if (muteTime > 0)
        PSendSysMessage(LANG_PINFO_MUTE, secsToTimeString(muteTime - time(NULL), true).c_str());

    if (banTime >= 0)
        PSendSysMessage(LANG_PINFO_BAN, banTime > 0 ? secsToTimeString(banTime - time(NULL), true).c_str() : "permanently", bannedby.c_str(), banreason.c_str());

    std::string race_s, Class_s;
    switch (race)
    {
        case RACE_HUMAN:            race_s = "Human";       break;
        case RACE_ORC:              race_s = "Orc";         break;
        case RACE_DWARF:            race_s = "Dwarf";       break;
        case RACE_NIGHTELF:         race_s = "Night Elf";   break;
        case RACE_UNDEAD_PLAYER:    race_s = "Undead";      break;
        case RACE_TAUREN:           race_s = "Tauren";      break;
        case RACE_GNOME:            race_s = "Gnome";       break;
        case RACE_TROLL:            race_s = "Troll";       break;
        case RACE_GOBLIN:           race_s = "Goblin";      break;
        case RACE_BLOODELF:         race_s = "Blood Elf";   break;
        case RACE_DRAENEI:          race_s = "Draenei";     break;
        case RACE_NAGA:             race_s = "Naga";        break;
        case RACE_BROKEN:         race_s = "Broken";    break;
        case RACE_VRYKUL:           race_s = "Vrykul";      break;
    }
    switch (Class)
    {
        case CLASS_WARRIOR:         Class_s = "Warrior";        break;
        case CLASS_PALADIN:         Class_s = "Paladin";        break;
        case CLASS_HUNTER:          Class_s = "Hunter";         break;
        case CLASS_ROGUE:           Class_s = "Rogue";          break;
        case CLASS_PRIEST:          Class_s = "Priest";         break;
        case CLASS_DEATH_KNIGHT:    Class_s = "Death Knight";   break;
        case CLASS_SHAMAN:          Class_s = "Shaman";         break;
        case CLASS_MAGE:            Class_s = "Mage";           break;
        case CLASS_WARLOCK:         Class_s = "Warlock";        break;
        case CLASS_DRUID:           Class_s = "Druid";          break;
    }

    std::string timeStr = secsToTimeString(total_player_time, true, true);
    uint32 gold = money /GOLD;
    uint32 silv = (money % GOLD) / SILVER;
    uint32 copp = (money % GOLD) % SILVER;
    PSendSysMessage(LANG_PINFO_LEVEL, race_s.c_str(), Class_s.c_str(), timeStr.c_str(), level, gold, silv, copp);

    // Add map, zone, subzone and phase to output
    int locale = GetSessionDbcLocale();
    std::string areaName = "<unknown>";
    std::string zoneName = "";

    MapEntry const* map = sMapStore.LookupEntry(mapId);

    AreaTableEntry const* area = GetAreaEntryByAreaID(areaId);
    if (area)
    {
        areaName = area->area_name[locale];

        AreaTableEntry const* zone = GetAreaEntryByAreaID(area->zone);

        if (zone)
            zoneName = zone->area_name[locale];
    }

    if (target)
    {
        if (!zoneName.empty())
            PSendSysMessage(LANG_PINFO_MAP_ONLINE, map->name[locale], zoneName.c_str(), areaName.c_str(), phase);
        else
            PSendSysMessage(LANG_PINFO_MAP_ONLINE, map->name[locale], areaName.c_str(), "<unknown>", phase);
    }
    else
        PSendSysMessage(LANG_PINFO_MAP_OFFLINE, map->name[locale], areaName.c_str());

    if (target)
    {
            PSendSysMessage(LANG_PINFO_CHEATS, cheatcooldown.c_str(), cheatcasttime.c_str(), cheatpower.c_str());
            //PSendSysMessage(LANG_PINFO_TOGGLE, summon.c_str(), appear.c_str());
    }

    return true;
}

//rename characters
bool ChatHandler::HandleCharacterRenameCommand(const char* args)
{
    Player* target;
    uint64 targetGuid;
    std::string targetName;
    if (!extractPlayerTarget((char*)args, &target, &targetGuid, &targetName))
        return false;

    if (target)
    {
        // check online security
        if (HasLowerSecurity(target, 0))
            return false;

        PSendSysMessage(LANG_RENAME_PLAYER, GetNameLink(target).c_str());
        target->SetAtLoginFlag(AT_LOGIN_RENAME);
    }
    else
    {
        // check offline security
        if (HasLowerSecurity(NULL, targetGuid))
            return false;

        std::string oldNameLink = playerLink(targetName);

        PSendSysMessage(LANG_RENAME_PLAYER_GUID, oldNameLink.c_str(), GUID_LOPART(targetGuid));

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);

        stmt->setUInt16(0, uint16(AT_LOGIN_RENAME));
        stmt->setUInt32(1, GUID_LOPART(targetGuid));

        CharacterDatabase.Execute(stmt);
    }

    return true;
}

// customize characters
bool ChatHandler::HandleCharacterCustomizeCommand(const char* args)
{
    Player* target;
    uint64 targetGuid;
    std::string targetName;
    if (!extractPlayerTarget((char*)args, &target, &targetGuid, &targetName))
        return false;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);

    stmt->setUInt16(0, uint16(AT_LOGIN_CUSTOMIZE));

    if (target)
    {
        PSendSysMessage(LANG_CUSTOMIZE_PLAYER, GetNameLink(target).c_str());
        target->SetAtLoginFlag(AT_LOGIN_CUSTOMIZE);

        stmt->setUInt32(1, target->GetGUIDLow());
    }
    else
    {
        std::string oldNameLink = playerLink(targetName);

        stmt->setUInt32(1, GUID_LOPART(targetGuid));

        PSendSysMessage(LANG_CUSTOMIZE_PLAYER_GUID, oldNameLink.c_str(), GUID_LOPART(targetGuid));
    }

    CharacterDatabase.Execute(stmt);

    return true;
}

bool ChatHandler::HandleCharacterChangeFactionCommand(const char* args)
{
    Player* target;
    uint64 targetGuid;
    std::string targetName;

    if (!extractPlayerTarget((char*)args, &target, &targetGuid, &targetName))
        return false;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);

    stmt->setUInt16(0, uint16(AT_LOGIN_CHANGE_FACTION));

    if (target)
    {
        PSendSysMessage(LANG_CUSTOMIZE_PLAYER, GetNameLink(target).c_str());
        target->SetAtLoginFlag(AT_LOGIN_CHANGE_FACTION);

        stmt->setUInt32(1, target->GetGUIDLow());
    }
    else
    {
        std::string oldNameLink = playerLink(targetName);

        PSendSysMessage(LANG_CUSTOMIZE_PLAYER_GUID, oldNameLink.c_str(), GUID_LOPART(targetGuid));

        stmt->setUInt32(1, GUID_LOPART(targetGuid));
    }

    CharacterDatabase.Execute(stmt);

    return true;
}

bool ChatHandler::HandleCharacterChangeRaceCommand(const char * args)
{
    Player* target;
    uint64 targetGuid;
    std::string targetName;
    if (!extractPlayerTarget((char*)args, &target, &targetGuid, &targetName))
        return false;

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ADD_AT_LOGIN_FLAG);

    stmt->setUInt16(0, uint16(AT_LOGIN_CHANGE_RACE));

    if (target)
    {
        // TODO : add text into database
        PSendSysMessage(LANG_CUSTOMIZE_PLAYER, GetNameLink(target).c_str());
        target->SetAtLoginFlag(AT_LOGIN_CHANGE_RACE);

        stmt->setUInt32(1, target->GetGUIDLow());
    }
    else
    {
        std::string oldNameLink = playerLink(targetName);

        // TODO : add text into database
        PSendSysMessage(LANG_CUSTOMIZE_PLAYER_GUID, oldNameLink.c_str(), GUID_LOPART(targetGuid));

        stmt->setUInt32(1, GUID_LOPART(targetGuid));
    }

    CharacterDatabase.Execute(stmt);

    return true;
}

bool ChatHandler::HandleCharacterReputationCommand(const char* args)
{
    Player* target;
    if (!extractPlayerTarget((char*)args, &target))
        return false;

    LocaleConstant loc = GetSessionDbcLocale();

    FactionStateList const& targetFSL = target->GetReputationMgr().GetStateList();
    for (FactionStateList::const_iterator itr = targetFSL.begin(); itr != targetFSL.end(); ++itr)
    {
        const FactionState& faction = itr->second;
        FactionEntry const* factionEntry = sFactionStore.LookupEntry(faction.ID);
        char const* factionName = factionEntry ? factionEntry->name[loc] : "#Not found#";
        ReputationRank rank = target->GetReputationMgr().GetRank(factionEntry);
        std::string rankName = GetTrinityString(ReputationRankStrIndex[rank]);
        std::ostringstream ss;
        if (m_session)
            ss << faction.ID << " - |cffffffff|Hfaction:" << faction.ID << "|h[" << factionName << ' ' << localeNames[loc] << "]|h|r";
        else
            ss << faction.ID << " - " << factionName << ' ' << localeNames[loc];

        ss << ' ' << rankName << " (" << target->GetReputationMgr().GetReputation(factionEntry) << ')';

        if (faction.Flags & FACTION_FLAG_VISIBLE)
            ss << GetTrinityString(LANG_FACTION_VISIBLE);
        if (faction.Flags & FACTION_FLAG_AT_WAR)
            ss << GetTrinityString(LANG_FACTION_ATWAR);
        if (faction.Flags & FACTION_FLAG_PEACE_FORCED)
            ss << GetTrinityString(LANG_FACTION_PEACE_FORCED);
        if (faction.Flags & FACTION_FLAG_HIDDEN)
            ss << GetTrinityString(LANG_FACTION_HIDDEN);
        if (faction.Flags & FACTION_FLAG_INVISIBLE_FORCED)
            ss << GetTrinityString(LANG_FACTION_INVISIBLE_FORCED);
        if (faction.Flags & FACTION_FLAG_INACTIVE)
            ss << GetTrinityString(LANG_FACTION_INACTIVE);

        SendSysMessage(ss.str().c_str());
    }
    return true;
}

bool ChatHandler::HandleLookupEventCommand(const char* args)
{
    if (!*args)
        return false;

    std::string namepart = args;
    std::wstring wnamepart;

    // converting string that we try to find to lower case
    if (!Utf8toWStr(namepart, wnamepart))
        return false;

    wstrToLower(wnamepart);

    bool found = false;
    uint32 count = 0;
    uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
    GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();

    for (uint32 id = 0; id < events.size(); ++id)
    {
        GameEventData const& eventData = events[id];

        std::string descr = eventData.description;
        if (descr.empty())
            continue;

        if (Utf8FitTo(descr, wnamepart))
        {
            if (maxResults && count++ == maxResults)
            {
                PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                return true;
            }

            char const* active = activeEvents.find(id) != activeEvents.end() ? GetTrinityString(LANG_ACTIVE) : "";

            if (m_session)
                PSendSysMessage(LANG_EVENT_ENTRY_LIST_CHAT, id, id, eventData.description.c_str(), active);
            else
                PSendSysMessage(LANG_EVENT_ENTRY_LIST_CONSOLE, id, eventData.description.c_str(), active);

            if (!found)
                found = true;
        }
    }

    if (!found)
        SendSysMessage(LANG_NOEVENTFOUND);

    return true;
}

bool ChatHandler::HandleCombatStopCommand(const char* args)
{
    Player* target = NULL;
    
    if (args && strlen(args) > 0)
    {
        target = sObjectAccessor->FindPlayerByName(args);
        if (!target)
        {
            SendSysMessage(LANG_PLAYER_NOT_FOUND);
            SetSentErrorMessage(true);
            return false;
        }
    }
    
    if (!target)
        if (!extractPlayerTarget((char*)args, &target))
            return false;

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    target->CombatStop();
    target->getHostileRefManager().deleteReferences();
    return true;
}

bool ChatHandler::HandleLookupPlayerIpCommand(const char* args)
{
    std::string ip;
    int32 limit;
    char* limit_str;

    Player *chr = getSelectedPlayer();
    if (!*args)
    {
        // NULL only if used from console
        if (!chr || chr == GetSession()->GetPlayer())
            return false;

        ip = chr->GetSession()->GetRemoteAddress();
        limit = -1;
    }
    else
    {
        ip = strtok ((char*)args, " ");
        limit_str = strtok (NULL, " ");
        limit = limit_str ? atoi (limit_str) : -1;
    }

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BY_IP);
    stmt->setString(0, ip);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    return LookupPlayerSearchCommand(result, limit);
}

bool ChatHandler::HandleLookupPlayerAccountCommand(const char* args)
{
    if (!*args)
        return false;

    std::string account = strtok ((char*)args, " ");
    char* limit_str = strtok (NULL, " ");
    int32 limit = limit_str ? atoi (limit_str) : -1;

    if (!AccountMgr::normalizeString (account))
        return false;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_LIST_BY_NAME);
    stmt->setString(0, account);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    return LookupPlayerSearchCommand (result, limit);
}

bool ChatHandler::HandleLookupPlayerEmailCommand(const char* args)
{
    if (!*args)
        return false;

    std::string email = strtok ((char*)args, " ");
    char* limit_str = strtok (NULL, " ");
    int32 limit = limit_str ? atoi (limit_str) : -1;

    PreparedStatement* stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_LIST_BY_EMAIL);
    stmt->setString(0, email);
    PreparedQueryResult result = LoginDatabase.Query(stmt);

    return LookupPlayerSearchCommand(result, limit);
}

bool ChatHandler::LookupPlayerSearchCommand(PreparedQueryResult result, int32 limit)
{
    if (!result)
    {
        PSendSysMessage(LANG_NO_PLAYERS_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    int i = 0;
    uint32 count = 0;
    uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);
    do
    {
        if (maxResults && count++ == maxResults)
        {
            PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
            return true;
        }

        Field* fields = result->Fetch();
        uint32 acc_id = fields[0].GetUInt32();
        std::string acc_name = fields[1].GetString();

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_GUID_NAME_BY_ACC);
        stmt->setUInt32(0, acc_id);
        PreparedQueryResult result2 = CharacterDatabase.Query(stmt);

        if (result2)
        {
            PSendSysMessage(LANG_LOOKUP_PLAYER_ACCOUNT, acc_name.c_str(), acc_id);

            uint64 guid = 0;
            std::string name;

            do
            {
                Field* charfields = result2->Fetch();
                guid = charfields[0].GetUInt64();
                name = charfields[1].GetString();

                PSendSysMessage(LANG_LOOKUP_PLAYER_CHARACTER, name.c_str(), guid);
                ++i;

            } while (result2->NextRow() && (limit == -1 || i < limit));
        }
    } while (result->NextRow());

    if (i == 0)                                                // empty accounts only
    {
        PSendSysMessage(LANG_NO_PLAYERS_FOUND);
        SetSentErrorMessage(true);
        return false;
    }

    return true;
}

bool ChatHandler::HandleRepairitemsCommand(const char* args)
{
    Player* target;
    if (!extractPlayerTarget((char*)args, &target))
        return false;

    // check online security
    if (HasLowerSecurity(target, 0))
        return false;

    // Repair items
    target->DurabilityRepairAll(false, 0, false);

    PSendSysMessage(LANG_YOU_REPAIR_ITEMS, GetNameLink(target).c_str());
    if (needReportToTarget(target))
        ChatHandler(target).PSendSysMessage(LANG_YOUR_ITEMS_REPAIRED, GetNameLink().c_str());
    return true;
}

bool ChatHandler::HandleWaterwalkCommand(const char* args)
{
    if (!*args)
        return false;

    Player* player = getSelectedPlayer();

    if (!player)
    {
        PSendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    // check online security
    if (HasLowerSecurity(player, 0))
        return false;

    if (strncmp(args, "on", 3) == 0)
        player->SetMovement(MOVE_WATER_WALK);               // ON
    else if (strncmp(args, "off", 4) == 0)
        player->SetMovement(MOVE_LAND_WALK);                // OFF
    else
    {
        SendSysMessage(LANG_USE_BOL);
        return false;
    }

    PSendSysMessage(LANG_YOU_SET_WATERWALK, args, GetNameLink(player).c_str());
    if (needReportToTarget(player))
        ChatHandler(player).PSendSysMessage(LANG_YOUR_WATERWALK_SET, args, GetNameLink().c_str());
    return true;
}

bool ChatHandler::HandleCreatePetCommand(const char* /*args*/)
{
    Player* player = m_session->GetPlayer();
    Creature* creatureTarget = getSelectedCreature();

    if (!creatureTarget || creatureTarget->isPet() || creatureTarget->GetTypeId() == TYPEID_PLAYER)
    {
        PSendSysMessage(LANG_SELECT_CREATURE);
        SetSentErrorMessage(true);
        return false;
    }

    CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(creatureTarget->GetEntry());
    // Creatures with family 0 crashes the server
    if (cInfo->family == 0)
    {
        PSendSysMessage("This creature cannot be tamed. (family id: 0).");
        SetSentErrorMessage(true);
        return false;
    }

    if (player->GetPetGUID())
    {
        PSendSysMessage("You already have a pet");
        SetSentErrorMessage(true);
        return false;
    }

    // Everything looks OK, create new pet
    Pet* pet = new Pet(player, HUNTER_PET);
    if (!pet->CreateBaseAtCreature(creatureTarget))
    {
        delete pet;
        PSendSysMessage("Error 1");
        return false;
    }

    creatureTarget->setDeathState(JUST_DIED);
    creatureTarget->RemoveCorpse();
    creatureTarget->SetHealth(0); // just for nice GM-mode view

    pet->SetUInt64Value(UNIT_FIELD_CREATEDBY, player->GetGUID());
    pet->SetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE, player->getFaction());

    if (!pet->InitStatsForLevel(creatureTarget->getLevel()))
    {
        sLog->outError("InitStatsForLevel() in EffectTameCreature failed! Pet deleted.");
        PSendSysMessage("Error 2");
        delete pet;
        return false;
    }

    // prepare visual effect for levelup
    pet->SetUInt32Value(UNIT_FIELD_LEVEL, creatureTarget->getLevel()-1);

    pet->GetCharmInfo()->SetPetNumber(sObjectMgr->GeneratePetNumber(), true);
    // this enables pet details window (Shift+P)
    pet->InitPetCreateSpells();
    pet->SetFullHealth();

    pet->GetMap()->AddToMap(pet->ToCreature());

    // visual effect for levelup
    pet->SetUInt32Value(UNIT_FIELD_LEVEL, creatureTarget->getLevel());

    player->SetMinion(pet, true);
    pet->SavePetToDB(PET_SAVE_AS_CURRENT);
    player->PetSpellInitialize();

    return true;
}

bool ChatHandler::HandlePetLearnCommand(const char* args)
{
    if (!*args)
        return false;

    Player* player = m_session->GetPlayer();
    Pet* pet = player->GetPet();

    if (!pet)
    {
        PSendSysMessage("You have no pet");
        SetSentErrorMessage(true);
        return false;
    }

    uint32 spellId = extractSpellIdFromLink((char*)args);

    if (!spellId || !sSpellMgr->GetSpellInfo(spellId))
        return false;

    // Check if pet already has it
    if (pet->HasSpell(spellId))
    {
        PSendSysMessage("Pet already has spell: %u", spellId);
        SetSentErrorMessage(true);
        return false;
    }

    // Check if spell is valid
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo || !SpellMgr::IsSpellValid(spellInfo))
    {
        PSendSysMessage(LANG_COMMAND_SPELL_BROKEN, spellId);
        SetSentErrorMessage(true);
        return false;
    }

    pet->learnSpell(spellId);

    PSendSysMessage("Pet has learned spell %u", spellId);
    return true;
}

bool ChatHandler::HandlePetUnlearnCommand(const char *args)
{
    if (!*args)
        return false;

    Player* player = m_session->GetPlayer();
    Pet* pet = player->GetPet();

    if (!pet)
    {
        PSendSysMessage("You have no pet");
        SetSentErrorMessage(true);
        return false;
    }

    uint32 spellId = extractSpellIdFromLink((char*)args);

    if (pet->HasSpell(spellId))
        pet->removeSpell(spellId, false);
    else
        PSendSysMessage("Pet doesn't have that spell");

    return true;
}

bool ChatHandler::HandleLookupTitleCommand(const char* args)
{
    if (!*args)
        return false;

    // can be NULL in console call
    Player* target = getSelectedPlayer();

    // title name have single string arg for player name
    char const* targetName = target ? target->GetName() : "NAME";

    std::string namepart = args;
    std::wstring wnamepart;

    if (!Utf8toWStr(namepart, wnamepart))
        return false;

    // converting string that we try to find to lower case
    wstrToLower(wnamepart);

    uint32 counter = 0;                                     // Counter for figure out that we found smth.
    uint32 maxResults = sWorld->getIntConfig(CONFIG_MAX_RESULTS_LOOKUP_COMMANDS);

    // Search in CharTitles.dbc
    for (uint32 id = 0; id < sCharTitlesStore.GetNumRows(); id++)
    {
        CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
        if (titleInfo)
        {
            int loc = GetSessionDbcLocale();
            std::string name = titleInfo->name[loc];
            if (name.empty())
                continue;

            if (!Utf8FitTo(name, wnamepart))
            {
                loc = 0;
                for (; loc < TOTAL_LOCALES; ++loc)
                {
                    if (loc == GetSessionDbcLocale())
                        continue;

                    name = titleInfo->name[loc];
                    if (name.empty())
                        continue;

                    if (Utf8FitTo(name, wnamepart))
                        break;
                }
            }

            if (loc < TOTAL_LOCALES)
            {
                if (maxResults && counter == maxResults)
                {
                    PSendSysMessage(LANG_COMMAND_LOOKUP_MAX_RESULTS, maxResults);
                    return true;
                }

                char const* knownStr = target && target->HasTitle(titleInfo) ? GetTrinityString(LANG_KNOWN) : "";

                char const* activeStr = target && target->GetUInt32Value(PLAYER_CHOSEN_TITLE) == titleInfo->bit_index
                    ? GetTrinityString(LANG_ACTIVE)
                    : "";

                char titleNameStr[80];
                snprintf(titleNameStr, 80, name.c_str(), targetName);

                // send title in "id (idx:idx) - [namedlink locale]" format
                if (m_session)
                    PSendSysMessage(LANG_TITLE_LIST_CHAT, id, titleInfo->bit_index, id, titleNameStr, localeNames[loc], knownStr, activeStr);
                else
                    PSendSysMessage(LANG_TITLE_LIST_CONSOLE, id, titleInfo->bit_index, titleNameStr, localeNames[loc], knownStr, activeStr);

                ++counter;
            }
        }
    }
    if (counter == 0)                                       // if counter == 0 then we found nth
        SendSysMessage(LANG_COMMAND_NOTITLEFOUND);
    return true;
}

bool ChatHandler::HandleCharacterTitlesCommand(const char* args)
{
    if (!*args)
        return false;

    Player* target;
    if (!extractPlayerTarget((char*)args, &target))
        return false;

    LocaleConstant loc = GetSessionDbcLocale();
    char const* targetName = target->GetName();
    char const* knownStr = GetTrinityString(LANG_KNOWN);

    // Search in CharTitles.dbc
    for (uint32 id = 0; id < sCharTitlesStore.GetNumRows(); id++)
    {
        CharTitlesEntry const* titleInfo = sCharTitlesStore.LookupEntry(id);
        if (titleInfo && target->HasTitle(titleInfo))
        {
            std::string name = titleInfo->name[loc];
            if (name.empty())
                continue;

            char const* activeStr = target && target->GetUInt32Value(PLAYER_CHOSEN_TITLE) == titleInfo->bit_index
                ? GetTrinityString(LANG_ACTIVE)
                : "";

            char titleNameStr[80];
            snprintf(titleNameStr, 80, name.c_str(), targetName);

            // send title in "id (idx:idx) - [namedlink locale]" format
            if (m_session)
                PSendSysMessage(LANG_TITLE_LIST_CHAT, id, titleInfo->bit_index, id, titleNameStr, localeNames[loc], knownStr, activeStr);
            else
                PSendSysMessage(LANG_TITLE_LIST_CONSOLE, id, titleInfo->bit_index, name.c_str(), localeNames[loc], knownStr, activeStr);
        }
    }
    return true;
}