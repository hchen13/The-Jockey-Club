// Private route loading implementation; included by race_system.cpp.
void EnsureRelationshipGroup()
{
    if (g_relationship_group != 0) {
        return;
    }
    if (!PED::ADD_RELATIONSHIP_GROUP("HORSE_SPEED_RACE_OPPONENT", &g_relationship_group)) {
        Log("warning: failed to create custom relationship group");
        return;
    }
    const Ped player = PLAYER::PLAYER_PED_ID();
    const Hash player_group = PED::GET_PED_RELATIONSHIP_GROUP_HASH(player);
    PED::SET_RELATIONSHIP_BETWEEN_GROUPS(kRelationshipNeutral, g_relationship_group, player_group);
    PED::SET_RELATIONSHIP_BETWEEN_GROUPS(kRelationshipNeutral, player_group, g_relationship_group);
    Log("created isolated neutral relationship group for spawned opponents");
}

// The [CorridorHalfWidth] key of the selected station: its slot in the table,
// empty for an original encounter. The file is re-read only when the selection
// changes or a freeze asks for it - the F11 page is its only writer, and every
// change it makes arrives here as a request.
const char* CorridorStationKey() {
    const auto* station=StationOf(g_variant);
    if(!station)return "";
    for(const auto& [key,course]:kCourseKeys)if(key==station->key)return course;
    return station->key;
}
std::optional<float> CorridorOverride(bool reread=false) {
    const std::string key=CorridorStationKey();
    if(reread || key!=g_corridor_key){g_corridor_key=key;g_corridor_override=corridor_override::Read(g_ini_path,key.c_str());}
    return g_corridor_override;
}
bool RouteDataAvailable()
{
    if (CurrentRules().Hosted()) {
        g_route_start = SelectedFixedRoute().start.position;
        g_route_finish = g_variant->finish;
        g_course={};
        if(const auto& definition=SelectedFixedRoute().circuit;definition) {
            const bool frozen_now=!g_laps.Frozen();
            if(frozen_now) {
                // The instance takes the station's override; the route keeps its
                // own default (SameRoute compares it, so it is never written back).
                auto instance=*definition;
                if(const auto width=CorridorOverride(true))instance.strict_half_width=corridor_override::Effective(width,definition->strict_half_width);
                if(!g_laps.Freeze(instance,[](int a,int b){return MISC::GET_RANDOM_INT_IN_RANGE(a,b);}))return false;
            }
            g_course=g_laps.plan.course.path;
            BindObstacles();BindLinePins();
            // Said once, when it is frozen: this is asked every tick while a camp
            // race travels (thirty lines a second, 2026-09-18).
            if(frozen_now)Log("circuit instance frozen X="+std::to_string(g_laps.plan.extra)+" total="+std::to_string(g_laps.plan.Total())+
                " distance_m="+std::to_string(g_course.Length())+" single_loop="+std::to_string(g_laps.plan.single_loop)+
                " half_width_m="+std::to_string(g_laps.plan.course.strict_half_width)+" (route "+std::to_string(definition->strict_half_width)+")"+
                "; race and practice share the same frozen plan");
        }
        if(!SelectedFixedRoute().course_points.empty()) {
            if(!g_course.Build(SelectedFixedRoute().course_points))return false;
            BindObstacles();
        }
        g_route_points=g_navigation_route_points=static_cast<int>(g_course.points.size());
        return true;
    }
    int points = 0;
    Vector3 start{};
    int navigation_points = 0;
    if (!TASK::WAYPOINT_RECORDING_GET_NUM_POINTS(g_shortcut_route_name.c_str(), &points) || points < 2 ||
        !TASK::WAYPOINT_RECORDING_GET_NUM_POINTS(g_route_name.c_str(), &navigation_points) || navigation_points < 2) {
        return false;
    }
    if (!TASK::WAYPOINT_RECORDING_GET_COORD(g_shortcut_route_name.c_str(), 0, &start)) {
        return false;
    }
    g_route_points = points;
    g_navigation_route_points = navigation_points;
    g_route_start = start;
    g_route_finish = g_variant->finish;
    if (g_finish_volume == 0) {
        g_finish_volume = VOLUME::_CREATE_VOLUME_BOX_WITH_CUSTOM_NAME(
            g_route_finish, OriginalFinishRotation(g_variant->id), OriginalFinishScale(g_variant->id),
            "TheJockeyClub original horse-race finish");
    }
    return true;
}

