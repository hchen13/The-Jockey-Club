// Private traffic and entities implementation; included by race_system.cpp.
// the same positions; this does not scan ambient traffic or steer the player.
const std::vector<following::Mount>& FollowingRoster(double now)
{
    static std::vector<following::Mount> roster;
    static double sampled=-1;
    static std::uint64_t epoch=0;
    if(sampled==now && epoch==g_race_epoch)return roster;
    sampled=now;epoch=g_race_epoch;roster.clear();
    if(!CurrentRules().Hosted() || g_state!=State::Racing || g_course.points.size()<2)return roster;
    const auto add=[&](Ped horse,Ped rider,const course::Progress& progress) {
        if(!progress.current.valid || progress.continuity_lost || !horse || !rider ||
            !ENTITY::DOES_ENTITY_EXIST(horse) || !ENTITY::DOES_ENTITY_EXIST(rider) ||
            ENTITY::IS_ENTITY_DEAD(horse) || ENTITY::IS_ENTITY_DEAD(rider) || PED::GET_MOUNT(rider)!=horse)return;
        roster.push_back({horse,ENTITY::GET_ENTITY_COORDS(horse,true,false),ENTITY::GET_ENTITY_FORWARD_VECTOR(horse),
            progress.current.along,ENTITY::GET_ENTITY_SPEED(horse),race_pace::At(g_course,progress.current.along).z,true});
    };
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!GetAutoSurveyState().running)add(PED::GET_MOUNT(player),player,g_course_player);
    // A live retired rider still occupies physical space while leaving.
    add(g_opponent_horse,g_opponent_rider,g_course_rival);
    for(const auto& e:g_extra_rivals)if(
        HorseIdentity(e.horse.horse)==e.horse && HorseIdentity(e.rider.horse)==e.rider)
        add(e.horse.horse,e.rider.horse,e.proxy);
    return roster;
}
void UpdateNativePassing(native_follower::Follower& driver,native_follower::Recovery& recovery,
    Ped horse,float along,const race_pace::Plan& plan,const race_pace::Command& command,double now,float free_target)
{
    if(!CurrentRules().uses_lap_engine || driver.simulation_suspended) {
        recovery.passing.Reset();driver.RequestRouteOffset(0);return;
    }
    const auto& roster=FollowingRoster(now);
    const auto self=std::find_if(roster.begin(),roster.end(),[&](const auto& m){return m.horse==horse;});
    if(self==roster.end()){recovery.passing.Reset();driver.RequestRouteOffset(0);return;}
    auto observed=*self;observed.along=along;
    // Native progress is a visited node, not a guaranteed steering target.
    // Include it in the inspection window when it runs ahead of physical progress.
    const auto next=static_cast<std::size_t>(std::max(0,driver.progress+1));
    const float readback_end=next<driver.metres.size()?driver.metres[next]:along;
    const bool allow_new=!command.braking && !command.vehicle_limited &&
        !command.vehicle_blocked && !command.terrain_limited;
    driver.RequestRouteOffset(recovery.passing.Update(g_course,g_laps.plan.course.strict_half_width,
        plan,&g_obstacles,observed,roster,now,readback_end,free_target,allow_new));
}
#include "../field_roster_runtime.inl"

bool FixedDirectionRetirementPending(const OriginalRaceVariant* site)
{
    return std::any_of(g_retired_opponents.begin(),g_retired_opponents.end(),[&](const auto& old){
        return old.source_site==site->id && (old.rider.Valid() || old.horse.Valid());});
}

void TickFixedDirectionRenewals()
{
    if(!g_fixed_race_enabled ||
        std::none_of(g_fixed_direction_renew.begin(),g_fixed_direction_renew.end(),[](bool b){return b;}) ||
        CurrentRules().Hosted() || (g_state!=State::Scanning && g_state!=State::Ready) ||
        showcase::Active() || HUD::IS_PAUSE_MENU_ACTIVE() ||
        MISC::GET_MISSION_FLAG() || NETWORK::NETWORK_IS_SESSION_ACTIVE())return;
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player) || ENTITY::IS_ENTITY_DEAD(player))return;
    const auto position=ENTITY::GET_ENTITY_COORDS(player,true,false);
    if(!destination::Finite(position))return;
    // Each station with its own marker: a new direction moves the marker with it.
    for(auto& station:g_stations) {
        const auto* site=&station.variant;
        if(!g_fixed_direction_renew[StationSlot(station)] || !station.route)continue;
        // Do not create a second host at the previous finish while the old
        // instance is still talking/leaving there. Retirement is identity-bound.
        if(FixedDirectionRetirementPending(site))continue;
        if(DistanceSquared(position,site->encounter_anchor)<50.0F*50.0F ||
            DistanceSquared(position,site->finish)<50.0F*50.0F)continue;
        SelectFixedDirection(station,"completed race cycle retired");
    }
}

