// Private race start implementation; included by race_system.cpp.
// the existing countdown gate waits for all lines, and GO only starts drivers.
bool PrepareStartingLineFrame()
{
    if(g_start_lines.ready)return true;
    if(!g_start_lines.active) {
        LoadStartingLineSettings();
        g_terrain_profile={};g_host_terrain_caution=-1;
        for(auto& e:g_extra_rivals)e.terrain_caution=-1;
        if(CurrentRules().Field() && std::string_view(CorridorStationKey())=="paint-stakes") {
            const auto styles=terrain::DrawCaution(g_extra_rivals.size()+1,
                [](int n){return MISC::GET_RANDOM_INT_IN_RANGE(0,n);});
            if(styles.size()!=g_extra_rivals.size()+1 || !g_terrain_profile.Build(g_course)) {
                g_start_lines.error="terrain strategy preparation failed";return false;
            }
            g_host_terrain_caution=styles.front();
            for(std::size_t i=0;i<g_extra_rivals.size();++i)g_extra_rivals[i].terrain_caution=styles[i+1];
            Log("terrain strategies locked: station=paint-stakes steady="+std::to_string(styles.size()/2)+
                " total="+std::to_string(styles.size())+" spans="+std::to_string(g_terrain_profile.spans.size())+
                "; recorded route unchanged; no survivor guarantee; all-out auto-win unchanged");
            for(const auto& s:g_terrain_profile.spans)Log("terrain span: from_m="+std::to_string(s.from)+" to_m="+std::to_string(s.to));
        }
        g_start_lines.active=true;
        g_start_lines.percent=g_racing_line_percent;
        g_start_lines.margin_metres=g_racing_line_margin;
        g_start_lines.gap_caps=g_gap_speed_caps;
        g_start_lines.gap_crawl_mps=g_gap_crawl_mps;
        g_start_lines.continuous=g_continuous_route;
    }
    if(!HasRecordedCourse()) {g_start_lines.ready=true;return true;}
    if(g_start_lines.next==0) {
        const auto dynamics=ReadRacePaceDynamics();
        if(!BuildRacingLine(g_course,HostLineAggression(),g_racing_line) ||
            !BuildHostPacePlan(dynamics)) {
            if(RequiredRecordedLine()) {
                g_start_lines.error="required recorded passage has no legal starting line";
                return false;
            }
            // A one-on-one previously used traffic when its native line could
            // not be built. Preserve that outcome without retrying at GO.
            g_start_lines.host_failed=true;
            g_continuous_route=false;
            g_start_lines.continuous=false;
            g_racing_line={};g_pace_plan={};
            Log("starting line: host prebuild unavailable; traffic fallback at GO");
        } else Log("starting line: host prebuilt under blackout, build_ms="+std::to_string(g_racing_line.milliseconds));
        ++g_start_lines.next;
        return false;
    }
    const auto entrant=g_start_lines.next-1;
    if(CurrentRules().Field() && entrant<g_extra_rivals.size()) {
        auto& e=g_extra_rivals[entrant];
        if(!BuildEntrantLine(e)) {
            g_start_lines.error="entrant "+std::to_string(entrant+1)+" line/pace preparation failed";
            return false;
        }
        Log("starting line: entrant "+std::to_string(entrant+1)+" prebuilt under blackout, build_ms="+
            std::to_string(e.line.milliseconds));
        ++g_start_lines.next;
        return false;
    }
    g_start_lines.ready=true;
    Log("starting lines ready before reveal: host="+std::to_string(!g_start_lines.host_failed)+
        " entrants="+std::to_string(g_start_lines.next-1)+" builds="+std::to_string(g_line_build_serial));
    return true;
}

