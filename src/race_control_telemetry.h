#pragma once
#include "race_pace_trace.h"
#include <types.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace horse_races::drive_diagnostics {
// Shared by the host, field jockeys and bookmaker jockeys. It is never read
// by a controller, and contains copies of already computed state only.
struct Snapshot {
    bool valid=false;
    double captured_at=-1;
    race_pace::Trace pace;
    float target=0,effort=0,half_width=0;
    bool braking=false,terrain=false,following=false,vehicle=false;
    bool control=false,playing=false,suspended=false,vehicle_paused=false,propulsion_suspended=false;
    int status=0,failure=0,progress=-1,native_index=-1,entry=-1,chunk_end=-1,renewals=0;
    std::uint64_t task_generation=0,speed_write_sequence=0;
    double speed_write_at=-1,missed_since=-1,fault_at=-1;
    float route_offset_left=0,route_offset_submitted_left=0;
    std::uint64_t offset_write_sequence=0;
    double offset_write_at=-1;
    int missed_next=-1,fault_reason=0;
    unsigned faults=0;
    float request=0,transition_rate=0;
    bool absolute=false,waypoints_valid=false;
    int next=-1;
    // Previous, next and following registered waypoints. The native may use
    // a steering lookahead of its own; these are not a claimed steering target.
    std::array<Vector3,3> waypoints{};
    std::array<float,3> reference{};
};
template<class Command,class Follower,class Recovery,class Path>
Snapshot Capture(const Command& command,const Follower& follower,const Recovery& recovery,
    const Path& path,const std::vector<float>& reference,double now,float half_width) {
    Snapshot out;
    if(!race_pace::diagnostics_enabled)return out;
    out.valid=true;out.captured_at=now;out.pace=command.trace;
    out.target=command.target_mps;out.effort=command.effort;out.braking=command.braking;
    out.terrain=command.terrain_limited;out.following=command.following_limited;
    out.vehicle=command.vehicle_blocked||command.vehicle_limited;out.half_width=half_width;
    out.control=follower.control;out.playing=follower.playback_readback;
    out.suspended=follower.simulation_suspended;out.vehicle_paused=follower.vehicle_paused;
    out.propulsion_suspended=recovery.propulsion_suspended;
    out.status=static_cast<int>(follower.status);out.failure=static_cast<int>(follower.failure);
    out.progress=follower.progress;out.native_index=follower.native_index_readback;
    out.entry=follower.entry;out.chunk_end=static_cast<int>(follower.chunk_last);
    out.task_generation=follower.task_generation;out.speed_write_sequence=follower.speed_write_sequence;
    out.speed_write_at=follower.speed_write_at;out.request=follower.override_request;out.absolute=follower.override_absolute;
    out.route_offset_left=follower.route_offset_left;out.route_offset_submitted_left=follower.route_offset_submitted_left;
    out.offset_write_sequence=follower.offset_write_sequence;out.offset_write_at=follower.offset_write_at;
    out.transition_rate=follower.override_transition_rate;
    out.missed_since=follower.missed_since;out.missed_next=follower.missed_next;
    out.renewals=recovery.renewals;out.faults=recovery.faults;
    out.fault_reason=static_cast<int>(recovery.last_fault.reason);out.fault_at=recovery.last_fault.time;
    if(follower.control && follower.progress>=0 && path.points.size()>=2) {
        out.next=std::min(follower.progress+1,static_cast<int>(path.points.size())-1);
        out.waypoints_valid=true;
        for(int i=0;i<3;++i) {
            const auto index=static_cast<std::size_t>(std::clamp(out.next+i-1,0,static_cast<int>(path.points.size())-1));
            out.waypoints[static_cast<std::size_t>(i)]=path.points[index];
            out.reference[static_cast<std::size_t>(i)]=index<reference.size()?reference[index]:race_pace::kUnobserved;
        }
    }
    return out;
}
} // namespace horse_races::drive_diagnostics
