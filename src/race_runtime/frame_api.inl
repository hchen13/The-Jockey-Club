// Private frame api implementation; included by race_system.cpp.
struct RaceHelpFrame {
    ~RaceHelpFrame(){
        if(g_state==State::Disabled || bookmaker_runtime::Active())race_help::Reset();
        else race_help::Tick();
    }
};
void Tick(const ControlInput& input)
{
    // Drain after the boundary frame, so a pending warning cannot be posted
    // just before this frame observes that the player returned to the course.
    // Only our queued messages are discarded for spectator mode.
    RaceHelpFrame help_frame;
    // Destruction runs on every early return and after result processing.
    // The owned warning therefore completes its fade outside Racing too.
    BoundaryWarningFrame boundary_frame;
    {
        horse_diagnostics::Scope stage_cost("race: obstacle query retirement");
        obstacle_survey::QueryQueue::BeginFrame();
    }
    horse_motion::speed_learning::runtime::Tick();
    const Ped player_for_mount=PLAYER::PLAYER_PED_ID();
    if(player_for_mount && ENTITY::DOES_ENTITY_EXIST(player_for_mount) && PED::IS_PED_ON_MOUNT(player_for_mount))
        g_circuit_scene.RememberPlayerMount(PED::GET_MOUNT(player_for_mount));
    g_circuit_start.Background(); // Drain cancelled asynchronous runway queries without leaking handles.
    g_circuit_scene.Background(CircuitNow());
    TickNativeRaceMusic(); // Includes spectator races and pending invitation preloads.
    TickMusicAudition();
    if(g_trial.active && g_state!=State::Racing)TrialEnd();
    // Observe before any fall can eliminate a rider, and drain the same
    // horse's pending query afterwards, even when the player is dismounted.
    const bool observe_collisions=g_state==State::Racing && !HUD::IS_PAUSE_MENU_ACTIVE();
    bool save_collision_evidence=false;
    LearnFromCollision(g_host_learner,g_opponent_horse,g_opponent_rider,"host",g_laps.opponent,
        observe_collisions && !g_main_rival_out && !g_host_far_runner.active && !g_trial.remounting,&save_collision_evidence);
    LearnFromCollision(g_player_learner,g_player_mount,player_for_mount,"player",g_laps.player,
        observe_collisions && !g_trial.active,&save_collision_evidence);
    LearnFromPassage(g_host_passage,g_opponent_horse,"host",g_laps.opponent,
        observe_collisions && !g_main_rival_out && !g_host_far_runner.active && !g_trial.remounting && !g_host_learner.Busy());
    LearnFromPassage(g_player_passage,g_player_mount,"player",g_laps.player,
        observe_collisions && !g_trial.active && !g_player_learner.Busy());
    for(const auto& e:g_extra_rivals) {
        const bool observe=observe_collisions && !e.out && !e.retiring && !e.far_runner.active;
        auto it=g_entrant_learners.find(e.horse.horse);
        if(observe || it!=g_entrant_learners.end())LearnFromCollision(g_entrant_learners[e.horse.horse],
            e.horse.horse,e.rider.horse,"entrant",e.progress,observe,&save_collision_evidence);
        const auto passage=g_entrant_passages.find(e.horse.horse);
        if(observe || passage!=g_entrant_passages.end())LearnFromPassage(g_entrant_passages[e.horse.horse],
            e.horse.horse,"entrant",e.progress,observe && !g_entrant_learners[e.horse.horse].Busy());
    }
    // All same-frame observations reach the maps/planners immediately, with
    // one persistence pass after the rider loop rather than one per rider.
    if(save_collision_evidence)SaveObstacles("same-frame static observations");
    firearm_guard::Tick(); // All lifecycles, including retired actors and outfit galleries.
    if (g_restore_requested) RestoreOpponentRanks("frame retry");
    TickRetiredOpponents(); // Also runs when races/debug spawning are disabled.
    TickExtraRetirement();
    TickRaceHorseInspection(); // Includes result/retirement and drains asynchronous card cleanup in every state.
    for(const auto& e:g_extra_rivals)if(!e.retiring && ExtraHorseClaimed(e)) {
        RestoreExtraRanks();AbortRace("additional rival horse taken by player",false);return;
    }
    if (g_owns_mission_flag && !g_rank_lease.pending && g_state!=State::Racing && g_state!=State::PreparingCircuit && g_restore_requested==false)
        ReleaseMissionFlag();
    // Invitations/results are covered too: a player may take the host's horse
    // without ever accepting a race. Do this before range-based host cleanup.
    if (g_horse_plan_ready && g_opponent_horse && PlayerHasTakenOpponentHorse()) {
        firearm_guard::Release(g_opponent_horse);
        Log("rival horse claimed by player handle="+std::to_string(g_opponent_horse)+"; protect from race deletion");
        RestoreOpponentRanks("rival horse taken");AbortRace("rival horse taken by player",false);return;
    }
    if (g_rank_lease.pending || (g_state==State::Racing && g_horse_plan_ready)) {
        const Ped player=PLAYER::PLAYER_PED_ID();
        if (NETWORK::NETWORK_IS_SESSION_ACTIVE() || (g_race_player && player!=g_race_player)) {
            RestoreOpponentRanks("player/session change or rival horse taken");
            AbortRace("player/session changed or rival horse taken",false); return;
        }
        if (g_state!=State::Racing && (!player || ENTITY::IS_ENTITY_DEAD(player))) {
            AbortRace("player unavailable during horse preview",false);return;
        }
    }
    if (g_state == State::Disabled) {
        return;
    }
    FlushFixedCredit();
    TickObstacleSurvey();
    if (g_state==State::Scanning || g_state==State::Ready) RefreshHorseCatalog();
    if (Clock::now() >= g_language_poll) {
        g_language_poll = Clock::now() + std::chrono::seconds(1);
        if (i18n::Refresh()) {
            for (const auto& station : g_stations)
                if (station.route && station.start_blip && MAP::DOES_BLIP_EXIST(station.start_blip))
                    SetBlipDisplayName(station.start_blip, FixedRouteTitle(&station.variant).c_str());
            for (const auto& variant : kOriginalRaceVariants) {
                const Blip blip = g_start_blips[static_cast<std::size_t>(variant.id)];
                if (blip && MAP::DOES_BLIP_EXIST(blip))
                    SetBlipDisplayName(blip, i18n::Text("Horse race", "遭遇赛马"));
            }
            if (g_finish_blip && MAP::DOES_BLIP_EXIST(g_finish_blip)) {
                const std::string label = CurrentRules().Hosted() ? std::string(i18n::Text("Finish: ", "终点：")) + FixedFinishName() : i18n::Text("Horse race finish", "赛马终点");
                SetBlipDisplayName(g_finish_blip, label.c_str());
                UpdateCircuitLapMarker();
            }
            if (CurrentRules().Hosted() && g_opponent_blip && MAP::DOES_BLIP_EXIST(g_opponent_blip))
                SetBlipDisplayName(g_opponent_blip, FixedHostName());
            RefreshFixedPromptLabels();
            Log("UI language changed=" + std::to_string(i18n::language));
        }
    }
    TickFixedIconSetting();
    TickRaceMapMarkers(); // Map visibility must not wait for opponent/session cleanup.
    if(CurrentRules().Hosted() && !ProgressionBusy()) {
        if(const auto* station=StationOf(g_variant);station && !progression::runtime::Open(station->key)) {
            AbortRace("race progression changed: selected station is locked",false);return;
        }
    }
    {
        horse_diagnostics::Scope stage_cost("race: station route files");
        for (auto& station : g_stations) { PollStationRoute(station); ApplyPendingStationRoute(station); }
    }
    TickFixedDirectionRenewals();
    PollTestHostOverride();
    PollOpponentDifficulty();
    if (CurrentRules().Hosted() && (NETWORK::NETWORK_IS_SESSION_ACTIVE() ||
        (!g_owns_mission_flag && MISC::GET_MISSION_FLAG()))) {
        AbortRace("fixed race interrupted by another mission/session", false); return;
    }
    const bool f10_down = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    const bool page_left=(GetAsyncKeyState(VK_LEFT)&0x8000)!=0;
    const bool page_right=(GetAsyncKeyState(VK_RIGHT)&0x8000)!=0;
    DWORD foreground_process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
    const bool game_input = foreground_process == GetCurrentProcessId() && !HUD::IS_PAUSE_MENU_ACTIVE();
    const bool end_activity = input.end_activity || (game_input && f10_down && !g_f10_was_down);
    g_f10_was_down = f10_down;
    const int page_direction=game_input && !input.menu_open?((page_right && !g_page_right_was_down?1:0)-(page_left && !g_page_left_was_down?1:0)):0;
    g_page_left_was_down=page_left;g_page_right_was_down=page_right;

    if (HandleShowcaseInput(input.toggle_showcase, end_activity,page_direction)) return;

    if (end_activity && (CurrentRules().Hosted() || (g_state != State::Scanning && g_state != State::Ready))) {
        AbortRace("manual abort", true);
        for (auto& station : g_stations) ApplyPendingStationRoute(station);
        return;
    }

    // A bookmaker exhibition has its own twenty-actor scene. Keep the usual
    // one-on-one station scanner quiet while Clay owns the course; a nearby
    // Karen offer would otherwise race-spawn into this closed Valentine lane.
    if(bookmaker_runtime::Active()){
        if(g_state!=State::Disabled && g_state!=State::Scanning && g_state!=State::Ready)
            AbortRace("bookmaker scene owns the course",false);
        return;
    }

    PrepareResultSceneAssets();

    // The corridor highlight is drawn by TickRacing only: the maintainer wants
    // it during the race, not while standing at the station.
    switch (g_state) {
    case State::Disabled:
        break;
    case State::Scanning:
        TickScanning();
        break;
    case State::LoadingRoute:
        TickLoadingRoute();
        break;
    case State::Ready:
        TickReady();
        break;
    case State::LoadingModels:
        TickLoadingModels();
        break;
    case State::PreparingFixedOutfit:
        TickPreparingFixedOutfit();
        break;
    case State::Approaching:
        TickApproaching();
        break;
    case State::Offering:
        TickOffering();
        break;
    case State::Accepted:
        TickAccepted();
        break;
    case State::Declined:
        TickDeclined();
        break;
    case State::Racing:
        g_circuit_start.DrawGo();
        g_circuit_start.TickAfterGo();
        if(g_starter.fired && StateSeconds()>4)g_starter.Retire();
        TickRacing();
        break;
    case State::PreparingCircuit:
        TickPreparingCircuit();
        break;
    case State::Result:
        TickResult();
        break;
    case State::Departing:
        TickDeparting();
        break;
    }
}

OpponentCurveTarget GetOpponentCurveTarget()
{
    OpponentCurveTarget target{};
    if (g_state == State::Racing && g_opponent_horse != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_horse) &&
        !ENTITY::IS_ENTITY_DEAD(g_opponent_horse)) {
        target.horse = g_opponent_horse;
        // The physical controller uses final native ranks exactly like the
        // player. A planned difficulty/price is not a movement attribute.
        const horse_attributes::Ranks actual{
            ATTRIBUTE::GET_ATTRIBUTE_RANK(target.horse,5), ATTRIBUTE::GET_ATTRIBUTE_RANK(target.horse,6)};
        target.speed_level = actual.Valid()?actual.speed+1:0;
        target.acceleration_level = actual.Valid()?actual.acceleration+1:0;
        target.active = actual.Valid();
    }
    return target;
}