void StartRaceNow()
{
    // Discard this race's partial windows; stable profiles are restored on bind.
    g_competitive_follower.native_vector.learning.Reset();
    g_rider_skill=std::clamp(static_cast<int>(GetPrivateProfileIntA("RaceAI","SkillPercent",100,g_ini_path.c_str())),0,100);
    // Choose one navigation owner for the whole event. Changing this while a
    // kinematic handoff is active would strand a suspended native task.
    g_far_catchup_enabled=ReadFarCatchUpSetting();
    g_native_far_speed=ReadNativeFarSpeedSetting();
    g_native_speed_table=ReadNativeSpeedTableSetting();
    g_native_move_rate_trial=ReadNativeMoveRateTrialSetting();
    g_native_vector_trial=ReadNativeVectorTrialSetting();
    g_host_native_far_probe.Reset();g_extra_native_far_probes.clear();
    Log(std::string("far rival policy at GO: ")+(g_far_catchup_enabled?"off-screen simulation":"native locomotion")+
        " native_far_speed="+std::to_string(g_native_far_speed)+
        " private_speed_table_trial="+std::to_string(g_native_speed_table)+
        " native_move_rate_ab="+std::to_string(g_native_move_rate_trial)+
        " native_vector_speed="+std::to_string(g_native_vector_trial));
    if(g_start_lines.ready) {
        // The hidden preparation is the race snapshot; do not read a different
        // racing-line width or gap policy in the GO frame.
        g_racing_line_percent=g_start_lines.percent;
        g_racing_line_margin=g_start_lines.margin_metres;
        g_gap_speed_caps=g_start_lines.gap_caps;
        g_gap_crawl_mps=g_start_lines.gap_crawl_mps;
        g_continuous_route=g_start_lines.continuous;
    } else LoadStartingLineSettings();
    Log(std::string("gap speed caps: ")+(g_gap_speed_caps?"on (a gap narrower than 6 m slows the plan by its half-width)":"off (the line threads gaps at the pace the bends allow)"));
    DestroyOfferPrompts();
    if (g_horse_plan_ready && (PED::GET_MOUNT(PLAYER::PLAYER_PED_ID())==g_opponent_horse ||
        !ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) || ENTITY::IS_ENTITY_DEAD(g_opponent_rider))) {
        AbortRace("opponent unavailable or horse taken before start",false); return;
    }
    if (MISC::GET_MISSION_FLAG() && !g_owns_mission_flag) {
        AbortRace("another mission started during the invitation", false);
        return;
    }
    MISC::SET_MISSION_FLAG(true);
    g_owns_mission_flag = true;
    if (g_horse_plan_ready) {
        // A fixed host may already hold its preview lease. Reuse it only if the
        // full identity/original/target and live fields still match; never stack it.
        if (!EnsurePlannedOpponentRanks("race-start")) {
            AbortRace("temporary horse ranks failed validation/readback",true); return;
        }
    }
    AddOpponentBlip(); // Normally attached at the original awareness/hail stage; no duplicate or re-flash.
    RemoveStartBlips();
    g_finish_blip = MAP::BLIP_ADD_FOR_COORDS(kCoordinateBlipStyle, g_route_finish);
    if (g_finish_blip != 0) {
        const std::string name = CurrentRules().Hosted() ? std::string(i18n::Text("Finish: ", "终点：")) + FixedFinishName() : i18n::Text("Horse race finish", "赛马终点");
        SetBlipDisplayName(g_finish_blip, name.c_str());
    }
    if(!CurrentRules().uses_lap_engine && !g_guidance_early && !UsesSadieRemainingGps())MAP::START_GPS_MULTI_ROUTE(MISC::GET_HASH_KEY("COLOR_YELLOW"), true, false);
    if (CurrentRules().Hosted()) {
        const Vector3 start = ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true, false);
        if(CurrentRules().uses_lap_engine) {
            const auto rival=ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false);
            if(!g_laps.Start(start,rival,CircuitNow())){AbortRace("circuit start outside surveyed staging area",true);return;}
            g_course_player.Reset();g_course_rival.Reset();
            g_laps.Export(g_laps.player,start,g_course_player);g_laps.Export(g_laps.opponent,rival,g_course_rival);
            if(!DrawCircuitGps()){AbortRace("circuit GPS could not start",true);return;}
            Log("circuit started total="+std::to_string(g_laps.plan.Total())+" X="+std::to_string(g_laps.plan.extra)+
                "; continuous recorded-only GPS across first/regular/final seams; whole-lap/window display and missing-channel recovery; road router disabled; finish requires all ordered gates; town speed exemption active for both racers");
        } else if(HasRecordedCourse()) {
            g_course_player.Reset();g_course_rival.Reset();
            g_course_player.Update(g_course,start);
            g_course_rival.Update(g_course,ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false));
            const bool exact=g_guidance_early?g_course_gps.ready:
                g_course_gps.Register(g_course,"TJC_COURSE_"+std::to_string(GetCurrentProcessId())+
                    "_"+std::to_string(++g_course_serial));
            if(!exact || !g_course_gps.Draw(g_course,g_course_player,MISC::GET_HASH_KEY("COLOR_YELLOW"),true,false,UsesSadieRemainingGps())) {
                AbortRace("recorded-course GPS unavailable; technical cancellation without win/loss charge",true);return;
            }
            Log("shared course GPS: verified native recording; persistent draw, no per-node clear/rebuild");
        } else {
            MAP::ADD_POINT_TO_GPS_MULTI_ROUTE(start.x,start.y,start.z,false);
            MAP::ADD_POINT_TO_GPS_MULTI_ROUTE(g_route_finish.x,g_route_finish.y,g_route_finish.z,false);
        }
        g_fixed_previous_player = start;
        g_fixed_previous_rival = ENTITY::GET_ENTITY_COORDS(g_opponent_rider, true, false);
        g_fixed_samples_ready = true;
    } else {
        int navigation_start = 0;
        TASK::WAYPOINT_RECORDING_GET_CLOSEST_WAYPOINT(
            g_route_name.c_str(), ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true, false), &navigation_start);
        MAP::_START_GPS_CUSTOM_ROUTE_FROM_WAYPOINT_RECORDING_ROUTE(
            g_route_name.c_str(), navigation_start, g_navigation_route_points, MISC::GET_HASH_KEY("COLOR_YELLOW"), false,
            true);
    }
    if(!CurrentRules().uses_lap_engine && !UsesSadieRemainingGps())MAP::SET_GPS_MULTI_ROUTE_RENDER(true);
    if(CurrentRules().SingleLap())ShowObjective(i18n::Text(
        "Complete one lap and return to the starting line.",
        "完成一圈，返回起点。"));
    else if(CurrentRules().SingleTrip())ShowObjective(i18n::Text(
        "Follow the course to the finish.",
        "沿赛道前往终点。"));
    else if(CurrentRules().independent_laps)ShowObjective(i18n::Text("Complete ","完成 ")+std::to_string(g_laps.plan.Total())+i18n::Text(
        " laps and cross the finish line.",
        " 圈，冲过终点。"));
    else if(CurrentRules().uses_lap_engine) {
        const auto& route=SelectedFixedRoute();
        const std::string landmark=i18n::Label(route.start.name,route.start.name_zh);
        ShowObjective(i18n::Text("Complete ","完成 ")+std::to_string(g_laps.plan.Total())+
            i18n::Text(" laps, passing "," 圈，经过")+landmark+
            i18n::Text(" and returning to the start on the final lap.","，最后一圈返回起点。"));
    }
    else if (CurrentRules().geometry==rules::Geometry::OpenPoints) ShowObjective(std::string(i18n::Text("Be the first to reach ", "抢先抵达")) + FixedFinishName() +
        i18n::Text(".", "。"));
    else ShowLocalizedObjective("HORSE_RACE_OBJ");
    const float start_separation = std::sqrt(DistanceSquared(
        ENTITY::GET_ENTITY_COORDS(g_opponent_rider, true, false),
        ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true, false)));
    PlaySpeech(g_opponent_rider, PLAYER::PLAYER_PED_ID(),
        start_separation >= 12.0F ? g_rival->react_start : g_rival->lets_go, nullptr);
    if(CurrentRules().allow_wolves) {
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_opponent_horse,false);
        if(g_opponent_original_horse_relationship)
            PED::SET_PED_RELATIONSHIP_GROUP_HASH(g_opponent_horse,g_opponent_original_horse_relationship);
        Log("wolf trail: opponent horse native reactions/relationship restored; no fear or fall suppression");
    }
    if(!StartOriginalOpponentRoute())return;
    if(!StartExtraRacers()){AbortRace("extra entrant start validation failed; no win/loss charge",true);return;}
    if (!CurrentRules().Hosted() && race_speed_policy::Enabled()) {
        const float effort=race_pace::Effort(g_rider_skill);
        OverrideWaypointSpeed(g_opponent_rider,effort);
        g_competitive_follower.override_request=effort;
    }
    StartRaceMusic();
    if(!CurrentRules().allow_wolves)g_race_shocking_event = EVENT::ADD_SHOCKING_EVENT_FOR_ENTITY(
        MISC::GET_HASH_KEY("EVENT_SHOCKING_BEAT_SURPRISING"), PLAYER::PLAYER_PED_ID(), -1.0F, 30.0F, 20.0F,
        0.0F, -1.0F, 180.0F, false, false, -1, -1);
    if (CurrentRules().Hosted()) {
        g_initial_player_route_deviation = 0.0F;
        Log(CurrentRules().SingleLap()?"single-lap strict course: same corridor and recovery rules for both racers; no shortcut progress":
            HasRecordedCourse()?"shared course: persistent NPC/GPS nodes; full-course proximity; continuous shortcut rejoin; abandonment requires >100m AND not heading to finish for 15s":
            "fixed destination race: free road choice; abandonment requires >800m from A-B envelope AND not heading to finish for 15s");
    } else if (!TryGetRouteDeviation(PLAYER::PLAYER_PED_ID(), g_initial_player_route_deviation)) {
        g_initial_player_route_deviation = 0.0F;
        Log("warning: could not measure initial player route deviation");
    } else {
        Log("initial player route deviation=" + std::to_string(g_initial_player_route_deviation) +
            "m; abandon threshold=" +
            std::to_string(g_initial_player_route_deviation + kRouteDeviationAllowance) + "m");
    }
    g_next_progress_log = Clock::now();
    g_race_started_at = Clock::now();
    g_host_physical_history.Clear();g_host_physical_incident_emitted=false;
    g_opponent_damage = {{{HorseIdentity(g_opponent_rider), ENTITY::GET_ENTITY_HEALTH(g_opponent_rider), {}},
        {HorseIdentity(g_opponent_horse), ENTITY::GET_ENTITY_HEALTH(g_opponent_horse), {}}}};
    Log("race HP snapshot rider="+std::to_string(ENTITY::GET_ENTITY_HEALTH(g_opponent_rider))+"/"+
        std::to_string(ENTITY::GET_ENTITY_MAX_HEALTH(g_opponent_rider,false))+" horse="+
        std::to_string(ENTITY::GET_ENTITY_HEALTH(g_opponent_horse))+"/"+
        std::to_string(ENTITY::GET_ENTITY_MAX_HEALTH(g_opponent_horse,false))+"; read-only, no health override");
    g_next_banter_at = g_race_started_at + std::chrono::seconds(MISC::GET_RANDOM_INT_IN_RANGE(20, 40));
    g_race_cash_timeline = horse_ledger::runtime::Timeline();
    horse_ledger::runtime::CancelRace();
    if (CurrentRules().Hosted() && !GetAutoSurveyState().running) {
        horse_ledger::Win win;
        const auto* station = StationOf(g_variant);
        const bool limited = CurrentRules().breed.has_value();
        win.race_id = std::string(station ? station->key : "hosted") +
            (limited ? ":breed-limited:" : ":duel:") + std::to_string(static_cast<int>(g_variant->id));
        win.race_name = FixedRouteTitle(g_variant);
        const Hash player_model = ENTITY::GET_ENTITY_MODEL(PLAYER::PLAYER_PED_ID());
        win.protagonist = player_model == 0x00B69710U ? "John" : player_model == 0x0D7114C9U ? "Arthur" : "Unknown";
        win.kind = limited ? horse_ledger::RaceKind::BreedLimited : horse_ledger::RaceKind::Duel;
        win.practice = g_fixed_practice;
        win.net_cents = g_fixed_wager.net_reward; // The accepted wager freezes the actual settlement.
        win.opponents.push_back({FixedProfile().key, g_horse_model,
            g_opponent_speed_level, g_opponent_accel_level, true});
        for (const auto& rival : g_extra_rivals) {
            if (rival.retiring) continue;
            win.opponents.push_back({rival.inspection_name, rival.choice.model,
                rival.target.speed + 1, rival.target.acceleration + 1, false});
        }
        horse_ledger::runtime::BeginRace(PED::GET_MOUNT(PLAYER::PLAYER_PED_ID()), std::move(win));
    }
    SetState(State::Racing);
    if(CurrentRules().random_direction)
        g_fixed_direction_renew[FixedStationIndex(g_variant)]=true;
    g_circuit_scene.Started();
    if (CurrentRules().Hosted()) {
        g_mary_horse_health=ENTITY::GET_ENTITY_HEALTH(g_opponent_horse);
        g_mary_player_horse_health=ENTITY::GET_ENTITY_HEALTH(g_player_mount);
        g_mary.next_banter=MaryNow()+20.0;
        // A staged race already queued this with its fade; do not restart the
        // host mid-sentence, and never leave an unstaged race silent.
        if(!g_accept_line_queued)QueueAcceptedLine();
    }
}
