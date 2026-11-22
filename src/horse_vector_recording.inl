// Native vector observations, included in namespace horse_motion.
struct VectorRecordingScope {
    std::uint64_t started_tick_ms=0,sample_tick_ms=0;
};
inline void JsonNativeVector(std::ostream& o,const native_vector::Observation& v,
    const VectorRecordingScope* scope=nullptr){
    o<<",\"native_vector\":{\"state\":"<<v.state<<",\"calls\":"<<v.calls<<",\"modified\":"<<v.modified;
    o<<",\"target_mps\":";Number(o,v.target);o<<",\"before_mps\":";Number(o,v.before);
    o<<",\"after_mps\":";Number(o,v.after);o<<",\"multiplier\":";Number(o,v.multiplier);
    o<<",\"native_reference_mps\":";Number(o,v.native_reference);
    o<<",\"normal_reference_mps\":";Number(o,v.normal_reference);
    o<<",\"environment_valid\":"<<v.environment_valid<<",\"environment_status\":"<<v.environment_status;
    o<<",\"environment_resource_key\":"<<v.environment_resource_key<<",\"environment_normal_key\":"<<v.environment_normal_key;
    o<<",\"snow_inferred\":"<<v.snow_inferred<<",\"snow_evidence_source\":"<<v.snow_evidence_source<<",\"snow_weather_status\":"<<v.snow_weather_status<<",\"snow_evidence_status\":"<<v.snow_evidence_status;
    o<<",\"snow_evidence_distance_m\":";Number(o,v.snow_evidence_distance);
    o<<",\"snow_evidence_age_s\":";Number(o,v.snow_evidence_age);
    o<<",\"environment_limited\":"<<v.environment_limited<<",\"environment_ratio\":";Number(o,v.environment_ratio);
    o<<",\"unrestricted_target_mps\":";Number(o,v.unrestricted_target);
    o<<",\"world_target_mps\":";Number(o,v.world_target);o<<",\"world_speed_mps\":";Number(o,v.world_speed);
    o<<",\"feedback_gain\":";Number(o,v.feedback_gain);o<<",\"feedback_filtered_mps\":";Number(o,v.feedback_filtered);
    o<<",\"feedback_settled_seconds\":";Number(o,v.feedback_settled);
    o<<",\"feedback_native_interval_s\":";Number(o,v.feedback_native_interval);
    o<<",\"feedback_sample_age_s\":";Number(o,v.feedback_sample_age);
    const auto& l=v.learning;
    o<<",\"pace_learning\":{\"prior_mps\":";Number(o,l.prior);o<<",\"base_mps\":";Number(o,l.base);
    o<<",\"window_mean_mps\":";Number(o,l.window_mean);
    o<<",\"learned\":"<<l.learned<<",\"windows\":"<<l.windows<<",\"accepted\":"<<l.accepted<<",\"rejected\":"<<l.rejected;
    o<<",\"inherited\":"<<l.inherited<<",\"history_samples\":"<<l.history_samples;
    o<<",\"resource\":"<<v.pace_resource<<",\"slope\":"<<v.pace_slope<<",\"source\":"<<v.pace_source;
    o<<",\"grade\":";Number(o,v.pace_grade);o<<",\"measured_mps\":";Number(o,v.pace_measured);
    o<<",\"flat_base_mps\":";Number(o,v.pace_flat_base);o<<",\"ability_gain\":";Number(o,v.pace_ability_gain);
    o<<",\"native_terrain_ratio\":";Number(o,v.pace_native_terrain_ratio);
    o<<",\"gap_valid\":"<<l.gap_valid<<",\"gap_m\":";Number(o,l.gap);
    o<<",\"catchup_fraction\":";Number(o,l.bonus);o<<",\"target_mps\":";Number(o,l.target);o<<'}';
    const auto& s=v.sampling;
    o<<",\"pace_sampling\":{\"actual_gait\":";Number(o,s.actual_gait);
    o<<",\"minimum_gait\":";Number(o,s.minimum_gait);
    o<<",\"last_window_mean_gait\":";Number(o,s.mean_gait);
    o<<",\"last_window_resource\":"<<s.resource<<",\"last_window_slope\":"<<s.slope;
    o<<",\"windows\":"<<s.windows<<",\"qualified\":"<<s.qualified<<",\"last_window_reject_mask\":"<<s.reject_mask;
    o<<",\"p90_source\":"<<s.p90_source<<",\"p90_resource_samples\":"<<s.p90_resource_samples<<",\"p90_slope_samples\":"<<s.p90_slope_samples;
    o<<",\"terrain_flat_mps\":";Number(o,v.pace_terrain_flat);o<<",\"speed_preset_mps\":";Number(o,v.pace_preset);o<<'}';
    const auto& w=v.feedback_window;
    o<<",\"feedback_window\":{\"windows\":"<<w.windows<<",\"accepted\":"<<w.accepted<<",\"reject_mask\":"<<w.reject_mask;
    o<<",\"clock_ratio\":";Number(o,w.clock_ratio);o<<",\"eligible_fraction\":";Number(o,w.eligible_fraction);
    o<<",\"native_fraction\":";Number(o,w.native_fraction);o<<",\"speed_fraction\":";Number(o,w.speed_fraction);
    o<<",\"before_ratio\":";Number(o,w.before_ratio);o<<",\"after_ratio\":";Number(o,w.after_ratio);
    o<<",\"mean_z_mps\":";Number(o,w.mean_z);o<<",\"peak_z_mps\":";Number(o,w.peak_z);
    o<<",\"coherence\":";Number(o,w.coherence);o<<",\"heading_dot\":";Number(o,w.heading_dot);
    o<<",\"speed_delta_mps\":";Number(o,w.speed_delta);
    o<<",\"position_valid\":"<<w.position_valid<<",\"position_mean_z_mps\":";Number(o,w.position_mean_z);o<<'}';
    o<<",\"callbacks\":"<<v.callbacks<<",\"rejected_output\":"<<v.rejected_output<<",\"rejected_step\":"<<v.rejected_step;
    o<<",\"rejected_environment\":"<<v.rejected_environment;
    o<<",\"step_seconds\":";Number(o,v.step_seconds);o<<",\"checkpoint\":"<<v.checkpoint;
    o<<",\"registration\":"<<v.registration<<",\"resets\":"<<v.resets<<",\"suspensions\":"<<v.suspensions<<",\"reset_reason\":"<<v.reset_reason;
    o<<",\"bind_reason\":"<<v.bind_reason<<",\"release_reason\":"<<v.release_reason;
    if(native_vector::diagnostics_enabled.load(std::memory_order_relaxed)) {
        o<<",\"request_tick_ms\":"<<v.request_tick_ms<<",\"output_tick_ms\":"<<v.output_tick_ms;
        const auto xyz=[&](const char* key,const auto& values){o<<",\""<<key<<"\":";
            if(!v.output_tick_ms){o<<"null";return;}o<<'[';
            for(std::size_t i=0;i<3;++i){if(i)o<<',';Number(o,values[i]);}o<<']';};
        xyz("input_xyz",v.input_xyz);xyz("output_xyz",v.output_xyz);
    }
    if(scope) {
        // WindowsTrial intentionally keeps its last released snapshot. Describe
        // that snapshot's clock, never clear the live trial just for logging.
        const bool timed=native_vector::diagnostics_enabled.load(std::memory_order_relaxed);
        o<<",\"recording_scope\":{\"start_tick_ms\":"<<scope->started_tick_ms
            <<",\"sample_tick_ms\":"<<scope->sample_tick_ms;
        const auto stamp=[&](const char* name,std::uint64_t tick) {
            o<<",\""<<name<<"_scope\":\"";
            if(!timed||!tick)o<<"unavailable";
            else if(tick>scope->sample_tick_ms)o<<"after_sample";
            else if(tick<scope->started_tick_ms)o<<"before_recording";
            else o<<"within_recording";
            o<<"\",\""<<name<<"_age_ms\":";
            if(timed&&tick&&tick<=scope->sample_tick_ms)o<<scope->sample_tick_ms-tick;
            else o<<"null";
        };
        stamp("request",v.request_tick_ms);stamp("output",v.output_tick_ms);o<<'}';
    }
    o<<",\"ground_snow_candidate\":{\"status\":"<<v.ground_snow_status<<",\"material\":"<<v.ground_snow_material
        <<",\"flags\":"<<v.ground_snow_flags<<",\"height_delta_m\":";Number(o,v.ground_snow_height_delta);
    o<<",\"pace_material\":"<<v.pace_material<<'}';
    o<<'}';
}
