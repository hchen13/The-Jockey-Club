// Private actor loading implementation; included by race_system.cpp.
void TickLoadingRoute()
{
    if (CurrentRules().Hosted()) {
        if (!PlayerInSelectedSpawnBand()) { AbortRace("left fixed station pickup area", false); return; }
        if(!RouteDataAvailable()){AbortRace("fixed recorded course validation failed",true);return;}
        // A race chosen at the camp travels first, under its own blackout.
        if(g_camp_choice && !CampTravelReady())return;
        Log("fixed course ready: " + SelectedFixedRoute().start.name + " -> " + SelectedFixedRoute().finish.name +
            "; finish_pool_point=" + SelectedFixedRoute().finish.id + (HasRecordedCourse()?
            "; SHARED recorded centreline nodes="+std::to_string(g_course.points.size())+" length_m="+std::to_string(g_course.Length()):
            "; destination-only route (no course supplied)"));
        SetState(State::Ready);
        return;
    }
    if (!PlayerInSelectedSpawnBand()) {
        AbortRace("player left safe spawn band while route streamed", false);
        return;
    }
    const auto now = Clock::now();
    if (g_last_route_request == Clock::time_point{} ||
        std::chrono::duration<double>(now - g_last_route_request).count() >= 2.0) {
        TASK::REQUEST_WAYPOINT_RECORDING(g_route_name.c_str());
        if (g_shortcut_route_name != g_route_name) {
            TASK::REQUEST_WAYPOINT_RECORDING(g_shortcut_route_name.c_str());
        }
        g_route_requested = true;
        g_last_route_request = now;
    }
    if (!TASK::GET_IS_WAYPOINT_RECORDING_LOADED(g_route_name.c_str()) ||
        !TASK::GET_IS_WAYPOINT_RECORDING_LOADED(g_shortcut_route_name.c_str()) || !RouteDataAvailable()) {
        if (StateSeconds() > 30.0 && !g_route_delay_reported) {
            Log("route is still unavailable after 30 seconds: " + g_route_name);
            ShowObjective(i18n::Text("The course is not ready. The race cannot begin. Please try again later.", "赛道尚未准备妥当，比赛无法开始。请稍后再试。"));
            g_route_delay_reported = true;
            AbortRace("selected route loading timed out; other sites remain available", false);
        }
        return;
    }
    SetState(State::Ready);
    AddStartBlip();
    Log("prototype ready; ride into the marked area to trigger the invitation");
}

bool ValidateOpponentModel(Hash model,const char* role,const char* name)
{
    const bool in_cdimage=STREAMING::IS_MODEL_IN_CDIMAGE(model);
    const bool valid=STREAMING::IS_MODEL_VALID(model);
    if(in_cdimage && valid)return true;
    g_model_retry_after[g_variant->id]=Clock::now()+std::chrono::seconds(30);
    Log(std::string("invalid opponent ")+role+" model="+name+" hash="+std::to_string(model)+
        " in_cdimage="+std::to_string(in_cdimage)+" valid="+std::to_string(valid)+"; site retry delayed 30s");
    ShowObjective(i18n::Text("Your rival is not ready. The race cannot begin. Please try again later.", "对手尚未准备就绪，比赛无法开始。请稍后再试。"));
    AbortRace("opponent model validation failed; delayed retry",false);
    return false;
}

void BeginModelLoading()
{
    if(!ModelRetryAllowed(g_variant->id))return;
    const Ped player = PLAYER::PLAYER_PED_ID();
    const auto& rules=CurrentRules();
    // A route selection is not permission cached across save/load or read failure.
    if(!rules.Hosted() && native_progress::HoldsOurEncounters())return;
    if (player == 0 || !ENTITY::DOES_ENTITY_EXIST(player) || (!rules.Hosted() && !PED::IS_PED_ON_MOUNT(player))) {
        return;
    }
    if (MISC::GET_MISSION_FLAG()) {
        return;
    }
    g_player_mount = PED::GET_MOUNT(player);
    if (!rules.Hosted() && (g_player_mount == 0 || !ENTITY::DOES_ENTITY_EXIST(g_player_mount))) {
        g_player_mount = 0;
        return;
    }
    g_rider_model = MISC::GET_HASH_KEY(g_rival->rider_model);
    // Validate the rider before scanning/randomizing horse candidates.
    if(!ValidateOpponentModel(g_rider_model,"rider",g_rival->rider_model))return;
    g_horse_model = MISC::GET_HASH_KEY(g_rival->horse_model);
    // Nothing has been accepted at this point, so a precondition that is merely
    // unfinished must not surface as a cancelled race. Wait for it, and say once
    // which one we are waiting on instead of repeating an abort every frame.
    if (!PrepareHorsePlan()) {
        if (!g_plan_wait.empty()) {
            static std::string reported;
            if (reported != g_plan_wait) { reported = g_plan_wait; Log("waiting before offer: " + g_plan_wait); }
            return;
        }
        AbortRace("horse runtime catalog/selection unavailable; no guessed stats", true);
        return;
    }
    if(!ValidateOpponentModel(g_horse_model,"horse",g_fixed_selected_horse_name.c_str()))return;
    STREAMING::REQUEST_MODEL(g_rider_model, false);
    STREAMING::REQUEST_MODEL(g_horse_model, false);
    g_actor_allocation_started=g_actor_allocation_next=g_actor_allocation_log={};
    g_actor_allocation_attempts=0;g_actor_allocation_wait=false;
    g_opponent_models_requested = true;
    ReleaseRiderOutfitRequest();
    g_use_default_rider_outfit = rules.Hosted(); // Mary-Beth uses a complete model-local preset, not a racer outfit.
    // beat_horse_race.c func_142 waits for HAS_MODEL_LOADED before requesting an outfit.
    // REQUEST_MODEL only queues streaming; an outfit request here can fail on a cold load.
    Log("player entered original activation annulus; streaming bound opponent package");
    SetState(State::LoadingModels);
}

