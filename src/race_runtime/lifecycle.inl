// Private lifecycle implementation; included by race_system.cpp.
void ResetRaceState()
{
    g_camp_horse_dirt.reset();g_camp_horse_dirt_model=0;
    g_boundary_warning.FadeOut(CircuitNow());
    TrialEnd();
    g_host_physical_history.Clear();
    g_host_physical_incident_emitted=false;
    g_main_rival_out=false;g_extra_error.clear();g_extra_pool_relief={};cues::ForgetRearedHorses();
    g_fixed_progress_along=0;g_fixed_progress_at={};
    g_fixed_stall_along=-1000;g_fixed_stall_repeats=0;
    g_competitive_follower.Reset();g_host_navigation={};g_host_avoidance={};g_pace_plan={};g_racing_line={};g_pace_controller={};g_pace_command={};g_continuous_attempted=false;g_host_burst_shown=g_host_burst_earned=false;g_follower_retries={};
    g_host_far_runner.Reset();
    g_start_lines={};
    g_terrain_profile={};g_host_terrain_caution=-1;
    g_test_host_replace=false;g_test_host_position={};g_test_host_heading=0;
    RestoreFixedAvoidance();
    g_opponent_damage = {};
    g_mary={}; g_mary_talk_pressed=false;
    g_mary_horse_health=g_mary_player_horse_health=0; g_mary_fall_at={};
    RestoreOpponentRanks("state reset");
    g_horse_plan_ready=false; g_planned_natural={}; g_planned_target={}; g_race_player=0;
    g_opponent_horse_claimed=false;
    g_rider_appearance.Reset();
    g_fixed_selected_horse_name.clear();
    g_fixed_greeting_focused = false;
    g_fixed_greeting_next = {};
    g_fixed_result_waiting = false;
    g_fixed_samples_ready = false;
    g_fixed_previous_player = g_fixed_previous_rival = {};
    g_fixed_abandon_since = g_fixed_stall_since = g_fixed_last_task = {};
    g_fixed_route_retries = 0;
    g_course_player.Reset();g_course_rival.Reset();g_course_target=0;g_course_recovery_progress=0;
    g_navigation_telemetry={};
    g_navigation_pace.Reset();
    g_fixed_next_spawn_log = {};
    g_fixed_spawn_hidden_since={};g_fixed_spawn_visibility_wait=false;g_fixed_spawn_site_wait=false;g_fixed_spawn_wait_since={};g_fixed_spawn_occupied_since={};
    g_host_recovery_retry={};g_host_recovering=false;
    if (g_race_shocking_event != 0) {
        EVENT::REMOVE_SHOCKING_EVENT(g_race_shocking_event);
        g_race_shocking_event = 0;
    }
    g_player_mount = 0;
    g_player_progress_max = 0;
    g_opponent_progress_max = 0;
    g_reissued_route_task = false;
    g_route_task_reissued_at = Clock::time_point{};
    g_offer_phase = 0;
    g_offer_phase_started = Clock::time_point{};
    g_departure_task_started = false;
    g_initial_player_route_deviation = 0.0F;
    g_follow_approach_started = false;
    g_follow_approach_started_at = Clock::time_point{};
    g_result_phase = ResultPhase::Arrival;
    g_player_won = false;
    g_result_playlists_requested = false;
    g_result_started_at = Clock::time_point{};
    g_result_next_log = Clock::time_point{};
    g_result_scripted_line_attempted = false;
    g_result_scripted_line_at = Clock::time_point{};
    g_result_interaction_registered = false;
    g_result_interaction_positive = false;
    g_result_response_started = false;
    g_reward_granted = false;
    g_victory_toast_queued = false;
    g_race_started_at = Clock::time_point{};
    g_next_banter_at = Clock::time_point{};
    g_lead_candidate_since = Clock::time_point{};
    g_stable_leader = 0;
    g_lead_candidate = 0;
    g_ahead_taunt_index = 0;
    g_opponent_pass_index = 0;
    g_player_pass_index = 0;
    g_waiting_line_index = 0;
}

void CleanupRace()
{
    TrialEnd(); // Return the camera/player before retiring the viewed NPC.
    horse_ledger::runtime::CancelRace();
    StopNavigation();
    ReleaseMissionFlag();
    StopRaceAudio(false);
    DeleteSpawnedEntities();
    ReleaseModels();
    ResetRaceState();
}

