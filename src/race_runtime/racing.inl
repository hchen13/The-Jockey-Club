// Private racing implementation; included by race_system.cpp.
void TickBoundaryWarning(double now) {
    static race_elimination::BoundaryEdge toast_edge;
    if(g_state==State::Disabled){toast_edge.Update(false,g_race_epoch);race_help::CancelPendingBoundary();g_boundary_warning.Reset();return;}
    const bool warning=g_state==State::Racing&&CurrentRules().uses_lap_engine&&!g_trial.active&&
        !g_laps.player.off_course&&g_laps.player.boundary.Pending();
    if(toast_edge.Update(warning,g_race_epoch))
        race_help::Boundary(i18n::Text("You have left the course. Return to the track.","你已离开赛道，快回到赛道上。"));
    if(!warning)race_help::CancelPendingBoundary();
    g_boundary_warning.Tick(PLAYER::PLAYER_PED_ID(),warning,now);
}
struct BoundaryWarningFrame {
    double now=-1;
    ~BoundaryWarningFrame(){TickBoundaryWarning(now>=0?now:CircuitNow());}
};
void TickRacing(double circuit_time=CircuitNow())
{
    BoundaryWarningFrame boundary_frame{circuit_time};
    horse_diagnostics::Scope stage_cost("race: racing");
    if (g_state != State::Racing) return;
    g_host_catchup.Pause();g_pace_command.catchup_fraction=0;
    for(auto& e:g_extra_rivals){e.chase.Pause();e.command.catchup_fraction=0;}
    // Our own start rear left the horses agitated, which turns deep-snow gait into the plains sprint
    // (the user, 2026-09-22): each one is written back to the value it had before the rear.
    if(const int calmed=cues::CalmRearedHorses();calmed)
        Log("start rear: "+std::to_string(calmed)+" horse(s) calmed back to the agitation they had before it");
    // Watched before anything can end the race on this frame: the fall that
    // throws the host is the collision there is most to learn from, and the
    // incident handling below ends the race the frame it sees him unseated
    // (2026-09-19, Saint Denis: no probe, nothing learned).
    TickLineRebuild();   // At most one line a frame, and nothing at all unless one is queued.
    if(g_state!=State::Racing)return;
    if(g_trial.active && !TrialWatch())return;
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (MaryOwned() && player && !ENTITY::IS_ENTITY_DEAD(player)) {
        if (PED::IS_PED_RAGDOLL(player) || PED::IS_PED_FALLING(player) || TASK::IS_PED_GETTING_UP(player))
            g_mary_fall_at=Clock::now();
        const bool shot_rival=MaryHorseShotByPlayer(g_opponent_horse,g_mary_horse_health);
        const bool shot_own=MaryHorseShotByPlayer(g_player_mount,g_mary_player_horse_health);
        if (shot_rival || shot_own) {
            if (shot_rival) Log("opponent incident: " + ReadOpponentIncident(player).Detail());
            EndRaceWithoutWinner("HORSE_RACE_DISTANCE_FAIL","player shot a horse; race forfeited");
            g_mary.Queue(marybeth_dialogue::Event::ShotHorse,MaryNow(),8.0);
            return;
        }
    }
    if (player == 0 || !ENTITY::DOES_ENTITY_EXIST(player) || ENTITY::IS_ENTITY_DEAD(player) ||
        g_player_mount == 0 || !ENTITY::DOES_ENTITY_EXIST(g_player_mount) || ENTITY::IS_ENTITY_DEAD(g_player_mount)) {
        if(!g_trial.active) {
            const bool rider_exists=player && ENTITY::DOES_ENTITY_EXIST(player);
            const bool mount_exists=g_player_mount && ENTITY::DOES_ENTITY_EXIST(g_player_mount);
            const bool rider_dead=rider_exists && ENTITY::IS_ENTITY_DEAD(player);
            const bool horse_dead=mount_exists && ENTITY::IS_ENTITY_DEAD(g_player_mount);
            const auto reason=race_elimination::Incident(rider_dead,horse_dead,!rider_exists||!mount_exists,false);
            race_help::Elimination(race_elimination::Message("",reason,i18n::Chinese(),true));
        }
        if (CurrentRules().Hosted()) EndRaceWithoutWinner("HORSE_RACE_LOST","player or player horse unavailable");
        else FinishRace(false);
        return;
    }
    if (!PED::IS_PED_ON_MOUNT(player) || PED::GET_MOUNT(player) != g_player_mount) {
        if(!g_trial.active) {
            const bool falling=PED::IS_PED_FALLING(player)||PED::IS_PED_RAGDOLL(player)||TASK::IS_PED_GETTING_UP(player);
            race_help::Elimination(race_elimination::Message("",
                race_elimination::Incident(false,false,false,falling),i18n::Chinese(),true));
        }
        if (CurrentRules().Hosted()) {
            const bool fell=PED::GET_MOUNT(player)==0 && g_mary_fall_at!=Clock::time_point{} &&
                Clock::now()-g_mary_fall_at<std::chrono::seconds(3);
            EndRaceWithoutWinner("HORSE_RACE_LOST",fell?"player fell from horse":"player dismounted/changed horse");
            if (fell && MaryOwned()) g_mary.Queue(marybeth_dialogue::Event::FellOff,MaryNow(),8.0);
        } else FinishRace(false);
        return;
    }
    const auto incident = ReadOpponentIncident(player);
    // Before result/redispatch resets the actor, emit the bounded pre-incident
    // native readbacks captured from the existing mounted-racer readiness check.
    if(incident.Out() && !g_main_rival_out && !g_trial.remounting && !g_host_physical_incident_emitted) {
        g_host_physical_incident_emitted=true;
        DumpHostPhysicalReadback(circuit_time);
    }
    if ((incident.Out() && (!g_main_rival_out || incident.player_caused)) || incident.lassoed || incident.identity_changed)
        Log("opponent incident: " + incident.Detail());
    // Proven interference wins over accidental dismount/death classification.
    if (incident.lassoed) {
        EndRaceWithoutWinner("HORSE_RACE_DISTANCE_FAIL", "player interfered with opponent using a lasso");
        return;
    }
    if (incident.Out() && incident.player_caused) {
        EndRaceWithoutWinner("HORSE_RACE_LOST", "player attack caused opponent/rival horse to fall or die; race forfeited");
        return;
    }
    if (!incident.rider_exists || !incident.horse_exists || incident.identity_changed) {
        // The game removed a participant. That is not a precondition and not an
        // accident to reward: the race cannot go on, and no wager is charged.
        AbortRace("opponent entity unavailable or identity changed; not an accident win", true, Ender::Game);
        return;
    }
    if (!g_trial.active && PlayerAbandonedRoute(player)) {
        race_help::Elimination(race_elimination::Message("",race_elimination::Reason::LeftCourse,
            i18n::Chinese(),true));
        EndRaceWithoutWinner("HORSE_RACE_DISTANCE_FAIL", CurrentRules().Hosted() ?
            "left station course AND not heading to finish for 15 seconds" : "player exceeded the original route-distance limit");
        return;
    }
    if(g_trial.active && (incident.Out() || g_trial.remounting)){TrialRedispatch(incident.rider_dead,incident.horse_dead,"the fall");return;}
    if (incident.Out() && !CurrentRules().Field()) {
        const bool falling=!incident.rider_dead &&
            (PED::IS_PED_FALLING(g_opponent_rider)||PED::IS_PED_RAGDOLL(g_opponent_rider)||TASK::IS_PED_GETTING_UP(g_opponent_rider));
        const auto reason=race_elimination::Incident(incident.rider_dead,incident.horse_dead,false,falling);
        race_help::Elimination(race_elimination::Message(CurrentRules().Hosted()?FixedHostName():i18n::Text("Your rival","对手"),
            reason,i18n::Chinese()));
        WinRaceForOpponentAccident(incident.rider_dead ? "rider died" :
            (incident.horse_dead ? "horse died" : "rider fell/dismounted"));
        return;
    }
    if(CurrentRules().Field() && incident.Out() && !g_main_rival_out) {
        // Record the rider, not just the original horse, once at the actual
        // transition. A mismatched mount does not itself prove a fall.
        const bool rider_present=incident.rider_exists && !incident.rider_dead;
        const bool rider_mounted=rider_present && PED::IS_PED_ON_MOUNT(g_opponent_rider);
        const Ped current_mount=rider_mounted?PED::GET_MOUNT(g_opponent_rider):0;
        const bool falling=rider_present &&
            (PED::IS_PED_FALLING(g_opponent_rider)||PED::IS_PED_RAGDOLL(g_opponent_rider)||TASK::IS_PED_GETTING_UP(g_opponent_rider));
        FieldExitTiming timing;
        race_help::Elimination(race_elimination::Message(FixedHostName(),
            race_elimination::Incident(incident.rider_dead,incident.horse_dead,false,falling),i18n::Chinese()));
        timing.EndNotice();
        const auto rider_position=rider_present?ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false):Vector3{};
        const auto game_ms=MISC::GET_GAME_TIMER();
        const bool log_falling=rider_present && PED::IS_PED_FALLING(g_opponent_rider);
        const bool log_ragdoll=rider_present && PED::IS_PED_RAGDOLL(g_opponent_rider);
        timing.BeginCleanup();
        g_main_rival_out=true;g_racing_assist.Reset();g_host_far_runner.Reset();g_competitive_follower.Reset();RestoreOpponentRanks("host eliminated from multi-racer field");
        const auto elapsed=timing.EndCleanup();
        Log("Field host out rider="+std::to_string(g_opponent_rider)+" reason="+
            (incident.rider_dead?"rider-dead":incident.horse_dead?"horse-dead":"dismounted")+
            " game_ms="+std::to_string(game_ms)+
            " observed_mount="+std::to_string(current_mount)+" expected_mount="+std::to_string(g_opponent_horse)+
            " falling="+std::to_string(log_falling)+
            " ragdoll="+std::to_string(log_ragdoll)+
            " rider_xyz="+std::to_string(rider_position.x)+","+std::to_string(rider_position.y)+","+std::to_string(rider_position.z)+elapsed);
    }

    TickRaceMusic();
    if(!g_circuit_scene.TickActive(CircuitNow(),!RaceSpeechAvailable() || g_mary.pending.has_value(),
        [](Ped p){PlaySpeech(p,PLAYER::PLAYER_PED_ID(),"GENERIC_CHEER",nullptr,true);})) {
        // The exclusion declines what it cannot handle once the flag has
        // dropped; whatever still fails here is a report, not a cancelled race.
        static std::string reported;
        if(reported!=g_circuit_scene.error){reported=g_circuit_scene.error;Log("street scene problem during the race, not cancelling: "+g_circuit_scene.error);}
    }
    TickFarRivals(ENTITY::GET_ENTITY_COORDS(player,true,false),circuit_time);
    circuit::Outcome lap_outcome=circuit::Outcome::None;
    if(CurrentRules().uses_lap_engine) {
        const auto p=ENTITY::GET_ENTITY_COORDS(player,true,false),o=ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false);
        const bool player_off=g_laps.player.off_course,rival_off=g_laps.opponent.off_course;
        const int player_boundary=g_laps.player.boundary.Offenses(),rival_boundary=g_laps.opponent.boundary.Offenses();
        const auto before=g_laps.Hud();
        lap_outcome=CurrentRules().Field()?UpdateMultiProgress(p,o,circuit_time):g_laps.Update(p,o,circuit_time);
        LogBoundaryOffenses("player",player,g_laps.player,player_boundary);
        LogBoundaryOffenses("host",g_opponent_rider,g_laps.opponent,rival_boundary);
        if(player_off!=g_laps.player.off_course || rival_off!=g_laps.opponent.off_course)
            Log("circuit boundary sample: player_xyz="+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.z)+
                " opponent_xyz="+std::to_string(o.x)+","+std::to_string(o.y)+","+std::to_string(o.z));
        if(lap_outcome!=circuit::Outcome::None && g_laps.disqualified_result) {
            // A trial host who leaves the corridor is put back on the line, not judged.
            if(g_trial.active) {
                if(!g_trial.off_course_noted){g_trial.off_course_noted=true;Log("trial: the host left the corridor; put back on the line");}
                TrialRedispatch(false,false,"leaving the corridor");return;
            }
            if(!player_off && g_laps.player.off_course)
                race_help::Elimination(race_elimination::Message("",
                    g_laps.player.section_jump?race_elimination::Reason::SkippedSection:race_elimination::Reason::LeftCourse,
                    i18n::Chinese(),true));
            if(!CurrentRules().Field() && !rival_off && g_laps.opponent.off_course)
                race_help::Elimination(race_elimination::Message(CurrentRules().Hosted()?FixedHostName():i18n::Text("Your rival","对手"),
                    g_laps.opponent.section_jump?race_elimination::Reason::SkippedSection:race_elimination::Reason::LeftCourse,
                    i18n::Chinese()));
            FinishCircuitDisqualification(lap_outcome);return;
        }
        g_laps.Export(g_laps.player,p,g_course_player);g_laps.Export(g_laps.opponent,o,g_course_rival);
        const auto after=g_laps.Hud();
        // The player's own completed lap gets an audible mark; a rival moving
        // up a lap is a HUD change, not something to ring in the player's ear.
        if(after.visible && before.player_lap!=after.player_lap && after.player_lap>before.player_lap)cues::Lap();
        // Positive survey/passage evidence is available to safety queries and
        // the next starting solve. A lap crossing does not replace live lines;
        // confirmed impacts and explicit settings changes own runtime replans.
        if(after.visible && (before.player_lap!=after.player_lap || before.opponent_lap!=after.opponent_lap))
            Log("circuit laps player="+std::to_string(after.player_lap)+"/"+std::to_string(after.total)+
                " opponent="+std::to_string(after.opponent_lap)+"/"+std::to_string(after.total));
    }
    UpdateProgress(player, g_player_progress_max);
    const bool opponent_mounted = incident.mounted && !g_main_rival_out;
    if (opponent_mounted) {
        // Stock beat_horse_race refreshes this transient race flag on the
        // mount every script tick, including ordinary two-rider races.
        if(!g_racing_assist.owner.Known())g_racing_assist.Begin(g_opponent_horse);
        g_racing_assist.Tick(g_opponent_rider);
        UpdateProgress(g_opponent_rider, g_opponent_progress_max);
    }
    if (CurrentRules().Hosted()) {
        const Vector3 player_position = ENTITY::GET_ENTITY_COORDS(player, true, false);
        const Vector3 rival_position = ENTITY::GET_ENTITY_COORDS(g_opponent_rider, true, false);
        if (g_fixed_samples_ready && !CurrentRules().uses_lap_engine) {
            const auto player_arrival = (!HasRecordedCourse() || g_course_player.MayFinish(g_course))?
                destination::ArrivalFraction(g_fixed_previous_player,player_position,g_route_finish):std::optional<float>{};
            const auto rival_arrival = opponent_mounted && (!HasRecordedCourse() || g_course_rival.MayFinish(g_course)) ? destination::ArrivalFraction(
                g_fixed_previous_rival, rival_position, g_route_finish) : std::optional<float>{};
            if (player_arrival || rival_arrival) {
                if(g_trial.active){TrialComplete();return;}
                if (player_arrival && rival_arrival && std::abs(*player_arrival - *rival_arrival) < 0.001F) {
                    horse_ledger::runtime::CancelRace();
                    StopNavigation(); ReleaseMissionFlag(); StopRaceAudio(true);
                    SettleFixedWager(wager::Outcome::Void);
                    ShowObjective(i18n::Text("A dead heat. No money changed hands.", "比赛平局，没有金钱得失。"));
                    g_result_phase = ResultPhase::Interrupted; SetState(State::Result);
                    Log("fixed race dead heat within one sample");
                } else FinishRace(player_arrival && (!rival_arrival || *player_arrival < *rival_arrival));
                return;
            }
        }
        if(lap_outcome!=circuit::Outcome::None) {
            if(g_trial.active){TrialComplete();return;}
            if(lap_outcome==circuit::Outcome::Tie) {
                horse_ledger::runtime::CancelRace();
                StopNavigation();ReleaseMissionFlag();StopRaceAudio(true);SettleFixedWager(wager::Outcome::Void);
                ShowObjective(i18n::Text("A dead heat. No money changed hands.","比赛平局，没有金钱得失。"));
                g_result_phase=ResultPhase::Interrupted;SetState(State::Result);
            } else FinishRace(lap_outcome==circuit::Outcome::PlayerWon);
            return;
        }
        g_fixed_previous_player = player_position;
        g_fixed_previous_rival = rival_position;
        g_fixed_samples_ready = true;
        if (opponent_mounted) {
            const auto physical_readback=far_catchup::ReadPhysicalReady(g_opponent_horse);
            const bool physical_ready=physical_readback.ready;
            SaveHostPhysicalReadback(physical_readback,circuit_time,player_position,rival_position);
            const auto rejoin=far_catchup::RejoinNative(g_competitive_follower,g_opponent_rider,
                g_host_avoidance.path,g_host_avoidance.reference,g_course_rival.current.along,
                ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false),circuit_time,
                physical_ready && !g_host_far_runner.active,g_laps.plan.single_loop,
                [&]{return "TJC_AI_REJOIN_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(++g_course_serial);},
                ENTITY::IS_ENTITY_ON_SCREEN(g_opponent_horse)||ENTITY::IS_ENTITY_ON_SCREEN(g_opponent_rider),
                g_host_navigation.vehicles.Holding());
            if(rejoin==far_catchup::Rejoin::Started) {
                g_continuous_attempted=true;g_pace_controller={};
                g_fixed_stall_since={};g_fixed_progress_at={};g_fixed_stall_repeats=0;
                Log("far rival native rejoined host entry="+std::to_string(g_competitive_follower.entry)+
                    " along_m="+std::to_string(g_course_rival.current.along)+" planned_kmh="+
                    std::to_string(g_competitive_follower.handoff_speed*3.6F));
            } else if(rejoin==far_catchup::Rejoin::Failed) {
                Log("far rival native rejoin refused host; retrying bounded route");
                g_continuous_attempted=false;if(!StartOriginalOpponentRoute())return;
            }
            g_pace_command={false,0,race_pace::Effort(g_rider_skill)};
            if(HasRecordedCourse() && !g_pace_plan.path.points.empty())
                g_pace_command=g_pace_controller.Update(g_pace_plan,g_course_rival.current.along,
                    ENTITY::GET_ENTITY_SPEED(g_opponent_horse),g_rider_skill);
            if(race_pace::diagnostics_enabled && g_pace_command.trace.controller_evaluated)
                g_pace_command.trace.evaluated_game_s=circuit_time;
            UpdateHostCatchup(HasRecordedCourse()?PlayerProgressGap(g_course_rival,
                CurrentRules().uses_lap_engine?&g_laps.opponent:nullptr):catchup::RemainingGap(
                    destination::Distance(player_position,g_route_finish),destination::Distance(rival_position,g_route_finish),opponent_mounted));
            const float passing_target=race_pace::PassingTarget(g_pace_plan,g_pace_command,g_opponent_speed_level,
                PlayerRaceBurstEligible() && g_host_burst_earned?SelectedFixedRoute().host_level_bonus:0);
            native_follower::ProtectVehiclePace(g_host_navigation,g_pace_command,g_course_rival.current.along,
                ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false),circuit_time,g_opponent_horse,&g_pace_plan);
            following::RetireHandoff(g_competitive_follower,g_pace_command);
            UpdateNativePassing(g_competitive_follower,g_host_navigation,g_opponent_horse,
                g_course_rival.current.along,g_pace_plan,g_pace_command,circuit_time,passing_target);
            const bool navigation_paused=far_catchup::NativePaused(g_host_far_runner,g_competitive_follower,physical_ready);
            if(navigation_paused) {
                g_host_catchup.Pause();g_pace_command.catchup_fraction=0;
                // A sleeping horse's zero speed says nothing about route
                // progress. Never retire its driver or teleport it backwards.
                g_fixed_stall_since={};g_fixed_progress_at={};
            } else {
                if(HasRecordedCourse() && g_continuous_route && g_host_avoidance.path.points.size()>=2) {
                    const auto renewal=native_follower::Maintain(g_competitive_follower,g_host_navigation,
                        g_opponent_rider,g_host_avoidance.path,g_host_avoidance.reference,
                        g_course_rival.current.along,ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false),
                        circuit_time,g_pace_command.effort,
                        ENTITY::GET_ENTITY_SPEED(g_opponent_horse),
                        [&]{return "TJC_AI_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(++g_course_serial);},
                        native_far_speed::ForRider(g_pace_plan,g_pace_command,
                            g_native_far_speed && !g_far_catchup_enabled,g_opponent_horse,g_opponent_rider,
                            destination::Distance(rival_position,player_position),
                            g_course_rival.current.valid && !g_course_rival.continuity_lost,
                            PlayerRaceBurstEligible() && g_host_burst_earned?SelectedFixedRoute().host_level_bonus:0,g_native_speed_table || g_native_vector_trial,
                            g_native_vector_trial?&g_competitive_follower.native_vector.learning:nullptr,
                            PlayerProgressGap(g_course_rival,CurrentRules().uses_lap_engine?&g_laps.opponent:nullptr),FixedProfile().key),
                        g_native_speed_table && !g_pace_command.NativePaceLimited(),
                        g_native_move_rate_trial && !g_pace_command.NativePaceLimited(),
                        g_native_vector_trial,&g_pace_command,!RequiredRecordedLine());
                    if(renewal==native_follower::Renewal::Started) {
                        g_follower_retries.Record(g_course_rival.current.along);
                        g_fixed_stall_since={};g_fixed_progress_at={};g_fixed_stall_repeats=0;
                        Log("competitive driver renewed: entry="+std::to_string(g_competitive_follower.entry)+
                            " chunk_end="+std::to_string(g_competitive_follower.chunk_last)+
                            " qualified_along_m="+std::to_string(g_course_rival.current.along));
                    }
                }
                if(g_pace_command.vehicle_blocked) {
                    if(!g_continuous_route)g_competitive_follower.WaitForVehicle(g_opponent_rider,circuit_time);
                    g_fixed_stall_since={};g_fixed_progress_at={};
                } else {
                bool continuous=g_competitive_follower.control;
                const bool required_pending=!continuous && RequiredRecordedLine();
                if(required_pending) {
                    g_pace_command.braking=true;g_pace_command.catchup_fraction=0;g_host_catchup.Pause();
                    g_competitive_follower.handoff_speed=0;g_competitive_follower.handoff_until=0;
                    g_fixed_stall_since={};g_fixed_progress_at={};
                }
                if(!continuous && !required_pending) {
                    AdvanceRecordedRival();
                    // Traffic override belongs only to a traffic task, never to a
                    // live recording task. Both use the same chosen riding effort.
                    if(race_speed_policy::Enabled()) {
                        invoke<Void>(0xBAAB791AA72C2821,g_opponent_rider,g_pace_command.effort);
                        g_host_traffic_speed_owned=true;
                    } else if(g_host_traffic_speed_owned) {
                        const auto destination=HasRecordedCourse() && g_course_target<g_course.points.size()?
                            g_course.points[g_course_target]:g_route_finish;
                        MoveInTrafficToDestination(g_opponent_rider,destination,3.0F,-1.0F,3064);
                        g_host_traffic_speed_owned=false;
                    }
                    const float sleeping_target=HasRecordedCourse()?native_far_speed::ForRider(g_pace_plan,g_pace_command,
                            g_native_far_speed && !g_far_catchup_enabled,g_opponent_horse,g_opponent_rider,
                            destination::Distance(rival_position,player_position),
                            g_course_rival.current.valid && !g_course_rival.continuity_lost,
                            PlayerRaceBurstEligible() && g_host_burst_earned?SelectedFixedRoute().host_level_bonus:0,true,
                            g_native_vector_trial?&g_competitive_follower.native_vector.learning:nullptr,
                            PlayerProgressGap(g_course_rival,CurrentRules().uses_lap_engine?&g_laps.opponent:nullptr),FixedProfile().key):
                        native_far_speed::ForNativeTraffic(g_pace_command,g_native_far_speed && !g_far_catchup_enabled,
                            g_opponent_horse,g_opponent_rider,destination::Distance(rival_position,player_position),
                            PlayerRaceBurstEligible() && g_host_burst_earned?SelectedFixedRoute().host_level_bonus:0,
                            g_native_vector_trial?&g_competitive_follower.native_vector.learning:nullptr,FixedProfile().key,
                            g_host_catchup.gap_valid?std::optional<float>{g_host_catchup.gap}:std::nullopt);
                    native_far_speed::ApplyVector(g_competitive_follower.native_vector,g_opponent_rider,
                        sleeping_target,g_native_vector_trial,g_pace_command.NativePaceLimited());
                }
                // Never replay pre-brake momentum or flatten native Z on
                // a slope/closing threat. Retire it, including its grace.
                following::RetireHandoff(g_competitive_follower,g_pace_command);
                if(!g_pace_command.NativePaceLimited() && far_catchup::TransferHandoffVelocity(g_competitive_follower,g_pace_plan,
                    {circuit_time,ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false),player_position,
                        g_course_rival.current.along,ENTITY::GET_ENTITY_SPEED(g_opponent_horse),
                        g_course_rival.current.valid && !g_course_rival.continuity_lost,false,
                        ENTITY::IS_ENTITY_ON_SCREEN(g_opponent_horse)!=0,true,g_laps.plan.single_loop},
                    g_opponent_horse,physical_ready))
                    Log("far rival native velocity transferred host entry="+std::to_string(g_competitive_follower.entry));
                // Standing still is not the only way to stop racing on a recorded course. A host whose
                // navigation goal ends up behind her creeps toward it at a couple of
                // metres a second: never "stalled" by speed, never recovered, and
                // stationary as far as the race is concerned. Judge by how far she
                // has actually got along the course. A destination-only race has
                // no course metres to measure; its zero-speed stall timer below
                // remains responsible for recovering a genuinely stopped task.
                const bool recorded=HasRecordedCourse();
                const float along_now=g_course_rival.current.valid?g_course_rival.current.along:g_course_rival.furthest;
                const auto now_clock=Clock::now();
                // The wall clock does not know the rival was frozen on the starting
                // grid by this very mod. Fifteen metres in four seconds is
                // unreachable there by construction, so only arm the anchor once she
                // has actually been released.
                const bool released=far_catchup::MayRecover(StateSeconds(),circuit_time,g_host_far_runner,
                    g_competitive_follower,physical_ready);
                if(!recorded)g_fixed_progress_at={};
                else if(!released || g_fixed_progress_at==Clock::time_point{} ||
                    along_now>g_fixed_progress_along+kRivalProgressMetres) {
                    g_fixed_progress_along=along_now;g_fixed_progress_at=now_clock;
                }
                // Recorded drivers have a shared, forward-only progress watchdog.
                // This legacy watchdog is only for a traffic fallback. Running both
                // made a merely slow horse restart repeatedly or teleport backwards.
                const bool creeping=RivalTrafficCreeping(recorded,released,continuous,g_fixed_progress_at,now_clock);
                const bool stalled = released && (ENTITY::GET_ENTITY_SPEED(g_opponent_horse) < 0.5F || creeping);
                if (!stalled) g_fixed_stall_since = {};
                else if (g_fixed_stall_since == Clock::time_point{}) g_fixed_stall_since = Clock::now();
                if (stalled && !continuous && !required_pending && Clock::now() - g_fixed_stall_since >= std::chrono::seconds(creeping?0:10)) {
                    if(creeping)Log("rival made less than "+std::to_string(kRivalProgressMetres)+" m in "+
                        std::to_string(kRivalProgressSeconds)+" s at along_m="+std::to_string(along_now)+
                        "; treating as a stall so the recorded route is reissued");
                    g_fixed_progress_along=along_now;g_fixed_progress_at=Clock::now();
                    if (++g_fixed_route_retries > 3) { Log("rival stalled after three route recoveries; continuing to recover rather than cancelling"); g_fixed_route_retries=0; }
                    g_competitive_follower.StopControl();
                    // g_continuous_attempted is cleared only when the race or route
                    // resets, so recovering a merely SLOW host retired the native
                    // driver for the rest of the race and left her on the traffic
                    // fallback - which is the path whose goals go stale. A creep is
                    // not a driver failure. Follower::Start resets first, so a retry
                    // is safe. Task scheduling is never a disqualification and
                    // recovery never writes the horse's position or heading.
                    g_continuous_attempted=false;
                    if(!StartOriginalOpponentRoute())return;
                    g_fixed_stall_since = Clock::now();
                    Log("fixed traffic route recovery attempt="+std::to_string(g_fixed_route_retries)+
                        " rival_xyz="+std::to_string(rival_position.x)+","+std::to_string(rival_position.y)+","+
                        std::to_string(rival_position.z)+" shared_node="+std::to_string(g_course_target));
                }
                } // An occupied crossing keeps the native task and every recovery idle.
            } // Native driving/recovery only when physical riding is available.
        }
        TickMaryRacing();
        if(CurrentRules().Field())TickExtraDrivers(circuit_time);
        if(CurrentRules().uses_lap_engine) {
            if(!DrawCircuitGps()){static bool reported=false;if(!reported){reported=true;Log("circuit GPS unavailable during the race; racing on without guidance");}}
            DrawCorridorHighlight(true);DrawRacingLineHighlight();
        } else if(HasRecordedCourse())DrawCourseGps();
        if (Clock::now() >= g_next_progress_log) {
            if(CurrentRules().Field())for(std::size_t i=0;i<g_extra_rivals.size();++i) {
                const auto& e=g_extra_rivals[i];
                Log("multi progress entrant="+std::to_string(i+1)+" out="+std::to_string(e.out)+
                    " along_m="+std::to_string(e.proxy.current.along)+" lap="+std::to_string(g_laps.plan.Current(e.progress)+1)+
                    " native_node="+std::to_string(e.follower.progress)+" driver="+std::to_string(e.follower.control)+
                    " far="+std::to_string(e.far_runner.active)+" planned_kmh="+std::to_string(e.far_runner.speed*3.6F)+
                    " speed_kmh="+std::to_string(HorseIdentity(e.horse.horse)==e.horse?ENTITY::GET_ENTITY_SPEED(e.horse.horse)*3.6F:0));
            }
            if(HasRecordedCourse())Log("shared course remaining_along_m player="+
                std::to_string(g_course.Length()-g_course_player.current.along)+" rival="+
                std::to_string(g_course.Length()-g_course_rival.current.along)+" npc_target_node="+std::to_string(g_course_target)+
                " far="+std::to_string(g_host_far_runner.active)+" planned_kmh="+std::to_string(g_host_far_runner.speed*3.6F)+
                " player_deviation="+std::to_string(g_course_player.current.distance)+" rival_deviation="+std::to_string(g_course_rival.current.distance));
            else Log("fixed station race remaining_straight_m player=" +
                std::to_string(destination::Distance(player_position, g_route_finish)) + " rival=" +
                std::to_string(destination::Distance(rival_position, g_route_finish)));
            g_next_progress_log = Clock::now() + std::chrono::seconds(5);
        }
        if (StateSeconds() >= g_max_race_seconds) AbortRace("fixed station race timed out", true);
        return; // All original recording-specific completion/recovery below remains isolated.
    }
    if (Finished(player, g_player_progress_max)) {
        FinishRace(true);
        return;
    }
    if (opponent_mounted && Finished(g_opponent_rider, g_opponent_progress_max)) {
        FinishRace(false);
        return;
    }

    // Rockstar's horse-race script passes zero here. The SDK's second-parameter name/type is misleading.
    if (opponent_mounted && !TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(g_opponent_rider, nullptr)) {
        g_competitive_follower.native_vector.Release();
        g_competitive_follower.ResetWaypointCommand(); // A replacement task does not inherit a safety release.
        if (!g_reissued_route_task && StateSeconds() > 2.0) {
            int restart_point = std::clamp(g_opponent_progress_max, 0, g_route_points - 1);
            TASK::TASK_FOLLOW_WAYPOINT_RECORDING(
                g_opponent_rider, g_shortcut_route_name.c_str(), restart_point, kWaypointTaskFlags, -1, false, 0, -1);
            g_reissued_route_task = true;
            g_route_task_reissued_at = Clock::now();
            Log("reissued opponent route task at point " + std::to_string(restart_point));
        } else if (g_reissued_route_task && g_route_task_reissued_at != Clock::time_point{} &&
                   std::chrono::duration<double>(Clock::now() - g_route_task_reissued_at).count() > 6.0) {
            Log("opponent route playback stopped after a reissue; reissuing again rather than cancelling");
            g_reissued_route_task=false;g_route_task_reissued_at={};
            return;
        }
    } else if (opponent_mounted && !race_speed_policy::Enabled()) {
        g_competitive_follower.ReleaseSpeedTrials();
        if(g_competitive_follower.override_request!=0 || g_competitive_follower.override_absolute) {
            const int restart_point=std::clamp(g_opponent_progress_max,0,g_route_points-1);
            TASK::TASK_FOLLOW_WAYPOINT_RECORDING(
                g_opponent_rider,g_shortcut_route_name.c_str(),restart_point,kWaypointTaskFlags,-1,false,0,-1);
            g_competitive_follower.ResetWaypointCommand();
        }
    } else if (opponent_mounted) {
        const float effort=OriginalRivalPlaybackSpeed();
        const float request=native_far_speed::ForRider(g_pace_plan,g_pace_command,
            g_native_far_speed && !g_far_catchup_enabled,g_opponent_horse,g_opponent_rider,
            destination::Distance(ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false),
                ENTITY::GET_ENTITY_COORDS(player,true,false)),g_pace_command.target_mps>0,0,
            g_native_vector_trial,g_native_vector_trial?&g_competitive_follower.native_vector.learning:nullptr,
            PlayerProgressGap(g_course_rival));
        native_far_speed::ApplyVector(g_competitive_follower.native_vector,g_opponent_rider,request,
            g_native_vector_trial,g_pace_command.NativePaceLimited());
        const auto command=g_competitive_follower.PrepareWaypointCommand(effort,request,
            g_native_vector_trial,g_pace_command.NativePaceLimited());
        OverrideWaypointSpeed(g_opponent_rider,command.speed,command.absolute,command.transition_rate);
    } else {
        g_competitive_follower.native_vector.Release();
    }

    UpdateOriginalRivalBanter();

    const auto now = Clock::now();
    if (now >= g_next_progress_log) {
        Log("progress player=" + std::to_string(g_player_progress_max) + "/" + std::to_string(g_route_points) +
            " opponent=" + std::to_string(g_opponent_progress_max) + "/" + std::to_string(g_route_points));
        g_next_progress_log = now + std::chrono::seconds(5);
    }
    if (StateSeconds() >= g_max_race_seconds) {
        FinishRace(false);
    }
}
