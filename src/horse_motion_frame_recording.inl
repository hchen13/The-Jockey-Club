inline void JsonFrame(std::ostream& o,const NativeFrame& f,const AppliedFrame& a,
    const State* state=nullptr,const Config* config=nullptr) {
    if(!f.valid){o<<"null";return;}
    o<<"{\"horse\":"<<f.identity.horse<<",\"model\":"<<f.identity.model<<",\"rider\":"<<f.rider<<",\"xyz\":";
    Vector(o,f.position);o<<",\"velocity_mps\":";Vector(o,f.velocity);
    o<<",\"forward\":";Vector(o,f.forward);
    o<<",\"horizontal_kmh\":";Number(o,f.input.speed_kmh);
    auto ranks=[&](const char* name,const horse_attributes::Ranks& r){o<<",\""<<name<<"\":["<<r.speed<<','<<r.acceleration<<']';};
    ranks("raw_current",f.attributes.current);ranks("raw_base",f.attributes.entity_base);
    ranks("raw_bonus",f.attributes.bonus);ranks("raw_model_default",f.attributes.model_default);
    o<<",\"desired_before_control\":";Number(o,f.input.desired_blend);
    o<<",\"current_blend_xy\":";
    if(f.current_blend_valid){o<<'[';Number(o,f.current_blend_x);o<<',';Number(o,f.current_blend_y);o<<']';}else o<<"null";
    o<<",\"acceleration_requested\":"<<f.input.acceleration_requested<<",\"braking_requested\":"<<f.input.braking_requested<<",\"unsafe\":"<<f.input.environment.unsafe
        <<",\"in_water\":"<<f.input.environment.in_water<<",\"submerged\":";Number(o,f.submerged);
    o<<",\"reset_flags_h154_h174_r154_r174\":[";
    for(std::size_t i=0;i<f.reset_flags.size();++i){if(i)o<<',';o<<f.reset_flags[i];}o<<']';
    // Reset flags are transient; this is a read at observation time, not a
    // promise that another script did not alter the flag later in the frame.
    o<<",\"horse_reset_317\":";if(f.diagnostics)o<<f.horse_reset_317;else o<<"null";
    o<<",\"effective_native_cap\":null,\"native_cap_provider\":\"FULL_ENVIRONMENT_UNVERIFIED; see native_speed_volume_gait_cap\",\"force_applied\":";
    Number(o,a.force_applied);o<<",\"max_written\":";
    if(a.max_written)Number(o,*a.requested.max_blend);else o<<"null";
    o<<",\"desired_written\":";
    if(a.desired_written)Number(o,*a.requested.desired_blend);else o<<"null";
    auto optional=[&](const char* name,const std::optional<float>& value) {
        o<<",\""<<name<<"\":";if(value)Number(o,*value);else o<<"null";
    };
    optional("verified_script_max",f.limits.script_max);
    optional("native_speed_volume_gait_cap",f.volumes.max_blend);
    o<<",\"speed_volume_status\":"<<static_cast<int>(f.volumes.status)
        <<",\"speed_volume_matches\":"<<f.volumes.matches
        <<",\"speed_volume_static_count\":"<<f.volumes.static_count
        <<",\"speed_volume_script_count\":"<<f.volumes.script_count
        <<",\"speed_volume_mirrored\":"<<a.volume_mirrored;
    o<<",\"force_requested\":";Number(o,a.requested.forward_force);
    o<<",\"controller_suppressed\":"<<a.requested.suppressed;
    o<<",\"force_ramp_age_s\":";
    if(state && state->force_started)Number(o,f.input.now-*state->force_started);else o<<"null";
    o<<",\"acceleration_ramp_age_s\":";
    if(state && state->acceleration_started)Number(o,f.input.now-*state->acceleration_started);else o<<"null";
    o<<",\"force_ramp_duration_s\":";
    if(config && f.input.acceleration_level>=1 && f.input.acceleration_level<=10)
        Number(o,config->force_ramp_seconds[f.input.acceleration_level-1]);else o<<"null";
    o<<",\"status\":";
    if(!f.diagnostics)o<<"null";
    else {
        const auto& d=*f.diagnostics;
        o<<"{\"stamina_native\":";Number(o,d.stamina);o<<",\"max_stamina_native\":";Number(o,d.max_stamina);
        o<<",\"normalized_stamina_native\":";Number(o,d.normalized_stamina);
        o<<",\"fear\":";Number(o,d.fear);o<<",\"agitation\":";Number(o,d.agitation);
        o<<",\"ground_material\":";if(d.ground_material)o<<*d.ground_material;else o<<"null";
        o<<",\"stamina_core\":"<<d.stamina_core<<",\"health\":"<<d.health<<",\"max_health\":"<<d.max_health
            <<",\"bonding_rank\":"<<d.bonding_rank
            <<",\"on_screen\":"<<d.on_screen<<",\"visible\":"<<d.visible<<",\"static\":"<<d.is_static
            <<",\"frozen\":"<<d.frozen<<",\"collision_disabled\":"<<d.collision_disabled;
        o<<",\"rider_xyz\":";
        if(f.rider)Vector(o,d.rider_position);else o<<"null";
        o<<",\"rider_desired_blend\":";
        if(f.rider)Number(o,d.rider_desired_blend);else o<<"null";
        o<<",\"native_constraints\":";JsonNativeConstraints(o,d.native_constraints);
        o<<",\"snow_grid_shadow\":";JsonSnowGridShadow(o,d.snow_grid_shadow);
        o<<'}';
    }
    const auto& physics=f.physics_activation;
    o<<",\"physics_activation\":{\"eligible\":"<<physics.eligible<<",\"requested\":"<<physics.requested
        <<",\"requests_this_race\":"<<physics.requests<<",\"dynamic_requested\":"<<physics.dynamic_requested
        <<",\"collision_loaded\":"<<physics.collision_loaded<<",\"load_collision_flagged\":"<<physics.load_collision_flagged<<",\"simulation_lease\":"<<f.simulation_lease
        <<",\"player_recovery_pending\":"<<f.player_recovery_pending;
    auto observed_bool=[&](const char* key,const std::optional<bool>& value) {
        o<<",\""<<key<<"\":";if(value)o<<*value;else o<<"null";
    };
    observed_bool("static_before",physics.static_before);observed_bool("static_after",physics.static_after);
    observed_bool("collision_disabled_before",physics.collision_disabled_before);
    observed_bool("collision_disabled_after",physics.collision_disabled_after);
    o<<"}}";
}
inline void JsonNavigation(std::ostream& o,const horse_races::NavigationTelemetry& v,const NativeFrame& npc,
    const VectorRecordingScope* vector_scope=nullptr) {
    if(!v.valid){o<<"null";return;}
    o<<"{\"command_serial\":"<<v.command_serial<<",\"command_reason\":"<<v.command_reason
        <<",\"previous_target\":"<<v.previous_target<<",\"target\":"<<v.target<<",\"candidate\":"<<v.candidate
        <<",\"decision\":"<<v.decision<<",\"command_age_s\":";Number(o,v.command_age_s);
    o<<",\"physical_readiness\":";
    if(!v.physical_readiness)o<<"null";
    else {
        const auto& s=*v.physical_readiness;const auto& r=s.result;
        o<<"{\"read_attempted\":"<<r.attempted<<",\"read_time_s\":";Number(o,s.now);
        o<<",\"sample_age_s\":";Number(o,npc.input.now-s.now);
        o<<",\"race_token\":"<<s.race_token<<",\"horse\":"<<s.horse.horse
            <<",\"model\":"<<s.horse.model<<",\"address\":"<<s.horse.address
            <<",\"trial\":"<<s.trial<<",\"trial_focus_lease_active\":"<<s.trial_focus_lease_active
            <<",\"player_to_rider_m\":";if(s.distance_m)Number(o,*s.distance_m);else o<<"null";
        o<<",\"along_m\":";if(s.along_m)Number(o,*s.along_m);else o<<"null";
        const auto field=[&](const char* name,const std::optional<bool>& value) {
            o<<",\""<<name<<"\":";if(value)o<<*value;else o<<"null";
        };
        field("collision_disabled",r.collision_disabled);field("collision_loaded",r.collision_loaded);
        field("waiting_for_world",r.waiting);field("frozen",r.frozen);
        o<<",\"ready\":"<<r.ready<<'}';
    }
    o<<",\"goal\":[";for(std::size_t i=0;i<v.goal.size();++i){if(i)o<<',';Number(o,v.goal[i]);}o<<']';
    o<<",\"recorded\":"<<v.recorded<<",\"circuit\":"<<v.circuit<<",\"node_count\":"<<v.node_count
        <<",\"driver_mode\":"<<v.driver_mode<<",\"skill_percent\":"<<v.skill_percent
        <<",\"native_waypoint_progress\":"<<v.native_waypoint_progress
        <<",\"native_entry_index\":"<<v.native_entry_index<<",\"native_fallback_reason\":"<<v.native_fallback_reason
        <<",\"pace_braking\":"<<v.pace_braking
        <<",\"gate_recovery\":"<<v.gate_recovery<<",\"route_retries\":"<<v.route_retries
        <<",\"player_lap\":"<<v.player_lap<<",\"npc_lap\":"<<v.npc_lap<<",\"total_laps\":"<<v.total_laps
        <<",\"npc_next_gate\":"<<v.npc_next_gate<<",\"player_next_gate\":"<<v.player_next_gate
        <<",\"player_continuity_lost\":"<<v.player_continuity_lost
        <<",\"npc_continuity_lost\":"<<v.npc_continuity_lost;
    auto number=[&](const char* key,double n){o<<",\""<<key<<"\":";Number(o,n);};
    auto optional=[&](const char* key,const std::optional<float>& n){o<<",\""<<key<<"\":";if(n)Number(o,*n);else o<<"null";};
    number("nav_speed_mps",v.nav_speed_mps);number("player_along_m",v.player_along_m);number("npc_along_m",v.npc_along_m);
    number("player_deviation_m",v.player_deviation_m);number("npc_deviation_m",v.npc_deviation_m);
    number("course_length_m",v.course_length_m);
    o<<",\"racing_line_percent\":"<<v.racing_line_percent;
    number("line_max_offset_m",v.line_max_offset_m);number("line_planned_offset_m",v.line_planned_offset_m);
    number("line_offset_m",v.npc_line_offset_m);
    optional("remaining_to_target_m",v.remaining_to_target_m);optional("target_adjacent_cos",v.target_adjacent_cos);
    optional("next_gate_along_m",v.next_gate_along_m);
    optional("player_next_gate_along_m",v.player_next_gate_along_m);
    number("reference_speed_mps",v.reference_speed_mps);
    number("nominal_lookahead_m",v.nominal_lookahead_m);
    number("advance_threshold_m",v.advance_threshold_m);
    number("native_effort_readback",v.native_effort_readback);
    o<<",\"native_absolute_speed\":"<<v.native_absolute_speed;
    number("native_requested_mps",v.native_requested_mps);
    JsonNativeTable(o,v.native_table);
    JsonNativeRate(o,v.native_rate);
    JsonNativeVector(o,v.native_vector,vector_scope);
    number("pace_target_mps",v.pace_target_mps);number("pace_effort",v.pace_effort);
    o<<",\"following_limited\":"<<v.following_limited<<",\"following_horse\":"<<v.following_horse;
    number("following_gap_m",v.following_gap_m);
    o<<",\"terrain_limited\":"<<v.terrain_limited;
    number("terrain_caution",v.terrain_caution);number("terrain_height_error_m",v.terrain_height_error_m);
    o<<",\"creeping\":"<<v.creeping;
    number("progress_anchor_m",v.progress_anchor_m);number("progress_age_s",v.progress_age_s);
    o<<",\"target_distance_m\":";
    const float dx=v.goal[0]-npc.position.x,dy=v.goal[1]-npc.position.y;
    const float distance=std::hypot(dx,dy),forward=std::hypot(npc.forward.x,npc.forward.y);
    if(npc.valid)Number(o,distance);else o<<"null";
    o<<",\"forward_cos_to_target\":";
    if(npc.valid && distance>.01F && forward>.01F)
        Number(o,std::clamp((dx*npc.forward.x+dy*npc.forward.y)/(distance*forward),-1.0F,1.0F));else o<<"null";
    drive_recording::Json(o,v.driving);o<<'}';
}