void ReleaseRoute()
{
    g_camp_horse_dirt.reset();g_camp_horse_dirt_model=0;
    g_boundary_warning.FadeOut(CircuitNow());
    g_terrain_profile={};g_host_terrain_caution=-1;
    g_competitive_follower.Reset();g_host_navigation={};g_pace_plan={};g_racing_line={};g_pace_controller={};g_pace_command={};g_continuous_attempted=false;g_host_burst_shown=g_host_burst_earned=false;g_follower_retries={};
    g_host_far_runner.Reset();
    g_lap_guide.Release();g_guidance_early=false;g_accept_line_queued=false;
    g_lap_guide_current=-1;
    // Explicit cancellation can release the scene before a contact is
    // confirmed. Retire its query handles, without inventing an obstacle.
    auto cancel_impact=[](collision_learner::Learner& learner,const char* who) {
        if(!learner.Busy())return;
        Log(std::string("collision probe cancelled on route release: rider=")+who+
            " event="+std::to_string(learner.drops)+" map="+learner.event_map_key);
        learner.Cancel();
    };
    cancel_impact(g_host_learner,"host");cancel_impact(g_player_learner,"player");
    for(auto& entry:g_entrant_learners)cancel_impact(entry.second,"entrant");
    RetireObstacleSaves("route released");g_obstacles={};g_obstacle_profiles={};g_online_surveys={};
    g_obstacles_saved_revisions={};g_obstacles_line_revision=0;
    g_obstacles_writable.fill(true);g_obstacles_backup_pending.fill(false);
    g_host_learner.Reset();g_player_learner.Reset();g_entrant_learners.clear();g_obstacles_learned=false;
    g_host_passage.Reset();g_player_passage.Reset();g_entrant_passages.clear();
    g_line_pins.clear();g_line_pins_file={};g_line_pins_stamp={};g_line_pins_required_refused=g_recorded_reference_required=false;
    g_laps.Reset();
    g_corridor_ground.Reset();g_racing_line_view.Reset();g_corridor_pending.reset();
    g_course_gps.Release();g_course={};g_course_player.Reset();g_course_rival.Reset();
    g_fixed_progress_along=0;g_fixed_progress_at={};
    if (g_finish_volume != 0) {
        VOLUME::DELETE_VOLUME(g_finish_volume);
        g_finish_volume = 0;
    }
    if (g_route_requested) {
        TASK::REMOVE_WAYPOINT_RECORDING(g_route_name.c_str());
        if (g_shortcut_route_name != g_route_name) {
            TASK::REMOVE_WAYPOINT_RECORDING(g_shortcut_route_name.c_str());
        }
    }
    g_route_requested = false;
    g_route_points = 0;
    g_navigation_route_points = 0;
    g_route_start = {};
    g_route_finish = {};
    g_last_route_request = {};
    g_route_delay_reported = false;
}

void AwaitNextEncounter()
{
    if (CurrentRules().Hosted()) {
        FixedWaitingFlag(g_variant)=true;
        ReleaseRoute();
        g_variant = g_fixed_return_variant;
        g_rival = &RivalDefinition(g_variant->archetype);
        g_route_name = g_variant->full_route;
        g_shortcut_route_name = g_variant->shortcut_route;
        g_next_site_scan = {};
        SetState(State::Scanning);
        AddStartBlip();
        return;
    }
    g_site_waiting_for_leave[static_cast<std::size_t>(g_variant->id)] = true;
    ReleaseRoute();
    g_next_site_scan = {};
    SetState(State::Scanning);
    AddStartBlip();
}

void ReturnToReady()
{
    CleanupRace();
    AwaitNextEncounter();
}

void FlushFixedCredit()
{
    if (g_fixed_wager.pending_delta == 0) return;
    if (Clock::now() < g_cash_retry || NETWORK::NETWORK_IS_SESSION_ACTIVE()) return;
    const int amount = g_fixed_wager.pending_delta;
    const auto decision = horse_ledger::runtime::BeforePayment(amount, g_cash_timeline);
    if (decision == horse_ledger::runtime::PaymentDecision::Defer) return;
    if (decision == horse_ledger::runtime::PaymentDecision::Discard) {
        g_fixed_wager.PaymentSucceeded(); // Drop obsolete queued cash without touching the wallet.
        Log("race settlement discarded after timeline change cents=" + std::to_string(amount));
        return;
    }
    g_cash_retry = Clock::now() + std::chrono::seconds(1);
    horse_diagnostics::Stage("cash: balance transaction");
    Log("race settlement begin cents="+std::to_string(amount));
    const bool paid=amount>0?MONEY::_MONEY_INCREMENT_CASH_BALANCE(amount,MISC::GET_HASH_KEY("ADD_REASON_DEFAULT")):
        MONEY::_MONEY_DECREMENT_CASH_BALANCE(-amount);
    if (paid) {
        g_fixed_wager.PaymentSucceeded();
        horse_ledger::runtime::AfterPayment(amount);
        Log("race balance committed cents="+std::to_string(amount)+"; pending cleared before receipt");
        horse_diagnostics::Stage("cash: native right-side receipt");
        const int feed=transaction_feed::Cash(amount);
        Log("race net settlement cents="+std::to_string(amount)+" right_feed="+std::to_string(feed));
    } else {
        horse_ledger::runtime::PaymentFailed();
        Log("race net settlement failed; retained for retry, cents=" + std::to_string(amount));
    }
}

void SettleFixedWager(wager::Outcome outcome)
{
    if (!CurrentRules().wagered) return;
    const bool new_settlement=g_fixed_wager.open;
    g_fixed_wager.Settle(outcome);
    if (new_settlement && g_fixed_wager.pending_delta != 0) {
        g_cash_timeline = g_race_cash_timeline;
        horse_ledger::runtime::PaymentQueued(g_cash_timeline);
    }
    if(new_settlement && outcome==wager::Outcome::Win && !g_fixed_practice && !GetAutoSurveyState().running) {
        if(const auto* station=StationOf(g_variant);station && station->route)
            progression::runtime::Win(station->key,false,
                std::string_view(FixedProfile().key)!=station->route->host);
    }
    if(new_settlement)g_cash_retry={};
    FlushFixedCredit();
}
