// Private result scene implementation; included by race_system.cpp.
// The arrival presentation owns a temporary cap only when its stock value
// can be restored on a live override change.
decltype(HorseIdentity(Ped{})) g_result_speed_cap_owner{};
std::optional<float> g_result_speed_cap_previous;
void ReleaseResultSpeedCap()
{
    if(!g_result_speed_cap_previous)return;
    const auto owner=HorseIdentity(g_result_speed_cap_owner.horse);
    if(owner==g_result_speed_cap_owner && owner.Valid()) {
        const auto current=horse_motion::read_motion_limits(owner.address).script_max;
        if(!current)return; // Do not replace another owner's limit blindly.
        if(std::abs(*current-1.5F)<.001F)
            PED::SET_PED_MAX_MOVE_BLEND_RATIO(owner.horse,*g_result_speed_cap_previous);
    }
    g_result_speed_cap_previous.reset();
    g_result_speed_cap_owner={};
}

void PrepareResultSceneAssets()
{
    if (g_result_playlists_requested || g_result_scene == 0 ||
        !ANIMSCENE::DOES_ANIM_SCENE_EXIST(g_result_scene) ||
        !ANIMSCENE::IS_ANIM_SCENE_LOADED(g_result_scene, true, false)) {
        return;
    }
    const bool win_requested = ANIMSCENE::REQUEST_ANIM_SCENE_PLAY_LIST(g_result_scene, ResultPlaylist(false));
    const bool loss_requested = ANIMSCENE::REQUEST_ANIM_SCENE_PLAY_LIST(g_result_scene, ResultPlaylist(true));
    g_result_playlists_requested = true;
    Log(std::string("result scene loaded; requested original playlists: ") + ResultPlaylist(false) + "=" +
        std::to_string(win_requested) + " " + ResultPlaylist(true) + "=" + std::to_string(loss_requested));
}

void TryOriginalRivalResultLine()
{
    if (!HasScriptedRivalResultLine() || g_result_scripted_line_attempted || !RaceSpeechAvailable()) {
        return;
    }
    // This is the explicit original Weird branch, not a guessed generic audio fallback.
    // At most one attempt: a failed native call must not replay an uncertain audio event.
    g_result_scripted_line_attempted = true;
    g_result_scripted_line_at = Clock::now();
    PlaySpeech(g_opponent_rider, PLAYER::PLAYER_PED_ID(),
        g_player_won ? g_rival->player_won : g_rival->player_lost, nullptr);
}

bool ScriptedRivalResultLineSettled()
{
    return g_result_scripted_line_attempted &&
        std::chrono::duration<double>(Clock::now() - g_result_scripted_line_at).count() >= 1.0 &&
        RaceSpeechAvailable();
}

void BeginResultInteraction(const char* reason, bool allow_scripted_line = true)
{
    Log(std::string("result presentation ended: ") + reason + "; winner=" +
        (g_player_won ? "player" : "rival") + "; scripted_line_attempted=" +
        std::to_string(g_result_scripted_line_attempted));
    // Result dialogue is independent of the Weird scene's availability in the original script.
    // Keep uncertain/wrong scene playback stopped before using that verified actor/outcome line.
    ReleaseResultSpeedCap();
    ReleaseResultScene();
    g_offer_phase_started = Clock::now();
    if (allow_scripted_line && HasScriptedRivalResultLine() && !ScriptedRivalResultLineSettled()) {
        g_result_phase = ResultPhase::ScriptedRivalLine;
        Log("waiting for original Weird result line before post-race interaction");
        return;
    }
    // Original func_80 removes the rival marker when its finish presentation ends.
    RemoveBlip(g_opponent_blip);
    g_result_interaction_registered = CreateResultPrompts();
    g_result_phase = ResultPhase::Interaction;
}

void StartFixedResultDepartureMotion()
{
    if((g_mary.result_departing && g_departure_task_started) || MaryNow()<g_mary.result_next_departure_try)return;
    g_departure_task_started=StartNaturalDeparture(g_opponent_rider,g_opponent_horse,PlayerHasTakenOpponentHorse());
    g_mary.result_next_departure_try=MaryNow()+1;
    if(!g_mary.result_departing)
        Log("fixed result: departing 1.5 seconds after result speech; same-instance Talk remains available while riding away");
    g_mary.result_departing=true;
}

