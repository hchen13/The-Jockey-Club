// Private navigation telemetry implementation; included by race_system.cpp.

NavigationTelemetry GetNavigationTelemetry()
{
    auto v=g_navigation_telemetry;
    if(!v.command_serial || g_fixed_last_task==Clock::time_point{})return {};
    v.physical_readiness.reset();
    if(const auto* sample=g_host_physical_history.Latest(TrialForceToken(),HostPhysicalIdentity(),CircuitNow()))
        v.physical_readiness=*sample;
    v.valid=true;v.recorded=HasRecordedCourse();v.circuit=g_laps.Frozen();
    v.skill_percent=g_rider_skill;v.racing_line_percent=g_racing_line_percent;
    v.driver_mode=!CurrentRules().Hosted()?1:g_competitive_follower.control?2:g_continuous_attempted?3:0;
    v.pace_braking=g_pace_command.braking;v.pace_target_mps=g_pace_command.target_mps;v.pace_effort=g_pace_command.effort;
    v.terrain_limited=g_pace_command.terrain_limited;v.terrain_caution=g_host_terrain_caution;
    v.following_limited=g_pace_command.following_limited;v.following_horse=g_pace_command.following_horse;
    v.following_gap_m=g_pace_command.following_gap_m;
    v.terrain_height_error_m=TerrainHeightError(g_opponent_horse,g_course_rival.current.along);
    // Includes A->B traffic races, which have neither a recorded plan nor a
    // waypoint override. Their vector target must still be diagnosable.
    v.native_vector=g_competitive_follower.native_vector.Observe();
    if(CurrentRules().Hosted() && g_pace_plan.path.points.size()) {
        v.native_waypoint_progress=g_competitive_follower.progress;
        v.native_entry_index=g_competitive_follower.entry;
        v.native_fallback_reason=static_cast<int>(g_competitive_follower.failure);
        v.native_effort_readback=g_competitive_follower.override_readback;
        v.native_absolute_speed=g_competitive_follower.override_absolute;
        v.native_requested_mps=g_competitive_follower.override_absolute?g_competitive_follower.override_request:0;
        v.native_table=g_competitive_follower.speed_table.Observe();
        v.native_rate=g_competitive_follower.move_rate.observation;
        v.native_vector=g_competitive_follower.native_vector.Observe();
        if(v.driver_mode==2 && v.native_waypoint_progress>=0 && static_cast<std::size_t>(v.native_waypoint_progress)<g_pace_plan.path.points.size()) {
            // GET_PED_WAYPOINT_PROGRESS reports the node already REACHED, not
            // the one being ridden to. Logging it as the goal put every native
            // playback sample one node BEHIND the horse, so the recording read
            // like the stale-goal fallback even while playback was healthy.
            v.target=std::min(v.native_waypoint_progress+1,
                static_cast<int>(g_pace_plan.path.points.size())-1);
            const auto p=g_pace_plan.path.points[v.target];v.goal={p.x,p.y,p.z};
            // nav_speed_mps is written by the traffic driver only, so on native
            // playback every sample carried whatever the last fallback tick left.
            v.nav_speed_mps=g_opponent_horse && ENTITY::DOES_ENTITY_EXIST(g_opponent_horse)?
                ENTITY::GET_ENTITY_SPEED(g_opponent_horse):0;
        }
    } else if(!CurrentRules().Hosted() && g_state==State::Racing && g_opponent_rider &&
        TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(g_opponent_rider,nullptr)) {
        v.native_waypoint_progress=TASK::GET_PED_WAYPOINT_PROGRESS(g_opponent_rider);
        v.native_effort_readback=TASK::GET_PED_WAYPOINT_OVERRIDE_SPEED(g_opponent_rider);
        v.native_absolute_speed=g_competitive_follower.override_absolute;
        v.native_requested_mps=v.native_absolute_speed?g_competitive_follower.override_request:0;
        v.native_vector=g_competitive_follower.native_vector.Observe();
    }
    const auto telemetry_time=Clock::now();
    v.command_age_s=std::chrono::duration<double>(telemetry_time-g_fixed_last_task).count();
    v.driving=drive_diagnostics::Capture(g_pace_command,g_competitive_follower,g_host_navigation,
        g_host_avoidance.path,g_host_avoidance.reference,std::chrono::duration<double>(telemetry_time.time_since_epoch()).count(),
        g_laps.plan.course.strict_half_width);
    v.route_retries=g_fixed_route_retries;
    if(g_fixed_progress_at!=Clock::time_point{}) {
        v.progress_anchor_m=g_fixed_progress_along;
        v.progress_age_s=std::chrono::duration<double>(Clock::now()-g_fixed_progress_at).count();
        v.creeping=g_state==State::Racing && CurrentRules().Hosted() &&
            RivalTrafficCreeping(v.recorded,
                far_catchup::MayRecover(StateSeconds(),CircuitNow(),g_host_far_runner,
                    g_competitive_follower,far_catchup::PhysicalReady(g_opponent_horse)),
                g_competitive_follower.control,g_fixed_progress_at,Clock::now());
    }
    if(v.recorded) {
        v.node_count=static_cast<int>(g_course.points.size());v.course_length_m=g_course.Length();
        v.player_along_m=g_course_player.current.along;v.npc_along_m=g_course_rival.current.along;
        v.player_deviation_m=g_course_player.current.distance;v.npc_deviation_m=g_course_rival.current.distance;
        v.player_continuity_lost=g_course_player.continuity_lost;v.npc_continuity_lost=g_course_rival.continuity_lost;
        // The line being ridden against the line that was planned, both as
        // signed offsets from the centreline at the host's along.
        if(CurrentRules().Hosted() && !g_racing_line.path.points.empty()) {   // The line is denser than the course since 2026-09-17.
            v.line_max_offset_m=g_racing_line.max_offset;
            v.line_planned_offset_m=PlannedLineOffset(g_racing_line,v.npc_along_m);
            if(g_course_rival.current.valid && g_opponent_rider && ENTITY::DOES_ENTITY_EXIST(g_opponent_rider))
                v.npc_line_offset_m=SignedLateral(g_course,g_course_rival.current,ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false));
        }
        const auto& target_path=v.driver_mode==2?g_pace_plan.path:g_course;
        const auto& target_metres=v.driver_mode==2?g_pace_plan.reference:g_course.metres;
        if(v.target>=0 && static_cast<std::size_t>(v.target)<target_path.points.size() &&
            static_cast<std::size_t>(v.target)<target_metres.size()) {
            v.remaining_to_target_m=target_metres[v.target]-v.npc_along_m;
            if(v.target>0 && static_cast<std::size_t>(v.target+1)<target_path.points.size()) {
                const auto a=target_path.points[v.target-1],b=target_path.points[v.target],c=target_path.points[v.target+1];
                const float lengths=destination::Distance(a,b)*destination::Distance(b,c);
                if(lengths>.0001F)v.target_adjacent_cos=std::clamp(((b.x-a.x)*(c.x-b.x)+(b.y-a.y)*(c.y-b.y))/lengths,-1.0F,1.0F);
            }
        }
    }
    if(v.circuit) {
        const auto laps=g_laps.Hud();v.player_lap=laps.player_lap;v.npc_lap=laps.opponent_lap;v.total_laps=laps.total;
        v.gate_recovery=g_laps.rival_gate_recovery;
        if(g_laps.player.next_gate<g_laps.plan.course.gates.size()) {
            v.player_next_gate=static_cast<int>(g_laps.player.next_gate);
            v.player_next_gate_along_m=g_laps.plan.course.gates[g_laps.player.next_gate].along;
        }
        if(g_laps.opponent.next_gate<g_laps.plan.course.gates.size()) {
            v.npc_next_gate=static_cast<int>(g_laps.opponent.next_gate);
            v.next_gate_along_m=g_laps.plan.course.gates[g_laps.opponent.next_gate].along;
        }
    }
    return v;
}

