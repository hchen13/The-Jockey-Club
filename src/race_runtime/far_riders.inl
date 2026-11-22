// Private far riders implementation; included by race_system.cpp.
namespace trial_force=horse_motion::force_trial;
std::uint64_t TrialForceToken();
void HostPhysicalDispatchStarted() {
    g_host_physical_history.Clear();g_host_physical_incident_emitted=false;
}
physical_readback::Identity HostPhysicalIdentity() {
    const auto& id=g_opponent_damage[1].identity;
    if(id.horse!=g_opponent_horse)return {};
    return {id.horse,id.model,id.address};
}
void SaveHostPhysicalReadback(const physical_readback::Result& result,double now,
    const Vector3& player,const Vector3& rival) {
    // Positions are already read by the racing controller; diagnostics do not
    // add entity reads. The distance is to the rival rider, not its contact patch.
    const auto identity=HostPhysicalIdentity();const auto token=TrialForceToken();
    if(!g_host_physical_history.Due(token,identity,now))return;
    const float dx=player.x-rival.x,dy=player.y-rival.y,dz=player.z-rival.z;
    const float distance=std::hypot(std::hypot(dx,dy),dz);
    const std::optional<float> along=g_course_rival.current.valid && std::isfinite(g_course_rival.current.along)?
        std::optional<float>{g_course_rival.current.along}:std::nullopt;
    const bool focus=g_trial.active && g_trial.spectator.Active();
    g_host_physical_history.Add({result,identity,token,now,std::isfinite(distance)?std::optional<float>{distance}:std::nullopt,along,
        g_trial.active,focus});
}
void DumpHostPhysicalReadback(double now) {
    const auto token=TrialForceToken();const auto identity=HostPhysicalIdentity();
    const auto opt=[](const std::optional<bool>& value) {
        return value?(*value?"1":"0"):"unobserved";
    };
    bool printed=false;
    g_host_physical_history.ForEach(token,identity,now,[&](const physical_readback::Sample& s) {
        printed=true;
        std::ostringstream line;
        line<<std::fixed<<std::setprecision(3)<<"host collision readiness: t="<<s.now
            <<" age_s="<<(now-s.now)<<" stale="<<((now-s.now)>.25)<<" race="<<s.race_token<<" horse="<<s.horse.horse
            <<" model="<<s.horse.model<<" address="<<s.horse.address<<" trial="<<s.trial
            <<" trial_focus_lease_active="<<s.trial_focus_lease_active<<" player_to_rider_m=";
        if(s.distance_m)line<<*s.distance_m;else line<<"unobserved";
        line<<" along_m=";if(s.along_m)line<<*s.along_m;else line<<"unobserved";
        line<<" read_attempted="<<s.result.attempted
            <<" collision_disabled="<<opt(s.result.collision_disabled)
            <<" collision_loaded="<<opt(s.result.collision_loaded)
            <<" waiting_for_world="<<opt(s.result.waiting)<<" frozen="<<opt(s.result.frozen)
            <<" ready="<<s.result.ready;
        // The bounded tail is incident evidence, not a repeated status line.
        // Folding would keep its oldest entries and discard the latest ones.
        log_sink::Shared().Write("[Race] "+line.str(),true);
    });
    if(!printed)Log("host collision readiness: unavailable (no recent same-race same-horse sample)");
    g_host_physical_history.Clear();
}
trial_force::Actor TrialForceActor(Ped ped) {
    const auto id=world::Identify(ped);
    return {id.entity,id.model,id.address};
}
std::uint64_t TrialForceToken() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        g_race_started_at.time_since_epoch()).count());
}
bool TrialForceOwns(const trial_force::Target& target) {
    return g_state==State::Racing && g_trial.active && !g_trial.remounting && g_trial.spectator.Active() &&
        target.race_token==TrialForceToken() && target.dispatch==g_trial.riders &&
        target.horse.ped==g_opponent_horse && target.rider.ped==g_opponent_rider &&
        TrialForceActor(g_opponent_horse)==target.horse && TrialForceActor(g_opponent_rider)==target.rider;
}
bool TrialForceGuard(const trial_force::Target& target) {
    if(!TrialForceOwns(target))return false;
    const Ped player=PLAYER::PLAYER_PED_ID();
    return player && player!=target.rider.ped && player!=target.horse.ped &&
        g_player_mount!=target.horse.ped && PED::GET_MOUNT(player)!=target.horse.ped &&
        !ENTITY::IS_ENTITY_DEAD(target.horse.ped) && !ENTITY::IS_ENTITY_DEAD(target.rider.ped) &&
        PED::IS_PED_ON_MOUNT(target.rider.ped) && PED::GET_MOUNT(target.rider.ped)==target.horse.ped &&
        PED::_GET_RIDER_OF_MOUNT(target.horse.ped,true)==target.rider.ped;
}
trial_force::LineRevision TrialForceLine() {
    return {g_line_build_serial,g_obstacles.revision,g_obstacles_line_revision,g_course_serial};
}
void TrialForceBegin() {
    auto& probe=trial_force::probe;
    const bool bound=probe.Begin({TrialForceActor(g_opponent_horse),TrialForceActor(g_opponent_rider),TrialForceToken(),g_trial.riders,TrialForceLine()},
        CircuitNow(),TrialForceGuard,[](const std::string& line) {
            // The bounded pre-incident tail must survive the ordinary repeated
            // line folding budget. This is <=1 routine line/s plus 24 on exit.
            log_sink::Shared().Write("[Race] "+line,true);
        },TrialForceOwns);
    if(!bound)Log("trial force: lease refused; original point interface retained");
}
void TrialForceObserve(bool immediate=false) {
    auto& probe=trial_force::probe;
    if(!probe.Active())return;
    try {
    const double now=CircuitNow();probe.Heartbeat(now);
    if(!immediate && !probe.Due(now))return;
    // Observation intentionally permits a dismounted target, so the final
    // snapshot can record the native transition before the lease is revoked.
    if(!TrialForceOwns(probe.Bound())){probe.End("trial-or-identity-changed");return;}
    const Ped horse=probe.Bound().horse.ped,rider=probe.Bound().rider.ped;
    const auto vec=[](Vector3 v){return trial_force::Vec{v.x,v.y,v.z};};
    trial_force::Sample s;s.now=now;
    s.along=g_course_rival.current.valid?g_course_rival.current.along:g_course_rival.furthest;
    s.position=vec(ENTITY::GET_ENTITY_COORDS(horse,true,false));s.velocity=vec(ENTITY::GET_ENTITY_VELOCITY(horse,0));
    s.forward=vec(ENTITY::GET_ENTITY_FORWARD_VECTOR(horse));s.rotation=vec(ENTITY::GET_ENTITY_ROTATION(horse,2));
    s.mounted=PED::IS_PED_ON_MOUNT(rider) && PED::GET_MOUNT(rider)==horse;
    s.horse_air=ENTITY::IS_ENTITY_IN_AIR(horse,0);s.horse_falling=PED::IS_PED_FALLING(horse);
    s.horse_ragdoll=PED::IS_PED_RAGDOLL(horse);s.rider_falling=PED::IS_PED_FALLING(rider);s.rider_ragdoll=PED::IS_PED_RAGDOLL(rider);
    s.horse_static=ENTITY::IS_ENTITY_STATIC(horse);s.collision_disabled=ENTITY::GET_ENTITY_COLLISION_DISABLED(horse);
    s.horse_dead=ENTITY::IS_ENTITY_DEAD(horse);s.rider_dead=ENTITY::IS_ENTITY_DEAD(rider);
    s.engine_scaled_ms=MISC::GET_GAME_TIMER();s.engine_unscaled_ms=MISC::_GET_GAME_TIMER_NON_SCALED_CLIPPED();
    s.line=TrialForceLine();
    probe.Observe(s,immediate);
    } catch(...) {probe.End("observation-exception");throw;}
}
void TrialForceEnd(const char* reason,bool terminal=false) {
    TrialForceObserve(true);
    if(terminal)trial_force::probe.Finish(reason);else trial_force::probe.End(reason);
}
float TrialHeading(const course::Path& path,float along) {
    const Vector3 a=race_pace::At(path,std::max(0.0F,along-2)),b=race_pace::At(path,std::min(path.Length(),along+2));
    return std::fmod(std::atan2(-(b.x-a.x),b.y-a.y)*180.0F/3.14159265F+720.0F,360.0F);
}
void TrialBegin() {
    if(!g_trial.armed || g_trial.active)return;
    if(!CurrentRules().Hosted() || !HasRecordedCourse() || !g_opponent_rider || !g_opponent_horse) {
        g_trial.armed=false;Log("trial: not this race - it has no recorded course or no host");return;
    }
    g_trial.armed=false;
    if(!g_trial.spectator.Begin(PLAYER::PLAYER_PED_ID(),g_player_mount,g_opponent_rider,g_opponent_horse)) {
        AbortRace("trial spectator participants unavailable",true,Ender::Game);return;
    }
    g_trial.active=true;g_trial.riders=1;g_trial.incidents=0;g_trial.started=CircuitNow();
    HostPhysicalDispatchStarted();
    g_trial.collision_ready=false;
    g_trial.remounting=false;g_trial.summary.clear();g_trial.off_course_noted=false;
    g_host_learner.ResetMotion();g_player_learner.ResetMotion();
    g_host_learner.cooldown_until=g_player_learner.cooldown_until=0;
    g_host_passage.Break();g_player_passage.Break();
    TrialForceBegin();
    Log("trial: rider 1 dispatched; native spectator focus on horse="+std::to_string(g_opponent_horse)+
        " rider="+std::to_string(g_opponent_rider)+"; player parked, free camera look");
    ShowObjective(i18n::Text("Trial: watching the host; move the mouse to look around, F11 stops it","试跑：镜头跟随 host，移动鼠标自由环视；F11 可停止"));
}
// The inputs held off while the player is parked: moving, spurring, stopping,
// dismounting, jumping, fighting, whistling. Looking round is not among them.
void TrialHold() {
    for(const char* action:{"INPUT_HORSE_MOVE_LR","INPUT_HORSE_MOVE_UD","INPUT_MOVE_LR","INPUT_MOVE_UD","INPUT_MOVE_UP_ONLY",
        "INPUT_HORSE_SPRINT","INPUT_SPRINT","INPUT_HORSE_STOP","INPUT_HORSE_EXIT","INPUT_JUMP","INPUT_HORSE_JUMP",
        "INPUT_ATTACK","INPUT_AIM","INPUT_MELEE_ATTACK","INPUT_HORSE_MELEE_LEFT","INPUT_HORSE_MELEE_RIGHT","INPUT_WHISTLE"})
        PAD::DISABLE_CONTROL_ACTION(0,MISC::GET_HASH_KEY(action),true);
}
bool TrialWatch() {
    if(!g_trial.spectator.Tick()) {
        AbortRace("trial spectator lost its participants or game session",true,Ender::Game);return false;
    }
    TrialHold();
    TrialForceObserve();
    const bool ready=far_catchup::PhysicalReady(g_opponent_horse);
    if(ready!=g_trial.collision_ready) {
        g_trial.collision_ready=ready;
        Log("trial native spectator physics: horse="+std::to_string(g_opponent_horse)+
            " ready="+std::to_string(ready)+" collision_disabled="+
            std::to_string(ENTITY::GET_ENTITY_COLLISION_DISABLED(g_opponent_horse)));
    }
    return true;
}
// A relocation starts fresh motion observations on the same learned map and
// horse. Discard unfinished speed samples, retaining accepted capabilities.
void ResetTrialMotion() {
    g_host_learner.ResetMotion();g_player_learner.ResetMotion();
    g_host_learner.cooldown_until=g_player_learner.cooldown_until=0;
    g_host_passage.Break();g_player_passage.Break();
    g_competitive_follower.Reset();g_host_far_runner.Reset();g_host_navigation={};
    auto& learning=g_competitive_follower.native_vector.learning;
    learning.BreakTravel();learning.reference.Break();
    learning.frame=-1;learning.wall=0;learning.environment={};learning.sampling={};
    g_follower_retries={};g_pace_controller={};g_pace_command={};
    // The shared boost gate sees a real recovery stop for at least this frame.
    g_pace_command.braking=true;HostBoostChanged(false);
    g_host_avoidance.Reset(g_pace_plan.path,g_racing_line.reference);
    g_fixed_progress_at={};g_fixed_stall_since={};g_fixed_abandon_since={};g_fixed_last_task={};
    g_fixed_stall_along=-1000;g_fixed_stall_repeats=0;g_fixed_route_retries=0;
    g_fixed_samples_ready=false;g_fixed_previous_player=g_fixed_previous_rival={};
    g_navigation_telemetry={};g_navigation_pace.Reset();g_course_recovery_progress=0;
    g_continuous_attempted=false;
}
void TrialEnd() {
    TrialForceEnd("trial-ended"); // Also revoke a stale lease when active is already false.
    g_trial.spectator.Stop();
    if(!g_trial.active)return;
    g_trial.active=false;g_trial.remounting=false;
    ResetTrialMotion();
    g_trial.collision_ready=false;
    Log("trial: over after "+std::to_string(g_trial.riders)+" riders and "+std::to_string(g_trial.incidents)+" falls in "+
        std::to_string(static_cast<int>(CircuitNow()-g_trial.started))+" s"+(g_trial.summary.empty()?"":"; "+g_trial.summary));
}
void TrialComplete() {
    TrialForceEnd("finished-course",true);
    g_trial.summary="the host reached the finish";
    char text[128];
    std::snprintf(text,sizeof text,i18n::Text("Trial complete: the host finished after %d riders and %d falls","试跑完成：host 跑到终点，共派出 %d 位骑手、摔 %d 次"),g_trial.riders,g_trial.incidents);
    ShowObjective(text);
    AbortRace("trial complete: the host reached the finish after "+std::to_string(g_trial.riders)+" riders",false,Ender::Game);
}
// The host is put back on the line and started again; called every frame
// the incident stands until his driver is running.
void TrialRedispatch(bool rider_dead,bool horse_dead,const char* why) {
    // Fall, off-course and learner waits all finish the previous dispatch.
    // Never carry its pre-teleport world observations to the next attempt.
    g_host_physical_history.Clear();g_host_physical_incident_emitted=true;
    TrialForceEnd(why,true); // Seal the observed outcome before waits/remounts; never rehabilitate an invalid run.
    if(rider_dead || horse_dead){g_trial.summary="the host or his horse died";AbortRace("trial: the host or his horse died",true,Ender::Game);return;}
    const double now=CircuitNow();
    // A game-second deadline belongs to the impact event. Frame count is not
    // a clock: thirty high-FPS frames can precede the native result by far.
    g_pace_command.braking=true;
    if(!g_trial.remounting) {
        if(g_host_learner.Busy())return;
        ++g_trial.incidents;++g_trial.riders;
        const float here=g_course_rival.current.valid?g_course_rival.current.along:g_course_rival.furthest;
        g_trial.dispatch_along=std::max(0.0F,here-kTrialBackMetres);
        const Vector3 p=race_pace::At(g_course,g_trial.dispatch_along);
        Log(std::string("trial: impact event=")+std::to_string(g_host_learner.drops)+" terminal result="+
            collision_learner::Name(g_host_learner.outcome)+
            " revision="+std::to_string(g_host_learner.revision_before)+"->"+
            std::to_string(g_host_learner.revision_after)+" current_revision="+std::to_string(g_obstacles.revision));
        ResetTrialMotion();
        g_fixed_progress_along=g_trial.dispatch_along;
        for(Ped ped:{g_opponent_rider,g_opponent_horse}) {
            TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped,false,false);
            ENTITY::SET_ENTITY_HEALTH(ped,ENTITY::GET_ENTITY_MAX_HEALTH(ped,false),0);
        }
        ENTITY::SET_ENTITY_COORDS(g_opponent_horse,p,false,false,true,false);
        ENTITY::SET_ENTITY_HEADING(g_opponent_horse,TrialHeading(g_course,g_trial.dispatch_along));
        ENTITY::SET_ENTITY_VELOCITY(g_opponent_horse,0,0,0);
        PED::SET_PED_ONTO_MOUNT(g_opponent_rider,g_opponent_horse,-1,true);
        STREAMING::REQUEST_COLLISION_AT_COORD(p);
        g_trial.remounting=true;g_trial.remount_asked=now;g_trial.remount_since=now;
        Log("trial: rider "+std::to_string(g_trial.riders)+" put on the line at "+std::to_string(static_cast<int>(g_trial.dispatch_along))+
            " m after "+why+" at "+std::to_string(static_cast<int>(here))+" m");
        return;
    }
    if(PED::GET_MOUNT(g_opponent_rider)!=g_opponent_horse) {
        if(now-g_trial.remount_since>kTrialRemountSeconds){g_trial.summary="the host would not remount";AbortRace("trial: the host would not remount",true,Ender::Game);return;}
        if(now-g_trial.remount_asked>=.5){PED::SET_PED_ONTO_MOUNT(g_opponent_rider,g_opponent_horse,-1,true);g_trial.remount_asked=now;}
        return;
    }
    if(g_trial.spectator.Active() && !far_catchup::PhysicalReady(g_opponent_horse)) {
        return;
    }
    // Mounted: the trackers start from here - the lap engine's too, which
    // otherwise holds the fall's metres until the rider passes them again and
    // never moves after a disqualification - and the driver on lines rebuilt
    // from what was learned.
    g_trial.remounting=false;g_trial.off_course_noted=false;
    const Vector3 p=ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false);
    g_course_rival.Reset();
    if(CurrentRules().uses_lap_engine && g_laps.Frozen()) {
        if(!g_laps.ResumeOpponent(p,now))Log("trial: the lap engine would not resume the host's progress");
        g_laps.Export(g_laps.opponent,p,g_course_rival);
    } else {g_course_rival.furthest=g_trial.dispatch_along;g_course_rival.Update(g_course,p);}
    // Reset again after the remount wait: no old observation may span it.
    ResetTrialMotion();g_fixed_progress_along=g_trial.dispatch_along;
    if(!StartOriginalOpponentRoute(true))return;
    HostPhysicalDispatchStarted();
    TrialForceBegin();
    Log("trial: rider "+std::to_string(g_trial.riders)+" started at "+std::to_string(static_cast<int>(g_trial.dispatch_along))+
        " m map_revision="+std::to_string(g_obstacles.revision)+" line_build_serial="+std::to_string(g_line_build_serial)+
        " line_map_revision="+std::to_string(g_obstacles_line_revision)+" route_serial="+std::to_string(g_course_serial)+
        " impact_event="+std::to_string(g_host_learner.drops)+" impact_result="+collision_learner::Name(g_host_learner.outcome)+
        " driver="+(g_competitive_follower.control?"line":RequiredRecordedLine()?"recording-retry-pending":"traffic"));
}
void AdvanceFarRider(const char* name,far_catchup::Runner& runner,native_follower::Follower& follower,Ped rider,Ped horse,
    const race_pace::Plan& plan,const course::Progress& progress,Vector3 player,double now,
    native_follower::Recovery& recovery)
{
    if(!rider || !horse || !ENTITY::DOES_ENTITY_EXIST(rider) || !ENTITY::DOES_ENTITY_EXIST(horse) ||
        ENTITY::IS_ENTITY_DEAD(rider) || ENTITY::IS_ENTITY_DEAD(horse)) {runner.Reset();return;}
    const bool mounted=PED::IS_PED_ON_MOUNT(rider) && PED::GET_MOUNT(rider)==horse;
    const Vector3 before=ENTITY::GET_ENTITY_COORDS(horse,true,false);
    const bool on_screen=ENTITY::IS_ENTITY_ON_SCREEN(horse) || ENTITY::IS_ENTITY_ON_SCREEN(rider);
    const bool asleep=ENTITY::GET_ENTITY_COLLISION_DISABLED(horse)!=0;
    race_pace::Command clearance{false,std::numeric_limits<float>::infinity(),3};
    native_follower::ProtectVehiclePace(recovery,clearance,progress.current.along,before,now,horse,&plan,runner.speed);
    if(far_catchup::HoldForVehicle(runner,follower,rider,clearance.vehicle_blocked,now))return;
    far_catchup::HoldPendingSpeed(runner,follower,now);
    const auto step=runner.Tick(plan,{now,before,player,progress.current.along,
        ENTITY::GET_ENTITY_SPEED(horse),progress.current.valid && !progress.continuity_lost &&
        progress.current.distance<25.0F,asleep,on_screen,mounted,g_laps.plan.single_loop,clearance.target_mps});
    if(step.entered)Log(std::string("far rival simulation entered ")+name+" distance_m="+
        std::to_string(static_cast<int>(destination::Distance(before,player)))+" line_m="+
        std::to_string(static_cast<int>(progress.current.along))+" start_kmh="+std::to_string(step.speed*3.6F)+
        " quick_handoff="+(step.resumed?"1":"0"));
    if(step.visible_bridge_started)Log(std::string("far rival visible handoff ")+name+
        " kinematic_move_paused=1 collision_still_disabled=1 planned_kmh="+std::to_string(step.speed*3.6F)+
        " distance_m="+std::to_string(static_cast<int>(destination::Distance(before,player))));
    if(!far_catchup::ObserveHandoff(follower,rider,step)) {
        runner.Suspend(now);Log(std::string("far rival navigation suspension refused ")+name);return;
    }
    if(step.left) {
        const float native_before=ENTITY::GET_ENTITY_SPEED(horse);
        // Velocity is transferred later, AFTER re-entering a fresh forward
        // recording and receiving a valid native task readback.
        Log(std::string("far rival simulation released ")+name+" reason="+(step.reason?step.reason:"unknown")+
            " moves="+std::to_string(step.moves)+" on_screen="+(on_screen?"1":"0")+
            " native_before_kmh="+std::to_string(native_before*3.6F)+
            " planned_kmh="+std::to_string(step.speed*3.6F)+" rejoin_pending="+
            (follower.simulation_suspended?"1":"0"));
    }
    if(step.wake_requested) {
        STREAMING::REQUEST_COLLISION_AT_COORD(before);
        const bool loaded=ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(horse)!=0;
        const bool ready=loaded && !ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(horse) &&
            !ENTITY::_IS_ENTITY_FROZEN(horse);
        if(ready) {
            // The engine may keep a distant mount non-colliding after its
            // geometry has streamed in. This is our own race mount, now
            // visible and locally loaded; restore physics before handing off.
            ENTITY::SET_ENTITY_COLLISION(horse,true,true);
            PHYSICS::ACTIVATE_PHYSICS(horse);
        }
        if(step.visible_bridge_started)Log(std::string("far rival visible wake requested ")+name+
            " collision_loaded="+(loaded?"1":"0")+" ready="+(ready?"1":"0")+
            " collision_restored="+(!ENTITY::GET_ENTITY_COLLISION_DISABLED(horse)?"1":"0"));
    }
    if(!step.move)return;
    // Defensive guard: a future runner change must not reintroduce visible
    // coordinate/heading writes to mounted riders.
    if(on_screen) {runner.Suspend(now);Log(std::string("far rival visible move rejected ")+name);return;}
    // During the off-screen hysteresis band collision may already be enabled.
    // The native task is suspended; prevent residual physical velocity from
    // being added to the route-distance step we are about to apply.
    ENTITY::SET_ENTITY_VELOCITY(horse,0,0,0);
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(horse,step.position,false,false,false);
    ENTITY::SET_ENTITY_HEADING(horse,step.heading);
    const auto after=ENTITY::GET_ENTITY_COORDS(horse,true,false);
    const auto rider_at=ENTITY::GET_ENTITY_COORDS(rider,true,false);
    if(destination::Distance(after,step.position)>2.0F || destination::Distance(after,rider_at)>6.0F ||
        !PED::IS_PED_ON_MOUNT(rider) || PED::GET_MOUNT(rider)!=horse) {
        // This build may refuse an off-screen ped move, or a mount attachment
        // might not follow. Do not keep advancing a private clock and then
        // jump the rider forward all at once; restore the old spot and retry
        // only after a short cooldown.
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(horse,before,false,false,false);
        Log(std::string("far rival simulation rejected ")+name+" after native move/readback; cooling down");
        runner.Suspend(now);return;
    }
}
void TickFarRivals(Vector3 player,double now)
{
    if(!CurrentRules().Hosted() || !HasRecordedCourse())return;
    // A trial's native focus follows its host even when the user orbits the
    // camera away. Never substitute an off-screen coordinate simulation.
    if(g_trial.active)return;
    if(!race_speed_policy::Enabled()) {
        // Return all simulated racers to native navigation; the follower keeps
        // the suspension token until its stock playback has been reissued.
        g_host_far_runner.Reset();
        for(auto& rival:g_extra_rivals)rival.far_runner.Reset();
    }
    if(!race_speed_policy::Enabled() || !g_far_catchup_enabled) {
        const auto observe=[&](const char* name,native_far_probe::Probe& probe,
            Ped rider,Ped horse,const course::Progress& progress,const native_follower::Follower& follower){
            if(!rider || !horse || !ENTITY::DOES_ENTITY_EXIST(rider) || !ENTITY::DOES_ENTITY_EXIST(horse)){
                probe.Reset();return;
            }
            const auto at=ENTITY::GET_ENTITY_COORDS(horse,true,false);
            const float distance=destination::Distance(at,player);
            const bool visible=ENTITY::IS_ENTITY_ON_SCREEN(horse) || ENTITY::IS_ENTITY_ON_SCREEN(rider);
            const bool distant=distance>far_catchup::kEnterDistance &&
                progress.current.valid && !progress.continuity_lost;
            if(const auto reading=probe.Observe(now,progress.current.along,at.x,at.y,distant))
                Log(std::string("native far trial ")+name+" seconds="+std::to_string(reading->seconds)+
                    " distance_m="+std::to_string(distance)+" route_delta_m="+
                    std::to_string(reading->course_metres)+" world_delta_m="+
                    std::to_string(reading->world_metres)+" native_kmh="+
                    std::to_string(ENTITY::GET_ENTITY_SPEED(horse)*3.6F)+" collision_disabled="+
                    std::to_string(ENTITY::GET_ENTITY_COLLISION_DISABLED(horse))+" waypoint="+
                    std::to_string(TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(rider,nullptr))+
                    " status="+std::to_string(static_cast<int>(follower.status))+
                    " effort="+std::to_string(follower.override_readback)+
                    " on_screen="+std::to_string(visible)+
                    " absolute_speed="+std::to_string(follower.override_absolute)+
                    " requested_mps="+std::to_string(follower.override_absolute?follower.override_request:0)+
                    " table_state="+horse_motion::native_speed_table::Name(follower.speed_table.Status())+
                    " table_native_mps="+std::to_string(follower.speed_table.NativeTop())+
                    " table_applied_mps="+std::to_string(follower.speed_table.AppliedTop())+
                    " table_checkpoint="+std::to_string(follower.speed_table.Checkpoint())+
                    " table_observed_task_mps="+std::to_string(follower.speed_table.ObservedTaskTop())+
                    " table_observed_motion_mps="+std::to_string(follower.speed_table.ObservedMotionTop())+
                    " table_derived_mps="+std::to_string(follower.speed_table.DerivedTop())+
                    " table_range_cap_mps="+std::to_string(follower.speed_table.AppliedLimit())+
                    " move_rate_phase="+std::to_string(follower.move_rate.observation.phase)+
                    " move_rate_request="+std::to_string(follower.move_rate.observation.requested)+
                    " move_rate_accepted="+std::to_string(follower.move_rate.observation.accepted)+
                    " move_rate_readback="+(follower.move_rate.observation.after?
                        std::to_string(*follower.move_rate.observation.after):"unavailable")+
                    follower.native_vector.Detail()+
                    " measured_world_kmh="+std::to_string(reading->world_metres/reading->seconds*3.6));
        };
        if(!g_main_rival_out && !g_trial.remounting)
            observe("host",g_host_native_far_probe,g_opponent_rider,g_opponent_horse,
                g_course_rival,g_competitive_follower);
        if(CurrentRules().Field()){
            if(g_extra_native_far_probes.size()!=g_extra_rivals.size())
                g_extra_native_far_probes.resize(g_extra_rivals.size());
            for(std::size_t i=0;i<g_extra_rivals.size();++i){
                auto& e=g_extra_rivals[i];
                if(e.out || e.retiring)continue;
                const auto name="entrant="+std::to_string(i+1);
                observe(name.c_str(),g_extra_native_far_probes[i],e.rider.horse,e.horse.horse,
                    e.proxy,e.follower);
            }
        }
        return;
    }
    if(!g_main_rival_out && !g_trial.remounting)
        AdvanceFarRider("host",g_host_far_runner,g_competitive_follower,g_opponent_rider,g_opponent_horse,
            g_pace_plan,g_course_rival,player,now,g_host_navigation);
    if(CurrentRules().Field())for(std::size_t i=0;i<g_extra_rivals.size();++i) {
        auto& e=g_extra_rivals[i];
        if(e.out || e.retiring || HorseIdentity(e.horse.horse)!=e.horse || HorseIdentity(e.rider.horse)!=e.rider)continue;
        const std::string name="entrant="+std::to_string(i+1);
        AdvanceFarRider(name.c_str(),e.far_runner,e.follower,e.rider.horse,e.horse.horse,
            e.pace,e.proxy,player,now,e.navigation);
    }
}
