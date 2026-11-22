// Private native navigation implementation; included by race_system.cpp.
bool BuildHostRuntimeLine(const race_pace::Dynamics& dynamics)
{
    if(!BuildRuntimeLine(HostLineAggression(),dynamics,g_host_terrain_caution,
        g_racing_line,g_pace_plan,g_host_avoidance,"host"))return false;
    g_host_navigation.passing.Reset();g_competitive_follower.RequestRouteOffset(0);
    g_pace_controller={};g_pace_command={};
    return true;
}
void RetireHostLine()
{
    // Clear the actual consumer, not only its control flag. Maintain may not
    // renew an old path after the current geometry has refused that path.
    g_competitive_follower.Reset();g_racing_line={};g_pace_plan={};g_host_avoidance={};
    g_pace_controller={};g_pace_command={};g_host_navigation.passing.Reset();
    g_fixed_progress_at={};g_fixed_stall_since={};
}
bool StartOriginalOpponentRoute(bool refresh_line)
{
    if (CurrentRules().Hosted()) {
        const bool required=RequiredRecordedLine();
        if(required)g_continuous_route=true;
        // grays1.c func_1406: copy float ABI and traffic flags, not the whole mission.
        // Retire the host's own idle task before the mounted traffic task takes
        // control. Do not leave an independent stand-still task on the horse.
        TASK::CLEAR_PED_TASKS(g_opponent_horse, false, false);
        g_racing_assist.Begin(g_opponent_horse);
        Vector3 destination=g_route_finish;
        const float speed=ENTITY::GET_ENTITY_SPEED(g_opponent_horse);
        g_navigation_pace.Reset(speed);
        g_navigation_telemetry.nav_speed_mps=speed;
        g_navigation_telemetry.reference_speed_mps=CurrentRules().uses_lap_engine?g_navigation_pace.peak_mps:speed;
        g_navigation_telemetry.nominal_lookahead_m=course::LookAheadMetres(g_navigation_telemetry.reference_speed_mps,CurrentRules().uses_lap_engine);
        g_navigation_telemetry.advance_threshold_m=course::HandoffMetres(g_navigation_telemetry.reference_speed_mps,CurrentRules().uses_lap_engine);
        if(HasRecordedCourse() && (!CurrentRules().uses_lap_engine || g_laps.Running())) {
            const auto position=ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false);
            if(!CurrentRules().uses_lap_engine)g_course_rival.Update(g_course,position);
            g_course_target=CurrentRules().uses_lap_engine?g_laps.RivalTarget(g_navigation_pace.peak_mps,position):
                course::LookAhead(g_course,g_course_rival,speed);
            // On a recovery use a nearer point rather than endlessly reissue
            // the same whole-map traffic path. Never relocate either actor.
            if(g_fixed_route_retries)g_course_target=std::min(g_course_target,
                g_course.Segment(g_course_rival.current.along+25)+1);
            destination=g_course.points[g_course_target];
        }
        if(HasRecordedCourse() && g_continuous_route && !g_continuous_attempted) {
            g_continuous_attempted=true;
            const auto dynamics=ReadRacePaceDynamics();
            // The executed points are the racing line's; everything ordered
            // stays on the centreline: the entry index is found on g_course
            // (node i of the line IS node i of g_course) and the follower is
            // ordered in centreline metres (line.reference), so its section
            // window keeps judging g_course_rival's centreline along. An
            // offset line is up to 150 m shorter per lap - judged in its own
            // metres it would trip WrongSection mid-race.
            // Recovery and traffic fallback restart the SAME race on the SAME frozen course.
            // Solving its line again cost 61 ms per restart on the Paint field race. A live
            // corridor/percent change explicitly requests refresh_line from the rebuild queue.
            const bool reusable=!refresh_line && !g_line_pins_required_refused && g_racing_line.verified &&
                !g_racing_line.path.points.empty() && !g_pace_plan.path.points.empty() &&
                g_racing_line.reference.size()==g_pace_plan.path.points.size();
            const bool prepared=reusable || BuildHostRuntimeLine(dynamics);
            if(!prepared)RetireHostLine();
            if(prepared) {
                const Vector3 origin=ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false);
                if(g_host_avoidance.path.points.size()!=g_pace_plan.path.points.size())
                    g_host_avoidance.Reset(g_pace_plan.path,g_racing_line.reference);
                const auto first=native_follower::EntryIndex(g_host_avoidance.path,g_course_rival.current.along,origin,&g_host_avoidance.reference,1.0F);
                if(g_competitive_follower.Start(g_opponent_rider,g_host_avoidance.path,
                    "TJC_AI_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(++g_course_serial),first,CircuitNow(),
                    &g_host_avoidance.reference,&origin)) {
                    g_fixed_last_task=Clock::now();
                    g_navigation_telemetry.Command(static_cast<int>(first),
                        {g_pace_plan.path.points[first].x,g_pace_plan.path.points[first].y,g_pace_plan.path.points[first].z},1);
                    Log("competitive driver: continuous mounted waypoint; racing-line nodes="+std::to_string(g_pace_plan.path.points.size())+
                        " chunk="+std::to_string(first)+".."+std::to_string(g_competitive_follower.chunk_last)+
                        " skill="+std::to_string(g_rider_skill)+" entry="+std::to_string(first)+
                        " flags="+std::to_string(native_follower::kFlags)+RacingLineSummary(g_racing_line,HostLineAggression())+
                        "; explicit ordered entry on centreline metres; anticipatory bend pacing; horse avoidance="+race_avoidance::kRaceName);
                    return true;
                }
            }
            if(required) {
                if(!prepared) {
                    AbortRace("required recorded passage has no legal line; no wager charged",true,Ender::Course);
                    return false;
                }
                // The geometry is valid. Let Maintain retry the native consumer
                // on its existing cooldown; traffic could bypass the bridge.
                Log("required recorded line retained; awaiting native recording retry");
                return true;
            }
            Log("competitive driver: custom recording unavailable; using traffic fallback, race remains active");
        }
        if(required)return true;
        MoveInTrafficToDestination(g_opponent_rider,destination,3.0F,-1.0F,3064);
        g_host_traffic_speed_owned=race_speed_policy::Enabled();
        if(g_host_traffic_speed_owned)
            invoke<Void>(0xBAAB791AA72C2821, g_opponent_rider, race_pace::Effort(g_rider_skill));
        g_fixed_last_task = Clock::now();
        g_navigation_telemetry.Command(HasRecordedCourse()?static_cast<int>(g_course_target):-1,
            {destination.x,destination.y,destination.z},g_fixed_route_retries?3:1);
        Log("fixed rival traffic task -> "+(HasRecordedCourse()?"course node "+std::to_string(g_course_target):
            SelectedFixedRoute().finish.name)+"; desired blend=3.0; horse avoidance="+race_avoidance::kRaceName);
        return true;
    }
    g_competitive_follower.ResetWaypointCommand();
    int closest_point = 0;
    Vector3 route_position{};
    Vector3 road_position{};
    const Vector3 opponent_position = ENTITY::GET_ENTITY_COORDS(g_opponent_rider, true, false);
    const bool has_entry = TASK::WAYPOINT_RECORDING_GET_CLOSEST_WAYPOINT(
                               g_shortcut_route_name.c_str(), opponent_position, &closest_point) &&
        TASK::WAYPOINT_RECORDING_GET_COORD(g_shortcut_route_name.c_str(), closest_point, &route_position) &&
        PATHFIND::GET_CLOSEST_VEHICLE_NODE(route_position, &road_position, 1, 3.0F, 0.0F);

    int sequence = 0;
    TASK::OPEN_SEQUENCE_TASK(&sequence);
    if (has_entry && DistanceSquared(opponent_position, road_position) > 100.0F) {
        TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(0, road_position, 2.0F, -1, 0.25F, 1048576, 40000.0F);
    }
    // The original racer follows the shortcut recording, while the player's GPS renders the full recording.
    TASK::TASK_FOLLOW_WAYPOINT_RECORDING(
        0, g_shortcut_route_name.c_str(), 0, kWaypointTaskFlags, -1, false, 0, -1);
    TASK::CLOSE_SEQUENCE_TASK(sequence);
    TASK::TASK_PERFORM_SEQUENCE(g_opponent_rider, sequence);
    TASK::CLEAR_SEQUENCE_TASK(&sequence);
    // Read the original continuous route for pace planning only. Never rewrite
    // its asset, select a new shortcut or replay recorded capture velocities.
    if(g_pace_plan.path.points.empty() && g_route_points>=2 && g_route_points<=1024) {
        std::vector<Vector3> nodes;
        for(int i=0;i<g_route_points;++i) {
            Vector3 p{};if(!TASK::WAYPOINT_RECORDING_GET_COORD(g_shortcut_route_name.c_str(),i,&p)){nodes.clear();break;}
            nodes.push_back(p);
        }
        course::Path path;
        if(path.Build(nodes))g_pace_plan.Build(path,ReadRacePaceDynamics());
    }
    g_fixed_last_task=Clock::now();
    g_navigation_telemetry.Command(0,{g_route_finish.x,g_route_finish.y,g_route_finish.z},1);
    return true;
}