void BeginDeparture()
{
    ForgetRecycledRaceActors();
    g_mary.result_approaching=false;
    const bool mary_goodbye=MaryOwned() && g_mary.phase==marybeth_dialogue::Phase::Result &&
        (g_mary.result_attempted || g_mary.result_failed) && !g_mary.farewell_attempted && MaryInRange(30.0F) &&
        !PED::IS_PED_IN_COMBAT(PLAYER::PLAYER_PED_ID(),0) && !PED::IS_PED_IN_COMBAT(g_opponent_rider,0);
    RemoveBlip(g_opponent_blip);
    DestroyOfferPrompts();
    StopNavigation();
    ReleaseMissionFlag();
    StopRaceAudio(false);
    ReleaseResultSpeedCap();
    ReleaseResultScene();
    if(!g_departure_task_started)
        g_departure_task_started=StartNaturalDeparture(g_opponent_rider,g_opponent_horse,PlayerHasTakenOpponentHorse());
    SetState(State::Departing);
    if (mary_goodbye) {
        g_mary.farewell_attempted=true;
        const int hour=CLOCK::GET_CLOCK_HOURS();
        const bool evening=(hour>=18 || hour<5) && FixedHasVoice(marybeth_dialogue::Event::Evening) &&
            MISC::GET_RANDOM_INT_IN_RANGE(0,6)==0;
        g_mary.Queue(evening?marybeth_dialogue::Event::Evening:marybeth_dialogue::Event::Goodbye,MaryNow(),5.0);
        TickMaryVoice();
    }
}

