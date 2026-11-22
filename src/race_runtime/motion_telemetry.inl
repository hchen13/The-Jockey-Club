// Private motion telemetry implementation; included by race_system.cpp.
void LogExemptionRefusal(const char* reason,std::size_t roster) {
    Log(std::string("town speed exemption not applied: ")+reason+
        " (roster entities="+std::to_string(roster)+", cap="+
        std::to_string(horse_motion::kMaxExemptEntities)+")");
}
// A breed-challenge host returns to yellow; other rivals keep their base.
// Colour writes and retries never create an earned-start dialogue event.
void HostBoostChanged(bool active) {
    active=active && PlayerRaceBurstEligible();
    const bool changed=active!=g_host_burst_shown;
    if(!changed && g_opponent_colour_ready)return;
    g_host_burst_shown=active;
    if(g_opponent_blip && MAP::DOES_BLIP_EXIST(g_opponent_blip)) {
        g_opponent_colour_ready=bookmaker_blips::ApplyColour(g_opponent_blip,
            bookmaker_blips::Desired(CurrentRules().breed.has_value(),active));
    }
}
void HostBoostStarted() {
    if(PlayerRaceBurstEligible() && g_host_burst_earned && g_state==State::Racing && MaryOwned())
        g_mary.Queue(marybeth_dialogue::Event::Start,MaryNow(),6.0);
}
void PlayerBurstChanged(bool active,double seconds) {
    if(!active)return;
    char text[160];
    std::snprintf(text,sizeof text,i18n::Text("The stimulant gives your horse a burst of speed for %.0f seconds.","兴奋剂让你的马短暂爆发，速度提升持续 %.0f 秒。"),seconds);
    ShowObjective(text);
}
void LogHostBoost(bool active,float gait,float bar,float force,bool started) {
    Log(std::string("field host bonus ")+(active?(started?"earned":"resumed"):"withdrawn")+
        " at gait "+std::to_string(gait)+"; bar "+std::to_string(bar)+
        ", boosted force "+std::to_string(force));
}
void ResetCatchup(){
    g_host_catchup.Reset();g_pace_command.catchup_fraction=0;
    g_competitive_follower.native_vector.Release();
    for(auto& e:g_extra_rivals){e.chase.Reset();e.command.catchup_fraction=0;e.follower.native_vector.Release();}
}
void SetMotionBoostState(int horse,bool earned,bool awake) {
    if(horse==g_opponent_horse) {
        g_host_burst_earned=earned&&g_state==State::Racing&&PlayerRaceBurstEligible();
        if(awake)g_competitive_follower.native_vector.Release();
        return;
    }
    for(auto& e:g_extra_rivals)if(horse==e.horse.horse&&awake)e.follower.native_vector.Release();
}
MotionContext GetMotionContext()
{
    MotionContext c;
    c.circuit_staging=g_state==State::PreparingCircuit;
    c.racing=g_state==State::Racing;
    c.npc_braking=c.racing && g_pace_command.braking;
    c.eliminated=g_main_rival_out;
    c.real_burst_display_eligible=PlayerRaceBurstEligible();
    // Neither a speed nor the route decides the bonus (a rank's speed differs
    // uphill and on sand; a straightness test was reachable on 1% of New
    // Austin): the horse's own gait does, read where the gate runs. The race
    // contributes only that she is racing and not braking for a bend.
    if(const auto& route=SelectedFixedRoute();PlayerRaceBurstEligible() && route.host_level_bonus>0) {
        c.host_level_bonus=route.host_level_bonus;
        c.host_boost_seconds=route.host_boost_seconds;
        const auto* station=StationOf(g_variant);
        if(station && std::string_view(station->key)=="arthur-camp-duel" &&
            FixedProfile().id==characters::Id::Arthur && !CurrentRules().breed)
            c.host_boost_cooldown_seconds=static_cast<float>(host_boost::kArthurDuelCooldownSeconds);
        c.host_boost_eligible=c.racing && !g_pace_command.braking;
    }
    c.present=(c.racing || g_state==State::Result || g_state==State::Departing) && g_opponent_horse &&
        ENTITY::DOES_ENTITY_EXIST(g_opponent_horse);
    c.town_speed_exempt=c.racing && c.present;
    c.npc_override_intent=c.racing && c.present && !c.eliminated &&
        horse_motion::OwnedRaceOverrideIntent(g_competitive_follower,g_host_navigation,g_pace_command,g_opponent_rider);
    c.catchup_engaged=c.racing&&c.present&&!c.eliminated&&g_host_catchup.Active();
    c.catchup_active=c.catchup_engaged&&(c.npc_override_intent||(!g_pace_command.braking&&!g_pace_command.NativePaceLimited()));
    c.catchup_fraction=c.catchup_active?g_host_catchup.fraction:0;
    c.catchup_gap_valid=c.racing&&!c.eliminated&&g_host_catchup.gap_valid;
    c.catchup_gap_m=c.catchup_gap_valid?g_host_catchup.gap:0;
    if(c.present) {
        c.horse=g_opponent_horse;c.rider=g_opponent_rider;c.site=g_variant->id;c.phase=static_cast<int>(g_state);
        c.race_token=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            g_race_started_at.time_since_epoch()).count());
    }
    return c;
}

