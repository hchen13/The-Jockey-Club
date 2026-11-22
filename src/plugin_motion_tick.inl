// Per-frame player, host and field horse motion integration.
// Included at the original definition site; declaration and initialization order are preserved.
void TickAutoCurves()
{
    const bool apply=g_enabled;
    // Game seconds, not wall seconds: a pause must not age the force ramps, break
    // the host's hold, or retire the recording that is meant to explain the race.
    const double now=horse_races::GameSeconds();
    const auto context=horse_races::bookmaker_runtime::Active()?
        horse_races::bookmaker_runtime::MainMotion():horse_races::GetMotionContext();
    const auto extras=horse_races::bookmaker_runtime::Active()?
        horse_races::bookmaker_runtime::ExtraMotion():horse_races::GetAdditionalMotionContexts();
    const bool bookmaker_racing=horse_races::bookmaker_runtime::Active() && context.racing;
    if(g_motion_recorder.file.is_open() && g_motion_recorder.bookmaker && !bookmaker_racing &&
        horse_races::bookmaker_runtime::Active() &&
        g_motion_recorder.token==horse_races::bookmaker_runtime::RecordingToken())
        g_motion_recorder.WriteBookmakerResult(now,horse_races::bookmaker_runtime::RecordingResult());
    const bool needs_bookmaker_header=bookmaker_racing && g_motion_recorder.enabled &&
        context.race_token!=g_motion_recorder.exhausted_token &&
        (!g_motion_recorder.file.is_open() || g_motion_recorder.token!=context.race_token || !g_motion_recorder.bookmaker);
    const std::string bookmaker_header=needs_bookmaker_header?horse_races::bookmaker_runtime::RecordingMetadata():std::string{};
    const bool record=g_motion_recorder.Due(context,now,bookmaker_racing,bookmaker_header);
    // One shared snapshot per recording tick, never on ordinary game frames.
    // This is a read-only diagnostic; no value enters pace or horse control.
    const auto snow_grid_header=record?horse_motion::snow_grid_probe::CaptureHeader():horse_motion::snow_grid_probe::Header{};
    const auto sample_snow_grid=[&](horse_motion::NativeFrame& frame){
        if(record && frame.valid && frame.diagnostics)
            frame.diagnostics->snow_grid_shadow=horse_motion::snow_grid_probe::Capture(
                snow_grid_header,frame.position.x,frame.position.y);
    };
    if(g_last_recording_path!=g_motion_recorder.path) {
        g_last_recording_path=g_motion_recorder.path;
        Log("paired native motion recording: "+g_last_recording_path);
    }
    if(!apply && !record && !context.present) {
        g_player_speed_learning.Stop();g_player_speed_learning.Report(Log);
        ReleasePlayerGaitExperiment("override inactive");
        ReleasePlayerGaitMinimum();g_player_gait={};
        g_race_simulation.Release();ResetRaceOpponentCurve();g_host_boost.Reset();g_host_boost_reported=false;
        horse_races::HostBoostChanged(false);
        g_exemption_reported=horse_motion::ExemptionRefusal::None;
        for(auto& motion:g_extra_motion)motion.Reset();
        for(auto& burst:g_bookmaker_bursts)burst.Reset();
        g_bookmaker_burst_reported.fill(false);
        g_extra_physics={};return;
    }
    const Ped player=PLAYER::PLAYER_PED_ID();
    const Ped mount=PED::IS_PED_ON_MOUNT(player)?PED::GET_MOUNT(player):0;
    std::vector<Ped> active_actors;
    if(mount){active_actors.push_back(player);active_actors.push_back(mount);}
    if(context.present && !context.eliminated && context.horse!=mount) {
        active_actors.push_back(context.rider);active_actors.push_back(context.horse);
    }
    for(const auto& e:extras)if(e.present && !e.eliminated && e.horse!=mount) {
        active_actors.push_back(e.rider);active_actors.push_back(e.horse);
    }
    const bool simulation_lease=g_race_simulation.ApplyGroup(apply && context.racing,context.race_token,active_actors);
    // This used to fail silently for any grid beyond six rider/horse pairs.
    // One transition record per roster/refusal makes the next field test
    // independently auditable without writing every frame.
    static std::uint64_t reported_simulation_token=0;
    static std::size_t reported_simulation_size=0;
    static bool reported_simulation_active=false;
    static const char* reported_simulation_refusal=nullptr;
    if(context.racing && (reported_simulation_token!=context.race_token ||
        reported_simulation_size!=active_actors.size() || reported_simulation_active!=simulation_lease ||
        reported_simulation_refusal!=g_race_simulation.refusal)) {
        Log("race simulation lease: pairs="+std::to_string(active_actors.size()/2)+
            " active="+std::to_string(simulation_lease)+
            (simulation_lease?std::string():std::string(" reason=")+(g_race_simulation.refusal?g_race_simulation.refusal:"unknown")));
        reported_simulation_token=context.race_token;
        reported_simulation_size=active_actors.size();
        reported_simulation_active=simulation_lease;
        reported_simulation_refusal=g_race_simulation.refusal;
    } else if(!context.racing)reported_simulation_token=0;
    for(std::size_t i=0;i<g_extra_motion.size();++i)if(i>=extras.size() || !extras[i].present || extras[i].horse==mount)
        g_extra_motion[i].Reset();
    if(apply && mount && (mount!=g_auto_mount || Clock::now()>=g_next_attribute_refresh))SelectAutoMount(mount);
    // Release the former NPC's owned cap before a player who has taken that
    // horse receives this frame's player curve, never overwrite it afterwards.
    if(!context.present || context.horse==mount)ResetRaceOpponentCurve();
    // A refusal is reported once per change, not every frame: an oversized
    // roster silently exempting nobody is exactly what pinned the host to a
    // walk through Armadillo for a whole race.
    horse_motion::ExemptionRefusal exemption_refusal=horse_motion::ExemptionRefusal::None;
    const bool open_circuit=horse_motion::ApplyCircuitSpeedExemptionToRoster(
        apply && context.town_speed_exempt,active_actors,&exemption_refusal);
    if(apply && context.town_speed_exempt && exemption_refusal!=g_exemption_reported) {
        g_exemption_reported=exemption_refusal;
        if(exemption_refusal!=horse_motion::ExemptionRefusal::None)
            horse_races::LogExemptionRefusal(horse_motion::ExemptionRefusalName(exemption_refusal),
                active_actors.size());
    } else if(!apply || !context.town_speed_exempt)g_exemption_reported=horse_motion::ExemptionRefusal::None;
    auto player_frame=horse_motion::Observe(mount,now,PAD::IS_CONTROL_PRESSED(0,INPUT_HORSE_SPRINT),record);
    sample_snow_grid(player_frame);
    // The game has the reins, not the player: a stable's exit, a cutscene, a
    // fade. The mount is placed and walked by script then, and nothing of
    // ours is written to it - no cap, no gait, no force - until control is
    // back. 2026-09-19: a horse taken out of a stable fell and sank through
    // the ground, once in several tries; whether these writes were the cause
    // is not proven, but a mount the player cannot steer is not one for the
    // curve to steer either.
    const bool reins_held=!PLAYER::IS_PLAYER_CONTROL_ON(PLAYER::PLAYER_ID()) || CAM::IS_SCREEN_FADED_OUT() ||
        CAM::IS_SCREEN_FADING_OUT() || CAM::IS_SCREEN_FADING_IN();
    if(reins_held!=g_reins_held_reported) {
        g_reins_held_reported=reins_held;
        Log(reins_held?"player mount: the game holds the reins (control off or fading); the curve writes nothing until it returns":
            "player mount: control returned; the curve resumes");
    }
    if(reins_held)player_frame.input.environment.unsafe=true;
    const bool mount_recovery_pending=RepairMountCollision(player,mount,
        !reins_held && !context.circuit_staging,now,context.racing,player_frame.valid?&player_frame.position:nullptr);
    player_frame.player_recovery_pending=mount_recovery_pending;
    // No accumulating custom push/gait writes into an unverified physics state.
    // Native player control remains available; the normal curve resumes only
    // after collision AND dynamic simulation stay healthy across engine frames.
    if(mount_recovery_pending)player_frame.input.environment.unsafe=true;
    auto npc_frame=horse_motion::Observe(context.horse,now,context.racing && !context.eliminated,record,context.present);
    sample_snow_grid(npc_frame);
    player_frame.input.braking_requested=context.racing && PAD::IS_CONTROL_PRESSED(0,INPUT_HORSE_STOP);
    npc_frame.input.braking_requested=context.npc_braking && !context.npc_override_intent;
    horse_motion::RespectNativeNpcIntent(npc_frame,context.npc_override_intent);
    npc_frame.input.hold_race_override_force=context.npc_override_intent;
    player_frame.simulation_lease=npc_frame.simulation_lease=simulation_lease;
    const bool maintain_race_physics=apply && context.racing && context.horse!=mount;
    player_frame.physics_activation=g_player_physics.Apply(player_frame,player,maintain_race_physics,context.race_token);
    npc_frame.physics_activation=g_opponent_physics.Apply(npc_frame,context.rider,maintain_race_physics,context.race_token);
    // The player's burst: a stimulant actually used while racing, confirmed
    // by the original horse script's consumption marker and stat increment,
    // lasting by the mount's
    // bonding; while it runs the curve drives the mount as speed level 11
    // (level 10 with that slot's force raised) and acceleration level 10.
    auto player_config=g_motion_config;
    const horse_races::stimulant::Scope stimulant_scope{context.racing?context.race_token:0,player,mount};
    if(g_stimulant.Bind(stimulant_scope)) {
        g_stimulant_detection.Reset();g_stimulant_observed_reported=g_stimulant_counts_reported=false;
        if(g_stimulant_reported){Log("player stimulant burst released: race/player/mount changed game_ms="+std::to_string(MISC::GET_GAME_TIMER()));horse_races::PlayerBurstChanged(false,0);}
        g_stimulant_reported=false;
    }
    if(apply && context.racing && mount && player_frame.valid) {
        const int inventory=INVENTORY::_INVENTORY_GET_INVENTORY_ID_FROM_PED(player);
        const auto count=[&](const char* item){return INVENTORY::_INVENTORY_GET_INVENTORY_ITEM_COUNT_WITH_ITEMID(inventory,MISC::GET_HASH_KEY(item),false);};
        const horse_races::stimulant::Counts counts{count("consumable_horse_stimulant"),count("consumable_potent_horse_stimulant"),
            std::max(count("consumable_special_horse_stimulant"),count("consumable_special_horse_stimulant_crafted"))};
        // The game's bonding rank runs 0..4; the mod's levels 1..5 (a fresh horse is level 1).
        const int bonding=std::clamp(ATTRIBUTE::GET_ATTRIBUTE_RANK(mount,7)+1,1,5);
        // Diagnostic only: infinite-items trainers may keep these unchanged,
        // and a discarded/removed item is not a dose.
        if(!g_stimulant_counts_reported || counts.regular!=g_stimulant_counts.regular || counts.potent!=g_stimulant_counts.potent || counts.special!=g_stimulant_counts.special) {
            Log("player stimulant inventory: regular="+std::to_string(counts.regular)+" potent="+std::to_string(counts.potent)+
                " special="+std::to_string(counts.special)+" (inventory id "+std::to_string(inventory)+", bonding "+std::to_string(bonding)+")");
            g_stimulant_counts=counts;g_stimulant_counts_reported=true;
        }
        const auto observed=horse_races::stimulant::ReadConsumption(horse_races::stimulant::NativeApi{},player);
        const auto& interaction=observed.interaction;
        const auto& previous=g_stimulant_observed;
        if(!g_stimulant_observed_reported || observed.supported!=previous.supported || observed.total!=previous.total ||
            observed.status!=previous.status ||
            interaction.known!=previous.interaction.known || interaction.state!=previous.interaction.state ||
            interaction.item!=previous.interaction.item || interaction.mount!=previous.interaction.mount ||
            interaction.started_ms!=previous.interaction.started_ms || interaction.consumed!=previous.interaction.consumed) {
            Log("player stimulant consumption: "+std::string(!observed.supported?"unsupported game layout; detection disabled":
                observed.total<0?"consumption stat unavailable; detection disabled":
                !interaction.known?"interaction fields unavailable; awaiting confirmed action":"1491.50 read-only")+
                " total="+std::to_string(observed.total)+" state="+std::to_string(interaction.state)+
                " item="+std::to_string(interaction.item)+" target="+std::to_string(interaction.mount)+
                " action_ms="+std::to_string(interaction.started_ms)+" consumed="+std::to_string(interaction.consumed)+
                " snapshot="+horse_races::stimulant::SampleStatusName(observed.status)+
                (!interaction.known?" raw_state="+std::to_string(observed.raw_fields[0])+
                    " raw_f5="+std::to_string(observed.raw_fields[1])+
                    " raw_f6="+std::to_string(observed.raw_fields[2])+
                    " raw_f7="+std::to_string(observed.raw_fields[3]):std::string{}));
            g_stimulant_observed=observed;g_stimulant_observed_reported=true;
        }
        const auto dose=g_stimulant_detection.Update(now,stimulant_scope,observed.total,interaction);
        const auto taken=g_stimulant.Update(now,dose,bonding,g_stimulant_config);
        const auto levels=horse_races::stimulant::Boosted(g_stimulant_config,player_frame.input.speed_level,player_frame.input.acceleration_level);
        if(taken!=horse_races::stimulant::Tier::None) {
            Log(std::string("player stimulant ")+horse_races::stimulant::TierName(taken)+" taken: burst "+std::to_string(g_stimulant.Seconds())+
                " s at bonding "+std::to_string(bonding)+", speed "+std::to_string(player_frame.input.speed_level)+" -> "+std::to_string(levels.speed)+
                " accel "+std::to_string(player_frame.input.acceleration_level)+" -> "+std::to_string(levels.acceleration)+
                " force -> "+std::to_string(horse_races::stimulant::BurstForce(g_auto_high_speed_forces,levels.speed))+
                " (counts regular="+std::to_string(counts.regular)+" potent="+std::to_string(counts.potent)+" special="+std::to_string(counts.special)+") game_ms="+std::to_string(MISC::GET_GAME_TIMER()));
            horse_races::PlayerBurstChanged(true,g_stimulant.Seconds());g_stimulant_reported=true;
        }
        if(g_stimulant.Active(now)) {
            player_frame.input.speed_level=levels.curve_speed;player_frame.input.acceleration_level=levels.curve_acceleration;
            if(levels.curve_speed>=6)
                player_config.high_speed_forces[static_cast<std::size_t>(levels.curve_speed-6)]=horse_races::stimulant::BurstForce(g_auto_high_speed_forces,levels.speed);
        }
    } else g_stimulant_detection.Reset(); // No stale consumption credited after remount.
    if(g_stimulant_reported && !g_stimulant.Active(now)) {
        g_stimulant_reported=false;g_stimulant.Expire();
        Log("player stimulant burst over game_ms="+std::to_string(MISC::GET_GAME_TIMER()));horse_races::PlayerBurstChanged(false,0);
    }
    PollStimulantHudConfig();DrawStimulantHud(now);
    g_player_speed_learning.Observe(player,player_frame,apply,
        !reins_held && !context.circuit_staging,
        horse_settings::PlayerGaitExperimentMode()!=0 || g_gait_experiment.mode!=0,
        context.racing && g_stimulant.Active(now),context.racing?&g_stimulant_observed:nullptr);
    g_player_speed_learning.Report(Log);
    const auto player_applied=g_player_motion.Apply(player_config,player_frame,apply && !context.circuit_staging);
    TickPlayerGaitRelease(mount,player_frame,now,apply && !context.circuit_staging &&
        !reins_held && !player_frame.input.environment.unsafe && !player_frame.input.environment.in_water && player_frame.valid);
    TickPlayerGaitExperiment(player,mount,&player_frame,now,
        apply && !context.present && !context.racing && !context.circuit_staging &&
        !reins_held && !horse_settings::Open());
    // Earned full-gallop burst and immediate near chase share one curve
    // transformation. The larger level bonus wins; native attributes stay put.
    const int host_level=npc_frame.input.speed_level;
    const float host_force=(context.host_level_bonus>0 && npc_frame.valid &&
        host_level>=6 && host_level<=10)
        ? horse_races::host_boost::BonusForce(g_auto_high_speed_forces,host_level,context.host_level_bonus)
        : 0.0F;
    // The bar is a gait, the horse's own: the gallop fully blended in, held
    // for kHoldSeconds. Nothing about the route or a speed enters.
    const float host_bar=host_force>0?horse_races::host_boost::kGaitBar:0.0F;
    const float host_gait=npc_frame.current_blend_valid?npc_frame.current_blend_y:0.0F;
    // Charge uses the real gait; a positive race override can sustain intent
    // across a low native desired reading. A missing read must not be credited
    // as qualified charge time.
    auto host_reason=HostBoostReason(context,npc_frame,mount,apply);
    const bool transient_host_identity=context.host_boost_seconds>0 && context.racing && context.present &&
        !context.eliminated && context.horse!=mount && !npc_frame.identity.Valid() &&
        g_host_boost.CanRetain(context.horse,context.rider,context.race_token);
    if(transient_host_identity && host_reason==horse_races::host_boost::Reason::Identity)
        host_reason=horse_races::host_boost::Reason::FrameInvalid;
    const bool host_eligible=host_reason==horse_races::host_boost::Reason::Ready;
    if(!transient_host_identity)g_host_boost.Bind({npc_frame.identity,context.rider,context.race_token});
    const bool host_earned=g_host_boost.Update(now,host_gait,host_bar,host_reason,
        horse_races::bookmaker_runtime::Active()?horse_races::bookmaking::kBurstChargeSeconds:
            horse_races::host_boost::kHoldSeconds,static_cast<double>(context.host_boost_seconds),
        static_cast<double>(context.host_boost_cooldown_seconds));
    auto host_config=g_motion_config;host_config.rider_gait_cap=g_rider_gait_cap;host_config.rider_force_offset=g_rider_force_offset;
    auto entrant_config=g_motion_config;entrant_config.rider_gait_cap=g_rider_gait_cap;entrant_config.rider_force_offset=g_rider_force_offset;
    const bool host_near=npc_frame.valid && !ENTITY::GET_ENTITY_COLLISION_DISABLED(context.horse);
    if(horse_races::bookmaker_runtime::Active())
        horse_races::bookmaker_runtime::SetMotionBoostState(context.horse,host_earned && !transient_host_identity,host_near);
    else horse_races::SetMotionBoostState(context.horse,host_earned && !transient_host_identity,host_near);
    const bool host_boosted=horse_races::host_boost::ApplyNearBonus(host_config,npc_frame.input,
        apply && context.racing && !context.eliminated && context.horse!=mount,
        npc_frame.valid && npc_frame.identity.Valid(),
        host_near,
        context.catchup_active,context.catchup_fraction,host_earned,context.host_level_bonus);
    const bool host_display=horse_races::host_boost::DisplayActive(
        apply && context.racing && !context.eliminated && context.horse!=mount,
        npc_frame.valid && npc_frame.identity.Valid(),context.real_burst_display_eligible,
        host_boosted,host_near,host_earned,context.host_boost_seconds>0);
    const float host_effective_force=host_boosted && npc_frame.input.speed_level>=6?
        host_config.high_speed_forces[static_cast<std::size_t>(npc_frame.input.speed_level-6)]:0;
    static std::uint64_t boost_diagnostic_token=0;
    static double boost_diagnostic_at=0;
    if(context.racing && host_bar>0 && (boost_diagnostic_token!=context.race_token || now>=boost_diagnostic_at)) {
        boost_diagnostic_token=context.race_token;boost_diagnostic_at=now+15;
        std::string counts;
        for(std::size_t i=1;i<static_cast<std::size_t>(horse_races::host_boost::Reason::Count);++i) {
            const auto reason=static_cast<horse_races::host_boost::Reason>(i);
            if(g_host_boost.Blocked(reason) || g_host_boost.Resets(reason))
                counts+=" "+std::string(horse_races::host_boost::ReasonName(reason))+"="+
                    std::to_string(g_host_boost.Blocked(reason))+"/"+std::to_string(g_host_boost.Resets(reason));
        }
        Log("field host bonus gate reason="+std::string(horse_races::host_boost::ReasonName(g_host_boost.LastReason()))+
            " charged_s="+std::to_string(g_host_boost.HeldFor(now))+" drops="+std::to_string(g_host_boost.Drops())+
            " cumulative_blocked_frames/resets"+counts);
    }
    if(host_display!=g_host_boost_reported) {
        g_host_boost_reported=host_display;
        horse_races::LogHostBoost(host_display,host_gait,host_bar,host_effective_force,g_host_boost.Started());
        if(horse_races::bookmaker_runtime::Active())
            horse_races::bookmaker_runtime::BurstChanged(0,host_display,g_host_boost.Started());
    }
    // The presentation owns its cache and may retry a missing/new blip even
    // when the gate state did not change. Revocation also applies at bar 0.
    horse_races::HostBoostChanged(host_display);
    if(!horse_races::bookmaker_runtime::Active() && host_earned && g_host_boost.Started())
        horse_races::HostBoostStarted();
    const auto npc_applied=g_opponent_motion.Apply(host_config,npc_frame,
        apply && context.racing && !context.eliminated && context.horse!=mount,apply && context.present && context.horse!=mount && !open_circuit);
    // Every jockey, not just the host: a field of seven recorded as two is a
    // recording of the wrong horses. Written only while racing; the 30 s tail
    // after the result belongs to the pair alone.
    const bool record_field=record && context.racing;
    const auto entrants=record_field?(horse_races::bookmaker_runtime::Active()?
        horse_races::bookmaker_runtime::ExtraEntrantTelemetry():horse_races::GetAdditionalEntrantTelemetry()):
        std::vector<horse_races::EntrantTelemetry>{};
    std::vector<horse_motion::EntrantSample> field_samples;
    for(std::size_t i=0;i<g_extra_motion.size();++i) {
        const horse_races::MotionContext e=i<extras.size()?extras[i]:horse_races::MotionContext{};
        auto frame=horse_motion::Observe(e.horse,now,e.racing && !e.eliminated,record_field && e.present,e.present);
        sample_snow_grid(frame);
        frame.input.braking_requested=e.npc_braking && !e.npc_override_intent;
        horse_motion::RespectNativeNpcIntent(frame,e.npc_override_intent);
        frame.input.hold_race_override_force=e.npc_override_intent;
        frame.simulation_lease=simulation_lease;
        const bool racing=apply && e.racing && !e.eliminated && e.horse!=mount;
        frame.physics_activation=g_extra_physics[i].Apply(frame,e.rider,racing,context.race_token);
        auto extra_config=entrant_config;
        const bool bookmaker=horse_races::bookmaker_runtime::Active();
        bool earned=false;
        if(bookmaker && i<extras.size()){
            const int level=frame.input.speed_level;
            const float bonus=frame.valid?
                horse_races::host_boost::BonusForce(entrant_config.high_speed_forces,level,e.host_level_bonus):0.0F;
            const float bar=bonus>0?horse_races::host_boost::kGaitBar:0.0F;
            const auto reason=HostBoostReason(e,frame,mount,apply);
            g_bookmaker_bursts[i].Bind({frame.identity,e.rider,e.race_token});
            earned=g_bookmaker_bursts[i].Update(now,frame.current_blend_valid?frame.current_blend_y:0,
                bar,reason,horse_races::bookmaking::kBurstChargeSeconds,e.host_boost_seconds);
        }else g_bookmaker_bursts[i].Reset();
        const bool physical_awake=frame.valid && !ENTITY::GET_ENTITY_COLLISION_DISABLED(e.horse);
        // PhysicsActivation may have woken a mount after race tick published
        // its sleeping lease. Withdraw it before ANY near curve force.
        if(bookmaker)horse_races::bookmaker_runtime::SetMotionBoostState(e.horse,earned,physical_awake);
        else horse_races::SetMotionBoostState(e.horse,earned,physical_awake);
        const bool boosted=horse_races::host_boost::ApplyNearBonus(extra_config,frame.input,
            racing,frame.valid && frame.identity.Valid(),physical_awake,
            e.catchup_active,e.catchup_fraction,earned,e.host_level_bonus);
        const bool display=horse_races::host_boost::DisplayActive(racing,
            frame.valid && frame.identity.Valid(),e.real_burst_display_eligible,
            boosted,physical_awake,earned,e.host_boost_seconds>0);
        if(bookmaker && display!=g_bookmaker_burst_reported[i])
            horse_races::bookmaker_runtime::BurstChanged(i+1,display,g_bookmaker_bursts[i].Started());
        g_bookmaker_burst_reported[i]=display;
        const auto applied=g_extra_motion[i].Apply(extra_config,frame,racing,apply && e.present && e.horse!=mount && !open_circuit);
        if(record_field && e.present) {
            horse_motion::EntrantSample sample{{},frame,applied,g_extra_motion[i].curve};
            sample.boost_active=g_bookmaker_burst_reported[i];
            sample.earned_boost_active=earned;sample.near_bonus_applied=boosted;sample.catchup_engaged=e.catchup_engaged;
            sample.catchup_active=e.catchup_active;sample.catchup_gap_valid=e.catchup_gap_valid;
            sample.catchup_fraction=e.catchup_fraction;sample.catchup_gap_m=e.catchup_gap_m;
            // Joined by horse handle. Both lists come from the same roster in
            // the same order, but a handle cannot be misaligned.
            for(const auto& t:entrants)if(t.horse==e.horse){sample.entrant=t;break;}
            field_samples.push_back(std::move(sample));
        }
    }
    if(!mount)g_auto_mount=0;
    if(apply) {
        if(context.racing && npc_frame.valid && npc_frame.attributes.current.Valid()) {
            if(context.horse!=g_race_curve_mount || host_level!=g_race_curve_speed_level ||
                npc_frame.input.acceleration_level!=g_race_curve_accel_level) {
                g_race_curve_mount=context.horse;g_race_curve_speed_level=host_level;
                g_race_curve_accel_level=npc_frame.input.acceleration_level;
                Log("AutoCurve opponent actual native ranks: displayed speed="+std::to_string(g_race_curve_speed_level)+
                    " acceleration="+std::to_string(g_race_curve_accel_level));
            }
        } else if(!context.present)ResetRaceOpponentCurve();
    }
    if(record) {
        const horse_motion::SampleTiming timing{MISC::GET_FRAME_COUNT(),MISC::GET_GAME_TIMER(),MISC::GET_FRAME_TIME(),
            open_circuit,static_cast<int>(exemption_refusal)};
        // Recorded whether or not a bonus is configured: "this race has none" is
        // itself the answer to half the questions asked of these files.
        const horse_motion::HostBoostSample boost{context.host_level_bonus>0,host_gait>=host_bar && host_bar>0,
            host_eligible,host_display,host_level,host_gait,host_bar,host_effective_force,g_host_boost.HeldFor(now),g_host_boost.Drops(),
            host_earned,host_boosted,context.catchup_engaged};
        // The sampler has already observed this frame above. Read its state
        // only at the recorder's existing 10 Hz boundary; do not make new
        // native calls or mistake a retained last-window decision for a fresh one.
        horse_motion::PlayerLearningTrace learning_trace;
        const auto& learner=g_player_speed_learning;
        const auto& sampler=learner.sampler;
        const auto& learned=sampler.context;
        learning_trace.observer_frame=learner.frame;
        learning_trace.horse=learner.binding.mount.handle;
        learning_trace.current=learner.frame==timing.frame && learner.binding.Valid() && player_frame.valid &&
            learner.binding.player==player && learner.binding.mount.handle==player_frame.identity.horse;
        if(learning_trace.current){
            learning_trace.sampler_frames=sampler.frames;learning_trace.eligible_frames=sampler.eligible_frames;
            learning_trace.accepted=sampler.accepted;learning_trace.rejected_windows=sampler.rejected_windows;
            learning_trace.rejection=sampler.rejection;
            learning_trace.context_resource=learned.profile.context.resource;
            learning_trace.context_slope=static_cast<unsigned>(learned.profile.context.slope);
            learning_trace.context_material=learned.profile.context.material;
            learning_trace.slope_slice_pending=learned.slope_slice_pending;
            learning_trace.sampling=learned.sampling;learning_trace.diagnostics=learned.diagnostics;
            learning_trace.reference=learned.reference.Observe();
            learning_trace.reference_pending_seconds=learned.reference.PendingSeconds();
            learning_trace.has_previous_reference_window=learned.reference.HasPreviousWindow();
            if(std::isfinite(learned.diagnostics.last_window_clock) &&
                learned.gait_clock>=learned.diagnostics.last_window_clock)
                learning_trace.last_window_age=learned.gait_clock-learned.diagnostics.last_window_clock;
            if(std::isfinite(learned.diagnostics.last_pair_clock) &&
                learned.gait_clock>=learned.diagnostics.last_pair_clock)
                learning_trace.last_pair_age=learned.gait_clock-learned.diagnostics.last_pair_clock;
            learning_trace.candidate_active=learned.candidate_active;
            if(learned.candidate_active && learned.gait_clock>=learned.qualified_span_start){
                learning_trace.candidate_span=learned.gait_clock-learned.qualified_span_start;
                learning_trace.candidate_min_speed=learned.qualified_min_speed;
                learning_trace.candidate_max_speed=learned.qualified_max_speed;
            }
        }
        g_motion_recorder.Write(context,now,apply,player_frame,player_applied,npc_frame,npc_applied,
            horse_races::bookmaker_runtime::Active()?horse_races::bookmaker_runtime::MainNavigationTelemetry():
                horse_races::GetNavigationTelemetry(),g_player_motion.curve,g_opponent_motion.curve,g_motion_config,timing,boost,
            field_samples,context.racing && g_stimulant.Active(now),&learning_trace);
    }
}
