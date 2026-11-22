#pragma once
#include "race_control_telemetry.h"
#include <cmath>
#include <ostream>

namespace horse_motion::drive_recording {
inline void Number(std::ostream& o,double value){if(std::isfinite(value))o<<value;else o<<"null";}
inline void Time(std::ostream& o,double value){const auto precision=o.precision();o.precision(15);Number(o,value);o.precision(precision);}
inline void Vector(std::ostream& o,Vector3 value){o<<'[';Number(o,value.x);o<<',';Number(o,value.y);o<<',';Number(o,value.z);o<<']';}
inline void Pace(std::ostream& o,const horse_races::race_pace::Trace& p) {
    o<<"{\"controller_evaluated\":"<<p.controller_evaluated;
    o<<",\"evaluated_game_s\":";Time(o,p.evaluated_game_s);
    const auto n=[&](const char* key,double v){o<<",\""<<key<<"\":";Number(o,v);};
    n("input_along_m",p.input_along_m);n("input_mps",p.input_speed_mps);n("top_mps",p.top_mps);
    n("cornering",p.lateral_mps2);n("lookahead_line_m",p.lookahead_line_m);n("curvature",p.lookahead_curvature);
    n("plan_target_mps",p.plan_target_mps);n("plan_effort",p.plan_effort);
    o<<",\"plan_braking\":"<<p.plan_braking<<",\"plan_bend\":"<<p.plan_bend<<",\"plan_terrain\":"<<p.plan_terrain_limited;
    o<<",\"following_evaluated\":"<<p.following_evaluated<<",\"following_applied\":"<<p.following_applied
        <<",\"following_horse\":"<<p.following_horse;
    n("following_input_mps",p.following_input_target_mps);n("following_raw_cap_mps",p.following_cap_mps);
    n("following_input_effort",p.following_input_effort);
    o<<",\"following_input_braking\":"<<p.following_input_braking<<",\"following_braking\":"<<p.following_braking;
    n("following_gap_m",p.following_gap_m);n("following_lead_mps",p.following_lead_mps);
    n("following_closing_mps",p.following_closing_mps);n("following_lateral_m",p.following_lateral_m);
    n("following_output_mps",p.following_target_mps);n("following_effort",p.following_effort);
    o<<",\"supplement_evaluated\":"<<p.supplement_evaluated<<",\"supplement_reason\":\""
        <<horse_races::race_pace::Name(p.supplement_reason)<<"\",\"supplement_calls\":"<<p.supplement_calls
        <<",\"supplement_source\":"<<p.supplement_source<<",\"supplement_enabled\":"<<p.supplement_enabled
        <<",\"supplement_progress_valid\":"<<p.supplement_progress_valid<<",\"supplement_table_trial\":"<<p.supplement_table_trial
        <<",\"supplement_gap_valid\":"<<p.supplement_gap_valid<<",\"supplement_safety\":"<<p.supplement_safety;
    o<<",\"supplement_input_braking\":"<<p.supplement_input_braking<<",\"supplement_speed_level\":"<<p.supplement_speed_level;
    n("supplement_distance_m",p.supplement_distance_m);n("supplement_input_mps",p.supplement_input_target_mps);
    n("supplement_input_effort",p.supplement_input_effort);n("supplement_base_mps",p.supplement_learned_base_mps);
    n("supplement_gap_m",p.supplement_gap_m);n("supplement_burst_levels",p.supplement_burst_levels);
    n("supplement_catchup_fraction",p.supplement_catchup_fraction);
    n("supplement_output_mps",p.supplement_target_mps);o<<'}';
}
inline void Json(std::ostream& o,const horse_races::drive_diagnostics::Snapshot& s) {
    if(!s.valid)return;
    o<<",\"drive_trace\":{\"v\":1,\"captured_game_s\":";Time(o,s.captured_at);
    const auto n=[&](const char* key,double v){o<<",\""<<key<<"\":";Number(o,v);};
    n("target_mps",s.target);n("effort",s.effort);n("judge_half_width_m",s.half_width);
    o<<",\"braking\":"<<s.braking<<",\"terrain\":"<<s.terrain<<",\"following\":"<<s.following
        <<",\"vehicle\":"<<s.vehicle<<",\"pace\":";Pace(o,s.pace);
    o<<",\"task\":{\"generation\":"<<s.task_generation<<",\"speed_write_seq\":"<<s.speed_write_sequence
        <<",\"speed_write_game_s\":";Time(o,s.speed_write_at);
    n("request",s.request);n("transition_rate",s.transition_rate);
    n("route_offset_left_m",s.route_offset_left);n("route_offset_submitted_left_m",s.route_offset_submitted_left);
    o<<",\"offset_write_seq\":"<<s.offset_write_sequence<<",\"offset_write_game_s\":";Time(o,s.offset_write_at);
    o<<",\"absolute_mps\":"<<s.absolute<<",\"control\":"<<s.control<<",\"playing\":"<<s.playing
        <<",\"suspended\":"<<s.suspended<<",\"vehicle_paused\":"<<s.vehicle_paused
        <<",\"propulsion_suspended\":"<<s.propulsion_suspended<<",\"status\":"<<s.status<<",\"failure\":"<<s.failure
        <<",\"progress\":"<<s.progress<<",\"native_index\":"<<s.native_index<<",\"entry\":"<<s.entry
        <<",\"chunk_end\":"<<s.chunk_end<<",\"renewals\":"<<s.renewals<<",\"fault_count\":"<<s.faults
        <<",\"fault_reason\":"<<s.fault_reason<<",\"missed_next\":"<<s.missed_next;
    o<<",\"missed_since_game_s\":";Time(o,s.missed_since);o<<",\"fault_game_s\":";Time(o,s.fault_at);
    o<<",\"next_waypoint\":"<<s.next<<",\"waypoints\":";
    if(!s.waypoints_valid)o<<"null";
    else {
        o<<'[';for(std::size_t i=0;i<s.waypoints.size();++i){if(i)o<<',';o<<"{\"ref_m\":";
            Number(o,s.reference[i]);o<<",\"xyz\":";Vector(o,s.waypoints[i]);o<<'}';}o<<']';
    }
    o<<"}}";
}
} // namespace horse_motion::drive_recording