std::vector<MotionContext> GetAdditionalMotionContexts()
{
    std::vector<MotionContext> result;
    const auto shared=GetMotionContext();
    for(const auto& e:g_extra_rivals)if(!e.retiring && e.horse.Valid()) {
        // The earned edge belongs to the host alone, not to the field.
        auto c=shared;c.horse=e.horse.horse;c.rider=e.rider.horse;c.eliminated=e.out;
        c.host_level_bonus=0;c.host_boost_seconds=0;c.host_boost_cooldown_seconds=0;c.host_boost_eligible=false;
        c.real_burst_display_eligible=false;
        c.present=HorseIdentity(c.horse)==e.horse && HorseIdentity(c.rider)==e.rider;
        c.npc_braking=e.command.braking;
        c.npc_override_intent=c.racing && c.present && !c.eliminated &&
            horse_motion::OwnedRaceOverrideIntent(e.follower,e.navigation,e.command,e.rider.horse);
        c.catchup_engaged=c.racing&&c.present&&!c.eliminated&&e.chase.Active();
        c.catchup_active=c.catchup_engaged&&(c.npc_override_intent||(!e.command.braking&&!e.command.NativePaceLimited()));
        c.catchup_fraction=c.catchup_active?e.chase.fraction:0;
        c.catchup_gap_valid=c.racing&&!c.eliminated&&e.chase.gap_valid;
        c.catchup_gap_m=c.catchup_gap_valid?e.chase.gap:0;
        result.push_back(c);
    }
    return result;
}

// Signed lateral distance of a position from the centreline at its
// projection, + = left of travel: the sign convention of the line's offsets,
// so a recording can lay the ridden offset over the planned one.
float TerrainHeightError(Ped horse,float along)
{
    if(g_terrain_profile.metres.empty() || !horse || !ENTITY::DOES_ENTITY_EXIST(horse))return 0;
    const auto at=ENTITY::GET_ENTITY_COORDS(horse,true,false);
    const auto projected=course::Project(g_course,at,std::max(0.F,along-25),along+25);
    return projected.valid?at.z-terrain::At(g_course,projected.along).z:0;
}
float SignedLateral(const course::Path& path,const course::Projection& at,Vector3 position)
{
    if(!at.valid || at.segment+1>=path.points.size())return 0;
    const auto a=path.points[at.segment],b=path.points[at.segment+1];
    const float cross=(b.x-a.x)*(position.y-a.y)-(b.y-a.y)*(position.x-a.x);
    return cross<0?-at.distance:at.distance;
}
// The line's planned offset at a centreline along, interpolated between nodes.
float PlannedLineOffset(const racing_line::Result& line,float centre_along)
{
    if(line.offsets.size()<2 || line.offsets.size()!=line.reference.size() || !std::isfinite(centre_along))return 0;
    const auto i=std::min(line.Index(centre_along),line.offsets.size()-2);
    const float span=line.reference[i+1]-line.reference[i];
    const float t=span>0?std::clamp((centre_along-line.reference[i])/span,0.0F,1.0F):0;
    const auto& offsets=line.canonical_offsets.size()==line.offsets.size()?line.canonical_offsets:line.offsets;
    return offsets[i]+(offsets[i+1]-offsets[i])*t;
}
// Same membership and order as GetAdditionalMotionContexts(), so the two can
// be joined by position as well as by horse handle.
std::vector<EntrantTelemetry> GetAdditionalEntrantTelemetry()
{
    std::vector<EntrantTelemetry> result;
    const double now=CircuitNow();
    for(std::size_t i=0;i<g_extra_rivals.size();++i) {
        const auto& e=g_extra_rivals[i];
        if(e.retiring || !e.horse.Valid())continue;
        EntrantTelemetry t;
        t.index=static_cast<int>(i);t.horse=e.horse.horse;t.rider=e.rider.horse;t.out=e.out;
        t.pace_braking=e.command.braking;t.pace_target_mps=e.command.target_mps;t.pace_effort=e.command.effort;
        t.driving=drive_diagnostics::Capture(e.command,e.follower,e.navigation,e.avoidance.path,
            e.avoidance.reference,now,g_laps.plan.course.strict_half_width);
        t.terrain_limited=e.command.terrain_limited;t.terrain_caution=e.terrain_caution;
        t.following_limited=e.command.following_limited;t.following_horse=e.command.following_horse;
        t.following_gap_m=e.command.following_gap_m;
        t.terrain_height_error_m=TerrainHeightError(e.horse.horse,e.proxy.current.along);
        t.follower_control=e.follower.control;
        // A follower that was started and is no longer in control has fallen
        // back to traffic - the reading the host reports through
        // g_continuous_attempted. One never started is plain traffic.
        t.driver_mode=e.follower.control?2:e.follower.starts>0?3:0;
        t.native_fallback_reason=static_cast<int>(e.follower.failure);
        t.native_waypoint_progress=e.follower.progress;t.native_entry_index=e.follower.entry;
        t.native_effort_readback=e.follower.override_readback;
        t.native_absolute_speed=e.follower.override_absolute;
        t.native_requested_mps=e.follower.override_absolute?e.follower.override_request:0;
        t.native_table=e.follower.speed_table.Observe();
        t.native_rate=e.follower.move_rate.observation;
        t.native_vector=e.follower.native_vector.Observe();
        t.along_m=e.proxy.current.along;
        t.next_gate=static_cast<int>(e.progress.next_gate);
        t.route_deviation_m=e.proxy.current.distance;
        t.restarts=e.retries.total;
        t.stalled_s=e.stalled_since>=0?now-e.stalled_since:-1;
        t.traffic_target=static_cast<int>(e.traffic_target);
        t.raw_target={e.target.speed,e.target.acceleration};
        t.line_aggression=e.line_aggression;t.line_max_offset_m=e.line.max_offset;
        if(e.proxy.current.valid && e.rider.Valid())
            t.line_offset_m=SignedLateral(g_course,e.proxy.current,ENTITY::GET_ENTITY_COORDS(e.rider.horse,true,false));
        result.push_back(t);
    }
    return result;
}
