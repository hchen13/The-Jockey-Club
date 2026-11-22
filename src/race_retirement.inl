// Private race actors outlive their race/dialogue session. Holding mission
// ownership prevents ambient cleanup while the player is still watching.
struct RetiredOpponent {
    race_horses::Identity rider{}, horse{};
    bool claimed=false, departure_started=false;
    Clock::time_point safe_since{};
    int source_site=-1; // Fixed direction may renew only after this owned pair retires.
    std::string inspection_model, inspection_name; // Preserve the same horse/name after the race state resets.
};
std::vector<RetiredOpponent> g_retired_opponents;
Clock::time_point g_retired_next_tick{};
constexpr std::size_t kRetiredOpponentLimit=8;

bool RetiredIdentityExists(const race_horses::Identity& id)
{
    return id.horse && HorseIdentity(id.horse)==id;
}
void ForgetRecycledRaceActors()
{
    for (std::size_t i=0;i<g_opponent_damage.size();++i) {
        Ped& ped=i==0?g_opponent_rider:g_opponent_horse;
        const auto& owner=g_opponent_damage[i].identity;
        if (ped && owner.Valid() && owner.horse==ped && HorseIdentity(ped)!=owner) {
            Log("cleanup ignored recycled active race actor="+std::to_string(ped));ped=0;
        }
    }
    // Offering/result dialogue already binds Mary before the race damage watch
    // starts. A replaced model is not a mod-owned actor to drive or retire.
    if (CurrentRules().Hosted() && g_mary.rider.Valid() && g_mary.rider.handle==g_opponent_rider &&
        MaryIdentity(g_opponent_rider)!=g_mary.rider) {
        Log("cleanup ignored replaced Mary-Beth session actor");g_opponent_rider=0;
    }
}
bool StartNaturalDeparture(Ped rider,Ped horse,bool claimed)
{
    const bool r=rider && ENTITY::DOES_ENTITY_EXIST(rider) && !ENTITY::IS_ENTITY_DEAD(rider);
    const bool h=horse && ENTITY::DOES_ENTITY_EXIST(horse) && !ENTITY::IS_ENTITY_DEAD(horse);
    if (r) {
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(rider,false);
        PED::SET_PED_CAN_BE_TARGETTED(rider,true);
        // Do not cancel a fall, undo a lasso, or steal a claimed horse back.
        if (PED::IS_PED_RAGDOLL(rider) || PED::IS_PED_FALLING(rider) || TASK::IS_PED_GETTING_UP(rider) ||
            PED::IS_PED_LASSOED(rider) || PED::IS_PED_HOGTIED(rider) || PED::IS_PED_BEING_HOGTIED(rider)) return false;
        if (h && !claimed) {
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(horse,false);
            if (PED::IS_PED_RAGDOLL(horse) || PED::IS_PED_FALLING(horse)) return false;
            TASK::CLEAR_PED_TASKS(horse,false,false); // Release the finish/pickup mounted idle.
            if (PED::GET_MOUNT(rider)!=horse) {
                int sequence=0;
                TASK::OPEN_SEQUENCE_TASK(&sequence);
                TASK::TASK_MOUNT_ANIMAL(0,horse,10000,-1,1.0F,1,0,0);
                MoveInTraffic(0,1.25F,8704);
                TASK::CLOSE_SEQUENCE_TASK(sequence);
                TASK::TASK_PERFORM_SEQUENCE(rider,sequence);
                TASK::CLEAR_SEQUENCE_TASK(&sequence);
            } else MoveInTraffic(rider,1.25F,8704);
        } else TASK::TASK_WANDER_STANDARD(rider,10.0F,10);
        PED::SET_PED_KEEP_TASK(rider,true);
    } else if (h && !claimed) {
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(horse,false);
        TASK::TASK_WANDER_STANDARD(horse,10.0F,10);
        PED::SET_PED_KEEP_TASK(horse,true);
    }
    return true;
}
// The player is somewhere else entirely: instant travel, a save. A pair that
// far away is in a world the game has let go of - no navmesh, no traffic,
// nothing loaded - and a mount-and-ride-off sequence there is a request the
// game cannot serve (2026-09-19: the sixth heap fault came two seconds after
// such a departure was asked for 2.4 km away). Released where it stands.
constexpr float kFarRetireMetres=600;
bool FarFromPlayer(const race_horses::Identity& id)
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!id.horse || !player || !ENTITY::DOES_ENTITY_EXIST(player) || !ENTITY::DOES_ENTITY_EXIST(id.horse))return false;
    return DistanceSquared(ENTITY::GET_ENTITY_COORDS(id.horse,true,false),ENTITY::GET_ENTITY_COORDS(player,true,false))>kFarRetireMetres*kFarRetireMetres;
}
void ReleaseFar(const race_horses::Identity& id)
{
    if(!RetiredIdentityExists(id))return;
    Ped ped=id.horse;
    firearm_guard::Release(ped);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped,false);
    ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&ped);
}
bool RetireSpawnedActors(bool ranks_safe,bool claimed)
{
    auto rider=HorseIdentity(g_opponent_rider);
    const auto horse=HorseIdentity(g_opponent_horse);
    if(ranks_safe && !claimed && (FarFromPlayer(rider) || FarFromPlayer(horse))) {
        ReleaseFar(rider);ReleaseFar(horse);
        g_opponent_rider=g_opponent_horse=0;
        Log("opponent retired far from the player; released where it stands, no departure");
        return true;
    }
    const auto presented=[](const race_horses::Identity& id) {
        return id.horse && (ENTITY::IS_ENTITY_VISIBLE(id.horse) || ENTITY::IS_ENTITY_ON_SCREEN(id.horse));
    };
    // A never-presented loading failure can still be cleaned immediately.
    // Any presented actor or pending rank restoration takes the guarded path.
    if (!presented(rider) && !presented(horse) && ranks_safe) return false;
    if (!rider.horse && !horse.horse) return false;
    if (rider.horse && !presented(rider)) {
        // A failed hidden-outfit preload must not leave an invisible rider
        // controlling the already-visible horse.
        Ped hidden=rider.horse;PED::DELETE_PED(&hidden);rider={};
    }
    RetiredOpponent retired{rider,horse,claimed,g_departure_task_started,{}};
    retired.source_site=CurrentRules().Hosted()?g_variant->id:-1;
    if(CurrentRules().Hosted()) {
        retired.inspection_model=g_fixed_selected_horse_name;
        retired.inspection_name=FixedHostName();
    }
    if (!retired.departure_started)
        retired.departure_started=StartNaturalDeparture(rider.horse,horse.horse,claimed);
    if (claimed && ranks_safe && horse.horse) {
        // Ownership handoff, not cleanup: the player's taken horse remains
        // natural and must not be driven or deleted by its former rival.
        Ped taken=horse.horse;
        firearm_guard::Release(taken);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(taken,false);
        ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&taken);
        retired.horse={};
    }
    g_retired_opponents.push_back(retired);
    g_opponent_rider=g_opponent_horse=0;
    Log("opponent retired under visual protection; rider="+std::to_string(rider.horse)+
        " horse="+std::to_string(horse.horse)+" retained_pairs="+std::to_string(g_retired_opponents.size()));
    return true;
}
bool RetiredActorSafe(const race_horses::Identity& id,Ped player)
{
    if (!RetiredIdentityExists(id)) return true;
    if (!player || !ENTITY::DOES_ENTITY_EXIST(player) || HUD::IS_PAUSE_MENU_ACTIVE()) return false;
    const Vector3 p=ENTITY::GET_ENTITY_COORDS(id.horse,true,false);
    const Vector3 listener=ENTITY::GET_ENTITY_COORDS(player,true,false);
    if (!destination::Finite(p) || !destination::Finite(listener)) return false;
    const float radius=std::max(120.0F,g_release_radius);
    return DistanceSquared(p,listener)>radius*radius && !ENTITY::IS_ENTITY_ON_SCREEN(id.horse) &&
        !CAM::IS_SPHERE_VISIBLE(p,3.0F);
}
void TickRetiredOpponents()
{
    const auto now=Clock::now();
    if (now<g_retired_next_tick) return;
    g_retired_next_tick=now+std::chrono::milliseconds(250);
    const Ped player=PLAYER::PLAYER_PED_ID();
    for (auto it=g_retired_opponents.begin();it!=g_retired_opponents.end();) {
        if (!RetiredIdentityExists(it->rider)) it->rider={};
        if (!RetiredIdentityExists(it->horse)) it->horse={};
        if (!it->rider.horse && !it->horse.horse) { it=g_retired_opponents.erase(it);continue; }
        if (player && it->horse.horse && (PED::GET_MOUNT(player)==it->horse.horse ||
            PED::_GET_RIDER_OF_MOUNT(it->horse.horse,true)==player ||
            (it->rider.horse && PED::GET_PEDS_JACKER(it->rider.horse)==player))) it->claimed=true;
        const bool ranks_pending=g_rank_lease.pending && it->horse==g_rank_lease.owner;
        if (it->claimed && it->horse.horse && !ranks_pending) {
            Ped horse=it->horse.horse;
            firearm_guard::Release(horse);
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(horse,false);
            ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&horse);
            it->horse={}; // Never force a former rider back onto the player's horse.
            it->departure_started=false;
        }
        if (!it->claimed && !ranks_pending && (FarFromPlayer(it->rider) || FarFromPlayer(it->horse))) {
            ReleaseFar(it->rider);ReleaseFar(it->horse);
            Log("retired opponent far from the player; released where it stands, no departure");
            it=g_retired_opponents.erase(it);continue;
        }
        if (!it->departure_started)
            it->departure_started=StartNaturalDeparture(it->rider.horse,it->horse.horse,it->claimed);
        if (ranks_pending || !RetiredActorSafe(it->rider,player) || !RetiredActorSafe(it->horse,player)) {
            it->safe_since={}; ++it;continue;
        }
        if (it->safe_since==Clock::time_point{}) it->safe_since=now;
        if (now-it->safe_since<std::chrono::seconds(3)) { ++it;continue; }
        for (const auto& id:{it->rider,it->horse}) if (RetiredIdentityExists(id)) {
            Ped ped=id.horse;
            PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped,false);
            PED::SET_PED_KEEP_TASK(ped,true);
            ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&ped); // No DELETE_PED, fade or teleport.
        }
        Log("retired opponent safely released: both actors distant and off-camera for 3 seconds");
        it=g_retired_opponents.erase(it);
    }
}
