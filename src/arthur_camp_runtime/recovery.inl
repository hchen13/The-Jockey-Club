// Private implementation of arthur_camp.cpp; included inside its original namespace.
// World replacement, teardown, recovery and stood-down actors.
// Nothing this camp made is here any more: a save came up and the world was
// built again underneath it. Every handle, blip, prompt and scenario point we
// hold now belongs to whatever the new world put in its place, so none of
// them may be used - not to delete, not to release, not even to look at.
// Checked only when Arthur has gone, which he always has when this happens.
bool WorldReplaced() {
    if(g_objects.empty())return false;
    // The props are frozen where they were put and never move, so one of them
    // still being ours settles it: a couple of natives a frame, the whole list
    // only when the sample comes back empty.
    const std::size_t step=std::max<std::size_t>(1,g_objects.size()/8);
    for(std::size_t i=0;i<g_objects.size();i+=step)
        if(StillOurs(g_objects[i].handle,g_objects[i].model,g_objects[i].at))return false;
    for(const auto& p:g_objects)if(StillOurs(p.handle,p.model,p.at))return false;
    return true;
}
// Drop the whole camp without touching the world. The one thing still worth
// asking for is the removal of our model hides: a hide is named by a model
// and a place, never by a handle, so asking to lift one that is already gone
// costs nothing and lifting one that survived is exactly right.
void Forget(const std::string& why) {
    DiscardPreservedHorses();g_horse_world=0;g_horse_world_invalidated=false;
    ShowMapPieces();ShowOutside(false);   // Named by model and place; the handles are the new world's.
    ForgetOffer(); // Drop old-world prompt/entity handles without unregistering or deleting them.
    g_locker={};
    g_locker_flow=LockerFlow::Idle;
    g_seat_points.clear();g_seat_point_of.clear();g_activity_point.clear();g_has_pending=false;g_walking=false;g_walking_to_door=false;
    ReleaseScenarioAssets();
    g_objects.clear();g_placed.clear();
    firearm_guard::Discard(g_arthur);
    CancelHorseModels("world_replaced");
    for(const auto& horse:g_horses)firearm_guard::Discard(horse.ped);
    g_horses.clear();
    g_arthur=0;g_targeted=false;g_recovering=false;g_stood_down=false;g_racing_horse=0;g_horse_stood_down=false;
    ForgetMary(false);   // A handle in the new world is somebody else's.
    g_blip=0;g_markers_on=false;ForgetRaceMarkers(); // A blip id in the new world is somebody else's; the markers are made again.
    g_ipls.clear();           // ...and the new world switches on its own map files.
    ReleaseAssets();
    Log("camp forgotten: "+why);
    g_phase=Phase::Idle;g_presence=Presence::Absent;g_rebuild=false;g_dressing_done=true;
    g_within_reach=false;g_return_dressing_stage=0;g_return_dressing_at={};
}
void Unload(const std::string& why) {
    if(SyncPreservedHorseWorld() && g_phase!=Phase::Idle) {
        Forget("loaded-world timeline changed before teardown; old references discarded");return;
    }
    if(!PreservedHorseWorldCurrent() && g_phase!=Phase::Idle) {
        Forget("loaded-world timeline unavailable before teardown; old references discarded");return;
    }
    if(g_phase==Phase::Live && WorldReplaced()) {
        Forget("a save came up before scene teardown; old entity handles left alone");return;
    }
    const PlayerHorses player;
    ReleaseInteractions();
    ShowMapPieces();ShowOutside(true);ReleaseOffer();
    for(const int point:g_seat_points)TASK::_DELETE_SCENARIO_POINT(point);
    g_seat_points.clear();g_seat_point_of.clear();g_activity_point.clear();g_recovering=false;
    g_has_pending=false;g_walking=false;g_walking_to_door=false;g_stood_down=false;
    ReleaseScenarioAssets();
    int kept=0;
    for(auto& placed:g_objects) {
        if(!StillOurs(placed.handle,placed.model,placed.at)){if(Exists(placed.handle))++kept;continue;}
        Object o=placed.handle;ENTITY::SET_ENTITY_AS_MISSION_ENTITY(o,true,true);OBJECT::DELETE_OBJECT(&o);
    }
    g_objects.clear();g_placed.clear();
    ForgetMary(true);
    if(g_arthur) {
        firearm_guard::Discard(g_arthur); // Teardown deletes him; no restoration retry may outlive that allocation.
        if(StillOurs(g_arthur,Key("PLAYER_ZERO"),Point{},0)){Ped p=g_arthur;PED::DELETE_PED(&p);}
        else if(Exists(g_arthur))++kept;
        g_arthur=0;
    }
    CancelHorseModels("scene_teardown");
    int ownership_unknown=0;
    for(auto& horse:g_horses) {
        if(!PreservedHorseWorldCurrent()){Forget("world changed during teardown ownership probe; old references discarded");return;}
        if(!SameCampHorse(horse)) {
            firearm_guard::Discard(horse.ped);
            if(horse.ped)++kept;
            continue;
        }
        // Feeding/brushing can claim a temporary horse without ever mounting
        // it. Check native bindings and the stable saved GUID before deletion.
        const auto claim=CheckedPlayerClaim(horse,player);
        if(!claim) {
            if(!PreservedHorseWorldCurrent()){Forget("world changed during teardown ownership probe; old references discarded");return;}
            SetCampHorseFleeBlock(horse,false);
            ++kept;continue;
        }
        horse.ownership=claim->state;
        if(claim->state==individual_identity::Ownership::Owned) {
            HandCampHorseToPlayer(horse,claim->evidence,Clock::now());continue;
        }
        if(claim->state==individual_identity::Ownership::Unknown) {
            SetCampHorseFleeBlock(horse,false);
            SuspendCampHorseProtection(horse,"scene_teardown");
            PreserveCampHorse(horse);
            LogHorseState(horse,"ownership_unknown","scene_teardown_kept_entity "+claim->evidence);
            ++ownership_unknown;continue;
        }
        // The exact NotOwned resident is about to be deleted. No restoration
        // retry may outlive it and later touch a recycled identity.
        firearm_guard::Discard(horse.ped);
        Ped p=horse.ped;PED::DELETE_PED(&p);
    }
    g_horses.clear();
    g_racing_horse=0;g_horse_stood_down=false;
    if(kept)Log(std::to_string(kept)+" handles were no longer what this camp made (a save came up under it); left alone");
    if(ownership_unknown)Log(std::to_string(ownership_unknown)+" horse ownership probes were unknown; those entities were left alone");
    ReleaseIpls();   // The marker stays: it is the layout's, not the camp's.
    ReleaseAssets();
    if(g_phase!=Phase::Idle)Log("camp down: "+why);
    g_phase=Phase::Idle;g_presence=Presence::Absent;g_rebuild=false;g_dressing_done=true;
    g_within_reach=false;g_return_dressing_stage=0;g_return_dressing_at={};
}
// He was killed or streamed away. The camp stands; his model is asked for
// again and he is put back at his activity. Only if that fails is the camp
// struck - before, the whole camp went with him and did not come back while
// the player stood looking at it (2026-09-16).
void TickRecovery(const Clock::time_point& now) {
    std::string missing;
    if(AssetsReady(missing)) {
        const std::size_t a=std::min(g_activity,g_layout.scenes.size()-1);
        const Pose spot=Grounded(ActivityPose(g_layout.scenes[a]),true,0);
        const bool made=MakeArthur(spot);
        ReleaseAssets();g_recovering=false;
        if(made){Log("Arthur is back at his "+std::string(ActivityName(a)));StartActivity(a,true,"back where he was");return;}
        Log("Arthur could not be made again; striking the camp");
        Unload("Arthur lost");g_retry_after=now+60s;return;
    }
    if(now-g_recover_since<=30s)return;
    ReleaseAssets();g_recovering=false;
    Log("Arthur's model did not load in 30 s ("+missing+"); striking the camp");
    Unload("Arthur lost");g_retry_after=now+60s;
}
void StandDown(Ped ped,bool away) {
    if(!Exists(ped))return;
    ENTITY::SET_ENTITY_VISIBLE(ped,!away);
    ENTITY::SET_ENTITY_COLLISION(ped,!away,!away);
    PED::SET_PED_CAN_BE_TARGETTED(ped,!away);
}
void TickStandDown() {
    if(!PreservedHorseWorldCurrent())return;
    const bool theirs=horse_races::FixedHostIsArthurHere();
    if(theirs!=g_stood_down) {
        g_stood_down=theirs;
        StandDown(g_arthur,theirs);
        Log(theirs?"a race host Arthur is out; the camp's own stands down":"the race host Arthur is gone; the camp's own is back");
    }
    // The horse he was asked to ride is out there under him; this one waits.
    const auto resident=std::find_if(g_horses.begin(),g_horses.end(),[](const Horse& h){return h.ped==g_racing_horse;});
    const bool horse_same=resident!=g_horses.end() && SameCampHorse(*resident);
    bool horse_away=theirs && horse_same && AvailableCampHorse(*resident,PlayerHorses{});
    if(horse_away && !g_horse_stood_down && !RecheckCampHorseAction(*resident,"first_stand_down")) {
        horse_away=false;g_racing_horse=0;
    }
    if(horse_away!=g_horse_stood_down) {
        g_horse_stood_down=horse_away;
        if(horse_same)StandDown(g_racing_horse,horse_away);
        if(!horse_away)g_racing_horse=0;
    }
}