void AdvanceRecordedRival()
{
    if(!CurrentRules().Hosted() || !HasRecordedCourse() || !g_opponent_rider || !g_opponent_horse || RequiredRecordedLine())return;
    if(CurrentRules().uses_lap_engine && !g_laps.Running())return;
    const Vector3 position=ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false);
    if(!CurrentRules().uses_lap_engine)g_course_rival.Update(g_course,position);
    if(g_course_target>=g_course.points.size())return;
    const float speed=ENTITY::GET_ENTITY_SPEED(g_opponent_horse);
    g_navigation_pace.Observe(speed);
    const float reference_speed=CurrentRules().uses_lap_engine?g_navigation_pace.peak_mps:speed;
    g_navigation_telemetry.nav_speed_mps=speed;
    g_navigation_telemetry.reference_speed_mps=reference_speed;
    g_navigation_telemetry.nominal_lookahead_m=course::LookAheadMetres(reference_speed,CurrentRules().uses_lap_engine);
    g_navigation_telemetry.advance_threshold_m=course::HandoffMetres(reference_speed,CurrentRules().uses_lap_engine);
    g_navigation_telemetry.candidate=-1;
    const float remaining=g_course.metres[g_course_target]-g_course_rival.current.along;
    // Do not shrink the handoff radius while the native destination task is
    // already braking. Keep this command's peak approach speed until replaced.
    const bool advance=remaining<course::HandoffMetres(reference_speed,CurrentRules().uses_lap_engine) ||
        destination::Distance(position,g_course.points[g_course_target])<15.0F;
    if(!advance){g_navigation_telemetry.decision=1;return;}
    if(Clock::now()-g_fixed_last_task<std::chrono::milliseconds(500)){g_navigation_telemetry.decision=2;return;}
    const auto target=CurrentRules().uses_lap_engine?g_laps.RivalTarget(reference_speed,position):course::LookAhead(g_course,g_course_rival,speed);
    g_navigation_telemetry.candidate=static_cast<int>(target);
    g_navigation_telemetry.decision=course::HandoffDecision(g_course_target,target,g_laps.rival_gate_recovery);
    if(g_navigation_telemetry.decision!=4)return;
    g_course_target=target;const auto point=g_course.points[target];
    STREAMING::REQUEST_COLLISION_AT_COORD(point);
    // Do NOT clear the horse's locomotion/standing task for every intermediate
    // point. Replace only the rider's traffic destination before he brakes.
    MoveInTrafficToDestination(g_opponent_rider,point,3.0F,-1.0F,3064);
    g_fixed_last_task=Clock::now();
    g_navigation_telemetry.Command(static_cast<int>(target),{point.x,point.y,point.z},2);
    g_navigation_pace.Reset(speed);
    if(g_course_rival.furthest>g_course_recovery_progress+50) {
        g_fixed_route_retries=0;g_course_recovery_progress=g_course_rival.furthest;
    }
}

void LoadStartingLineSettings()
{
    g_racing_line_percent=std::clamp(static_cast<int>(GetPrivateProfileIntA("RaceAI","RacingLinePercent",100,g_ini_path.c_str())),0,100);
    g_racing_line_margin=tracking_margin::Read(g_ini_path);
    g_gap_speed_caps=GetPrivateProfileIntA("RaceAI","GapSpeedCaps",0,g_ini_path.c_str())!=0;
    {
        char value[32]{};GetPrivateProfileStringA("RaceAI","GapCrawlKmh","22",value,sizeof value,g_ini_path.c_str());
        const float kmh=static_cast<float>(std::atof(value));
        g_gap_crawl_mps=(std::isfinite(kmh) && kmh>=10 && kmh<=60)?kmh/3.6F:obstacle_learning::kCrawlMps;
    }
    g_continuous_route=GetPrivateProfileIntA("RaceAI","ContinuousRecordedRoutes",1,g_ini_path.c_str())!=0;
    if(RequiredRecordedLine())g_continuous_route=true;
}

// First called under blackout after every field horse has been staged, then
// reused if F11 changes the width during the visible countdown. One expensive
// line per frame avoids the six-rider 375 ms GO frame seen on the Paint course;