// One horse length. A rival matching the player stride for stride is not ahead
// of them, and without a margin the place would flicker every frame.
constexpr float kAheadMetres=1.5F;
circuit::View GetLapView(){
    horse_diagnostics::Scope stage_cost("race: lap view");
    // The settings HUD is refreshed before Tick() retires an overlapping
    // invitation/race. A bookmaker scene owns the screen from the moment its
    // menu opens, so never feed a previous duel's native lap board into it.
    if(bookmaker_runtime::Active())return {};
    auto view=g_state==State::Racing?g_laps.Hud():circuit::View{};
    if(view.visible && CurrentRules().Hosted())view.opponent_name=FixedHostName();
    // Every race reports the same two facts, two horses included (the user, 2026-09-21): the player's
    // place, and the distance to the leading rival (+ the player trails, - the rival does).
    if(view.visible) {
        const std::string host_name=CurrentRules().Hosted()?FixedHostName():view.opponent_name;
        // Metres() saturates at the next gate, so ties at a gate or at the line
        // are real. A finisher therefore has to outrank every unfinished rider
        // explicitly, or a tie would quietly be handed to the player.
        const auto standing=[&](const circuit::Progress& p){
            return std::pair{p.Finished()?1:0,p.Metres(g_laps.plan.course)};
        };
        const auto player=standing(g_laps.player);
        const auto ahead=[&](std::pair<int,float> them){
            if(them.first!=player.first)return them.first>player.first;
            return them.second>player.second+kAheadMetres;
        };
        int place=1;
        float best=-1;bool have_best=false;
        const auto consider=[&](const circuit::Progress& p,int lap,const std::string& name){
            const auto s=standing(p);
            if(ahead(s))++place;
            const float along=s.second+(s.first?1e6F:0);
            if(!have_best || along>best){best=along;have_best=true;
                view.opponent_lap=lap;view.opponent_name=name;}
        };
        if(!g_main_rival_out)consider(g_laps.opponent,g_laps.plan.Current(g_laps.opponent)+1,host_name);
        for(std::size_t i=0;i<g_extra_rivals.size();++i) {
            const auto& e=g_extra_rivals[i];
            if(e.out || e.retiring)continue;
            consider(e.progress,g_laps.plan.Current(e.progress)+1,field_race::EntrantName(i));
        }
        view.player_place=place;
        // Show the same active field in both the place and the denominator.
        // A disqualified/retiring jockey has already left the ranking above.
        view.field_size=1+static_cast<int>(!g_main_rival_out);
        for(const auto& e:g_extra_rivals)if(!e.out && !e.retiring)++view.field_size;
        view.player_leads=place==1;
        if(have_best)view.gap_metres=(best>=1e6F?best-1e6F:best)-player.second;
    }
    return view;
}

// F11 > Race debug > Auto survey: the trial. The row shows whether one is
// armed for the next race or running, and its riders and falls; ENTER arms
// it, disarms it, or stops the one running.