void SelectOriginalVariant(const OriginalRaceVariant& variant)
{
    // Only called while idle: the entire package remains immutable until cleanup/departure.
    ReleaseRoute();
    g_variant = &variant;
    g_rival = &RivalDefinition(variant.archetype);
    g_route_name = variant.full_route;
    g_shortcut_route_name = variant.shortcut_route;
    Log("selected original package #" + std::to_string(variant.id) + " rider=" + g_rival->rider_model +
        " horse=" + g_rival->horse_model + " goal=" + variant.goal_speech);
    SetState(State::LoadingRoute);
}

bool PlayerCanMeetRival()
{
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (player == 0 || !ENTITY::DOES_ENTITY_EXIST(player) || ENTITY::IS_ENTITY_DEAD(player) ||
        !PED::IS_PED_ON_MOUNT(player) || MISC::GET_MISSION_FLAG() || NETWORK::NETWORK_IS_SESSION_ACTIVE()) {
        return false;
    }
    const Ped mount = PED::GET_MOUNT(player);
    return mount != 0 && ENTITY::DOES_ENTITY_EXIST(mount) && !ENTITY::IS_ENTITY_DEAD(mount);
}

// True while a race chosen at Arthur's camp is still being set up. Every other
// host is met on the road: the player rides into the station's area, the host
// appears there, and the prompts are pressed face to face, so the pipeline
// checks throughout that the player is still standing where the host is. The
// camp is not that. Arthur is already in it, the prompt is on him, and the
// course starts 644 m away - the host waits on its start line and the grid
// teleports everyone there. So the checks that measure the player against the
// host's own spot have to stand aside from the press until the grid is built.
// A minute is all they stand aside for: if the race has not begun by then the
// ordinary checks come back and abort it in the ordinary way (2026-09-17).
// The camp is 644 m from the start line, so the race cannot be run where it is
// chosen. The screen goes black on the press; under it the player is set down
// behind the start line; and from there everything happens exactly where it
// happens at every other host, on ground that is streamed in because the
// player is standing on it. The screen lifts on the countdown (2026-09-17).
bool CampTravelReady()
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player))return false;
    Ped mount=PED::GET_MOUNT(player);
    const bool on_foot=!mount || !ENTITY::DOES_ENTITY_EXIST(mount);
    if(on_foot && (!g_camp_horse || !ENTITY::DOES_ENTITY_EXIST(g_camp_horse) || ENTITY::IS_ENTITY_DEAD(g_camp_horse)))return false;
    if(g_camp_moved) {
        const Ped expected=g_camp_horse?g_camp_horse:mount;
        if(!expected || !ENTITY::DOES_ENTITY_EXIST(expected))return false;
        if(PED::GET_MOUNT(player)!=expected) {
            // Put straight onto the horse did not take: mount it the ordinary
            // way, once, and keep waiting. The minute's grace bounds this.
            if(!g_camp_mount_tasked && Clock::now()-g_camp_moved_at>std::chrono::seconds(1)) {
                g_camp_mount_tasked=true;
                TASK::TASK_MOUNT_ANIMAL(player,expected,10000,-1,2.0F,1,0,0);
                Log("camp race: the rider was not on the horse after the journey; mounting it the ordinary way");
            }
            return false;
        }
        return ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(expected)!=0 &&
            ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(expected)==0;
    }
    if(!CAM::IS_SCREEN_FADED_OUT())return false;   // Nobody is moved in view.
    const auto& route=SelectedFixedRoute();
    // A short way along the recorded line: ground the course was ridden over,
    // rather than whatever lies behind the start, and far enough ahead that the
    // host may still be put down on the start line itself. The grid moves
    // everyone to their slot a moment later, so this only has to be safe.
    Vector3 park=route.start.position;
    bool found=false;
    if(route.circuit) {
        // A ring or a single recorded run keeps its geometry in `shared`;
        // independent laps keep the first lap in `entry`. The spot is the
        // exact distance along the line, between nodes, not the next node -
        // that node was 37 m out and the staging aborted (2026-09-17).
        const auto& line=(route.circuit->single_loop || route.circuit->single_trip)?
            route.circuit->shared:route.circuit->entry;
        float along=0;
        for(std::size_t i=1;i<line.size() && !found;++i) {
            const float step=std::hypot(line[i].x-line[i-1].x,line[i].y-line[i-1].y);
            if(!(step>0))continue;
            if(along+step>=kCampParkAhead) {
                const float t=(kCampParkAhead-along)/step;
                park={line[i-1].x+(line[i].x-line[i-1].x)*t,line[i-1].y+(line[i].y-line[i-1].y)*t,
                    line[i-1].z+(line[i].z-line[i-1].z)*t};
                found=true;
            }
            along+=step;
        }
    }
    if(!found) {
        // A route with no recorded line: step towards the finish instead.
        const Vector3 to{route.finish.position.x-park.x,route.finish.position.y-park.y,0};
        const float length=std::hypot(to.x,to.y);
        if(length<kCampParkAhead)return false;
        park={park.x+to.x/length*kCampParkAhead,park.y+to.y/length*kCampParkAhead,park.z};
    }
    STREAMING::REQUEST_COLLISION_AT_COORD(park);
    if(on_foot) {
        // Their own horse goes first, and they are put on it where it stands.
        mount=g_camp_horse;
        ENTITY::SET_ENTITY_COORDS(mount,park,false,false,true,false);
        if(route.grid_heading)ENTITY::SET_ENTITY_HEADING(mount,*route.grid_heading);
        ENTITY::SET_ENTITY_VELOCITY(mount,0,0,0);
        PED::SET_PED_ONTO_MOUNT(player,mount,-1,true);
    } else {
        ENTITY::SET_ENTITY_COORDS(mount,park,false,false,true,false);
        if(route.grid_heading)ENTITY::SET_ENTITY_HEADING(mount,*route.grid_heading);
        ENTITY::SET_ENTITY_VELOCITY(mount,0,0,0);
    }
    g_camp_moved=true;g_camp_moved_at=Clock::now();g_camp_mount_tasked=false;
    Log(std::string("camp race: ")+(on_foot?"player and their own horse":"player")+" set down "+
        std::to_string(static_cast<int>(kCampParkAhead))+
        " m along the line from the start, under the blackout; the host, the grid and the clearance follow there");
    return false;
}
bool CampChoicePending()
{
    if(!g_camp_choice)return false;
    if(Clock::now()<=g_camp_choice_by)return true;
    g_camp_choice=false;
    Log("camp race: not under way a minute after it was chosen; the ordinary checks take it from here");
    return false;
}
bool PlayerInSelectedSpawnBand()
{
    if (CurrentRules().Hosted()) {
        const Ped player = PLAYER::PLAYER_PED_ID();
        if (player == 0 || !ENTITY::DOES_ENTITY_EXIST(player) || ENTITY::IS_ENTITY_DEAD(player) ||
            MISC::GET_MISSION_FLAG() || NETWORK::NETWORK_IS_SESSION_ACTIVE()) return false;
        if (CampChoicePending()) return true;   // Chosen at the camp; nobody rode out to meet anybody.
        return DistanceSquared(ENTITY::GET_ENTITY_COORDS(player, true, false), g_variant->encounter_anchor) <=
                kFixedHostRetentionRadius*kFixedHostRetentionRadius;
    }
    if (!PlayerCanMeetRival()) {
        return false;
    }
    const float distance_squared = DistanceSquared(
        ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true, false), g_variant->encounter_anchor);
    return distance_squared >= g_activation_min_radius * g_activation_min_radius &&
        distance_squared <= g_offer_abandon_radius * g_offer_abandon_radius;
}