void TickResult()
{
    if(!race_speed_policy::Enabled() || g_result_phase!=ResultPhase::Arrival)ReleaseResultSpeedCap();
    // Before the presentation: it may depart, which releases the guidance.
    TickRetainedGuidance();
    if (CurrentRules().Hosted()) {
        RemoveBlip(g_opponent_blip);
        TickMaryResult();
        return;
    }
    if (g_result_phase == ResultPhase::Interrupted) {
        if (StateSeconds() >= 3.0) {
            BeginDeparture();
        }
        return;
    }
    if (g_opponent_rider == 0 || !ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_rider) || g_opponent_horse == 0 ||
        !ENTITY::DOES_ENTITY_EXIST(g_opponent_horse) || ENTITY::IS_ENTITY_DEAD(g_opponent_horse)) {
        BeginDeparture();
        return;
    }

    PrepareResultSceneAssets();
    const char* playlist = ResultPlaylist(g_player_won);
    const double phase_seconds = std::chrono::duration<double>(Clock::now() - g_offer_phase_started).count();
    switch (g_result_phase) {
    case ResultPhase::Arrival: {
        const float finish_distance = std::sqrt(DistanceSquared(
            ENTITY::GET_ENTITY_COORDS(g_opponent_rider, true, false), g_route_finish));
        if (finish_distance <= 20.0F && race_speed_policy::Enabled()) {
            const auto owner=HorseIdentity(g_opponent_horse);
            if(g_result_speed_cap_previous && owner!=g_result_speed_cap_owner)ReleaseResultSpeedCap();
            if(!g_result_speed_cap_previous && owner.Valid()) {
                if(const auto previous=horse_motion::read_motion_limits(owner.address).script_max) {
                    g_result_speed_cap_owner=owner;g_result_speed_cap_previous=previous;
                    PED::SET_PED_MAX_MOVE_BLEND_RATIO(owner.horse,1.5F);
                }
            }
        }
        if (finish_distance > 5.0F) {
            if (StateSeconds() >= 30.0) {
                Log("result presentation skipped: opponent did not reach finish; settled result unchanged");
                BeginDeparture();
                return;
            }
            if(TASK::IS_WAYPOINT_PLAYBACK_GOING_ON_FOR_PED(g_opponent_rider,nullptr)) {
                if(race_speed_policy::Enabled()) {
                    OverrideWaypointSpeed(g_opponent_rider,2.5F);
                    g_competitive_follower.override_request=2.5F;
                } else if(g_competitive_follower.override_request!=0 && g_route_points>0) {
                    const int restart=std::clamp(g_opponent_progress_max,0,g_route_points-1);
                    TASK::TASK_FOLLOW_WAYPOINT_RECORDING(
                        g_opponent_rider,g_shortcut_route_name.c_str(),restart,kWaypointTaskFlags,-1,false,0,-1);
                    g_competitive_follower.ResetWaypointCommand();
                }
            }
            return;
        }
        TASK::TASK_STAND_STILL(g_opponent_horse, -1);
        TASK::TASK_STAND_STILL(g_opponent_rider, -1);
        g_offer_phase_started = Clock::now();
        g_result_next_log = Clock::time_point{};
        g_result_phase = ResultPhase::Preparing;
        Log(std::string("preparing result presentation: winner=") + (g_player_won ? "player" : "rival") +
            " requested_playlist=" + playlist +
            (HasScriptedRivalResultLine() ? " speech_owner=original_script" : " speech_owner=animscene"));
        return;
    }
    case ResultPhase::Preparing: {
        if (g_result_scene == 0 || !ANIMSCENE::DOES_ANIM_SCENE_EXIST(g_result_scene)) {
            BeginResultInteraction("animscene unavailable");
            return;
        }
        if (phase_seconds >= 15.0) {
            BeginResultInteraction("scene/playlist loading timed out");
            return;
        }
        if (!ANIMSCENE::IS_ANIM_SCENE_LOADED(g_result_scene, true, false)) {
            return;
        }
        const bool playlist_exists = ANIMSCENE::_DOES_ANIM_SCENE_PLAY_LIST_EXIST(g_result_scene, playlist);
        const bool playlist_loaded = ANIMSCENE::_IS_ANIM_SCENE_PLAYBACK_LIST_PHASE_LOADED(g_result_scene, playlist);
        if (Clock::now() >= g_result_next_log) {
            const bool playlist_loading = ANIMSCENE::_IS_ANIM_SCENE_PLAYBACK_LIST_PHASE_LOADING(g_result_scene, playlist);
            Log(std::string("result playlist probe: requested=") + playlist +
                " exists=" + std::to_string(playlist_exists) + " loaded=" + std::to_string(playlist_loaded) +
                " loading=" + std::to_string(playlist_loading));
            if (!playlist_loaded) {
                const bool requested = ANIMSCENE::REQUEST_ANIM_SCENE_PLAY_LIST(g_result_scene, playlist);
                Log(std::string("result playlist request retry: ") + playlist + "=" + std::to_string(requested));
            }
            g_result_next_log = Clock::now() + std::chrono::seconds(2);
        }
        // Vanilla does not gate this branch on DOES_*_EXIST. Do not interpret one false response
        // as proof the requested resource is permanently absent; require actual loading instead.
        // Keep the bounded wait and expected/opposite active-branch checks before accepting playback.
        if (!playlist_loaded) {
            return;
        }
        ANIMSCENE::SET_ANIM_SCENE_PLAY_LIST(g_result_scene, playlist, true);
        g_offer_phase_started = Clock::now();
        g_result_phase = ResultPhase::Settling;
        Log(std::string("result playlist loaded and selected: ") + playlist + "; yielding before start");
        return;
    }
    case ResultPhase::Settling: {
        if (g_result_scene == 0 || !ANIMSCENE::DOES_ANIM_SCENE_EXIST(g_result_scene)) {
            BeginResultInteraction("animscene lost before start");
            return;
        }
        // Like func_78/func_79, separate selection, stopping the horse and START_ANIM_SCENE.
        const double stop_timeout = g_player_won ? 5.0 : 8.0;
        const bool stopped = ENTITY::GET_ENTITY_SPEED(g_opponent_horse) <= 0.1F &&
            TASK::IS_PED_STILL(g_opponent_horse);
        if (phase_seconds < 0.25 || (!stopped && phase_seconds < stop_timeout)) {
            return;
        }
        if (!RaceSpeechAvailable()) {
            if (phase_seconds >= 15.0) {
                BeginResultInteraction("speech did not clear before scene start");
            }
            return;
        }
        Vector3 origin = ENTITY::GET_ENTITY_COORDS(g_opponent_horse, true, false);
        float ground_z = origin.z;
        if (MISC::GET_GROUND_Z_FOR_3D_COORD(origin, &ground_z, false)) {
            origin.z = ground_z;
        }
        const Vector3 rotation = ENTITY::GET_ENTITY_ROTATION(g_opponent_horse, 2);
        ANIMSCENE::SET_ANIM_SCENE_ORIGIN(g_result_scene, origin, rotation, 2);
        PHYSICS::_0xBDDA142759307528(g_opponent_horse);
        PHYSICS::_0xBDDA142759307528(g_opponent_rider);
        TASK::_0xDF94844D474F31E5(g_opponent_horse);
        ANIMSCENE::START_ANIM_SCENE(g_result_scene);
        g_result_started_at = Clock::now();
        g_result_phase = ResultPhase::Starting;
        Log(std::string("result scene start requested: ") + playlist + "; awaiting native confirmation");
        return;
    }
    case ResultPhase::Starting:
    case ResultPhase::Playing: {
        if (g_result_scene == 0 || !ANIMSCENE::DOES_ANIM_SCENE_EXIST(g_result_scene)) {
            BeginResultInteraction("animscene lost after start");
            return;
        }
        const double scene_seconds = std::chrono::duration<double>(Clock::now() - g_result_started_at).count();
        const bool running = ANIMSCENE::IS_ANIM_SCENE_RUNNING(g_result_scene, false);
        const bool expected_active = ANIMSCENE::_IS_ANIM_SCENE_PLAYBACK_LIST_PHASE_ACTIVE(g_result_scene, playlist);
        const bool opposite_active = ANIMSCENE::_IS_ANIM_SCENE_PLAYBACK_LIST_PHASE_ACTIVE(
            g_result_scene, ResultPlaylist(!g_player_won));
        const bool finished = ANIMSCENE::IS_ANIM_SCENE_FINISHED(g_result_scene, false) ||
            ANIMSCENE::HAS_ANIM_SCENE_EXITED(g_result_scene, false);
        if (Clock::now() >= g_result_next_log) {
            Log(std::string("result scene status: requested=") + playlist + " running=" + std::to_string(running) +
                " expected_active=" + std::to_string(expected_active) +
                " opposite_active=" + std::to_string(opposite_active) + " finished=" + std::to_string(finished) +
                " phase=" + std::to_string(ANIMSCENE::GET_ANIM_SCENE_PHASE(g_result_scene)) +
                " rival_speech=" + std::to_string(SpeechPlaying(g_opponent_rider)) +
                " player_speech=" + std::to_string(SpeechPlaying(PLAYER::PLAYER_PED_ID())));
            g_result_next_log = Clock::now() + std::chrono::seconds(2);
        }
        if (opposite_active) {
            BeginResultInteraction("opposite result playlist active; stopped without replay", false);
            return;
        }
        if (g_result_phase == ResultPhase::Starting) {
            if (running && expected_active) {
                g_result_phase = ResultPhase::Playing;
                Log(std::string("result branch confirmed by native: ") + playlist +
                    (HasScriptedRivalResultLine() ? "; original script owns result speech" : "; scene owns result speech"));
            } else if (scene_seconds >= 5.0 || finished) {
                BeginResultInteraction("requested scene branch never confirmed");
                return;
            }
        }
        if (g_result_phase == ResultPhase::Playing) {
            TryOriginalRivalResultLine();
        }
        // Novice/Confident keep their existing scene-owned path. Weird needs its own original
        // GOAL_PLYR_* call, once, and a speech-start grace period before enabling reply prompts.
        if (finished && RaceSpeechAvailable() &&
            (!HasScriptedRivalResultLine() || ScriptedRivalResultLineSettled())) {
            BeginResultInteraction("scene finished");
        } else if (scene_seconds >= 60.0) {
            BeginResultInteraction("scene completion timed out; stopped without replay");
        }
        break;
    }
    case ResultPhase::ScriptedRivalLine: {
        if (!g_result_scripted_line_attempted && phase_seconds >= 20.0) {
            BeginResultInteraction("original scripted result line could not start; no replay", false);
            return;
        }
        TryOriginalRivalResultLine();
        if (ScriptedRivalResultLineSettled()) {
            BeginResultInteraction("original scripted result line settled", false);
        } else if (g_result_scripted_line_attempted &&
            std::chrono::duration<double>(Clock::now() - g_result_scripted_line_at).count() >= 20.0) {
            BeginResultInteraction("original scripted result line wait timed out; no replay", false);
        }
        break;
    }
    case ResultPhase::Interaction: {
        if (g_result_interaction_registered) {
            if (HUD::_UI_PROMPT_IS_PRESSED(g_accept_prompt)) {
                DestroyOfferPrompts();
                PlaySpeech(PLAYER::PLAYER_PED_ID(), g_opponent_rider,
                    g_player_won ? g_rival->result_positive_win : g_rival->result_positive_loss, nullptr);
                g_result_interaction_registered = false;
                g_result_interaction_positive = true;
                g_result_response_started = false;
                g_offer_phase_started = Clock::now();
                g_result_phase = ResultPhase::PlayerReply;
                return;
            }
            if (HUD::_UI_PROMPT_IS_PRESSED(g_decline_prompt)) {
                DestroyOfferPrompts();
                PlaySpeech(PLAYER::PLAYER_PED_ID(), g_opponent_rider,
                    g_player_won ? g_rival->result_antagonize_win : g_rival->result_antagonize_loss, nullptr);
                g_result_interaction_registered = false;
                g_result_interaction_positive = false;
                g_result_response_started = false;
                g_offer_phase_started = Clock::now();
                g_result_phase = ResultPhase::PlayerReply;
                return;
            }
        }
        if (std::chrono::duration<double>(Clock::now() - g_offer_phase_started).count() >= 12.0) {
            BeginDeparture();
        }
        break;
    }
    case ResultPhase::PlayerReply: {
        const double elapsed = std::chrono::duration<double>(Clock::now() - g_offer_phase_started).count();
        if (!g_result_response_started && ((elapsed >= 1.0 && !SpeechPlaying(PLAYER::PLAYER_PED_ID())) ||
                                               elapsed >= 5.0)) {
            const char* response = "RESPONSE_GENERIC";
            if (g_variant->archetype == RivalArchetype::Novice) {
                response = g_player_won
                    ? (g_result_interaction_positive ? "VICTIMIZED_REACT" : "GENERIC_INSULT_MED_MALE")
                    : (g_result_interaction_positive ? "RESPONSE_C" : "RESPONSE_IDLING");
            }
            PlaySpeech(g_opponent_rider, PLAYER::PLAYER_PED_ID(), response, nullptr);
            g_result_response_started = true;
            g_offer_phase_started = Clock::now();
            g_result_phase = ResultPhase::OpponentReply;
        }
        break;
    }
    case ResultPhase::OpponentReply:
        if ((std::chrono::duration<double>(Clock::now() - g_offer_phase_started).count() >= 1.0 &&
                !SpeechPlaying(g_opponent_rider)) ||
            std::chrono::duration<double>(Clock::now() - g_offer_phase_started).count() >= 5.0) {
            BeginDeparture();
        }
        break;
    default:
        BeginDeparture();
        break;
    }
}

void FinishDeparture()
{
    ReleaseSpawnedEntities();
    ReleaseModels();
    ResetRaceState();
    AwaitNextEncounter();
    // Every completed race, encounter or fixed, redraws where the three riders
    // can next be found. An aborted race does not: it was never finished.
    RollEncounterPoints("race completed");
    Log("race session released; presented opponents remain under off-camera cleanup guard");
}

void TickDeparting()
{
    ForgetRecycledRaceActors();
    if (CurrentRules().Hosted()) TickMaryVoice();
    // Session handoff is not entity release. A separate identity-bound caretaker
    // retains visible actors, so a new pickup cannot inherit their old dialogue.
    const bool farewell_settled=!CurrentRules().Hosted() || MarySpeechSettled();
    if ((StateSeconds()>=3.0 && farewell_settled) || StateSeconds()>=15.0) {
        FinishDeparture();
        return;
    }
    if (!g_departure_task_started)
        g_departure_task_started=StartNaturalDeparture(g_opponent_rider,g_opponent_horse,PlayerHasTakenOpponentHorse());
}
