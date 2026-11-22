// Meadow residents: preserve each configured slot, release player takeovers,
// and replace only an absent resident while its part of the world is near.
// The construction module retains the single camp-side native creation entry.
Ped SpawnCampHorse(Hash model,const Pose& pose);
void ReleaseHorseModel(Horse& horse) {
    if(!horse.model_requested)return;
    // Recovery may already be requesting the same model in the scene batch.
    // That batch owns the release until ReleaseAssets runs.
    if(std::find(g_models.begin(),g_models.end(),horse.model)==g_models.end())
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(horse.model);
    horse.model_requested=false;horse.requested_at={};
}
void CancelHorseModels(const char* why) {
    for(auto& horse:g_horses)if(horse.model_requested) {
        ReleaseHorseModel(horse);
        LogHorseState(horse,"request_cancelled",std::string("reason=")+why);
    }
}
void ClearCampHorse(Horse& horse,const Clock::time_point& now) {
    firearm_guard::Discard(horse.ped);
    if(horse.ped==g_racing_horse){g_racing_horse=0;g_horse_stood_down=false;}
    horse.ped=0;horse.address=0;horse.retry_after=now+10s;
    horse.flee_checked=horse.flee_blocked=false;
    horse.flee_recheck_after={};
    horse.ownership=individual_identity::Ownership::Unknown;
    horse.ownership_issue=individual_identity::OwnershipIssue::None;
}
void SuspendCampHorseProtection(Horse& horse,const char* context) {
    horse.guard_suspended=true;
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse)){firearm_guard::Discard(horse.ped);return;}
    const firearm_guard::Identity expected{horse.ped,horse.model,horse.address};
    if(!firearm_guard::ReleaseOnceAndDiscard(expected))
        Log(CampHorseLabel(horse)+" guard_release_unconfirmed context="+context+"; retry discarded; native proofs may remain");
}
bool ResumeCampHorseProtection(Horse& horse) {
    if(!horse.guard_suspended)return true;
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse)){firearm_guard::Discard(horse.ped);return false;}
    if(!firearm_guard::Track(horse.ped,true)) {
        // Track can itself leave a failed rollback pending. Keep this horse
        // inactive and retry only after another explicit ownership probe.
        firearm_guard::Discard(horse.ped);return false;
    }
    horse.guard_suspended=false;return true;
}
void HandCampHorseToPlayer(Horse& horse,const std::string& evidence,const Clock::time_point& now) {
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse)){firearm_guard::Discard(horse.ped);return;}
    LogHorseState(horse,"player_handoff",evidence+"; it is theirs now");
    PreserveCampHorse(horse);
    SetCampHorseFleeBlock(horse,false);
    // Lift only our mount/leading and resident event restrictions. Do not
    // assign ownership, change attributes, or rewrite any inventory GUID.
    PED::SET_PED_CONFIG_FLAG(horse.ped,kBlockMountHorsePrompt,false);
    PED::SET_PED_CONFIG_FLAG(horse.ped,300,false);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(horse.ped,false);
    PED::_SET_PED_CAN_BE_LASSOED(horse.ped,true);
    if(g_racing_horse==horse.ped && g_horse_stood_down) {
        ENTITY::SET_ENTITY_VISIBLE(horse.ped,true);
        ENTITY::SET_ENTITY_COLLISION(horse.ped,true,true);
        PED::SET_PED_CAN_BE_TARGETTED(horse.ped,true);
    }
    SuspendCampHorseProtection(horse,"player_handoff");
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse)){ClearCampHorse(horse,now);return;}
    Ped owned=horse.ped;ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&owned);
    ReleaseHorseModel(horse);ClearCampHorse(horse,now);
}
bool RecheckCampHorseAction(Horse& horse,const char* action) {
    const auto claim=CheckedPlayerClaim(horse,PlayerHorses{});
    if(!claim)return false;
    // Ownership may change inside a native metadata probe. Recheck the live
    // slots and exact entity before a race-start or hide transaction.
    if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse))return false;
    if(const char* binding=PlayerHorses{}.Binding(horse.ped)) {
        HandCampHorseToPlayer(horse,binding,Clock::now());return false;
    }
    horse.ownership=claim->state;
    horse.ownership_issue=claim->issue;
    if(claim->state==individual_identity::Ownership::Owned) {
        HandCampHorseToPlayer(horse,claim->evidence,Clock::now());return false;
    }
    if(claim->state==individual_identity::Ownership::Unknown) {
        SuspendCampHorseProtection(horse,action);
        PreserveCampHorse(horse);
        // Brushing may register our resident in an unmapped special slot.
        // The race creates its own mount; keep the original visible and do
        // not relax teardown or protection policy on this ambiguous evidence.
        const bool race_start=std::string_view(action)=="duel_race_start" || std::string_view(action)=="stakes_race_start";
        if(claim->issue==individual_identity::OwnershipIssue::SpecialSlot && race_start){
            LogHorseState(horse,"ownership_unknown",std::string("race_offer_allowed ")+claim->evidence);
            return !ENTITY::IS_ENTITY_DEAD(horse.ped);
        }
        LogHorseState(horse,"ownership_unknown",std::string("blocked_action=")+action+" "+claim->evidence);return false;
    }
    return ResumeCampHorseProtection(horse) && !ENTITY::IS_ENTITY_DEAD(horse.ped);
}
bool MakeCampHorse(Horse& slot,const char* why) {
    const Pose pose=Grounded(slot.stand,true,0);
    const Ped horse=SpawnCampHorse(slot.model,pose);
    if(!Exists(horse)){LogHorseState(slot,"create_failed",std::string("reason=")+why);return false;}
    const auto address=world::Identify(horse).address;
    if(!address) {
        // Freshly created in this call, without yielding: this is still ours.
        Ped failed=horse;PED::DELETE_PED(&failed);
        LogHorseState(slot,"create_failed","reason=resident_identity_unavailable");return false;
    }
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(horse,true,true);
    appearance::InitializeHorse(horse);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(horse,true);
    PED::SET_PED_CAN_BE_TARGETTED(horse,true);
    FLOCK::_SET_ANIMAL_IS_WILD(horse,false);
    PED::SET_PED_CONFIG_FLAG(horse,kBlockMountHorsePrompt,true);
    PED::SET_PED_CONFIG_FLAG(horse,297,true); // ForceInteractionLockonOnTargetPed.
    PED::SET_PED_CONFIG_FLAG(horse,300,true); // DisablePlayerHorseLeading.
    PED::SET_PED_CONFIG_FLAG(horse,211,true); // GiveAmbientDefaultTaskIfMissionPed.
    // Surveyed gang-camp flags. The four undocumented flags
    // (186,230,304,324) remain unset: copying those lost all six horses before.
    for(int on:{6,208,209,277,319,400})PED::SET_PED_CONFIG_FLAG(horse,on,true);
    for(int off:{113,136,301,312})PED::SET_PED_CONFIG_FLAG(horse,off,false);
    FLOCK::SET_ANIMAL_TUNING_BOOL_PARAM(horse,25,false);
    FLOCK::SET_ANIMAL_TUNING_BOOL_PARAM(horse,24,false);
    PED::_CLEAR_ACTIVE_ANIMAL_OWNER(horse,false);
    PED::SET_PED_OWNS_ANIMAL(g_arthur,horse,false);
    PED::_SET_PED_CAN_BE_LASSOED(horse,false);
    invoke<Void>(0xF74E134F40192884,horse,1); // _SET_PED_SHOULD_IGNORE_AVOIDANCE_VOLUMES.
    ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(horse,5,9);
    ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(horse,6,9);
    firearm_guard::Track(horse,true);
    TASK::TASK_START_SCENARIO_IN_PLACE_HASH(horse,Key(g_layout.horse_scenario),-1,true,0,pose.heading,false);
    slot.ped=horse;slot.address=address;slot.stand=pose;
    // This is the allocation we have just created and assigned to Arthur,
    // without yielding. Later maintenance/teardown must probe ownership anew.
    slot.ownership=individual_identity::Ownership::NotOwned;
    slot.ownership_issue=individual_identity::OwnershipIssue::None;
    slot.guard_suspended=false;
    SetCampHorseFleeBlock(slot,true);
    LogHorseState(slot,"resident",std::string("reason=")+why);
    return true;
}
bool HorseSlotOccupiedByPlayer(const Horse& horse,const PlayerHorses& player) {
    if(!PreservedHorseWorldCurrent())return true; // Unknown world cannot authorize a spawn or old-reference read.
    const auto occupies=[&](const Point& at) {
        return !std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z) ||
            (std::hypot(at.x-horse.stand.position.x,at.y-horse.stand.position.y)<4.0F &&
                std::abs(at.z-horse.stand.position.z)<3.0F);
    };
    const auto nearby=[&](Ped ped) {
        if(!Exists(ped))return false;
        const auto at=ENTITY::GET_ENTITY_COORDS(ped,true,false);
        return occupies({at.x,at.y,at.z});
    };
    // This list outlives g_horses. Validate only after the epoch fence above;
    // a numerically identical triple in another save is never inspected.
    for(auto it=g_preserved_horses.begin();it!=g_preserved_horses.end();) {
        if(it->world!=g_horse_world || !it->id.Valid() ||
            !world::Still({it->id.ped,it->id.model,it->id.address})) {
            it=g_preserved_horses.erase(it);continue;
        }
        const auto at=ENTITY::GET_ENTITY_COORDS(it->id.ped,true,false);it->at={at.x,at.y,at.z};
        if(occupies(it->at))return true;
        ++it;
    }
    for(const Ped own:{player.mount,player.active,player.saddle,player.temporary})if(nearby(own))return true;
    return false;
}
void TickPlayerHorseHandoffs(const Clock::time_point& now) {
    if(g_horses.empty() || !PreservedHorseWorldCurrent())return;
    const Player id=PLAYER::PLAYER_ID();
    const Ped temporary=PLAYER::_GET_TEMP_PLAYER_HORSE(id);
    Ped active=0,saddle=0,mount=0;
    if(now>=g_fast_horse_check){
        g_fast_horse_check=now+250ms;
        active=PLAYER::_GET_ACTIVE_HORSE_FOR_PLAYER(id);
        saddle=PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(id);
        const Ped player=PLAYER::PLAYER_PED_ID();mount=player?PED::GET_MOUNT(player):0;
    }
    // No world scan: only compare the native slots with the six exact
    // residents, then re-read ownership and identity before any handoff.
    for(auto& horse:g_horses)if(horse.ped &&
        (horse.ped==temporary || horse.ped==active || horse.ped==saddle || horse.ped==mount)) {
        if(!PreservedHorseWorldCurrent() || !SameCampHorse(horse))continue;
        if(const char* binding=PlayerHorses{}.Binding(horse.ped))HandCampHorseToPlayer(horse,binding,now);
    }
}
void MaintainHorses(const Clock::time_point& now) {
    if(!PreservedHorseWorldCurrent())return;
    TickPlayerHorseHandoffs(now);
    const bool race=horse_races::CampRaceUnderway() || horse_races::FixedHostIsArthurHere();
    if(race || g_recovering)CancelHorseModels(race?"race_underway":"Arthur_recovering");
    if(now<g_horse_check)return;
    g_horse_check=now+10s;
    const PlayerHorses player;
    for(auto& horse:g_horses) {
        if(!horse.ped)continue;
        if(!SameCampHorse(horse)) {
            LogHorseState(horse,"resident_lost",Exists(horse.ped)?"reason=identity_changed":"reason=entity_gone");
            ClearCampHorse(horse,now);continue;
        }
        const auto claim=CheckedPlayerClaim(horse,player);
        if(!claim){if(!PreservedHorseWorldCurrent())return;ClearCampHorse(horse,now);continue;}
        horse.ownership=claim->state;
        horse.ownership_issue=claim->issue;
        if(claim->state==individual_identity::Ownership::Owned) {
            HandCampHorseToPlayer(horse,claim->evidence,now);continue;
        }
        if(claim->state==individual_identity::Ownership::Unknown) {
            SuspendCampHorseProtection(horse,"maintenance");
            LogHorseState(horse,"ownership_unknown",claim->evidence);continue;
        }
        if(horse.state=="ownership_unknown")LogHorseState(horse,"resident","reason=ownership_confirmed_not_player");
        if(ENTITY::IS_ENTITY_DEAD(horse.ped)) {
            LogHorseState(horse,"resident_lost","reason=dead");
            Ped dead=horse.ped;PED::DELETE_PED(&dead);ClearCampHorse(horse,now);continue;
        }
        ResumeCampHorseProtection(horse);
        if(race || g_recovering)continue;
        const Vector3 at=ENTITY::GET_ENTITY_COORDS(horse.ped,true,false);
        const float strayed=std::hypot(at.x-horse.stand.position.x,at.y-horse.stand.position.y);
        if(!PED::IS_PED_RAGDOLL(horse.ped) && (!PED::IS_PED_USING_ANY_SCENARIO(horse.ped) || strayed>g_layout.horses_radius+6)) {
            if(strayed>g_layout.horses_radius+6)ENTITY::SET_ENTITY_COORDS(horse.ped,horse.stand.position.x,horse.stand.position.y,horse.stand.position.z,false,false,false,true);
            TASK::TASK_START_SCENARIO_IN_PLACE_HASH(horse.ped,Key(g_layout.horse_scenario),-1,true,0,horse.stand.heading,false);
        }
    }
    if(race || g_recovering || !Exists(g_arthur) || ENTITY::IS_ENTITY_DEAD(g_arthur)) {
        CancelHorseModels(race?"race_underway":"Arthur_recovering");return;
    }
    // One model request or creation per ten-second pass. This never enumerates
    // the world or adopts a same-model horse standing elsewhere.
    for(auto& horse:g_horses)if(!horse.ped && now>=horse.retry_after) {
        if(HorseSlotOccupiedByPlayer(horse,player)) {
            ReleaseHorseModel(horse);LogHorseState(horse,"waiting_for_slot","reason=player_horse_nearby");continue;
        }
        if(!horse.model_requested) {
            STREAMING::REQUEST_MODEL(horse.model,false);horse.model_requested=true;horse.requested_at=now;
            LogHorseState(horse,"model_requested","reason=vacant_slot");return;
        }
        if(!STREAMING::HAS_MODEL_LOADED(horse.model)) {
            if(now-horse.requested_at>=30s) {
                ReleaseHorseModel(horse);horse.retry_after=now+30s;
                LogHorseState(horse,"model_timeout","retry_seconds=30");
            }
            return;
        }
        if(!MakeCampHorse(horse,"vacant_slot"))horse.retry_after=now+30s;
        ReleaseHorseModel(horse);return;
    }
}
