#pragma once
#include "runtime_paths.h"
#include "horse_motion_native.h"
#include "race_system.h"
#include "horse_drive_recording.h"
#include "telemetry_compress.h"
#include "telemetry_retention.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>
#include <atomic>

namespace horse_motion {
inline void Number(std::ostream& o,double v){if(std::isfinite(v))o<<v;else o<<"null";}
inline void Vector(std::ostream& o,const Vector3& v){o<<'[';Number(o,v.x);o<<',';Number(o,v.y);o<<',';Number(o,v.z);o<<']';}
inline void JsonSnowGridShadow(std::ostream& o,const std::optional<snow_grid_probe::Sample>& shadow){
    if(!shadow){o<<"null";return;}
    const auto& grid=*shadow;
    o<<"{\"status\":"<<static_cast<unsigned>(grid.status)<<",\"center_xy\":[";
    Number(o,grid.center[0]);o<<',';Number(o,grid.center[1]);o<<"],\"width\":";
    Number(o,grid.width);o<<",\"flag\":"<<static_cast<unsigned>(grid.flag)
        <<",\"cell\":["<<grid.x<<','<<grid.y<<"],\"raw\":";
    if(grid.status==snow_grid_probe::Status::Ready)o<<static_cast<unsigned>(grid.raw);else o<<"null";
    o<<'}';
}
inline void JsonNativeConstraints(std::ostream& o,const native_constraint_probe::Snapshot& s){
    o<<"{\"valid\":"<<s.valid<<",\"status\":"<<static_cast<int>(s.status)
        <<",\"freshness_verified\":false,\"speed_authority\":false";
    if(!s.valid){o<<'}';return;}
    const auto number=[&](const char* name,float v){o<<",\""<<name<<"\":";Number(o,v);};
    const auto field=[&](const char* name,const native_constraint_probe::Field& f,bool boolean=false){
        o<<",\""<<name<<"\":{\"status\":"<<static_cast<int>(f.status)<<",\"value\":";
        if(!f.Present())o<<"null";else if(boolean)o<<f.Boolean();else Number(o,f.Number());o<<'}';
    };
    const auto point=[&](const char* name,const std::array<float,3>& v){
        o<<",\""<<name<<"\":[";for(std::size_t i=0;i<3;++i){if(i)o<<',';Number(o,v[i]);}o<<']';
    };
    o<<",\"generation\":"<<s.context.generation;
    o<<",\"cache_update_gate_858\":"<<s.cache_update_gate_858;
    number("request_value",s.request_value);
    o<<",\"request_gait\":"<<s.request_gait<<",\"request_type\":"<<s.request_type;
    number("gait_bound_2bc",s.gait_bound_2bc);number("gait_bound_2c0",s.gait_bound_2c0);
    number("cached_integrated_mps",s.integrated_speed_mps);number("cached_generated_mps",s.generated_speed_mps);
    number("cached_slope_reduction_mps",s.SlopeReduction());number("cached_water_reduction_mps",s.WaterReduction());
    number("cached_surface_reduction_mps",s.SurfaceReduction());number("cached_total_reduction_mps",s.TotalReduction());
    o<<",\"cached_reductions_mps\":[";
    for(std::size_t i=0;i<s.reductions_mps.size();++i){if(i)o<<',';Number(o,s.reductions_mps[i]);}o<<']';
    o<<",\"surface_valid\":"<<s.surface_valid<<",\"surface_material_index\":"<<s.surface_material_index;
    point("surface_position",s.surface_position);point("surface_normal",s.surface_normal);number("surface_depth_raw",s.surface_depth_raw);
    o<<",\"surface_query_input\":{\"status\":"<<static_cast<int>(s.surface_query_input.status);
    if(s.surface_query_input.status==native_constraint_probe::QueryInputStatus::Ready){
        std::array<float,3> position{};
        for(std::size_t i=0;i<position.size();++i)
            position[i]=native_environment::At<float>(s.surface_query_input.raw_position,i*4);
        point("position",position);
        o<<",\"raw_word_470\":"<<s.surface_query_input.raw_word_470;
    } else o<<",\"position\":null,\"raw_word_470\":null";
    o<<'}';
    field("surface_depth",s.surface_depth);field("signed_slope",s.signed_slope);field("alternate_slope",s.alternate_slope);
    field("alternate_slope_selected",s.alternate_slope_selected,true);field("slope_bypass",s.slope_bypass,true);o<<'}';
}
#include "horse_vector_recording.inl"
inline void JsonNativeRate(std::ostream& o,const native_move_rate::Observation& v){
    o<<",\"native_move_rate\":{\"state\":"<<v.state<<",\"phase\":"<<v.phase
        <<",\"accepted\":"<<v.accepted<<",\"requested\":";Number(o,v.requested);
    o<<",\"phase_seconds\":";Number(o,v.phase_seconds);
    o<<",\"before\":";if(v.before)Number(o,*v.before);else o<<"null";
    o<<",\"after\":";if(v.after)Number(o,*v.after);else o<<"null";o<<'}';
}
inline void JsonNativeTable(std::ostream& o,const native_speed_table::Observation& v){
    o<<",\"native_speed_table\":{\"state\":"<<v.state<<",\"checkpoint\":"<<v.checkpoint
        <<",\"active\":"<<v.active<<",\"restore_mask\":"<<v.restore_mask;
    const auto value=[&](const char* name,float n){o<<",\""<<name<<"\":";Number(o,n);};
    value("native_mps",v.native_top);value("applied_mps",v.applied_top);
    // Last validated pre-write observation, not a claim of post-write motion.
    value("observed_task_mps",v.observed_task);value("observed_motion_mps",v.observed_motion);
    value("derived_native_mps",v.derived);
    value("native_limit_mps",v.native_limit);value("applied_limit_mps",v.applied_limit);
    value("observed_task_limit_mps",v.observed_task_limit);value("observed_motion_limit_mps",v.observed_motion_limit);
    value("derived_native_limit_mps",v.derived_limit);o<<'}';
}
#include "horse_motion_frame_recording.inl"
struct SampleTiming {
    int frame=0,game_ms=0;float frame_seconds=0;bool circuit_exempt=false;
    int exemption_refusal=0;   // horse_motion::ExemptionRefusal.
};
// Every term of the field host's bonus, every tick. Without these a recording
// can only show that her force never changed; it cannot say whether the route
// declared no bonus, the ground was never straight enough, or she simply never
// held the bar long enough - which are three completely different bugs.
struct HostBoostSample {
    bool configured=false;  // The route declares a bonus at all.
    bool gallop=false;      // The gait term alone: at or above the bar this tick.
    bool eligible=false;    // Every non-gait term together.
    bool active=false;      // Visible earned burst; catch-up is recorded separately.
    int level=0;            // Displayed speed rank the bonus is measured at.
    float gait=0,bar=0,force=0; // Current move blend ratio, the gait bar, the boosted force.
    double held_s=0;
    unsigned drops=0;       // Holds broken so far. A coarse sample still reveals a fine-grained gate.
    bool earned_active=false,near_bonus_applied=false,catchup_engaged=false;
};
inline void JsonHostBoost(std::ostream& o,const HostBoostSample& b) {
    o<<"{\"configured\":"<<b.configured<<",\"gallop\":"<<b.gallop<<",\"eligible\":"<<b.eligible
        <<",\"active\":"<<b.active<<",\"level\":"<<b.level<<",\"drops\":"<<b.drops<<",\"gait\":";
    Number(o,b.gait);o<<",\"bar\":";Number(o,b.bar);o<<",\"force\":";Number(o,b.force);o<<",\"held_s\":";Number(o,b.held_s);
    o<<",\"earned_active\":"<<b.earned_active<<",\"near_bonus_applied\":"<<b.near_bonus_applied
        <<",\"catchup_engaged\":"<<b.catchup_engaged<<'}';
}
// One field entrant, every tick while racing. A recording of the player and
// the host is a recording of two horses in a race of nine; a jockey pinned at
// 56 km/h for a whole trip could be seen on the track and nowhere else.
struct EntrantSample {
    horse_races::EntrantTelemetry entrant; // Pace and driver, from the race.
    NativeFrame frame;                     // Observed before this tick's control.
    AppliedFrame applied;                  // What the shared curve asked of it.
    State state;                           // Its own ramp clocks.
    bool boost_active=false;
    bool earned_boost_active=false,near_bonus_applied=false,catchup_engaged=false;
    bool catchup_active=false,catchup_gap_valid=false;
    float catchup_fraction=0,catchup_gap_m=0;
};
inline void JsonCatchup(std::ostream& o,bool active,bool valid,float fraction,float gap) {
    o<<"{\"active\":"<<active<<",\"gap_valid\":"<<valid<<",\"fraction\":";Number(o,fraction);
    o<<",\"gap_m\":";if(valid)Number(o,gap);else o<<"null";o<<'}';
}
// Compact by design: seven copies of JsonFrame at 10 Hz would outgrow the file
// cap, and most of that frame (physics readback, provider notes) is the same
// for the whole field. What a pinned jockey needs is here - what the horse is,
// how fast it is going, what the curve asked, what the town volumes said, and
// what the planner and driver were doing. A frame whose horse is gone keeps
// every key with null values, so a column never changes meaning mid-file.
inline void JsonEntrant(std::ostream& o,const EntrantSample& s,
    const VectorRecordingScope* vector_scope=nullptr) {
    const auto& e=s.entrant;const auto& f=s.frame;const auto& a=s.applied;
    const bool v=f.valid;
    o<<"{\"index\":"<<e.index<<",\"horse\":"<<e.horse<<",\"rider\":"<<e.rider<<",\"valid\":"<<v<<",\"out\":"<<e.out;
    o<<",\"boost_active\":"<<s.boost_active;
    o<<",\"model\":";if(v)o<<f.identity.model;else o<<"null";
    auto number=[&](const char* key,double x){o<<",\""<<key<<"\":";if(v)Number(o,x);else o<<"null";};
    auto integer=[&](const char* key,long long x){o<<",\""<<key<<"\":";if(v)o<<x;else o<<"null";};
    auto flag=[&](const char* key,bool x){o<<",\""<<key<<"\":";if(v)o<<x;else o<<"null";};
    auto ranks=[&](const char* key,const horse_attributes::Ranks& r){
        o<<",\""<<key<<"\":";if(v)o<<'['<<r.speed<<','<<r.acceleration<<']';else o<<"null";
    };
    auto optional=[&](const char* key,const std::optional<float>& x){o<<",\""<<key<<"\":";if(v && x)Number(o,*x);else o<<"null";};
    o<<",\"xyz\":";if(v)Vector(o,f.position);else o<<"null";
    number("horizontal_kmh",f.input.speed_kmh);
    integer("speed_level",f.input.speed_level);integer("acceleration_level",f.input.acceleration_level);
    ranks("raw_current",f.attributes.current);ranks("raw_base",f.attributes.entity_base);
    ranks("raw_bonus",f.attributes.bonus);ranks("raw_model_default",f.attributes.model_default);
    o<<",\"current_blend_xy\":";
    if(v && f.current_blend_valid){o<<'[';Number(o,f.current_blend_x);o<<',';Number(o,f.current_blend_y);o<<']';}else o<<"null";
    number("desired_before_control",f.input.desired_blend);
    o<<",\"horse_reset_317\":";if(v && f.diagnostics)o<<f.horse_reset_317;else o<<"null";
    number("force_applied",a.force_applied);number("force_requested",a.requested.forward_force);
    o<<",\"max_written\":";if(v && a.max_written)Number(o,*a.requested.max_blend);else o<<"null";
    o<<",\"desired_written\":";if(v && a.desired_written)Number(o,*a.requested.desired_blend);else o<<"null";
    flag("controller_suppressed",a.requested.suppressed);flag("braking_requested",f.input.braking_requested);
    flag("unsafe",f.input.environment.unsafe);flag("in_water",f.input.environment.in_water);
    o<<",\"reset_flags_h154_h174_r154_r174\":";
    if(v){o<<'[';for(std::size_t i=0;i<f.reset_flags.size();++i){if(i)o<<',';o<<f.reset_flags[i];}o<<']';}else o<<"null";
    optional("verified_script_max",f.limits.script_max);
    optional("native_speed_volume_gait_cap",f.volumes.max_blend);
    integer("speed_volume_status",static_cast<int>(f.volumes.status));flag("speed_volume_mirrored",a.volume_mirrored);
    flag("physics_requested",f.physics_activation.requested);flag("physics_collision_loaded",f.physics_activation.collision_loaded);
    flag("physics_load_collision_flagged",f.physics_activation.load_collision_flagged);
    o<<",\"force_ramp_age_s\":";if(v && s.state.force_started)Number(o,f.input.now-*s.state.force_started);else o<<"null";
    o<<",\"normalized_stamina_native\":";if(v && f.diagnostics)Number(o,f.diagnostics->normalized_stamina);else o<<"null";
    o<<",\"velocity_mps\":";if(v)Vector(o,f.velocity);else o<<"null";
    if(e.driving.valid){o<<",\"forward\":";if(v)Vector(o,f.forward);else o<<"null";}
    // Reuse the sampled native state. Without it a slower nearby jockey cannot
    // be compared with a distant host whose collision simulation is disabled.
    o<<",\"status\":";
    if(v && f.diagnostics) {
        const auto& d=*f.diagnostics;
        o<<"{\"health\":"<<d.health<<",\"max_health\":"<<d.max_health
        <<",\"bonding_rank\":"<<d.bonding_rank
        <<",\"on_screen\":"<<d.on_screen<<",\"visible\":"<<d.visible<<",\"static\":"<<d.is_static
            <<",\"frozen\":"<<d.frozen<<",\"collision_disabled\":"<<d.collision_disabled
            <<",\"mounted_rider\":"<<f.rider<<",\"rider_xyz\":";
        // This is only the rider currently attached to the observed mount, not
        // the race's original rider after a fall. Missing data stays null.
        if(f.rider)Vector(o,d.rider_position);else o<<"null";
        o<<",\"native_constraints\":";JsonNativeConstraints(o,d.native_constraints);
        o<<",\"snow_grid_shadow\":";JsonSnowGridShadow(o,d.snow_grid_shadow);
        o<<'}';
    } else o<<"null";
    // The race's own view of this rider does not depend on the frame.
    o<<",\"pace_target_mps\":";Number(o,e.pace_target_mps);
    o<<",\"following_limited\":"<<e.following_limited<<",\"following_horse\":"<<e.following_horse;
    o<<",\"following_gap_m\":";Number(o,e.following_gap_m);
    o<<",\"terrain_limited\":"<<e.terrain_limited<<",\"terrain_caution\":";Number(o,e.terrain_caution);
    o<<",\"terrain_height_error_m\":";Number(o,e.terrain_height_error_m);
    o<<",\"pace_braking\":"<<e.pace_braking<<",\"pace_effort\":";Number(o,e.pace_effort);
    o<<",\"driver_mode\":"<<e.driver_mode<<",\"follower_control\":"<<e.follower_control
        <<",\"native_fallback_reason\":"<<e.native_fallback_reason
        <<",\"native_waypoint_progress\":"<<e.native_waypoint_progress<<",\"native_entry_index\":"<<e.native_entry_index
        <<",\"native_effort_readback\":";Number(o,e.native_effort_readback);
    o<<",\"native_absolute_speed\":"<<e.native_absolute_speed;
    o<<",\"native_requested_mps\":";Number(o,e.native_requested_mps);
    JsonNativeTable(o,e.native_table);
    JsonNativeRate(o,e.native_rate);
    JsonNativeVector(o,e.native_vector,vector_scope);
    o<<",\"along_m\":";Number(o,e.along_m);
    o<<",\"next_gate\":"<<e.next_gate<<",\"route_deviation_m\":";Number(o,e.route_deviation_m);
    o<<",\"restarts\":"<<e.restarts<<",\"stalled_s\":";if(e.stalled_s>=0)Number(o,e.stalled_s);else o<<"null";
    o<<",\"traffic_target\":"<<e.traffic_target<<",\"raw_target\":["<<e.raw_target[0]<<','<<e.raw_target[1]<<']';
    o<<",\"line_aggression\":";Number(o,e.line_aggression);
    o<<",\"line_max_offset_m\":";Number(o,e.line_max_offset_m);
    o<<",\"line_offset_m\":";Number(o,e.line_offset_m);
    o<<",\"catchup\":";JsonCatchup(o,s.catchup_active,s.catchup_gap_valid,s.catchup_fraction,s.catchup_gap_m);
    o<<",\"earned_boost_active\":"<<s.earned_boost_active<<",\"near_bonus_applied\":"<<s.near_bonus_applied
        <<",\"catchup_engaged\":"<<s.catchup_engaged;
    drive_recording::Json(o,e.driving);o<<'}';
}
// Copy only existing script-thread state at the existing 10 Hz recording
// boundary. Completed-window values are snapshots and may be older than the
// current motion frame; serial and age make that distinction explicit.
struct PlayerLearningTrace {
    bool current=false,candidate_active=false,has_previous_reference_window=false;
    int observer_frame=-1,horse=0;
    std::uint64_t sampler_frames=0,eligible_frames=0,accepted=0,rejected_windows=0;
    unsigned rejection=0,context_resource=0,context_slope=0,context_material=0,slope_slice_pending=0;
    speed_learning::SamplingObservation sampling{};
    speed_learning::LearningDiagnostics diagnostics{};
    speed_learning::Observation reference{};
    float reference_pending_seconds=0,candidate_min_speed=0,candidate_max_speed=0;
    double last_window_age=std::numeric_limits<double>::quiet_NaN();
    double last_pair_age=std::numeric_limits<double>::quiet_NaN();
    double candidate_span=std::numeric_limits<double>::quiet_NaN();
};
inline void JsonPlayerLearning(std::ostream& o,const PlayerLearningTrace& t){
    o<<"{\"current\":"<<t.current<<",\"observer_frame\":"<<t.observer_frame<<",\"horse\":"<<t.horse;
    if(!t.current){o<<'}';return;}
    o<<",\"sampler_frames\":"<<t.sampler_frames<<",\"eligible_frames\":"<<t.eligible_frames
        <<",\"accepted\":"<<t.accepted<<",\"rejected_windows\":"<<t.rejected_windows
        <<",\"rejection\":"<<t.rejection<<",\"context_resource\":"<<t.context_resource
        <<",\"context_slope\":"<<t.context_slope<<",\"context_material\":"<<t.context_material
        <<",\"slope_slice_pending\":"<<t.slope_slice_pending;
    const auto& s=t.sampling;const auto& d=t.diagnostics;const auto& r=t.reference;
    o<<",\"actual_gait\":";Number(o,s.actual_gait);
    o<<",\"last_window\":{\"sequence\":"<<d.completed_windows<<",\"age_s\":";Number(o,t.last_window_age);
    o<<",\"resource\":"<<s.resource<<",\"slope\":"<<s.slope<<",\"reject_mask\":"<<s.reject_mask
        <<",\"unsettled_detail\":"<<d.last_detail<<",\"first_grade\":"<<d.last_first_grade
        <<",\"second_grade\":"<<d.last_second_grade<<",\"first_grade_ratio\":";Number(o,d.last_first_grade_ratio);
    o<<",\"second_grade_ratio\":";Number(o,d.last_second_grade_ratio);
    o<<",\"half_speed_delta_mps\":";Number(o,d.last_half_speed_delta);
    o<<",\"coherence\":";Number(o,d.last_coherence);
    o<<",\"mean_gait\":";Number(o,s.mean_gait);o<<",\"p90\":";Number(o,d.last_p90);
    o<<",\"p90_history_samples\":"<<d.last_p90_samples
        <<",\"p90_source\":"<<d.last_p90_source
        <<",\"p90_resource_samples\":"<<d.last_p90_resource_samples
        <<",\"p90_slope_samples\":"<<d.last_p90_slope_samples<<'}';
    o<<",\"p90_warmup_windows\":"<<d.p90_warmup<<",\"p90_below_windows\":"<<d.p90_below
        <<",\"p90_qualified_windows\":"<<d.p90_qualified
        <<",\"pair_completed\":"<<d.pair_completed<<",\"pair_promoted\":"<<d.pair_promoted
        <<",\"last_pair\":{\"state\":"<<d.last_pair_state<<",\"pending\":"<<d.last_pair_pending
        <<",\"pace_mps\":";Number(o,d.last_pair_pace);
    o<<",\"age_s\":";Number(o,t.last_pair_age);
    o<<"},\"candidate_active\":"<<t.candidate_active
        <<",\"candidate_selected_seconds\":";Number(o,d.candidate_seconds);
    o<<",\"candidate_span_s\":";Number(o,t.candidate_span);
    o<<",\"candidate_min_mps\":";if(t.candidate_active)Number(o,t.candidate_min_speed);else o<<"null";
    o<<",\"candidate_max_mps\":";if(t.candidate_active)Number(o,t.candidate_max_speed);else o<<"null";
    o<<",\"reference_pending_seconds\":";Number(o,t.reference_pending_seconds);
    o<<",\"reference_previous_window\":"<<t.has_previous_reference_window
        <<",\"reference_windows\":"<<r.windows<<",\"reference_accepted\":"<<r.accepted
        <<",\"reference_rejected\":"<<r.rejected<<",\"reference_base_mps\":";Number(o,r.base);
    o<<",\"candidate_breaks\":[";
    for(std::size_t i=0;i<d.breaks.size();++i){if(i)o<<',';o<<d.breaks[i];}
    o<<"]}";
}
inline void JsonSample(std::ostream& o,const horse_races::MotionContext& c,double elapsed,bool override_on,
    const NativeFrame& p,const AppliedFrame& pa,const NativeFrame& n,const AppliedFrame& na,
    const horse_races::NavigationTelemetry& navigation,const State& ps,const State& ns,
    const Config& config,const SampleTiming& timing,const HostBoostSample& boost={},
    const std::vector<EntrantSample>& extras={},bool player_stimulant=false,
    std::uint64_t recording_start_tick_ms=0,const PlayerLearningTrace* player_learning=nullptr) {
    // Seven significant digits: a centimetre on a four-digit coordinate, a
    // ten-millionth on a unit value. Nine was a fifth more file for noise.
    o<<std::boolalpha<<std::setprecision(7);
    o<<"{\"t\":";Number(o,elapsed);o<<",\"phase\":"<<c.phase<<",\"racing\":"<<c.racing
        <<",\"town_speed_exempt_requested\":"<<c.town_speed_exempt<<",\"override_on\":"<<override_on
        <<",\"circuit_exemption_applied\":"<<timing.circuit_exempt
        <<",\"exemption_refusal\":"<<timing.exemption_refusal<<",\"frame\":"<<timing.frame
        <<",\"game_ms\":"<<timing.game_ms<<",\"frame_seconds\":";Number(o,timing.frame_seconds);
    o<<",\"pair_distance_m\":";
    if(p.valid && n.valid)Number(o,std::hypot(p.position.x-n.position.x,p.position.y-n.position.y));else o<<"null";
    const auto sample_tick_ms=(recording_start_tick_ms||horse_races::race_pace::diagnostics_enabled)?GetTickCount64():0;
    const VectorRecordingScope scope{recording_start_tick_ms,sample_tick_ms};
    const auto* vector_scope=recording_start_tick_ms?&scope:nullptr;
    if(horse_races::race_pace::diagnostics_enabled)o<<",\"trace_sample_tick_ms\":"<<sample_tick_ms;
    o<<",\"navigation\":";JsonNavigation(o,navigation,n,vector_scope);
    o<<",\"host_boost\":";JsonHostBoost(o,boost);
    o<<",\"catchup\":";JsonCatchup(o,c.catchup_active,c.catchup_gap_valid,c.catchup_fraction,c.catchup_gap_m);
    o<<",\"player_stimulant\":{\"active\":"<<player_stimulant<<'}';
    if(player_learning){o<<",\"player_learning\":";JsonPlayerLearning(o,*player_learning);}
    o<<",\"player\":";JsonFrame(o,p,pa,&ps,&config);o<<",\"npc\":";JsonFrame(o,n,na,&ns,&config);
    o<<",\"extras\":[";
    for(std::size_t i=0;i<extras.size();++i){if(i)o<<',';JsonEntrant(o,extras[i],vector_scope);}
    o<<"]}\n";
}
// Automatic per-race native measurements, including 30s after settlement and
// override-OFF baselines. Each file is bounded - 128 MB and 30 minutes - and
// what is kept on disk is the retention pruner's business, not a count of
// files this process has opened; a per-process cap of eight meant a trial-run
// session simply stopped recording partway through and said nothing. The byte
// count is per file: left cumulative it made the cap a per-session one, and a
// field of seven in every sample would have exhausted it three races in.
struct Recorder {
    std::string directory,path;
    std::ofstream file;
    std::vector<char> buffer=std::vector<char>(256*1024); // The stream's own; a 5 KB sample a tenth of a second is one write a second.
    bool enabled=true;
    // Optional compression is serialized off the game thread. Existing
    // recordings are never bulk-compressed on startup; interrupted work is
    // retained for review. Keep the public default and readable MSZIP format.
    bool compress_finished=false;
    std::atomic<bool> compressing=false;
    std::mutex notices_mutex;std::vector<std::string> notices;
    void Notice(std::string line){std::lock_guard<std::mutex> lock(notices_mutex);notices.push_back(std::move(line));}
    void Compress(std::filesystem::path source) {
        if(compressing.exchange(true))return;
        try {std::thread([this,source]{
            struct Finished{std::atomic<bool>& active;~Finished(){active=false;}} finished{compressing};
            std::filesystem::path target;std::error_code ec;
            const auto before=std::filesystem::file_size(source,ec);
            if(horse_telemetry::CompressFile(source,&target)) {
                const auto after=std::filesystem::file_size(target,ec);
                Notice("motion recording compressed: "+target.filename().string()+" "+horse_telemetry::Megabytes(before)+" -> "+horse_telemetry::Megabytes(after));
            } else Notice("motion recording left uncompressed: "+source.filename().string());
        }).detach();} catch(...) {compressing=false;}
    }
    void FlushNotices(){std::vector<std::string> lines;{std::lock_guard<std::mutex> lock(notices_mutex);lines.swap(notices);}if(log)for(const auto& l:lines)log(l);}
    // Older recordings go the moment a new one exists; see telemetry_retention.h.
    horse_telemetry::Retention retention;
    std::function<void(const std::string&)> log; // One line per prune, with what went and why.
    std::uint64_t token=0, exhausted_token=0, bytes=0,started_tick_ms=0,byte_limit=0;
    unsigned files=0, samples=0;
    bool bookmaker=false,bookmaker_result_written=false;
    double started=0,next=0,after_result=0;
    void Close() {
        if(!file.is_open())return;
        file.flush();file.close();
        if(compress_finished && !path.empty())Compress(path);
        path.clear();
    }
    bool Due(const horse_races::MotionContext& c,double now,bool bookmaker_racing=false,
        std::string_view bookmaker_metadata={}) {
        FlushNotices();
        if(!enabled || (!c.present && !bookmaker_racing)){Close();return false;}
        if(file.is_open() && (token!=c.race_token || (c.racing && bookmaker!=bookmaker_racing)))Close();
        if(!file.is_open()) {
            if(!c.racing || c.race_token==exhausted_token)return false;
            if(compressing.load())return false;
            std::error_code ec;
            const auto dir=horse_races::runtime_paths::Resolve(directory).Recordings();
            if(!horse_races::files::CanCreateDirectory(dir)){
                exhausted_token=c.race_token;if(log)log("motion recording refused: unsafe output directory");return false;}
            std::filesystem::create_directories(dir,ec);
            constexpr std::uint64_t hard_bytes=256ull*1024*1024,header_reserve=16*1024;
            const horse_telemetry::Retention policy{
                std::min(retention.keep_files?retention.keep_files:24u,24u),
                std::min(retention.keep_bytes?retention.keep_bytes:hard_bytes,hard_bytes)};
            if(ec||!horse_telemetry::RecordingUsage(dir).valid) {
                exhausted_token=c.race_token;if(log)log("motion recording refused: unreadable or interrupted existing output preserved");return false;
            }
            const auto pruned=horse_telemetry::Prune(dir,policy,{},1,header_reserve);
            if(pruned.Any()&&log)log(pruned.Describe(policy));
            const auto usage=horse_telemetry::RecordingUsage(dir);
            if(!usage.valid||usage.files>=policy.keep_files||usage.bytes>policy.keep_bytes||
                header_reserve>policy.keep_bytes-usage.bytes) {
                exhausted_token=c.race_token;if(log)log("motion recording refused: storage capacity unavailable; existing outputs preserved");return false;
            }
            byte_limit=std::min<std::uint64_t>(horse_races::race_pace::diagnostics_enabled?hard_bytes:128ull*1024*1024,
                policy.keep_bytes-usage.bytes);
            const auto opening_tick_ms=GetTickCount64();
            const auto candidate=dir/("race-"+std::to_string(GetCurrentProcessId())+"-"+
                std::to_string(opening_tick_ms)+"-"+std::to_string(files)+".jsonl");
            if(ec || std::filesystem::exists(candidate))return false;
            token=c.race_token;samples=0;bytes=0;started=next=now;after_result=0;
            bookmaker=bookmaker_racing;bookmaker_result_written=false;
            started_tick_ms=opening_tick_ms;
            std::ostringstream header;header<<"{\"schema\":\"HorseSpeedLab.motion.v2\",\"build\":\"1.18.0\",\"source\":\"in-process native pre-control; explicit physics_activation before/after readback\","
                "\"sample_interval_s\":0.1,\"force_applied_semantics\":\"native command submitted, not proof of physics response\","
                "\"bonus_state_semantics\":\"active is presentation; near_bonus_applied selects curve bonus; force_applied is submitted propulsion\","
                "\"native_vector_observation_semantics\":\"retained request/output snapshots; recording_scope marks older snapshots; within_recording is not proof of current actuation\","
                "\"surface_query_input_semantics\":\"1491.50 physics+0x440 and uint16 at +0x470; consistent snapshot only, not proof of a recent query or word meaning\","
                "\"surface_query_input_status_codes\":{\"0\":\"unavailable\",\"1\":\"nonfinite position\",\"2\":\"changed between reads\",\"3\":\"consistent snapshot\",\"4\":\"unsupported producer code\"},"
                "\"snow_grid_shadow_semantics\":\"diagnostic only; shared once per recording tick, double-read for consistency; not proof of freshness, physical snow depth, or the native acceptance state; outside grid is unknown\","
                "\"snow_grid_shadow_status_codes\":{\"0\":\"unsupported build\",\"1\":\"unavailable\",\"2\":\"invalid input or grid\",\"3\":\"outside grid; unknown\",\"4\":\"changed during read\",\"5\":\"consistent raw byte, freshness unverified\"},"
                "\"navigation_reason_codes\":{\"1\":\"route start\",\"2\":\"advance\",\"3\":\"stall recovery\"},"
                "\"pace_source_codes\":{\"0\":\"unavailable\",\"2\":\"exact terrain and slope\",\"3\":\"same terrain flat\",\"4\":\"speed rank preset\",\"5\":\"other-rank same-context estimate\",\"6\":\"other-rank terrain-flat estimate\",\"7\":\"same horse native snow/default resource ratio, estimated\"},"
                "\"snow_evidence_source_codes\":{\"0\":\"none\",\"1\":\"legacy player observation\",\"2\":\"legacy NPC observation\",\"3\":\"legacy location observation\",\"4\":\"experimental ground material candidate; not measured snow depth\"},"
                "\"ground_snow_status_codes\":{\"0\":\"inactive\",\"1\":\"pending\",\"2\":\"no ground hit\",\"3\":\"other material\",\"4\":\"snow material candidate\",\"5\":\"implausible ground height\"},"
                "\"snow_evidence_status_codes\":{\"0\":\"legacy field unused\"},"
                "\"snow_evidence_time_weather_semantics\":\"legacy location and weather fields are unused; ground material candidate expires after 0.75 seconds\","
                "\"vector_release_reason_codes\":{\"0\":\"none\",\"1\":\"ineligible\",\"2\":\"awake\",\"3\":\"task changed\",\"4\":\"capture failed\",\"5\":\"install failed\",\"6\":\"acquire failed\",\"7\":\"publish failed\",\"8\":\"stopped\",\"9\":\"environment unavailable\",\"10\":\"pace suppressed; registration retained but worker permission withdrawn\"},"
                "\"pace_sampling_reject_bits\":{\"1\":\"ineligible awake full effort\",\"2\":\"clock mismatch\",\"4\":\"burst or tail\",\"8\":\"invalid actual gait\",\"16\":\"unsettled motion or context\",\"32\":\"window mean below recent gait P90 or P90 uncalibrated\",\"64\":\"agitation or recovery\"},"
                "\"p90_source_codes\":{\"0\":\"warming up or rejected\",\"1\":\"same resource fallback\",\"2\":\"same resource and slope\"},"
                "\"player_learning_agitation_rejection_bit\":2048,"
                "\"player_learning_semantics\":\"current marks same-frame player sampler identity; last_window is a retained completed-window snapshot, sequence and age identify repeats; candidate_selected_seconds are submitted P90 slices, not guaranteed accepted time\","
                "\"player_learning_pair_semantics\":\"last_pair is a retained snapshot of the last completed pair, not the current provisional bucket; ages use sampler observation clock, not wall time\","
                "\"player_learning_pair_state_codes\":{\"0\":\"unavailable or no pair\",\"1\":\"stored\",\"2\":\"waiting for 3 seconds\",\"3\":\"disagreed\",\"4\":\"promoted\",\"5\":\"capacity\"},"
                "\"player_learning_unsettled_bits\":{\"1\":\"travel below 2m\",\"2\":\"coherence below 0.985\",\"4\":\"half-window speed change over 0.35m/s\",\"8\":\"half-window grade differs\",\"16\":\"invalid half-window\"},"
                "\"player_learning_candidate_break_codes\":[\"other\",\"travel\",\"context\",\"ineligible\",\"rejected_window\",\"span\",\"speed_range\"],"
                "\"navigation_decision_codes\":{\"0\":\"not checked\",\"1\":\"distance not reached\",\"2\":\"500ms cooldown\",\"3\":\"same target\",\"4\":\"target changed\",\"5\":\"regressing goal suppressed\"},"
                "\"driver_mode_codes\":{\"0\":\"traffic\",\"1\":\"original waypoint\",\"2\":\"exact custom waypoint\",\"3\":\"traffic fallback\"},"
                "\"native_fallback_reason_codes\":{\"0\":\"none\",\"1\":\"invalid input\",\"2\":\"identity changed\",\"3\":\"invalid index\",\"4\":\"wrong route section\",\"5\":\"regressed\",\"6\":\"not started\",\"7\":\"stopped\",\"8\":\"missed waypoint\"},"
                "\"exemption_refusal_codes\":{\"0\":\"none\",\"1\":\"not open circuit\",\"2\":\"roster size\",\"3\":\"actor missing or dead\",\"4\":\"actor listed twice\",\"5\":\"rider not on their mount\",\"6\":\"nobody to exempt\"},"
                "\"extras\":\"one compact object per field entrant while racing; empty otherwise\","
                "\"native_environment_provider_verified\":false,\"speed_volume_reader\":\"1491 code-guarded live table\","
                "\"driving_trace_v1\":"<<(horse_races::race_pace::diagnostics_enabled?"true":"false")
                <<",\"driving_trace_phase\":\"cached command state; captured_game_s is copy time; evaluated_game_s is controller time; native frame follows race tasks and precedes motion curve\""
                <<",\"site\":"<<c.site<<",\"race_token\":"<<token<<",\"recording_start_tick_ms\":"<<started_tick_ms;
            if(bookmaker && !bookmaker_metadata.empty())header<<",\"bookmaker\":"<<bookmaker_metadata;
            header<<"}\n";const auto initial=header.str();
            if(initial.size()>byte_limit||!horse_races::files::WriteNew(candidate,initial)){
                exhausted_token=token;if(log)log("motion recording header could not be committed within capacity");return false;}
            file.clear(); // A previous race's write failure must not poison this stream.
            file.rdbuf()->pubsetbuf(buffer.data(),static_cast<std::streamsize>(buffer.size()));
            file.open(candidate,std::ios::out|std::ios::app|std::ios::binary);
            if(!file){exhausted_token=token;return false;}
            path=candidate.string();++files;bytes=initial.size();
        }
        if(!c.racing && after_result==0)after_result=now;
        const std::uint64_t cap=byte_limit;
        if(now-started>1800 || (after_result && now-after_result>30) || bytes>=cap) {
            if(bytes>=cap && log)log("motion recording reached its byte limit; remaining frames not recorded");
            exhausted_token=token;Close();return false;
        }
        if(now<next)return false;next=now+.1;return true;
    }
    void WriteBookmakerResult(double now,std::string_view result){
        if(!file.is_open() || !bookmaker || bookmaker_result_written || result.empty())return;
        std::ostringstream row;row<<"{\"t\":"<<(now-started)<<",\"racing\":false,\"player\":null,\"npc\":null,\"extras\":[],\"bookmaker_result\":"<<result<<"}\n";
        const auto text=row.str();
        if(bytes>byte_limit||text.size()>byte_limit-bytes){exhausted_token=token;Close();return;}
        file<<text;bytes+=text.size();
        file.flush();bookmaker_result_written=true;
        if(!file && log)log("bookmaker result could not be written to motion recording");
    }
    void Write(const horse_races::MotionContext& c,double now,bool override_on,
        const NativeFrame& p,const AppliedFrame& pa,const NativeFrame& n,const AppliedFrame& na,
        const horse_races::NavigationTelemetry& navigation,const State& ps,const State& ns,
        const Config& config,const SampleTiming& timing,const HostBoostSample& boost={},
        const std::vector<EntrantSample>& extras={},bool player_stimulant=false,const PlayerLearningTrace* player_learning=nullptr) {
        if(!file.is_open())return;
        std::ostringstream o;JsonSample(o,c,now-started,override_on,p,pa,n,na,navigation,ps,ns,config,timing,boost,extras,player_stimulant,started_tick_ms,player_learning);
        const auto line=o.str();
        if(bytes>byte_limit||line.size()>byte_limit-bytes){
            exhausted_token=token;Close();if(log)log("motion recording reached reserved capacity; remaining frames not recorded");return;}
        file<<line;bytes+=line.size();if(++samples%10==0)file.flush();
        if(!file){
            // Stop only this recording. Disk availability can recover before
            // the next race; configured diagnostics remain enabled.
            exhausted_token=token;Close();
            if(log)log("motion recording stopped after a write failure; retrying next race");
        }
    }
};
} // namespace horse_motion
