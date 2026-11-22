// Private configuration api implementation; included by race_system.cpp.
NativeRaceRewardStatus NativeRaceRewards()
{
    // Never display a previous save's values or convert read failure into No.
    if(DLC::GET_IS_LOADING_SCREEN_ACTIVE())return {};
    const auto display=[](native_reward::Status status) {
        return status==native_reward::Status::Awarded?NativeRaceRewardState::Yes:
            status==native_reward::Status::NotAwarded?NativeRaceRewardState::No:NativeRaceRewardState::Unknown;
    };
    return {display(native_progress::g_first_probe()),display(native_progress::g_probe())};
}
NativeEncounterConditions NativeEncounterState(){return native_candidates::Conditions();}

bool RandomRaceWorldMapMarkers(){return g_show_random_race_world_map_markers;}
bool SetRandomRaceWorldMapMarkers(bool enabled)
{
    if(!config::SetRaw(g_ini_path,"RacePrototype","ShowRandomRaceWorldMapMarkers",enabled))return false;
    if(enabled==g_show_random_race_world_map_markers)return true;
    g_show_random_race_world_map_markers=enabled;
    // No fixed/rival/finish/native blip changes, selection or spawn side effects.
    AddEncounterStartBlips();
    Log(std::string("encounter world-map markers=")+(enabled?"on":"off")+"; native eligible candidates or MOD live sites; companion dots, no extra radar launcher");
    return true;
}