void DeleteSpawnedEntities()
{
    ForgetRecycledRaceActors();
    g_competitive_follower.Reset(); // Detach any owned task before retirement/removal.
    const bool ranks_safe=RestoreOpponentRanks("entity cleanup");
    const bool taken=PlayerHasTakenOpponentHorse();
    RemoveBlip(g_opponent_blip);
    DestroyOfferPrompts();
    ReleaseResultScene();
    if (RetireSpawnedActors(ranks_safe,taken)) return;
    if (g_opponent_rider != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_rider)) {
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(g_opponent_rider, true, true);
        PED::DELETE_PED(&g_opponent_rider);
    }
    g_opponent_rider = 0;
    if (g_opponent_horse != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_horse)) {
        if (!taken) {
            Log("delete owned unclaimed race horse="+std::to_string(g_opponent_horse));
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(g_opponent_horse, true, true);
            PED::DELETE_PED(&g_opponent_horse);
        } else if (ranks_safe) {
            Log("preserve player-claimed race horse="+std::to_string(g_opponent_horse));
            firearm_guard::Release(g_opponent_horse);
            // Do not delete a horse underneath the player. It is now natural.
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_opponent_horse,false);
            ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&g_opponent_horse);
        } else {
            g_deferred_horse_release=true;
        }
    }
    g_opponent_horse = 0;
}

void ReleaseSpawnedEntities()
{
    ForgetRecycledRaceActors();
    g_competitive_follower.Reset();
    const bool ranks_safe=RestoreOpponentRanks("ambient release");
    const bool taken=PlayerHasTakenOpponentHorse();
    RemoveBlip(g_opponent_blip);
    DestroyOfferPrompts();
    ReleaseResultScene();
    if (RetireSpawnedActors(ranks_safe,taken)) return;
    if (!ranks_safe) g_deferred_horse_release=true;
    if (g_opponent_rider != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_rider)) {
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_opponent_rider, false);
        PED::SET_PED_CAN_BE_TARGETTED(g_opponent_rider, true);
        PED::SET_PED_KEEP_TASK(g_opponent_rider, true);
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&g_opponent_rider);
    }
    g_opponent_rider = 0;
    if (ranks_safe && g_opponent_horse != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_horse)) {
        if(taken)firearm_guard::Release(g_opponent_horse);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_opponent_horse, false);
        PED::SET_PED_KEEP_TASK(g_opponent_horse, true);
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&g_opponent_horse);
    }
    g_opponent_horse = 0;
}

void ReleaseRiderOutfitRequest()
{
    // The request ID is opaque: use the native validity check, not its signed value.
    if (g_rider_outfit_requested && PED::_IS_META_PED_OUTFIT_REQUEST_VALID(g_rider_outfit_request)) {
        PED::_RELEASE_META_PED_OUTFIT_REQUEST(g_rider_outfit_request);
    }
    g_rider_outfit_request = -1;
    g_rider_outfit_requested = false;
    g_rider_outfit_requested_at = Clock::time_point{};
}

void UseDefaultRiderOutfit(const std::string& reason)
{
    Log("original rider outfit fallback: " + reason + "; retaining model=" + g_rival->rider_model);
    ReleaseRiderOutfitRequest();
    g_use_default_rider_outfit = true;
}

void ReleaseModels()
{
    g_actor_allocation_started=g_actor_allocation_next=g_actor_allocation_log={};
    g_actor_allocation_attempts=0;g_actor_allocation_wait=false;
    ReleaseRiderOutfitRequest();
    g_use_default_rider_outfit = false;
    if (g_opponent_models_requested && g_rider_model != 0) {
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(g_rider_model);
    }
    if (g_opponent_models_requested && g_horse_model != 0) {
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(g_horse_model);
    }
    g_rider_model = 0;
    g_horse_model = 0;
    g_opponent_models_requested = false;
}