bool TryGetRouteDeviation(Ped ped, float& deviation)
{
    if (CurrentRules().Hosted()) {
        const auto position=ENTITY::GET_ENTITY_COORDS(ped,true,false);
        if(HasRecordedCourse()) {
            // Abandonment is distance from the SAME WHOLE course, not from an
            // old progress window. Finish/teleport eligibility remains separate.
            deviation=course::Project(g_course,position).distance;
        } else deviation=destination::DistanceFromSegment(position,g_route_start,g_route_finish);
        return std::isfinite(deviation);
    }
    int closest_point = 0;
    Vector3 route_position{};
    if (!TASK::WAYPOINT_RECORDING_GET_CLOSEST_WAYPOINT(
            g_shortcut_route_name.c_str(), ENTITY::GET_ENTITY_COORDS(ped, true, false), &closest_point) ||
        !TASK::WAYPOINT_RECORDING_GET_COORD(g_shortcut_route_name.c_str(), closest_point, &route_position)) {
        return false;
    }
    deviation = std::sqrt(DistanceSquared(ENTITY::GET_ENTITY_COORDS(ped, true, false), route_position));
    return std::isfinite(deviation);
}

// The staging point held by a ped that will not move on its own - an
// animal grazing, a townsfolk standing - the camp duel on 2026-09-20 waited
// on one for a minute and the race never began. After kSpawnClearSeconds
// occupied, whatever stands within kSpawnClearMetres of the point that is
// not the player, the player's mount or a mission entity of anyone's is
// taken away, as the circuit's clearance would take it at the start; a
// mission entity is asked to flee the spot instead. Logged with its model.
inline constexpr double kSpawnClearSeconds=8;
inline constexpr float kSpawnClearMetres=3.5F;
// Under the camp race's blackout the player sees nothing while the host's
// place is waited for; on 2026-09-20 that wait had no end and the game was
// closed on a black screen. Past this the race is given up and the screen
// given back, with a word.
inline constexpr double kSpawnBlackoutAbortSeconds=30;
void ClearFixedSpawnOccupants(Vector3 point)
{
    const auto items=ITEMSET::CREATE_ITEMSET(true);
    if(!ITEMSET::IS_ITEMSET_VALID(items))return;
    const int count=ENTITY::_GET_ENTITIES_NEAR_POINT(point.x,point.y,point.z,kSpawnClearMetres+1,items,1);
    const Ped player=PLAYER::PLAYER_PED_ID();
    const Ped mount=player?PED::GET_MOUNT(player):0;
    int removed=0,asked=0;
    if(count>=0 && count<4096 && count==ITEMSET::GET_ITEMSET_SIZE(items))for(int i=0;i<count;++i) {
        Ped e=static_cast<Ped>(ITEMSET::GET_INDEXED_ITEM_IN_ITEMSET(i,items));
        if(!e || !ENTITY::DOES_ENTITY_EXIST(e) || e==player || e==mount || PED::IS_PED_A_PLAYER(e))continue;
        const auto p=ENTITY::GET_ENTITY_COORDS(e,true,false);
        if(std::hypot(p.x-point.x,p.y-point.y)>kSpawnClearMetres)continue;
        const Hash model=ENTITY::GET_ENTITY_MODEL(e);
        if(ENTITY::IS_ENTITY_A_MISSION_ENTITY(e)) {
            TASK::TASK_SMART_FLEE_COORD(e,point.x,point.y,point.z,12.0F,8000,0,3.0F);++asked;
            Log("fixed station spawn: mission ped "+std::to_string(e)+" model="+std::to_string(model)+" on the staging point asked to move off");
        } else {
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e,true,true);PED::DELETE_PED(&e);++removed;
            Log("fixed station spawn: ped model="+std::to_string(model)+" standing on the staging point removed");
        }
    }
    ITEMSET::DESTROY_ITEMSET(items);
    if(!removed && !asked)Log("fixed station spawn: nothing within "+std::to_string(static_cast<int>(kSpawnClearMetres))+" m of the staging point to move, yet the game calls it occupied");
}