void Initialize(const std::string& directory)
{
    native_candidates::Reset();
    native_progress::g_state={}; // A loaded save must never inherit another save's awarded cache.
    g_encounter_marker_retry_at={};
    g_boundary_warning.Reset(); // ScriptHook re-enters after load; never retain owned screen FX.
    TrialEnd();trial_force::probe.ResetSession();g_trial.armed=false; // ScriptHook re-enters after loading a save.
    // Music has the same session boundary as race actors and route state.
    StopRaceMusic(false,"script_initialize");
    i18n::Refresh();
    g_language_poll = {};
    // ScriptHook runs this again whenever a save is loaded, and the globals
    // survive: a marker handle from the previous session is not 0, so nothing
    // redraws it, and the texture counts as requested while the launchers it
    // waited for are gone. Forget the old session's markers first - one that
    // outlived the load is removed - and this session draws its own below.
    // (Seen 2026-09-14: no station marker on any chapter 2 save until a race
    // had been run, because a race is the only other thing that drops them.)
    {
        int stale=0;
        for(const auto& station:g_stations)if(station.start_blip)++stale;
        for(const Blip blip:g_start_blips)if(blip)++stale;
        if(g_opponent_blip)++stale;
        if(g_finish_blip)++stale;
        RemoveStartBlips();
        RemoveBlip(g_opponent_blip);RemoveBlip(g_finish_blip);
        if(stale)Log("script session restarted: "+std::to_string(stale)+" marker handles of the previous session dropped and redrawn");
    }
    showcase::Initialize(directory);
    g_directory = directory;
    g_paths=runtime_paths::Resolve(directory);
    horse_races::log_sink::Shared().Open(g_paths.Log("TheJockeyClub.log").string());
    horse_motion::speed_learning::runtime::Initialize(directory,[](const std::string& entry){Log(entry);});
    // Distinct riders can fail in the same frame. Preserve each event instead
    // of letting the normal two-lines/second folding discard its evidence.
    native_follower::diagnostic_log=[](const std::string& entry){
        horse_races::log_sink::Shared().Write("[Race] "+entry,true);
    };
    g_fixed_previous_horses.fill(0);
    g_catalog_ready=false; g_catalog_player=0; g_horse_catalog.clear();
    g_model_retry_after.clear();
    g_attribute_backup.Load(directory);
    Log("horse attribute " + g_attribute_backup.status);
    g_ini_path = g_paths.MainConfig().string();
    g_price_reference_metres=0;
    g_corridor_highlight=std::clamp(static_cast<int>(GetPrivateProfileIntA("UI","CorridorHighlight",0,g_ini_path.c_str())),0,1);
    g_racing_line_highlight=std::clamp(static_cast<int>(GetPrivateProfileIntA("UI","RacingLineHighlight",0,g_ini_path.c_str())),0,1);
    g_ground_line_pitch_sign=GetPrivateProfileIntA("UI","GroundLinePitchSign",1,g_ini_path.c_str())<0?-1.0F:1.0F;
    g_corridor_key.clear();g_corridor_override.reset();g_corridor_pending.reset();g_corridor_ground.Reset();g_racing_line_view.Reset();
    g_test_host_override.clear();g_test_listener=dialogue_test::Listener::Auto;g_test_host_poll={};g_fixed_meetings.fill({});
    for (auto& station : g_stations) {
        station.config_path = g_paths.Config(station.routes_file).string();
        station.poll = {}; station.observed.reset(); station.parsed.reset(); station.pending.reset();
        station.issue.clear(); station.waiting_for_leave = false;
        station.route.reset();
    }
    g_rules={};
    g_fixed_directed_routes.fill(std::nullopt);g_fixed_direction_renew.fill(false);
    g_enabled = LoadFloat("Enabled", "1", 0.0F, 1.0F, 1.0F) >= 0.5F;
    g_far_catchup_enabled=false;g_native_far_speed=true;g_native_speed_table=false;g_native_move_rate_trial=false;g_native_vector_trial=false;
    g_host_native_far_probe.Reset();g_extra_native_far_probes.clear();
    g_show_start_markers = LoadFloat("ShowStartMarkers", "1", 0.0F, 1.0F, 1.0F) >= 0.5F;
    g_show_random_race_world_map_markers = config::ReadInt(g_ini_path,"RacePrototype",
        "ShowRandomRaceWorldMapMarkers",0,0,1)!=0;
    g_showcase_enabled = LoadFloat("ShowcaseEnabled", "1", 0.0F, 1.0F, 1.0F) >= 0.5F;
    g_fixed_race_enabled = LoadFloat("FixedStationRaceEnabled", "1", 0.0F, 1.0F, 1.0F) >= 0.5F;
    g_fixed_online_icon_enabled = LoadFloat("FixedRaceOnlineIcon", "1", 0.0F, 1.0F, 1.0F) >= 0.5F;
    g_fixed_icon_settings_poll = {};
    g_page_left_was_down=(GetAsyncKeyState(VK_LEFT)&0x8000)!=0;
    g_page_right_was_down=(GetAsyncKeyState(VK_RIGHT)&0x8000)!=0;
    const int variant_id = static_cast<int>(LoadFloat("OriginalVariant", "2", 0.0F, 10.0F, 2.0F));
    g_variant = &kOriginalRaceVariants[static_cast<std::size_t>(variant_id)];
    g_rival = &RivalDefinition(g_variant->archetype);
    g_route_name = g_variant->full_route;
    g_shortcut_route_name = g_variant->shortcut_route;
    g_activation_min_radius = LoadFloat(
        "ActivationMinRadius", "110.0", 50.0F, 200.0F, kOriginalActivationMinRadius);
    g_activation_max_radius = LoadFloat(
        "ActivationMaxRadius", "215.0", 100.0F, 300.0F, kOriginalActivationMaxRadius);
    g_offer_abandon_radius = LoadFloat(
        "EncounterCleanupRadius", "235.0", 150.0F, 400.0F, kOriginalEncounterCleanupRadius);
    g_release_radius = LoadFloat("NaturalReleaseRadius", "120.0", 60.0F, 500.0F, 120.0F);
    g_finish_radius = LoadFloat("FinishRadius", "18.0", 5.0F, 100.0F, 18.0F);
    g_max_race_seconds = LoadFloat("MaxRaceSeconds", "1200", 30.0F, 7200.0F, 1200.0F);
    RollEncounterPoints("startup");
    g_state = g_enabled ? State::Scanning : State::Disabled;
    g_state_entered = Clock::now();
    InvalidateRules("initialize");
    if (g_enabled) for (auto& station : g_stations) { PollStationRoute(station, true); ApplyPendingStationRoute(station); }
    Log(std::string("prototype initialized enabled=") + (g_enabled ? "true" : "false") +
        " original_variant=" + std::to_string(g_variant->id) + " route=" + g_route_name +
        " shortcut=" + g_shortcut_route_name + " auto-offer=true F11=outfit-display/activity-controls F10=abort");
    Log(std::string("race markers: master=") + (g_show_start_markers ? "on" : "off") +
        " random_world_map=" + (g_show_random_race_world_map_markers ? "on" : "off"));
    Log(std::string("fixed station race=") + (g_fixed_race_enabled ? "on" : "off"));
    std::string files;
    for (const auto& station : g_stations) files += std::string(files.empty() ? "" : ", ") + station.key + "=" + station.routes_file;
    Log("route config: startup read + one-second hot polling; stations: " + files);
    Log(std::string("fixed race icon: guarded_online=") + (g_fixed_online_icon_enabled ? "on" : "off") +
        " candidate=BLIP_MP_GAME_RACE_HORSE dict=blips_mp fallback=SP horseshoe; INI reloads every second");
    if (g_enabled) {
        AddStartBlip();
    }
}

