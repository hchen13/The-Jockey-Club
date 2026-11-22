// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Race launch and ordered per-frame jockey execution.
#include "launch.inl"
void AdvanceRace(){
    const double now=Now();
    const bool speed_override=race_speed_policy::Enabled();
    const bool simulate_far=speed_override && far_simulation_enabled;
    if(now-go_at>kRaceTimeout){ReturnFromRace(bm::Finish::AccidentalVoid,0,"race timeout");return;}
    if(now>=next_visibility_audit){
        next_visibility_audit=now+1;
        for(auto& j:jockeys)if(Same(j.rider_id) && !ENTITY::IS_ENTITY_DEAD(j.rider) &&
            (!ENTITY::IS_ENTITY_VISIBLE(j.rider) || ENTITY::GET_ENTITY_ALPHA(j.rider)<250)){
            Log(std::string("rider visibility recovered profile=")+j.profile->key+
                " preset="+std::to_string(j.appearance.current)+
                " alpha="+std::to_string(ENTITY::GET_ENTITY_ALPHA(j.rider)));
            ENTITY::RESET_ENTITY_ALPHA(j.rider);ENTITY::SET_ENTITY_VISIBLE(j.rider,true);
        }
    }
    if(!scene.TickActive(now,false,[](Ped){})){
        ReturnFromRace(bm::Finish::AccidentalVoid,0,"street isolation failed");return;
    }
    // Sample before the mounted check can eliminate the collision's rider.
    TickObstacleProfiles(true);
    std::array<Vector3,kRacers> positions{};std::array<bool,kRacers> alive{};
    // Fresh positions of the bounded race roster, shared by all jockeys. A
    // quarter-second-old neighbour could be five metres from its stored point.
    // Decisions remain 4 Hz; this also supplies the existing race adjudication.
    static std::vector<following::Mount> traffic;
    traffic.clear();
    for(std::size_t k=0;k<jockeys.size();++k) {
        const auto& other=jockeys[k];const auto& entrant=field.Riders()[k];
        const bool same_horse=Same(other.horse_id);
        if(same_horse)positions[k]=ENTITY::GET_ENTITY_COORDS(other.horse,true,false);
        alive[k]=same_horse && Same(other.rider_id) && !ENTITY::IS_ENTITY_DEAD(other.horse) &&
            !ENTITY::IS_ENTITY_DEAD(other.rider) && PED::GET_MOUNT(other.rider)==other.horse;
        if(!alive[k] || entrant.progress.continuity_lost)continue;
        const float at=entrant.progress.Metres(plan.course);
        traffic.push_back({other.horse,positions[k],
            ENTITY::GET_ENTITY_FORWARD_VECTOR(other.horse),at,ENTITY::GET_ENTITY_SPEED(other.horse),
            race_pace::At(plan.course.path,at).z,true});
    }
    const auto pursuit_gaps=bm::PursuitGaps(field.Riders(),plan.course,
        std::span<const bool>(alive.data(),jockeys.size()));
    const Ped player=PLAYER::PLAYER_PED_ID();
    Hash held_weapon=0;
    for(int hand:{0,1})if(WEAPON::GET_CURRENT_PED_WEAPON(player,&held_weapon,false,hand,false)){
        if(held_weapon==MISC::GET_HASH_KEY("WEAPON_THROWN_DYNAMITE") ||
           held_weapon==MISC::GET_HASH_KEY("WEAPON_THROWN_MOLOTOV") ||
           held_weapon==MISC::GET_HASH_KEY("WEAPON_THROWN_DYNAMITE_VOLATILE") ||
           held_weapon==MISC::GET_HASH_KEY("WEAPON_THROWN_VOLATILE_FIRE_BOTTLE")){
            if(PAD::IS_CONTROL_PRESSED(0,INPUT_ATTACK))player_throw_at=now;
        }
    }
    for(std::size_t i=0;i<jockeys.size();++i){
        auto& j=jockeys[i];
        if(!speed_override){
            j.chase.Reset();j.command.catchup_fraction=0;j.burst_earned=false;
            j.catchup.Reset();j.far_slow_since=0;
            j.follower.ReleaseSpeedTrials();
            j.follower.simulation_speed=0;j.follower.handoff_speed=0;
            j.follower.handoff_until=0;j.follower.handoff_settled_since=-1;
            j.follower.ResumeVehicleWait();
        }
        if(!alive[i]){
            j.racing_assist.Reset();
            j.chase.Reset();j.command.catchup_fraction=0;j.burst_earned=false;
            j.below_walking.Observe(now,0,false);
            if(now-player_throw_at<6 && (WEAPON::_HAS_ENTITY_BEEN_DAMAGED_BY_WEAPON_RECENTLY(j.rider,
                MISC::GET_HASH_KEY("WEAPON_THROWN_DYNAMITE"),6000) ||
                WEAPON::_HAS_ENTITY_BEEN_DAMAGED_BY_WEAPON_RECENTLY(j.horse,
                MISC::GET_HASH_KEY("WEAPON_THROWN_DYNAMITE"),6000) ||
                WEAPON::_HAS_ENTITY_BEEN_DAMAGED_BY_WEAPON_RECENTLY(j.rider,
                MISC::GET_HASH_KEY("WEAPON_THROWN_MOLOTOV"),6000) ||
                WEAPON::_HAS_ENTITY_BEEN_DAMAGED_BY_WEAPON_RECENTLY(j.horse,
                MISC::GET_HASH_KEY("WEAPON_THROWN_MOLOTOV"),6000)))player_sabotaged=true;
            continue;
        }
        // A finisher has a permanent place. Continuing to drive the native
        // GPS beyond the last node made its inevitable restart fail, voiding
        // the entire show and refunding even a losing fourth-place ticket.
        if(field.Riders()[i].status==bm::Field::Status::Finished){
            j.racing_assist.Reset();
            j.chase.Reset();j.command.catchup_fraction=0;j.burst_earned=false;
            j.below_walking.Observe(now,0,false);
            TickFinishedJockey(i,now);continue;
        }
        if(field.Riders()[i].status!=bm::Field::Status::Racing){j.racing_assist.Reset();j.chase.Reset();j.command.catchup_fraction=0;
            j.burst_earned=false;j.below_walking.Observe(now,0,false);continue;}
        j.racing_assist.Tick(j.rider);
        const auto& progress=field.Riders()[i].progress;
        const float along=progress.Metres(plan.course);
        if(!j.start_lag_logged && now-go_at>=8 && along<3){
            j.start_lag_logged=true;
            Log("spectator launch lag rider="+std::to_string(i)+" profile="+j.profile->key+
                " slot="+std::to_string(j.grid_slot)+" native_status="+
                std::to_string(static_cast<int>(j.follower.status))+" node="+
                std::to_string(j.follower.progress)+" chunk_end="+
                std::to_string(j.follower.chunk_last)+" playing="+
                std::to_string(TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(j.horse,nullptr))+" mps="+
                std::to_string(ENTITY::GET_ENTITY_SPEED(j.horse)));
        }
        const auto finish_this_frame=[&]{
            auto projected=progress;
            return projected.Update(plan.course,positions[i],now).finish_fraction.has_value();
        };
        // Observe sleep/kinematic movement BEFORE reading the native task.
        // Its old node can be kilometres behind qualified course progress;
        // that is a handoff, not a navigation failure or a retry-budget loss.
        const bool on_screen=ENTITY::IS_ENTITY_ON_SCREEN(j.horse)||ENTITY::IS_ENTITY_ON_SCREEN(j.rider);
        const auto before=positions[i];
        const float native_speed=ENTITY::GET_ENTITY_SPEED(j.horse);
        j.below_walking.Observe(now,native_speed);
        const float spectator_distance=destination::Distance(before,ENTITY::GET_ENTITY_COORDS(player,true,false));
        const bool physical_ready=far_catchup::PhysicalReady(j.horse);
        if(!speed_override && j.follower.simulation_suspended){
            // Native coarse locomotion can resume at qualified progress even
            // before distant collision wakes. Do not transfer simulated speed.
            const auto restored=far_catchup::RejoinNative(j.follower,j.rider,j.avoidance.path,
                j.avoidance.reference,along,before,now,physical_ready,false,
                [&]{return "TJC_BOOK_REJOIN_"+std::to_string(token)+"_"+std::to_string(i)+"_"+
                    std::to_string(++recording_serial);},true);
            if(restored==far_catchup::Rejoin::Started)j.controller={};
        }
        if(!simulate_far){
            if(const auto reading=j.native_far_probe.Observe(now,along,before.x,before.y,
                spectator_distance>far_catchup::kEnterDistance && !progress.continuity_lost))
                Log("native far trial jockey="+std::to_string(i)+" seconds="+
                    std::to_string(reading->seconds)+" distance_m="+std::to_string(spectator_distance)+
                    " route_delta_m="+std::to_string(reading->course_metres)+
                    " world_delta_m="+std::to_string(reading->world_metres)+
                    " native_kmh="+std::to_string(native_speed*3.6F)+
                    " collision_disabled="+std::to_string(ENTITY::GET_ENTITY_COLLISION_DISABLED(j.horse))+
                    " waypoint="+std::to_string(TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(j.rider,nullptr))+
                    " status="+std::to_string(static_cast<int>(j.follower.status))+
                    " effort="+std::to_string(j.follower.override_readback)+
                    " on_screen="+std::to_string(on_screen)+
                    " absolute_speed="+std::to_string(j.follower.override_absolute)+
                    " requested_mps="+std::to_string(j.follower.override_absolute?j.follower.override_request:0)+
                    " table_state="+horse_motion::native_speed_table::Name(j.follower.speed_table.Status())+
                    " table_checkpoint="+std::to_string(j.follower.speed_table.Checkpoint())+
                    " table_native_mps="+std::to_string(j.follower.speed_table.NativeTop())+
                    " table_applied_mps="+std::to_string(j.follower.speed_table.AppliedTop())+
                    " table_observed_task_mps="+std::to_string(j.follower.speed_table.ObservedTaskTop())+
                    " table_observed_motion_mps="+std::to_string(j.follower.speed_table.ObservedMotionTop())+
                    " table_derived_mps="+std::to_string(j.follower.speed_table.DerivedTop())+
                    " table_range_cap_mps="+std::to_string(j.follower.speed_table.AppliedLimit())+
                    " move_rate_phase="+std::to_string(j.follower.move_rate.observation.phase)+
                    " move_rate_request="+std::to_string(j.follower.move_rate.observation.requested)+
                    " move_rate_accepted="+std::to_string(j.follower.move_rate.observation.accepted)+
                    " move_rate_readback="+(j.follower.move_rate.observation.after?
                        std::to_string(*j.follower.move_rate.observation.after):"unavailable")+
                    j.follower.native_vector.Detail()+
                    " measured_world_kmh="+std::to_string(reading->world_metres/reading->seconds*3.6));
        }else{
        const bool asleep=ENTITY::GET_ENTITY_COLLISION_DISABLED(j.horse)!=0;
        if(!on_screen && spectator_distance>far_catchup::kEnterDistance && native_speed<1.0F){
            if(j.far_slow_since==0)j.far_slow_since=now;
        }else j.far_slow_since=0;
        // A remote mount can report collision enabled while its physics has
        // stopped being simulated. Only an unseen horse that has stayed slow
        // for a full second may take the off-screen kinematic path.
        const bool dormant=!on_screen && spectator_distance>far_catchup::kEnterDistance &&
            j.far_slow_since>0 && now-j.far_slow_since>=1.0;
        race_pace::Command clearance{false,std::numeric_limits<float>::infinity(),3};
        native_follower::ProtectVehiclePace(j.navigation,clearance,along,before,now,j.horse,&j.pace,j.catchup.speed);
        if(clearance.vehicle_limited)j.command=clearance;
        if(far_catchup::HoldForVehicle(j.catchup,j.follower,j.rider,clearance.vehicle_blocked,now))continue;
        far_catchup::HoldPendingSpeed(j.catchup,j.follower,now);
        const auto step=j.catchup.Tick(j.pace,{now,before,ENTITY::GET_ENTITY_COORDS(player,true,false),along,
            native_speed,!progress.continuity_lost,asleep||dormant,on_screen,true,false,clearance.target_mps});
        if(step.entered)Log("far jockey entered index="+std::to_string(i)+
            " distance_m="+std::to_string(static_cast<int>(spectator_distance))+
            " dormant="+std::to_string(dormant)+" planned_kmh="+std::to_string(step.speed*3.6F));
        if(!far_catchup::ObserveHandoff(j.follower,j.rider,step)){
            j.catchup.Suspend(now);Log("far jockey navigation suspension refused index="+std::to_string(i));
        }
        if(step.wake_requested){
            STREAMING::REQUEST_COLLISION_AT_COORD(before);
            if(ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(j.horse) &&
                !ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(j.horse)){
                ENTITY::SET_ENTITY_COLLISION(j.horse,true,true);PHYSICS::ACTIVATE_PHYSICS(j.horse);
            }
        }
        if(step.left)Log("far jockey released index="+std::to_string(i)+
            " reason="+(step.reason?step.reason:"unknown")+
            " on_screen="+std::to_string(on_screen)+
            " native_kmh="+std::to_string(native_speed*3.6F)+
            " planned_kmh="+std::to_string(step.speed*3.6F));
        if(step.move && !on_screen && j.follower.simulation_suspended){
            ENTITY::SET_ENTITY_VELOCITY(j.horse,0,0,0);
            ENTITY::SET_ENTITY_COORDS_NO_OFFSET(j.horse,step.position,false,false,false);
            ENTITY::SET_ENTITY_HEADING(j.horse,step.heading);
            const auto after=ENTITY::GET_ENTITY_COORDS(j.horse,true,false);
            const auto rider_at=ENTITY::GET_ENTITY_COORDS(j.rider,true,false);
            if(destination::Distance(after,step.position)>2 || destination::Distance(after,rider_at)>6 ||
                PED::GET_MOUNT(j.rider)!=j.horse){
                ENTITY::SET_ENTITY_COORDS_NO_OFFSET(j.horse,before,false,false,false);j.catchup.Suspend(now);
                Log("far jockey move rejected index="+std::to_string(i));
            }else positions[i]=after;
        }
        const auto rejoin=far_catchup::RejoinNative(j.follower,j.rider,j.avoidance.path,
            j.avoidance.reference,along,positions[i],now,physical_ready && !j.catchup.active,false,
            [&]{return "TJC_BOOK_REJOIN_"+std::to_string(token)+"_"+std::to_string(i)+"_"+
                std::to_string(++recording_serial);},on_screen,j.navigation.vehicles.Holding());
        if(rejoin==far_catchup::Rejoin::Started){
            j.controller={};
            Log("far jockey native rejoined index="+std::to_string(i)+" entry="+
                std::to_string(j.follower.entry)+" along_m="+std::to_string(along)+" planned_kmh="+
                std::to_string(j.follower.handoff_speed*3.6F));
        }
        }
        j.command=j.controller.Update(j.pace,along,ENTITY::GET_ENTITY_SPEED(j.horse));
        j.command.catchup_fraction=speed_override?j.chase.Update(token,j.horse_id.address,j.horse,j.horse_id.model,
            pursuit_gaps[i]):0;
        const float passing_target=race_pace::PassingTarget(j.pace,j.command,j.ranks.speed+1,
            j.burst_earned?bm::kBurstLevels:0);
        if(race_pace::diagnostics_enabled)j.command.trace.evaluated_game_s=now;
        if(speed_override)native_follower::ProtectVehiclePace(j.navigation,j.command,along,positions[i],now,j.horse,&j.pace);
        following::RetireHandoff(j.follower,j.command);
        if(j.follower.simulation_suspended) {
            j.navigation.passing.Reset();j.follower.RequestRouteOffset(0);
        } else {
            const following::Mount observed{j.horse,positions[i],ENTITY::GET_ENTITY_FORWARD_VECTOR(j.horse),along,
                native_speed,race_pace::At(plan.course.path,along).z,true};
            const auto next=static_cast<std::size_t>(std::max(0,j.follower.progress+1));
            const float readback_end=next<j.follower.metres.size()?j.follower.metres[next]:along;
            const bool allow_new=!j.command.braking && !j.command.vehicle_limited &&
                !j.command.vehicle_blocked && !j.command.terrain_limited;
            j.follower.RequestRouteOffset(j.navigation.passing.Update(plan.course.path,plan.course.strict_half_width,
                j.pace,&line_obstacles,observed,traffic,now,readback_end,passing_target,allow_new));
        }
        if(simulate_far && far_catchup::NativePaused(j.catchup,j.follower,physical_ready)){
            j.chase.Pause();j.command.catchup_fraction=0;continue;
        }
        // Keep the recording and task alive; passing changes only its local side target.
        if(finish_this_frame())continue;
        const auto renewal=native_follower::Maintain(j.follower,j.navigation,j.rider,
            j.avoidance.path,j.avoidance.reference,along,positions[i],now,
            speed_override?j.command.effort:3.F,
            ENTITY::GET_ENTITY_SPEED(j.horse),[&]{return "TJC_BOOK_"+std::to_string(token)+"_"+
                std::to_string(i)+"_"+std::to_string(++recording_serial);},
            native_far_speed::ForRider(j.pace,j.command,speed_override && native_far_speed_enabled && !simulate_far,
                j.horse,j.rider,spectator_distance,!progress.continuity_lost,
                j.burst_earned?bm::kBurstLevels:0,native_speed_table_enabled || native_vector_trial_enabled,
                native_vector_trial_enabled?&j.follower.native_vector.learning:nullptr,pursuit_gaps[i],j.profile->key),
            speed_override && native_speed_table_enabled && !j.command.NativePaceLimited(),
            speed_override && native_move_rate_trial_enabled && !j.command.NativePaceLimited(),
            speed_override && native_vector_trial_enabled,speed_override?&j.command:nullptr);
        if(renewal==native_follower::Renewal::Started) {
            j.retries.Record(along);
            Log("jockey navigation renewed index="+std::to_string(i)+" along="+std::to_string(along)+
                " entry="+std::to_string(j.follower.entry)+" chunk_end="+
                std::to_string(j.follower.chunk_last)+" renewal="+std::to_string(j.navigation.renewals));
        }else if(renewal==native_follower::Renewal::Refused) {
            // Maintain owns the rate-limited forward traffic task while the
            // recording renews. Never OUT/refund/cancel for task scheduling.
            Log("jockey forward navigation renewed index="+std::to_string(i)+" along="+
                std::to_string(along)+" recording not yet available");
        }
        if(j.command.vehicle_blocked)continue;
        if(speed_override && !j.follower.control)
            native_far_speed::ApplyVector(j.follower.native_vector,j.rider,
                native_far_speed::ForRider(j.pace,j.command,speed_override && native_far_speed_enabled && !simulate_far,
                    j.horse,j.rider,spectator_distance,!progress.continuity_lost,
                    j.burst_earned?bm::kBurstLevels:0,true,
                    native_vector_trial_enabled?&j.follower.native_vector.learning:nullptr,pursuit_gaps[i],j.profile->key),
                native_vector_trial_enabled,j.command.NativePaceLimited());
        following::RetireHandoff(j.follower,j.command);
        if(!j.command.NativePaceLimited() && simulate_far && far_catchup::TransferHandoffVelocity(j.follower,j.pace,
            {now,positions[i],{},along,ENTITY::GET_ENTITY_SPEED(j.horse),!progress.continuity_lost,
                false,on_screen,true,false},j.horse,physical_ready))
            Log("far jockey native velocity transferred index="+std::to_string(i));
    }
    std::array<bm::Field::Status,kRacers> before_status{};
    std::array<int,kRacers> before_offenses{};
    for(std::size_t i=0;i<jockeys.size();++i){
        before_status[i]=field.Riders()[i].status;
        before_offenses[i]=field.Riders()[i].progress.boundary.Offenses();
    }
    if(!field.Update(std::span<const Vector3>(positions.data(),jockeys.size()),
        std::span<const bool>(alive.data(),jockeys.size()),now)){
        ReturnFromRace(bm::Finish::AccidentalVoid,0,"field update invalid");return;
    }
    for(std::size_t i=0;i<jockeys.size();++i) {
        auto& j=jockeys[i];
        const auto& boundary=field.Riders()[i].progress.boundary;
        if(boundary.Offenses()>before_offenses[i])
            Log("boundary offense rider="+std::to_string(i)+" profile="+j.profile->key+
                " count="+std::to_string(boundary.Offenses())+" limit="+std::to_string(boundary.Limit()));
        if(field.Riders()[i].status!=bm::Field::Status::Racing){j.chase.Reset();j.command.catchup_fraction=0;}
        j.dot.Update(Same(j.rider_id)?j.rider:0,
            ticket.Stake()>0 && j.profile->id==ticket.Selection(),
            j.burst_active && field.Riders()[i].status==bm::Field::Status::Racing,
            tr::Text(j.profile->name,j.profile->chinese));
    }
    for(std::size_t i=0;i<jockeys.size();++i)if(before_status[i]==bm::Field::Status::Racing &&
        field.Riders()[i].status==bm::Field::Status::Out){
        auto& j=jockeys[i];j.racing_assist.Reset();
        // Elimination ends this consumer once. Otherwise its last lateral
        // request and speed leases survive without further traffic checks.
        j.navigation.passing.Reset();j.follower.Reset();
        const auto& result=field.Riders()[i];
        const bool identities_valid=Same(j.rider_id) && Same(j.horse_id);
        const bool dead=identities_valid && (ENTITY::IS_ENTITY_DEAD(j.rider)||ENTITY::IS_ENTITY_DEAD(j.horse));
        const char* reason=RecordingEliminationReason(alive[i],identities_valid,dead,
            result.progress.section_jump,result.progress.boundary.Offenses(),result.navigation_timeout);
        j.recording_out_reason=reason;
        if(result.navigation_timeout || !Same(j.rider_id) || !Same(j.horse_id))
            learning_excluded=true;
        Log("jockey out index="+std::to_string(i)+" profile="+j.profile->key+
            " reason="+reason+
            " no_progress_s="+std::to_string(now-result.last_progress_at)+
            " navigation_renewals="+std::to_string(j.navigation.renewals)+
            " at="+std::to_string(field.Riders()[i].progress.Metres(plan.course))+
            " xyz="+(Same(j.horse_id)?std::to_string(positions[i].x)+","+
                std::to_string(positions[i].y)+","+std::to_string(positions[i].z):"unavailable"));
    }
    for(std::size_t i=0;i<jockeys.size();++i)if(before_status[i]==bm::Field::Status::Racing &&
        field.Riders()[i].status==bm::Field::Status::Finished){
        jockeys[i].racing_assist.Reset();
        jockeys[i].finished_at=now;
        jockeys[i].finish_goodbye_pending=true;
        Log("jockey finished index="+std::to_string(i)+" profile="+jockeys[i].profile->key+
            " reason="+(field.Riders()[i].survival_win?"last eligible survivor":"course finish")+
            " place="+std::to_string(field.Place(i))+" finish_at="+
            std::to_string(field.Riders()[i].finish_at));
        TickFinishedJockey(i,now);
    }
    // A knocked-down rider normally loses. An explosive/fire sabotage by the
    // spectator forfeits the wager and ends the whole show, not just one rider.
    if(player_sabotaged){ReturnFromRace(bm::Finish::PlayerSabotage,0,"spectator sabotage");return;}
    if(!field.Running()){
        int place=0;
        for(std::size_t i=0;i<jockeys.size();++i)if(jockeys[i].profile->id==ticket.Selection())place=field.Place(i);
        for(std::size_t i=0;i<jockeys.size();++i){
            const auto& rider=field.Riders()[i];
            Log("final rider="+std::to_string(i)+" profile="+jockeys[i].profile->key+
                " status="+(rider.status==bm::Field::Status::Finished?"finished":"out")+
                " place="+std::to_string(rider.place)+" finish_at="+std::to_string(rider.finish_at)+
                " selected="+std::to_string(jockeys[i].profile->id==ticket.Selection()));
        }
        LearnCompletedRace();
        const bool no_winner=field.VisibleOrder().empty();
        if(no_winner)Log("no eligible winner after simultaneous elimination; accidental void");
        Settle(no_winner?bm::Finish::AccidentalVoid:place?bm::Finish::Placed:bm::Finish::Lost,place);
        Log("all riders resolved; waiting for spectator to leave");
        StopLeaderCamera(false);camera_blackout.Restore();RemoveCameraPrompts();camera_director.Reset();
        native_standings.Close(Now());
        if(music_started){horse_races::EndBookmakerAudio(true);music_started=false;}
        horse_motion::speed_learning::runtime::Flush();
        for(auto& rider:jockeys){rider.chase.Reset();rider.command.catchup_fraction=0;rider.burst_earned=false;}
        phase=Phase::Results;StartSlowdown();result_catalog_retry_at=Now();
    }
}
