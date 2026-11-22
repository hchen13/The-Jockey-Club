#pragma once
#include "race_course_native.h"
#include "horse_motion_native.h"
#include "race_native_far_speed.h"
#include "horse_native_speed_table_windows.h"
#include "horse_native_move_rate_windows.h"
#include "horse_native_vector_windows.h"
#include "race_native_passing.h"
#include "race_vehicle_clearance_native.h"
#include <functional>

namespace horse_races::native_follower {
#include "race_native_follower_recovery.inl"
struct Follower {
    course::NativeGps recording; // Own resource, NEVER shared with a GPS consumer.
    horse_motion::Identity rider;
    Status status=Status::Idle;
    Failure failure=Failure::None;
    double started=0,missing_since=-1,lagging_since=-1;
    double missed_since=-1;
    int missed_next=-1;
    Vector3 missed_position{};
    int progress=-1,starts=0,entry=-1,accepted_progress=-1; // progress and entry: nodes of the whole line.
    std::size_t base=0,chunk_last=0,nodes=0;                // The registered chunk: line nodes [base, chunk_last] of `nodes`.
    std::vector<float> metres;                              // Order metres of every node of the line.
    float override_readback=0;
    float override_request=0;
    bool override_absolute=false, speed_override_written=false;
    float override_transition_rate=native_far_speed::kWaypointTransitionRate;
    // Observation only, monotonic through task replacement within this owner.
    std::uint64_t task_generation=0,speed_write_sequence=0;
    double speed_write_at=-1;
    float route_offset_left=0,route_offset_submitted_left=0;
    std::uint64_t offset_write_sequence=0;
    double offset_write_at=-1;
    horse_motion::native_speed_table::WindowsLease speed_table;
    horse_motion::native_move_rate::WindowsTrial move_rate;
    horse_motion::native_vector::WindowsTrial native_vector;
    bool control=false,ever_playing=false;
    bool playback_readback=false;
    int native_index_readback=-1;
    bool simulation_suspended=false;
    bool vehicle_paused=false;
    float simulation_speed=0,handoff_speed=0;
    double simulation_awake_since=-1,handoff_until=0,handoff_settled_since=-1;
    bool RequestRouteOffset(float left) {
        if(!std::isfinite(left))return false;
        route_offset_left=std::clamp(left,-1.8F,1.8F);return true;
    }
    bool ApplyRouteOffset(double now=-1) {
        if(route_offset_left==route_offset_submitted_left)return true;
        if(!recording.owned || !control || simulation_suspended || vehicle_paused ||
           !rider.Valid() || horse_motion::Identify(rider.horse)!=rider ||
           !TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(rider.horse,nullptr))return false;
        const Ped player=PLAYER::PLAYER_PED_ID();
        const Ped mount=PED::GET_MOUNT(rider.horse);
        if(rider.horse==player || !mount || !PED::IS_PED_ON_MOUNT(rider.horse) ||
           (player && PED::GET_MOUNT(player)==mount))return false;
        // Stock mounted waypoint tasks support an in-place local offset.
        // Native +X is recording RIGHT, while the racing-line API uses LEFT.
        // Success means a task accepted the command, never physical clearance.
        if(!invoke<BOOL>(0xED98E10B0AFCE4B4,rider.horse,-route_offset_left,0.0F,0.0F))return false;
        route_offset_submitted_left=route_offset_left;
        ++offset_write_sequence;offset_write_at=now;return true;
    }
    void ReleaseSpeedTrials(){native_vector.Release();speed_table.Release();move_rate.Release();}
    void SuspendSpeedTrials(bool vector_enabled) {
        native_far_speed::ApplyVector(native_vector,rider.horse,0,vector_enabled);
        speed_table.Release();move_rate.Release();
    }
    void ResetWaypointCommand() {
        speed_override_written=false;override_request=0;override_absolute=false;override_transition_rate=native_far_speed::kWaypointTransitionRate;
    }
    native_far_speed::WaypointCommand PrepareWaypointCommand(float effort,float request,bool vector_compensation,bool safety_cap) {
        const auto command=native_far_speed::SelectWaypointCommand(effort,request,vector_compensation,safety_cap,
            override_absolute&&override_transition_rate==0);
        override_absolute=command.absolute;override_request=command.speed;override_transition_rate=command.transition_rate;
        return command;
    }
    void Reset() {
        ReleaseSpeedTrials(); // Restore before clearing the task or retiring its horse.
        // Clear only the owned consumer before removing its recording. Never
        // leave a live playback task pointing at released custom path storage.
        if(recording.owned && rider.Valid() && horse_motion::Identify(rider.horse)==rider &&
            (vehicle_paused || TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(rider.horse,nullptr)))
            TASK::CLEAR_PED_TASKS(rider.horse,false,false);
        recording.Release();rider={};status=Status::Idle;failure=Failure::None;started=0;missing_since=lagging_since=-1;
        missed_since=-1;missed_next=-1;
        progress=entry=accepted_progress=-1;base=chunk_last=nodes=0;metres.clear();starts=0;override_readback=0;control=ever_playing=false;
        ResetWaypointCommand();
        route_offset_left=route_offset_submitted_left=0;
        playback_readback=false;native_index_readback=-1;
        simulation_suspended=false;simulation_speed=handoff_speed=0;simulation_awake_since=-1;handoff_until=0;handoff_settled_since=-1;
        vehicle_paused=false;
    }
    bool Fail(Failure why){ReleaseSpeedTrials();failure=why;status=Status::Fallback;control=false;return false;}
    void ResumeVehicleWait() {
        if(vehicle_paused && control && horse_motion::Identify(rider.horse)==rider)
            TASK::WAYPOINT_PLAYBACK_RESUME(rider.horse,false,-1,0);
        vehicle_paused=false;
    }
    void WaitForVehicle(Ped ped,double now=-1) {
        if(!race_speed_policy::Enabled())return;
        ReleaseSpeedTrials();handoff_speed=0;handoff_until=0;handoff_settled_since=-1;
        missing_since=lagging_since=missed_since=-1;missed_next=-1;
        if(control && horse_motion::Identify(ped)==rider &&
            (vehicle_paused || TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(ped,nullptr))) {
            // Same consumer, as in the stock bank robbery/fishing scripts.
            // Zero here is an explicit absolute stop, not "no compensation".
            if(!vehicle_paused)TASK::WAYPOINT_PLAYBACK_PAUSE(ped,1,0,0);
            vehicle_paused=true;speed_override_written=true;override_absolute=true;override_request=0;
            override_transition_rate=native_far_speed::WaypointTransitionRate(true,true);
            invoke<Void>(0x7D7D2B47FA788E85,ped,0.0F,1,override_transition_rate,1);
            if(race_pace::diagnostics_enabled){++speed_write_sequence;speed_write_at=now;}
        } else if(horse_motion::Identify(ped).Valid() && PED::IS_PED_ON_MOUNT(ped)) {
            // A refused recording may have an owned traffic fallback.
            invoke<Void>(0xBAAB791AA72C2821,ped,0.0F);
        }
    }
    void StopControl() {
        ResumeVehicleWait();route_offset_left=0;ApplyRouteOffset();
        ReleaseSpeedTrials();control=false;
    } // Keep storage alive through result arrival/departure.
    // The race owns this mounted rider, including its traffic fallback. Clear
    // that consumer ONCE before advancing it kinematically, but keep recording
    // storage alive. Its old native node is not a valid goal after simulation.
    bool SuspendForSimulation(Ped ped,float speed) {
        ReleaseSpeedTrials();
        const auto identity=horse_motion::Identify(ped);
        if(!identity.Valid() || (rider.Valid() && identity!=rider))return false;
        if(!simulation_suspended) {
            TASK::CLEAR_PED_TASKS(ped,false,false);
            rider=identity;simulation_suspended=true;handoff_speed=0;handoff_until=0;
        }
        simulation_speed=std::isfinite(speed)?std::max(0.0F,speed):0;
        simulation_awake_since=-1;missing_since=lagging_since=-1;
        return true;
    }
    bool ReadyToResume(double now,bool physical_ready) {
        if(!simulation_suspended || !std::isfinite(now))return false;
        if(!physical_ready){simulation_awake_since=-1;return false;}
        if(simulation_awake_since<0 || now<simulation_awake_since)simulation_awake_since=now;
        // Distant collision flags can flicker awake for a single frame. Wait
        // for a short sustained wake, not dozens of native recording rebuilds.
        return now-simulation_awake_since>=.10;
    }
    bool HandoffGrace(double now) const {return simulation_suspended || now<handoff_until;}
    // The rider is at the end of the registered chunk and the line goes on:
    // the caller starts the next chunk from where the rider is.
    bool NeedsNextChunk() const {
        return control && !simulation_suspended && status==Status::Running && chunk_last+1<nodes && progress>=0 &&
            (progress>=static_cast<int>(chunk_last)-kChunkTail || AtChunkTail(static_cast<std::size_t>(progress)));
    }
    // Within kChunkTailMetres of the chunk's last node, in the line's order metres.
    bool AtChunkTail(std::size_t node) const {
        return node<metres.size() && chunk_last<metres.size() && metres[chunk_last]-metres[node]<=kChunkTailMetres;
    }
    // `reference` is the order metres of node i - the CENTRELINE metres when
    // the executed line is a racing line built on the centreline's nodes
    // (race_racing_line.h), so Tick's section window keeps comparing the
    // caller's centreline qualified_along with centreline metres. An offset
    // line is up to 150 m shorter per lap; judged in its own metres it would
    // trip WrongSection mid-race. Omitted or mismatched: the line's own metres.
    // `origin` is where the rider stands (see ChunkEnd); omitted, only the
    // line's own revisits end the chunk.
    bool Start(Ped ped,const course::Path& line,const std::string& unique_name,std::size_t first,double now,
        const std::vector<float>* reference=nullptr,const Vector3* origin=nullptr) {
        // Clearing/replacing a mounted task can reset velocity on the following
        // engine frame. Retain momentum across chunk changes/recovery too, not
        // just the first return from distant simulation.
        const auto previous=horse_motion::Identify(ped);
        const bool same=rider.Valid() && previous==rider;
        const float retained_offset=same&&control?route_offset_left:0;
        const Ped mount=same?PED::GET_MOUNT(ped):0;
        const float retained=mount?std::max(handoff_speed,ENTITY::GET_ENTITY_SPEED(mount)):0;
        Reset();
        route_offset_left=retained_offset;
        rider=horse_motion::Identify(ped);
        if(rider.Valid() && retained>1){handoff_speed=retained;handoff_until=now+3.0;}
        if(!rider.Valid() || first>=line.points.size() || line.points.size()<2 || !std::isfinite(now)){status=Status::Fallback;return false;}
        const std::size_t last=ChunkEnd(line,first,origin);
        course::Path chunk;
        // The last native hand-off may have only a few metres remaining. A full
        // course still needs 50 m, but rejecting this terminal recording
        // eliminated riders immediately before they reached the finish.
        const float minimum=last+1==line.points.size()?1.0F:50.0F;
        const bool terminal=first+1==line.points.size();
        std::vector<Vector3> points=terminal && origin?std::vector<Vector3>{*origin,line.points.back()}:
            std::vector<Vector3>(line.points.begin()+static_cast<std::ptrdiff_t>(first),
                line.points.begin()+static_cast<std::ptrdiff_t>(last)+1);
        if(!chunk.Build(points,terminal?.05F:minimum) ||
            !recording.Register(chunk,unique_name)) {
            status=Status::Fallback;return false;
        }
        metres=reference && reference->size()==line.points.size()?*reference:line.metres;
        entry=static_cast<int>(first);base=first;chunk_last=last;nodes=line.points.size();
        TASK::TASK_FOLLOW_WAYPOINT_RECORDING(ped,recording.name.c_str(),0,kFlags,
            static_cast<int>(chunk.points.size()-1),false,0,-1);
        if(race_pace::diagnostics_enabled)++task_generation;
        status=Status::Starting;started=now;control=true;++starts;
        return true;
    }
    bool Tick(double now,float effort,float qualified_along,float speed_mps,bool unsafe=false,float absolute_mps=0,bool table_trial=false,bool rate_trial=false,bool vector_trial=false,
        const course::Path* line=nullptr,const Vector3* position=nullptr,bool safety_cap=false,bool suppress_vector=false) {
        const bool speed_enabled=race_speed_policy::Enabled();
        if(!speed_enabled)ReleaseSpeedTrials();
        playback_readback=false;native_index_readback=-1; // freshness includes every early return
        if(simulation_suspended) {
            if(horse_motion::Identify(rider.horse)!=rider)return Fail(Failure::IdentityChanged);
            return true; // No stale native readback, timeout, retry or effort write while asleep.
        }
        if(!control || (status!=Status::Starting && status!=Status::Running)){ReleaseSpeedTrials();return false;}
        if(!std::isfinite(now) || now<started || !std::isfinite(effort) || effort<0 || effort>3.0F ||
            !std::isfinite(qualified_along) || qualified_along<0 || qualified_along>metres.back()+1 ||
            !std::isfinite(speed_mps) || speed_mps<0)return Fail(Failure::InvalidInput);
        if(horse_motion::Identify(rider.horse)!=rider)return Fail(Failure::IdentityChanged);
        if(unsafe){ReleaseSpeedTrials();return true;} // Pause/fall is adjudicated elsewhere, never a navigation retry.
        // Decide from the qualified physical position as well as the native
        // node. The engine can remove its task BEFORE the final-node readback,
        // which used to leave every later lap stopped at exactly the same spot.
        if(chunk_last+1<nodes && qualified_along>=metres[chunk_last]-kChunkTailMetres) {
            progress=std::max(progress,static_cast<int>(chunk_last)-kChunkTail);
            ever_playing=true;status=Status::Running;return true;
        }
        const bool playing=TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(rider.horse,nullptr);
        playback_readback=playing;native_index_readback=-1;
        if(playing) {
            const int native=TASK::GET_PED_WAYPOINT_PROGRESS(rider.horse);
            native_index_readback=native;
            const int count=static_cast<int>((recording.storage.size()-1)/8);
            if(native<0 || native>=count)return Fail(Failure::InvalidIndex);
            progress=static_cast<int>(std::min(chunk_last,base+static_cast<std::size_t>(native)));
            // The chunk's last nodes: reached, or snapped to early - either way
            // the chunk is over and the next one starts from where the rider is
            // (NeedsNextChunk). Not a section to judge.
            if(chunk_last+1<nodes && (native>=count-1-kChunkTail || AtChunkTail(static_cast<std::size_t>(progress)))) {
                accepted_progress=std::max(accepted_progress,progress);
                ever_playing=true;missing_since=-1;status=Status::Running;return true;
            }
            // Validate route ORDER, not distance to the node in world space:
            // a final-lap node can be only metres away from the start grid.
            const auto next=std::upper_bound(metres.begin(),metres.end(),qualified_along);
            const float next_metres=next==metres.end()?metres.back():*next;
            const float segment_metres=next==metres.begin()?metres.front():*std::prev(next);
            const float behind=std::min(segment_metres,qualified_along-35);
            // The native's target runs well ahead of the rider at speed and does
            // not come back the instant the rider slows: after a stumble on
            // 2026-09-19 it stood 60 m ahead of a rider doing 5 m/s, the window
            // had shrunk to 35 m, and three restarts later the host was handed
            // to traffic and left the course. A wrong section on a course that
            // revisits itself is a lap away, never 150 m: the window is at
            // least 100 m.
            const float ahead=std::max(next_metres,qualified_along+std::clamp(speed_mps*2+15,100.F,150.F));
            if(progress<std::max(0,entry-1) || metres[static_cast<std::size_t>(progress)]<behind || metres[static_cast<std::size_t>(progress)]>ahead)
                return Fail(Failure::WrongSection);
            // Native progress is the LAST visited node. Only the NEXT node can
            // be missed. Real forward motion beyond that node and away from its
            // position is stronger evidence than a lag behind the last node.
            // The old 8 m + .4 s rule fired after the 2026-10-19 boundary foul.
            const auto next_node=static_cast<std::size_t>(progress)+1;
            bool missed=false;
            if(line && position && next_node<line->points.size() && next_node<metres.size() &&
                qualified_along>metres[next_node]+.5F) {
                const Ped mount=PED::GET_MOUNT(rider.horse);
                if(mount && ENTITY::GET_ENTITY_COLLISION_DISABLED(mount)) {
                    const auto velocity=ENTITY::GET_ENTITY_VELOCITY(mount,0);
                    const auto goal=line->points[next_node];
                    const float speed=std::hypot(velocity.x,velocity.y);
                    missed=destination::Finite(velocity) && speed>1 &&
                        (goal.x-position->x)*velocity.x+(goal.y-position->y)*velocity.y<-.5F*speed;
                }
            }
            if(missed) {
                if(missed_since<0 || missed_next!=static_cast<int>(next_node)) {
                    missed_since=now;missed_next=static_cast<int>(next_node);missed_position=*position;
                } else if(now-missed_since>=.10 && destination::Distance(*position,missed_position)>.25F)
                    return Fail(Failure::MissedWaypoint);
                // Withdraw positive propulsion at the first observation; keep
                // any explicit native safety cap while confirming the miss.
                suppress_vector=true;
                if(speed_enabled)SuspendSpeedTrials(vector_trial);
                if(!safety_cap)absolute_mps=0;
                table_trial=rate_trial=false;
            } else {missed_since=-1;missed_next=-1;}
            // A native recording can cling to a short node at a lap seam even
            // after the rider has passed it. It then turns the horse around to
            // retrieve that node. Re-enter from qualified progress while the
            // horse is still on course; one transient projection ahead of the
            // readback is not enough to restart a healthy task. Sparse routes
            // may legitimately remain on the previous node for a long span.
            const auto i=static_cast<std::size_t>(progress);
            const float node_span=i+1<metres.size()?metres[i+1]-metres[i]:
                (i?metres[i]-metres[i-1]:0.0F);
            const float allowed_lag=std::max(8.0F,node_span*1.25F);
            if(qualified_along-metres[i]>allowed_lag) {
                if(lagging_since<0)lagging_since=now;
                else if(now-lagging_since>.4)return Fail(Failure::WrongSection);
            } else lagging_since=-1;
            if(accepted_progress>=0 && progress<accepted_progress) {
                if(metres[static_cast<std::size_t>(accepted_progress)]-metres[static_cast<std::size_t>(progress)]>kRegressMetres)return Fail(Failure::Regressed);
            } else {
                // Native cursors can briefly jump to a future target, then
                // return while the horse continues forward (Arthur 0.28.107:
                // 428 -> 432 -> 428). Only physically reached ordered nodes
                // may raise the reversal baseline. `next` uses the SAME
                // centreline/reference metres as qualified_along above.
                const int reached=static_cast<int>(std::max<std::ptrdiff_t>(0,next-metres.begin()-1));
                accepted_progress=std::max(accepted_progress,std::min(progress,reached));
            }
            ApplyRouteOffset(now);
            if(!speed_enabled) {
                // New tasks already use the recording's stock native movement;
                // do not submit script effort, absolute metres/sec or leases.
                ever_playing=true;missing_since=-1;status=Status::Running;return true;
            }
            // Keep the SAME task, with native gait conversion/bounds intact
            // while the vector filter owns distant ability compensation.
            // Keep a native absolute pace request authoritative for both the
            // task and any sleeping vector output.
            const auto command=PrepareWaypointCommand(effort,absolute_mps,vector_trial,safety_cap);
            // One treatment at a time. The new experiment is an engine-vector
            // output filter, not another rewrite of the rejected rate/table.
            if(vector_trial){table_trial=false;rate_trial=false;}
            // Same native task, only its horse's private gait/speed cache.
            // Never enable this for the player, an awake mount or traffic peds.
            const Ped mount=table_trial && override_absolute && PED::IS_PED_ON_MOUNT(rider.horse)?
                PED::GET_MOUNT(rider.horse):0;
            const auto horse=mount?horse_motion::Identify(mount):horse_motion::Identity{};
            if(horse.Valid() && rider.horse!=PLAYER::PLAYER_PED_ID() &&
                ENTITY::GET_ENTITY_COLLISION_DISABLED(mount))speed_table.Update(horse.address,absolute_mps,now);
            else speed_table.Release();
            // Keep experiments mutually exclusive. The failed private-table
            // trial cannot contaminate the native rate A/B measurement.
            const Ped rate_mount=rate_trial && !table_trial && override_absolute &&
                rider.horse!=PLAYER::PLAYER_PED_ID() && PED::IS_PED_ON_MOUNT(rider.horse)?
                PED::GET_MOUNT(rider.horse):0;
            if(rate_mount && ENTITY::DOES_ENTITY_EXIST(rate_mount) &&
                ENTITY::GET_ENTITY_COLLISION_DISABLED(rate_mount))move_rate.Update(rate_mount,absolute_mps,now);
            else move_rate.Release();
            native_far_speed::ApplyVector(native_vector,rider.horse,suppress_vector?0.F:command.vector_target,vector_trial,safety_cap);
            // SDK Any slots are NOT float slots. Preserve the float RATE ABI.
            invoke<Void>(0x7D7D2B47FA788E85,rider.horse,override_request,1,override_transition_rate,override_absolute?1:0);
            speed_override_written=true;
            if(race_pace::diagnostics_enabled){++speed_write_sequence;speed_write_at=now;}
            // The getter converts back to gait units even after a m/s write!
            override_readback=TASK::GET_PED_WAYPOINT_OVERRIDE_SPEED(rider.horse);
            ever_playing=true;missing_since=-1;status=Status::Running;return true;
        }
        // A previously active task disappearing is already a failure, not a
        // reason to leave the horse idle. Only initial native task scheduling
        // gets a short grace; a wrong-section readback fails in that same tick.
        ReleaseSpeedTrials();
        if(ever_playing)return Fail(Failure::Stopped);
        if(missing_since<0)missing_since=now;
        if(now-started>.5)return Fail(Failure::NotStarted);
        return true;
    }
};
enum class Renewal {None,Started,Waiting,Refused};
template<class MakeName>
inline Renewal Maintain(Follower& follower,Recovery& recovery,Ped rider,
    const course::Path& line,const std::vector<float>& reference,float along,
    Vector3 position,double now,float effort,float speed,MakeName make_name,float absolute_mps=0,bool table_trial=false,bool rate_trial=false,bool vector_trial=false,
    race_pace::Command* pace=nullptr,bool allow_forward_fallback=true) {
    if(follower.simulation_suspended)return Renewal::None;
    if(line.points.size()<2 || !std::isfinite(along) || !std::isfinite(now))return Renewal::Refused;
    const bool speed_enabled=race_speed_policy::Enabled();
    if(!speed_enabled) {
        follower.ReleaseSpeedTrials();
        follower.handoff_speed=0;follower.handoff_until=0;follower.handoff_settled_since=-1;
        if(follower.speed_override_written && follower.control) {
            // A previous absolute or gait override is sticky on its task.
            // Rebuild at qualified progress with native stock parameters,
            // preserving horse, position, route and race adjudication.
            const auto first=EntryIndex(line,along,position,&reference,1.0F);
            const float offset=follower.route_offset_left;
            recovery.Issued(along,now);
            const bool restored=follower.Start(rider,line,make_name(),first,now,&reference,&position);
            follower.RequestRouteOffset(offset);
            return restored?Renewal::Started:Renewal::Refused;
        }
        pace=nullptr;absolute_mps=0;table_trial=rate_trial=vector_trial=false;
        effort=0; // only validated by Tick; never submitted with the policy off
    }
    if(pace && pace->vehicle_blocked) {
        follower.WaitForVehicle(rider,now);
        // Waiting is real progress control, never a four-second stall.
        recovery.anchor=along;recovery.progress_at=now;recovery.forward_at=now;
        return Renewal::Waiting;
    }
    if(follower.vehicle_paused) {
        follower.ResumeVehicleWait();
        recovery.anchor=along;recovery.progress_at=now;
    }
    recovery.Observe(along,now);
    const bool controlled=follower.control;
    const bool safety_cap=pace && pace->NativePaceLimited();
    if(recovery.propulsion_suspended) {
        // Arguments were computed before Maintain. Close them before Tick can
        // reacquire a lease or reinterpret a boost as an absolute-speed cap.
        if(!safety_cap)absolute_mps=0;
        table_trial=rate_trial=false;
        follower.SuspendSpeedTrials(vector_trial);
    }
    const bool suppress_vector=recovery.propulsion_suspended || (pace && pace->vehicle_blocked);
    const bool healthy=follower.control && follower.Tick(now,effort,along,speed,false,absolute_mps,table_trial,rate_trial,vector_trial,&line,&position,safety_cap,suppress_vector);
    if(follower.missed_since>=0 || follower.failure==Failure::MissedWaypoint)recovery.propulsion_suspended=true;
    else if(healthy && follower.status==Status::Running && follower.playback_readback &&
        follower.native_index_readback>=0)recovery.propulsion_suspended=false;
    const bool missed_waypoint=recovery.propulsion_suspended;
    // The caller may run traffic fallback/boosts later in THIS frame. Withhold
    // them during confirmation and the existing retry cooldown, even when
    // there is no lateral cap. Do not invent an absolute speed for this state.
    if(missed_waypoint) {
        if(pace)pace->braking=true;
        follower.handoff_speed=0;follower.handoff_until=0;follower.handoff_settled_since=-1;
    }
    if(!healthy && controlled) {
        Fault fault;
        fault.reason=follower.failure;fault.time=now;fault.rider=rider;
        fault.entry=follower.entry;fault.progress=follower.progress;
        fault.native_index=follower.native_index_readback;fault.chunk_end=static_cast<int>(follower.chunk_last);
        fault.along=along;fault.speed=speed;fault.effort=effort;fault.position=position;
        fault.playing=follower.playback_readback;fault.recording=follower.recording.name;
        if(follower.progress>=0 && static_cast<std::size_t>(follower.progress+1)<line.points.size()) {
            fault.next=follower.progress+1;fault.goal=line.points[static_cast<std::size_t>(fault.next)];
        }
        if(ENTITY::DOES_ENTITY_EXIST(rider) && PED::IS_PED_ON_MOUNT(rider))fault.mount=PED::GET_MOUNT(rider);
        if(fault.mount && ENTITY::DOES_ENTITY_EXIST(fault.mount)) {
            fault.gait=TASK::GET_PED_DESIRED_MOVE_BLEND_RATIO(fault.mount);
            fault.collision_disabled=ENTITY::GET_ENTITY_COLLISION_DISABLED(fault.mount)!=0;
            fault.static_entity=ENTITY::IS_ENTITY_STATIC(fault.mount)!=0;
        }
        recovery.last_fault=std::move(fault);++recovery.faults;recovery.fault_pending=true;
        if(diagnostic_log)diagnostic_log("native navigation fault serial="+std::to_string(recovery.faults)+
            recovery.last_fault.Detail());
    }
    if(!healthy && controlled && Retryable(follower.failure) && horse_motion::Identify(rider)==follower.rider) {
        // Never let a failed, wrong-section recording keep steering during
        // the short resource-renewal cooldown. Preserve its earned momentum
        // before detaching, without releasing storage until Start resets it.
        if(missed_waypoint) {
            follower.handoff_speed=0;follower.handoff_until=0;follower.handoff_settled_since=-1;
        } else {
            follower.handoff_speed=std::max(follower.handoff_speed,speed);
            follower.handoff_until=now+3.0;
        }
        if(TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(rider,nullptr))TASK::CLEAR_PED_TASKS(rider,false,false);
    }
    const auto forward=[&] {
        if(!allow_forward_fallback)return;
        if((recovery.forward_at>=0 && now-recovery.forward_at<.5) ||
            !horse_motion::Identify(rider).Valid() || !PED::IS_PED_ON_MOUNT(rider))return;
        const auto goal=line.points[EntryIndex(line,along,position,&reference,12.0F)];
        // Float slots are stopping radius then requested speed, NOT effort.
        // Preserve the original mounted race ABI (-1 = native speed control).
        invoke<Void>(0xDCA3A13F7A45338B,rider,goal.x,goal.y,goal.z,3.0F,-1.0F,3064,0);
        if(speed_enabled)invoke<Void>(0xBAAB791AA72C2821,rider,std::min(effort,3.0F));
        PED::SET_PED_KEEP_TASK(rider,true);recovery.forward_at=now;
    };
    const bool chunk=healthy && follower.NeedsNextChunk();
    if(healthy && !chunk && !recovery.Creeping(now))return Renewal::None;
    if(!recovery.Ready(now,chunk)){if(!healthy)forward();return Renewal::Waiting;}
    // All nodes behind the current physical position are omitted, so renewing
    // a chunk can never ask the rider to turn around to fetch an old waypoint.
    const auto first=EntryIndex(line,along,position,&reference,1.0F);
    const std::string cause=chunk?"chunk-tail":healthy?"progress-stall":
        recovery.fault_pending?FailureName(recovery.last_fault.reason):"initial";
    const int previous_entry=follower.entry,previous_progress=follower.progress;
    recovery.Issued(along,now);
    const bool started=follower.Start(rider,line,make_name(),first,now,&reference,&position);
    // A failed consumer's momentum is not a trustworthy restart speed. Let the
    // new native task regain its line instead of reinjecting the old velocity.
    if(missed_waypoint){follower.handoff_speed=0;follower.handoff_until=0;follower.handoff_settled_since=-1;}
    if(diagnostic_log)diagnostic_log("native navigation renewal cause="+cause+
        " result="+(started?"started":"refused")+" rider="+std::to_string(rider)+
        " previous_entry="+std::to_string(previous_entry)+" previous_progress="+std::to_string(previous_progress)+
        " next_entry="+std::to_string(first)+" along_m="+std::to_string(along)+
        " speed_mps="+std::to_string(speed)+" faults="+std::to_string(recovery.faults)+
        " fault_age_s="+std::to_string(recovery.fault_pending?now-recovery.last_fault.time:-1));
    if(started){recovery.fault_pending=false;return Renewal::Started;}
    forward();return Renewal::Refused;
}
} // namespace horse_races::native_follower