void PollTestHostOverride() {
    if(Clock::now()<g_test_host_poll)return;g_test_host_poll=Clock::now()+std::chrono::seconds(1);
    char listener_value[24]{};
    GetPrivateProfileStringA("RacePrototype","TestDialoguePlayer","Auto",listener_value,sizeof(listener_value),g_ini_path.c_str());
    const auto listener=dialogue_test::Parse(listener_value);
    if(listener!=g_test_listener) {
        g_test_listener=listener;
        // Keep the real identity and this race's phase/result/once-only flags.
        // Unplayed selection is reselected for the new listener on the next tick.
        if(g_mary.pending){g_mary.pending->clip=-1;g_mary.pending->attempts=0;}
        g_mary.last_variant={};
        g_fixed_greeting_next={};
        Log(std::string("test dialogue listener=")+dialogue_test::Name(listener)+
            "; real player identity and race settlement unchanged");
    }
    char value[48]{};
    GetPrivateProfileStringA("RacePrototype","TestHostOverride","Auto",value,sizeof(value),g_ini_path.c_str());
    std::string requested=std::string(value)=="Auto"?"":value;
    bool reset_invalid_host=false;
    // Restoring Auto cannot strand an illegal duplicate protagonist test host.
    if(const auto* p=hosts::Find(requested);p && !hosts::AllowedPlayer(*p,
            ENTITY::GET_ENTITY_MODEL(PLAYER::PLAYER_PED_ID()),g_test_listener!=dialogue_test::Listener::Auto)) {
        if(g_test_host_override==requested){requested.clear();reset_invalid_host=true;}else return;
    }
    if(requested==g_test_host_override)return;
    if(!requested.empty()) {
        const auto* profile=hosts::Find(requested);
        if(!profile)return;
    }
    if(showcase::Active() || MISC::GET_MISSION_FLAG() || g_state==State::Accepted ||
        g_state==State::PreparingCircuit || g_state==State::Racing || g_state==State::Result ||
        g_state==State::Departing || g_state==State::Declined)return;
    const bool replace_here=CurrentRules().Hosted() && PlayerInSelectedSpawnBand() &&
        (g_state==State::Offering || g_state==State::Approaching || g_state==State::LoadingModels ||
         g_state==State::PreparingFixedOutfit || g_state==State::Ready);
    const bool horse_here=g_opponent_horse && ENTITY::DOES_ENTITY_EXIST(g_opponent_horse);
    const Vector3 replace_position=horse_here?ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false):
        (g_test_host_replace?g_test_host_position:g_variant->encounter_anchor);
    const float replace_heading=horse_here?ENTITY::GET_ENTITY_HEADING(g_opponent_horse):
        (g_test_host_replace?g_test_host_heading:0.0F);
    if(replace_here) {
        if(PlayerHasTakenOpponentHorse() || !RestoreOpponentRanks("F11 host replacement"))return;
        // Explicit test action permits removing only this owned preview pair in
        // view. Restore attributes first; shared cleanup preserves claimed horses.
        CleanupRace();
    } else if(CurrentRules().Hosted() && g_state!=State::Scanning && g_state!=State::Ready)
        AbortRace("test host changed before acceptance; old actor departs naturally",false);
    if(g_rank_lease.pending || g_restore_requested)return;
    if(CurrentRules().Hosted() && !replace_here){ReleaseRoute();SetState(State::Scanning);}
    g_test_host_override=requested;
    if(reset_invalid_host)WritePrivateProfileStringA("RacePrototype","TestHostOverride","Auto",g_ini_path.c_str());
    g_fixed_meetings.fill({});
    for(auto& station:g_stations)station.waiting_for_leave=false;
    g_next_site_scan={};
    if(replace_here) {
        g_rival=&FixedRivalDefinition();
        // A previous host's invalid model must not impose its per-site retry
        // delay on a different explicitly selected test model. One new attempt
        // per changed selection; normal model-failure backoff remains intact.
        g_model_retry_after.erase(g_variant->id);
        SetState(State::Ready);
        g_test_host_replace=true;g_test_host_position=replace_position;g_test_host_heading=replace_heading;
        Log("test host replacing owned preview in place; no leave radius or visibility wait");
        BeginModelLoading();
    }
    Log("test host applied="+(requested.empty()?std::string("Auto (station profiles)"):requested)+
        "; pickup preview replaced in place; active race/F9 changes deferred");
}
void PollOpponentDifficulty()
{
    if(Clock::now()<g_difficulty_poll)return;
    g_difficulty_poll=Clock::now()+std::chrono::milliseconds(250);
    // Preserve the accepted horse/ranks/price through preparation, racing and
    // results. Preview edits reuse the SAME horse and restore its previous lease
    // first, so repeatedly adjusting a range never stacks base-rank overrides.
    if(!g_variant || !g_rival || !g_horse_plan_ready || g_restore_requested ||
        (g_state!=State::Offering && g_state!=State::Approaching) ||
        !g_fixed_wager.CanBegin() || g_horse_info_card.Active() || showcase::Active() ||
        MISC::GET_MISSION_FLAG() || NETWORK::NETWORK_IS_SESSION_ACTIVE() || HUD::IS_PAUSE_MENU_ACTIVE() ||
        !g_opponent_horse || !ENTITY::DOES_ENTITY_EXIST(g_opponent_horse) ||
        !g_opponent_rider || !ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_horse) || ENTITY::IS_ENTITY_DEAD(g_opponent_rider) ||
        PlayerHasTakenOpponentHorse())return;
    const auto index=OpponentDifficultyIndex();
    const auto requested=opponent_difficulty::Read(g_ini_path,index);
    if(requested==g_planned_difficulty)return;
    auto target=DifficultyTarget(requested,true);
    target=opponent_difficulty::ResolveNaturalAcceleration(target,
        CurrentRules().Hosted()?&FixedProfile():nullptr,g_planned_natural,
        [](int low,int high){return MISC::GET_RANDOM_INT_IN_RANGE(low,high);},CurrentRules().Field());
    if(!target.Valid())return;
    std::vector<horse_attributes::Ranks> extra_targets;
    if(CurrentRules().Field()) {
        // No actors exist for the extras at offer time. Keep their count/coats
        // and update every planned rank from the host's newly edited bounds.
        for(const auto& e:g_extra_rivals) {
            const auto changed=ExtraDifficultyTarget(requested,e.choice.raw);
            if(!changed.Valid() || e.horse.Valid())return;
            extra_targets.push_back(changed);
        }
    }
    if(!RestoreOpponentRanks("F11 opponent range edit"))return;
    for(std::size_t i=0;i<extra_targets.size();++i)g_extra_rivals[i].target=extra_targets[i];
    g_planned_target=target;g_planned_difficulty=requested;
    if(!SelectDifficultyTier() || !EnsurePlannedOpponentRanks("F11 difficulty preview")) {
        AbortRace("opponent difficulty preview readback failed; no wager charged",true);return;
    }
    RefreshFixedPromptLabels();
    Log(std::string("F11 opponent difficulty applied in place: ")+opponent_difficulty::At(index).key+
        " displayed="+std::to_string(target.speed+1)+"/"+std::to_string(target.acceleration+1)+
        " ranges="+opponent_difficulty::Encode(requested)+"; same rider/horse; no debit; accepted races stay frozen");
}
