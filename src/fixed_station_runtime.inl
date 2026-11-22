// Fixed hosts only; original encounters keep their native distance policy.
Clock::time_point g_fixed_spawn_hidden_since{}, g_host_recovery_retry{};
bool g_host_recovering=false, g_fixed_spawn_visibility_wait=false;
bool g_fixed_spawn_site_wait=false;
constexpr float kFixedHostPreloadRadius=400.0F;
constexpr float kFixedHostRetentionRadius=450.0F;

bool FixedSpawnOutOfView(Vector3 point)
{
    return destination::Finite(point) && !HUD::IS_PAUSE_MENU_ACTIVE() &&
        !CAM::IS_SPHERE_VISIBLE(point.x,point.y,point.z+1.5F,4.0F);
}

bool FixedSpawnMayAppear(Vector3 point)
{
    if(!destination::Finite(point) || HUD::IS_PAUSE_MENU_ACTIVE())return false;
    // A camp race holds the screen black from the press until the grid is
    // built. Nothing can be seen arriving in that, so the host need not wait
    // for the player to look away - there is nothing to look with.
    if(g_camp_fade && CAM::IS_SCREEN_FADED_OUT())return true;
    if(FixedSpawnOutOfView(point))return true;
    // Fully prepared fixed hosts may appear in ordinary gameplay view even
    // on a near arrival. Requiring a distant/off-camera frame stranded nearby
    // station handoffs, whose preparation begins inside that distance already.
    // Magnified aim and cinematic cameras still wait for an ordinary view.
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player) ||
        !CAM::IS_GAMEPLAY_CAM_RENDERING() || PLAYER::IS_PLAYER_FREE_AIMING(PLAYER::PLAYER_ID()))return false;
    const float fov=CAM::GET_GAMEPLAY_CAM_FOV();
    return std::isfinite(fov) && fov>=50.0F;
}

bool FixedPlayerOverlaps(Vector3 point,float radius=3.5F)
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    const auto overlaps=[&](Ped entity) {
        if(!entity || !ENTITY::DOES_ENTITY_EXIST(entity))return false;
        const auto at=ENTITY::GET_ENTITY_COORDS(entity,true,false);
        return std::abs(at.z-point.z)<4.F && std::hypot(at.x-point.x,at.y-point.y)<radius;
    };
    return overlaps(player) || overlaps(player?PED::GET_MOUNT(player):0);
}

// No teleport, task flooding, revival, untying or stealing a player's mount.
bool MaintainFixedHostMount()
{
    const Ped rider=g_opponent_rider,horse=g_opponent_horse;
    const bool disturbed=PED::IS_PED_RAGDOLL(rider) || PED::IS_PED_FALLING(rider) ||
        TASK::IS_PED_GETTING_UP(rider) || PED::IS_PED_RAGDOLL(horse) || PED::IS_PED_FALLING(horse) ||
        PED::IS_PED_LASSOED(rider) || PED::IS_PED_HOGTIED(rider) || PED::IS_PED_LASSOED(horse) ||
        PED::IS_PED_IN_COMBAT(rider,0);
    if (PED::GET_MOUNT(rider)==horse && !disturbed) {
        if (g_host_recovering) {
            TASK::TASK_STAND_STILL(horse,-1);
            TASK::TASK_STAND_STILL(rider,-1);
            g_host_recovering=false;g_host_recovery_retry={};
            Log("fixed host remounted naturally; race/practice available again");
        }
        return true;
    }
    g_host_recovering=true;
    if (disturbed || PlayerHasTakenOpponentHorse() || PED::GET_MOUNT(rider)!=0 ||
        ScriptTaskActive(rider,"SCRIPT_TASK_MOUNT_ANIMAL") || Clock::now()<g_host_recovery_retry) return false;
    // Horse's original station idle survives most incidents. Reapply only after
    // both have recovered; the rider walks over using the native mount task.
    TASK::TASK_STAND_STILL(horse,-1);
    TASK::TASK_MOUNT_ANIMAL(rider,horse,10000,-1,2.0F,1,0,0);
    g_host_recovery_retry=Clock::now()+std::chrono::seconds(12);
    Log("fixed host recovering: native mount task, no teleport");
    return false;
}

void SelectFixedDirection(Station& station,const char* reason)
{
    if(!station.route)return;
    const std::size_t index=StationSlot(station);
    auto& variant=station.variant;
    const auto& definition=*station.route;
    const bool reverse=definition.random_direction && MISC::GET_RANDOM_INT_IN_RANGE(0,2)==1;
    g_fixed_directed_routes[index].reset();
    if(definition.random_direction)g_fixed_directed_routes[index]=route_config::Directed(definition,reverse);
    g_fixed_direction_renew[index]=false;InvalidateRules("direction published");
    const auto& resolved=ActiveFixedRoute(&variant);
    variant.encounter_anchor=resolved.host_point?resolved.host_point->position:resolved.start.position;
    variant.finish=resolved.finish.position;
    station.waiting_for_leave=false;
    if(station.start_blip && MAP::DOES_BLIP_EXIST(station.start_blip)) {
        MAP::SET_BLIP_COORDS(station.start_blip,station.camp_marker?*station.camp_marker:variant.encounter_anchor);
        SetBlipDisplayName(station.start_blip,FixedRouteTitle(&variant).c_str());
    }
    Log("fixed direction published site="+std::to_string(variant.id)+" "+resolved.start.id+" -> "+resolved.finish.id+
        " random="+std::to_string(definition.random_direction)+" reversed="+std::to_string(reverse)+
        " reason="+reason);
}

