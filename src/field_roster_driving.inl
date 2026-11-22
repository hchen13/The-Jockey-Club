// Only constructed at a field elimination. Append timings to the existing
// out-event log rather than issuing another synchronous write for telemetry.
struct FieldExitTiming {
    using Timer=std::chrono::steady_clock;
    Timer::time_point notice_started=Timer::now(),cleanup_started{};
    double notice_ms=0;
    void EndNotice(){notice_ms=std::chrono::duration<double,std::milli>(Timer::now()-notice_started).count();}
    void BeginCleanup(){cleanup_started=Timer::now();}
    std::string EndCleanup() const {
        const double cleanup_ms=std::chrono::duration<double,std::milli>(Timer::now()-cleanup_started).count();
        if(notice_ms+cleanup_ms<2.0)return {};
        return " notice_ms="+std::to_string(notice_ms)+" cleanup_ms="+std::to_string(cleanup_ms);
    }
};

float EntrantLineAggression(field_race::Entrant& e)
{
    if(e.line_jitter<0)e.line_jitter=std::clamp(static_cast<float>(MISC::GET_RANDOM_INT_IN_RANGE(0,1000))/999.0F,0.0F,1.0F);
    return HostLineAggression()*(1-.2F*e.line_jitter);
}
// A driver must see one complete generation of geometry, pace and execution
// storage. Failed candidates never partially replace its published plan.
bool BuildRuntimeLine(float aggression,const race_pace::Dynamics& dynamics,float caution,
    racing_line::Result& line,race_pace::Plan& pace,live_overtake::Session& avoidance,const char* who)
{
    const auto previous_revision=g_obstacles_line_revision;
    racing_line::Result candidate;
    if(!BuildRacingLine(g_course,aggression,candidate,dynamics,caution)) {
        g_obstacles_line_revision=previous_revision;return false;
    }
    std::vector<float> caps=LineCaps(candidate,dynamics);
    const bool any=std::any_of(caps.begin(),caps.end(),[&](float c){return c<dynamics.top_mps-.01F;});
    race_pace::Plan candidate_pace;live_overtake::Session candidate_avoidance;
    if(!candidate_pace.Build(candidate.path,dynamics,candidate.reference,any?&caps:nullptr,
        caution>=0?&g_terrain_profile:nullptr,caution) ||
        !candidate_avoidance.Reset(candidate_pace.path,candidate.reference)) {
        g_obstacles_line_revision=previous_revision;return false;
    }
    line=std::move(candidate);pace=std::move(candidate_pace);avoidance=std::move(candidate_avoidance);
    if(any)LogRoomCaps(who,caps,line.reference,dynamics.top_mps);
    if(caution>=0)Log(std::string("terrain plan: rider=")+who+
        " caution="+std::to_string(caution)+" recorded_nodes="+std::to_string(line.pinned_nodes));
    return true;
}
// The entrant's line and pace plan for the corridor width in force. Same
// build as the host's; the plan speaks centreline metres through reference.
bool BuildEntrantLine(field_race::Entrant& e)
{
    const auto dynamics=ReadRacePaceDynamics(e.horse.horse);
    const float aggression=EntrantLineAggression(e);
    if(!BuildRuntimeLine(aggression,dynamics,e.terrain_caution,e.line,e.pace,e.avoidance,"entrant"))return false;
    e.line_aggression=aggression;
    e.navigation.passing.Reset();e.follower.RequestRouteOffset(0);
    e.controller={};e.command={};
    return true;
}
void RetireEntrantLine(field_race::Entrant& e)
{
    e.follower.Reset();e.line={};e.pace={};e.avoidance={};e.controller={};e.command={};
    e.navigation.passing.Reset();e.stalled_since=-1;
    e.traffic_target=0; // The retired consumer no longer owns its former goal.
}
// Start (or restart) an entrant's native driver on its line. The entry is
// found on g_course - node i of the line is node i of g_course - and the
// follower is ordered in centreline metres, so its section window keeps
// judging the centreline `along` every caller already has.
bool StartExtraDriver(field_race::Entrant& e,float along,double now)
{
    // The line is denser than the recorded course since 2026-09-17, so the
    // follower is started on it and enters it by its own nodes.
    if(!g_continuous_route || !e.line.verified || e.pace.path.points.size()<2 ||
        e.avoidance.path.points.size()!=e.pace.path.points.size())return false;
    const Vector3 origin=ENTITY::GET_ENTITY_COORDS(e.rider.horse,true,false);
    return e.follower.Start(e.rider.horse,e.avoidance.path,"TJC_EXTRA_"+
        std::to_string(GetCurrentProcessId())+"_"+std::to_string(++g_course_serial),
        native_follower::EntryIndex(e.avoidance.path,along,origin,&e.avoidance.reference,1.0F),now,
        &e.avoidance.reference,&origin);
}
bool StartExtraRacers()
{
    horse_diagnostics::Scope stage_cost("race: field start");
    if(!CurrentRules().Field())return true;
    const double now=CircuitNow();
    for(auto& e:g_extra_rivals) {
        if(HorseIdentity(e.horse.horse)!=e.horse || HorseIdentity(e.rider.horse)!=e.rider ||
            PED::GET_MOUNT(e.rider.horse)!=e.horse.horse ||
            !e.progress.Start(g_laps.plan.course,1,ENTITY::GET_ENTITY_COORDS(e.rider.horse,true,false),now,2))return false;
        TASK::CLEAR_PED_TASKS(e.horse.horse,false,false);
        e.racing_assist.Begin(e.horse.horse);
        if(g_start_lines.ready && HasRecordedCourse()) {
            if(!e.line.verified || e.pace.path.points.size()<2 ||
                e.line.reference.size()!=e.pace.path.points.size())return false;
        } else if(!BuildEntrantLine(e))return false;
        if(!e.avoidance.Reset(e.pace.path,e.line.reference))return false;
        // Same native-recording / traffic fallback policy as the host. A
        // missing custom AI consumer must not cancel an otherwise valid race.
        if(g_continuous_route && !StartExtraDriver(e,0,now))
            Log(std::string("extra custom AI unavailable; ")+(RequiredRecordedLine()?"awaiting recording retry rider=":"using bounded forward traffic navigation rider=")+std::to_string(e.rider.horse));
        g_laps.Export(e.progress,ENTITY::GET_ENTITY_COORDS(e.rider.horse,true,false),e.proxy);
        e.blip=MAP::BLIP_ADD_FOR_ENTITY(kOriginalRivalBlipStyle,e.rider.horse);
        e.task_at=now;
        Log("Blackwater entrant GO rider="+std::to_string(e.rider.horse)+" horse="+std::to_string(e.horse.horse)+
            " displayed="+std::to_string(e.target.speed+1)+"/"+std::to_string(e.target.acceleration+1)+
            " entry="+std::to_string(e.follower.entry)+RacingLineSummary(e.line,e.line_aggression)+
            " horse avoidance="+race_avoidance::kRaceName);
    }
    return true;
}
circuit::Outcome UpdateMultiProgress(Vector3 player,Vector3 main,double now)
{
    const auto p=g_laps.player.Update(g_laps.plan.course,player,now);
    std::vector<circuit::Advance> rivals;
    std::vector<int> rival_ids; // -1 is the host; nonnegative values are the stable field indices.
    if(!g_main_rival_out) {
        const auto o=g_laps.opponent.Update(g_laps.plan.course,main,now);rivals.push_back(o);
        rival_ids.push_back(-1);
        if(o.disqualification_fraction) {
            FieldExitTiming timing;
            if(!GetAutoSurveyState().running)race_help::Elimination(race_elimination::Message(FixedHostName(),
                o.section_jump?race_elimination::Reason::SkippedSection:race_elimination::Reason::LeftCourse,
                i18n::Chinese()));
            timing.EndNotice();timing.BeginCleanup();
            g_main_rival_out=true;g_racing_assist.Reset();g_competitive_follower.Reset();
            RestoreOpponentRanks(o.section_jump?"host skipped a section":"host left circuit bounds");
            StartNaturalDeparture(g_opponent_rider,g_opponent_horse,false);
            const auto elapsed=timing.EndCleanup();
            Log("Field host out rider="+std::to_string(g_opponent_rider)+
                (o.section_jump?" reason=skipped-section":" reason=left-corridor")+
                " along_m="+std::to_string(g_laps.opponent.offence_along)+" game_ms="+std::to_string(MISC::GET_GAME_TIMER())+elapsed);
        }
    }
    bool all_out=g_main_rival_out;
    for(std::size_t entrant_index=0;entrant_index<g_extra_rivals.size();++entrant_index) {
        auto& e=g_extra_rivals[entrant_index];
        if(e.out){e.racing_assist.Reset();e.lease.Restore(HorseIdentity,ReadBaseRank,WriteBaseRank);continue;}
        const bool same_rider=HorseIdentity(e.rider.horse)==e.rider;
        const bool same_horse=HorseIdentity(e.horse.horse)==e.horse;
        const bool rider_dead=same_rider && ENTITY::IS_ENTITY_DEAD(e.rider.horse);
        const bool horse_dead=same_horse && ENTITY::IS_ENTITY_DEAD(e.horse.horse);
        const bool dismounted=same_rider && same_horse && !rider_dead && !horse_dead &&
            PED::GET_MOUNT(e.rider.horse)!=e.horse.horse;
        if(!same_rider || !same_horse || rider_dead || horse_dead || dismounted) {
            FieldExitTiming timing;
            if(!GetAutoSurveyState().running) {
                const bool falling=same_rider && !rider_dead &&
                    (PED::IS_PED_FALLING(e.rider.horse)||PED::IS_PED_RAGDOLL(e.rider.horse)||TASK::IS_PED_GETTING_UP(e.rider.horse));
                race_help::Elimination(race_elimination::Message(field_race::EntrantName(entrant_index),
                    race_elimination::Incident(rider_dead,horse_dead,!same_rider||!same_horse,falling),i18n::Chinese()));
            }
            timing.EndNotice();timing.BeginCleanup();
            e.out=true;e.racing_assist.Reset();e.chase.Reset();e.command.catchup_fraction=0;e.far_runner.Reset();e.follower.Reset();e.lease.Restore(HorseIdentity,ReadBaseRank,WriteBaseRank);
            if(HorseIdentity(e.rider.horse)==e.rider && HorseIdentity(e.horse.horse)==e.horse)
                StartNaturalDeparture(e.rider.horse,e.horse.horse,false);
            const auto elapsed=timing.EndCleanup();
            Log("Field entrant out rider="+std::to_string(e.rider.horse)+" reason="+
                (!same_rider?"rider-identity-lost":!same_horse?"horse-identity-lost":
                rider_dead?"rider-dead":horse_dead?"horse-dead":"dismounted")+
                " along_m="+std::to_string(e.progress.Metres(g_laps.plan.course))+elapsed);
            continue;
        }
        const auto position=ENTITY::GET_ENTITY_COORDS(e.rider.horse,true,false);
        const int boundary_before=e.progress.boundary.Offenses();
        const auto advance=e.progress.Update(g_laps.plan.course,position,now);rivals.push_back(advance);
        LogBoundaryOffenses("jockey-"+std::to_string(entrant_index),e.rider.horse,e.progress,boundary_before);
        rival_ids.push_back(static_cast<int>(entrant_index));
        if(advance.disqualification_fraction) {
            FieldExitTiming timing;
            if(!GetAutoSurveyState().running)race_help::Elimination(race_elimination::Message(field_race::EntrantName(entrant_index),
                advance.section_jump?race_elimination::Reason::SkippedSection:race_elimination::Reason::LeftCourse,
                i18n::Chinese()));
            timing.EndNotice();timing.BeginCleanup();
            e.out=true;e.racing_assist.Reset();e.chase.Reset();e.command.catchup_fraction=0;e.follower.Reset();e.lease.Restore(HorseIdentity,ReadBaseRank,WriteBaseRank);
            StartNaturalDeparture(e.rider.horse,e.horse.horse,false);
            const auto elapsed=timing.EndCleanup();
            Log("Field entrant out rider="+std::to_string(e.rider.horse)+
                (advance.section_jump?" reason=skipped-section":" reason=left-corridor")+
                " along_m="+std::to_string(e.progress.offence_along)+elapsed);
        }
        else {all_out=false;g_laps.Export(e.progress,position,e.proxy);}
    }
    const auto decision=field_race::Decide(p,rivals,all_out);
    g_laps.disqualified_result=decision.disqualification;
    if(decision.outcome!=circuit::Outcome::None) {
        if(decision.outcome==circuit::Outcome::PlayerWon)
            Log("race winner actor=player game_ms="+std::to_string(MISC::GET_GAME_TIMER()));
        else if(decision.outcome==circuit::Outcome::OpponentWon) {
            if(const auto first=field_race::FirstFinisher(rivals)) {
                const int id=rival_ids[*first];
                Log("race winner actor="+(id<0?std::string("host"):"jockey-"+std::to_string(id))+
                    " game_ms="+std::to_string(MISC::GET_GAME_TIMER()));
            }
        }
        Log("multi-racer result="+std::to_string(static_cast<int>(decision.outcome))+" foul="+
            std::to_string(decision.disqualification)+" all_rivals_out="+std::to_string(all_out));
        g_laps.Stop();
    }
    return decision.outcome;
}
void TickExtraDrivers(double now=CircuitNow())
{
    horse_diagnostics::Scope stage_cost("race: field drivers");
    const auto player_at=ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(),true,false);
    for(auto& e:g_extra_rivals)if(!e.out && !e.retiring) {
        e.racing_assist.Tick(e.rider.horse);
        const float speed=ENTITY::GET_ENTITY_SPEED(e.horse.horse),along=e.proxy.current.along;
        const bool physical_ready=far_catchup::PhysicalReady(e.horse.horse);
        const auto rejoin=far_catchup::RejoinNative(e.follower,e.rider.horse,e.avoidance.path,
            e.avoidance.reference,along,ENTITY::GET_ENTITY_COORDS(e.horse.horse,true,false),now,
            physical_ready && !e.far_runner.active,g_laps.plan.single_loop,
            [&]{return "TJC_EXTRA_REJOIN_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(++g_course_serial);},
            ENTITY::IS_ENTITY_ON_SCREEN(e.horse.horse)||ENTITY::IS_ENTITY_ON_SCREEN(e.rider.horse),
            e.navigation.vehicles.Holding());
        if(rejoin==far_catchup::Rejoin::Started) {
            e.controller={};e.stalled_since=-1;
            Log("far rival native rejoined entrant="+std::to_string(e.rider.horse)+
                " entry="+std::to_string(e.follower.entry)+" along_m="+std::to_string(along));
        } else if(rejoin==far_catchup::Rejoin::Failed)
            Log("far rival native rejoin refused entrant="+std::to_string(e.rider.horse));
        e.command=e.controller.Update(e.pace,along,speed,g_rider_skill);
        e.command.catchup_fraction=e.chase.Update(g_race_epoch,e.horse.address,e.horse.horse,e.horse.model,
            PlayerProgressGap(e.proxy,&e.progress),race_speed_policy::Enabled() &&
            HorseIdentity(e.horse.horse)==e.horse&&!GetAutoSurveyState().running);
        if(race_pace::diagnostics_enabled)e.command.trace.evaluated_game_s=now;
        const float passing_target=race_pace::PassingTarget(e.pace,e.command,e.target.speed+1);
        const auto guard_position=ENTITY::GET_ENTITY_COORDS(e.horse.horse,true,false);
        native_follower::ProtectVehiclePace(e.navigation,e.command,along,guard_position,now,e.horse.horse,&e.pace);
        following::RetireHandoff(e.follower,e.command);
        UpdateNativePassing(e.follower,e.navigation,e.horse.horse,along,e.pace,e.command,now,passing_target);
        if(far_catchup::NativePaused(e.far_runner,e.follower,physical_ready)) {
            e.chase.Pause();e.command.catchup_fraction=0;
            e.stalled_since=-1;continue;
        }
        // Only the same task's bounded side target changes; its registered
        // recording and resource lifetime remain unchanged during a pass.
        if(g_continuous_route) {
            const auto renewal=native_follower::Maintain(e.follower,e.navigation,e.rider.horse,
                e.avoidance.path,e.avoidance.reference,along,ENTITY::GET_ENTITY_COORDS(e.horse.horse,true,false),
                now,e.command.effort,speed,[&]{return "TJC_EXTRA_"+std::to_string(GetCurrentProcessId())+"_"+
                    std::to_string(++g_course_serial);},
                native_far_speed::ForRider(e.pace,e.command,g_native_far_speed && !g_far_catchup_enabled,
                    e.horse.horse,e.rider.horse,
                    destination::Distance(ENTITY::GET_ENTITY_COORDS(e.horse.horse,true,false),player_at),
                    e.proxy.current.valid && !e.proxy.continuity_lost,0,g_native_speed_table || g_native_vector_trial,
                    g_native_vector_trial?&e.follower.native_vector.learning:nullptr,PlayerProgressGap(e.proxy,&e.progress)),
                g_native_speed_table && !e.command.NativePaceLimited(),g_native_move_rate_trial && !e.command.NativePaceLimited(),
                g_native_vector_trial,&e.command,!RequiredRecordedLine());
            if(renewal==native_follower::Renewal::Started) {
                e.retries.Record(along);e.stalled_since=-1;
                Log("extra native driver renewed rider="+std::to_string(e.rider.horse)+
                    " entry="+std::to_string(e.follower.entry)+" along="+std::to_string(along));
            }
        }
        if(e.command.vehicle_blocked){
            if(!g_continuous_route)e.follower.WaitForVehicle(e.rider.horse,now);
            e.stalled_since=-1;continue;
        }
        bool continuous=e.follower.control;
        if(!continuous && RequiredRecordedLine()) {
            e.command.braking=true;e.command.catchup_fraction=0;e.chase.Pause();
            e.follower.handoff_speed=0;e.follower.handoff_until=0;
            e.stalled_since=-1;continue;
        }
        following::RetireHandoff(e.follower,e.command);
        if(!e.command.NativePaceLimited() && far_catchup::TransferHandoffVelocity(e.follower,e.pace,
            {now,ENTITY::GET_ENTITY_COORDS(e.horse.horse,true,false),{},along,speed,
                e.proxy.current.valid && !e.proxy.continuity_lost,false,
                ENTITY::IS_ENTITY_ON_SCREEN(e.horse.horse)!=0,true,g_laps.plan.single_loop},
            e.horse.horse,physical_ready))
            Log("far rival native velocity transferred entrant="+std::to_string(e.rider.horse));
        const bool may_recover=far_catchup::MayRecover(StateSeconds(),now,e.far_runner,e.follower,physical_ready);
        if(!may_recover || speed>=.5F)e.stalled_since=-1;
        else if(e.stalled_since<0)e.stalled_since=now;
        const bool recover=!continuous && e.stalled_since>=0 && now-e.stalled_since>=10;
        if(recover) {
            e.follower.StopControl();continuous=false;e.stalled_since=now;
            if(g_continuous_route && e.retries.Available(along) && StartExtraDriver(e,along,now)) {
                e.retries.Record(along);continuous=true;
                Log("extra native driver restarted rider="+std::to_string(e.rider.horse)+
                    " local_attempt="+std::to_string(e.retries.consecutive)+
                    " total="+std::to_string(e.retries.total)+" along="+std::to_string(along));
            } else Log(std::string(RequiredRecordedLine()?"extra recording retry pending rider=":"extra forward traffic recovery rider=")+std::to_string(e.rider.horse)+
                " along="+std::to_string(along));
        }
        if(continuous || RequiredRecordedLine())continue;
        const auto desired=course::LookAhead(g_course,e.proxy,std::max(12.0F,speed),true);
        const auto cap=g_course.Segment(g_laps.plan.course.gates[e.progress.next_gate].along+15)+1;
        const auto next=std::min(desired,cap);
        if(!e.traffic_target || recover || (next>e.traffic_target && now-e.task_at>=.5) ||
            (!race_speed_policy::Enabled() && e.traffic_speed_owned)) {
            e.traffic_target=next;e.task_at=now;
            MoveInTrafficToDestination(e.rider.horse,g_course.points[next],3.0F,-1.0F,3064);
        }
        if(race_speed_policy::Enabled()) invoke<Void>(0xBAAB791AA72C2821,e.rider.horse,e.command.effort);
        e.traffic_speed_owned=race_speed_policy::Enabled();
        native_far_speed::ApplyVector(e.follower.native_vector,e.rider.horse,
            native_far_speed::ForRider(e.pace,e.command,g_native_far_speed && !g_far_catchup_enabled,
                e.horse.horse,e.rider.horse,
                destination::Distance(ENTITY::GET_ENTITY_COORDS(e.horse.horse,true,false),player_at),
                e.proxy.current.valid && !e.proxy.continuity_lost,0,true,
                g_native_vector_trial?&e.follower.native_vector.learning:nullptr,PlayerProgressGap(e.proxy,&e.progress)),
            g_native_vector_trial,e.command.NativePaceLimited());
    }
}