// One poller for every station. The only difference between stations is data:
// a station with a built-in route ships its file, so absence is diagnosed and
// the built-in or last valid route retained; any other station's file is
// optional until the user's surveyed endpoints exist. A diagnostic is logged
// once per reason and cleared by the next valid read.
void PollStationRoute(Station& station,bool initial=false)
{
    if (!g_fixed_race_enabled || station.config_path.empty() || (!initial && Clock::now()<station.poll)) return;
    station.poll=Clock::now()+std::chrono::seconds(1);
    // Nothing has been written since the last look: the bytes cannot have changed, and an overlay's
    // base counts as part of this station's file. Measured 2026-09-21: reading all 14 every second
    // was 5-10.5 ms in one frame.
    const auto stamp=file_stamp::Of(station.config_path);
    const auto base_stamp=station.base_path.empty()?file_stamp::Stamp{}:file_stamp::Of(station.base_path);
    const bool settled=station.observed && station.parsed && *station.observed==*station.parsed && station.issue.empty();
    if(!initial && settled && station.stamp.Known() && stamp==station.stamp && base_stamp==station.base_stamp)return;
    station.stamp=stamp;station.base_stamp=base_stamp;
    const std::string prefix=std::string("route config ")+station.key+": ";
    const auto issue=[&](const std::string& reason) {
        if (station.issue==reason) return;
        station.issue=reason;Log(prefix+reason+"; retaining last valid route");
    };
    std::ifstream input(station.config_path,std::ios::binary);
    if (!input)return;
    std::array<char,65537> buffer{};
    input.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
    if (input.bad() || input.gcount()>65536) { station.observed.reset(); issue("file read failed or exceeds 64 KiB"); return; }
    std::string bytes(buffer.data(),static_cast<std::size_t>(input.gcount()));
    // An overlay file is read together with the file it stands on, so an edit
    // to either is an edit to this station's route.
    if(const std::string base_name=route_config::BaseOf(bytes);!base_name.empty()) {
        if(base_name.find_first_of("/\\:")!=std::string::npos){station.observed.reset();issue("[Overlay] Base must be a file name beside this one");return;}
        const std::string base=(std::filesystem::path(station.config_path).parent_path()/base_name).string();
        std::ifstream base_input(base,std::ios::binary);
        std::array<char,65537> base_buffer{};
        if(base_input)base_input.read(base_buffer.data(),static_cast<std::streamsize>(base_buffer.size()));
        if(!base_input.is_open() || base_input.bad() || base_input.gcount()>65536){station.observed.reset();issue("[Overlay] Base="+base_name+" missing, unreadable or over 64 KiB");return;}
        bytes=route_config::Overlay(std::string(base_buffer.data(),static_cast<std::size_t>(base_input.gcount())),bytes);
        station.base_path=base;station.base_stamp=file_stamp::Of(base);
    }
    // On hot edits, debounce complete bytes across two polls, not only the mtime.
    if (!initial && (!station.observed || *station.observed!=bytes)) { station.observed=bytes; return; }
    station.observed=bytes;
    if (station.parsed && *station.parsed==bytes && station.issue.empty()) return;
    station.parsed=bytes;
    std::string error;
    const auto parsed=route_config::Parse(bytes,error);
    if (!parsed) { issue("rejected "+error); return; }
    station.issue.clear();
    if (station.route && route_config::SameRoute(*parsed,*station.route)) {
        station.pending.reset();
        Log(prefix+"valid, matches active revision");
    } else {
        station.pending=*parsed;
        Log(prefix+"queued "+parsed->start.id+" -> "+parsed->finish.id);
    }
}
void ApplyPendingStationRoute(Station& station)
{
    if (!station.pending || CurrentRules().Hosted() || showcase::Active() ||
        (g_state!=State::Scanning && g_state!=State::Ready)) return;
    // The variant address remains stable. No active task/GPS/finish is ever mutated:
    // the selected-course lifecycle holds the previous definition until released.
    // A new host at this station is a new acquaintance.
    if (!station.route || station.route->host!=station.pending->host) g_fixed_meetings[StationSlot(station)]={};
    station.route=*station.pending;station.pending.reset();
    if(std::string_view(station.key)=="valentine-circuit") {
        g_price_reference_metres=station.route->circuit?distance_pricing::ThreeLapMetres(*station.route->circuit):0;
        Log("price yardstick Valentine three laps metres="+std::to_string(g_price_reference_metres));
    }
    InvalidateRules((std::string(station.routes_file)+" revision").c_str());
    station.waiting_for_leave=false;
    SelectFixedDirection(station,"route revision");
    const auto& profile=FixedProfile(&station.variant);
    const auto& p=station.variant.finish;
    Log(std::string("route config ")+station.key+": applied "+station.route->start.id+" -> "+station.route->finish.id+
        " finish=("+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z)+
        ") host="+profile.key+" displayed speed="+std::to_string(profile.minimum)+".."+std::to_string(profile.maximum));
}
