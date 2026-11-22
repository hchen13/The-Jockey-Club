// Private speech and prompts implementation; included by race_system.cpp.
#include "../race_music.inl"

void StopRaceAudio(bool result_fade)
{
    StopRaceMusic(result_fade);
    AUDIO::STOP_AUDIO_SCENE("horse_race_scene");
    AUDIO::STOP_AUDIO_SCENE("Horse_Race_Beat_Train_scene");
}

race_avoidance::Lease g_racing_assist;
void RestoreFixedAvoidance()
{
    g_racing_assist.Reset();
    for(auto& e:g_extra_rivals)e.racing_assist.Reset();
}

void TrialBegin();
void TrialEnd();
void SetState(State state)
{
    if(state==State::Disabled)g_boundary_warning.Reset();
    else if(g_state==State::Racing&&state!=State::Racing)g_boundary_warning.FadeOut(CircuitNow());
    if(state!=State::Racing)TrialEnd();
    if(g_state==State::Racing && state!=State::Racing)horse_motion::speed_learning::runtime::Flush();
    if(state==State::Racing && g_state!=State::Racing){++g_race_epoch;race_help::Reset();}
    if(state!=State::Racing){g_competitive_follower.StopControl();g_pace_controller={};g_pace_command={};
        g_host_catchup.Reset();g_host_burst_earned=false;for(auto& e:g_extra_rivals)e.chase.Reset();}
    if(state!=State::Racing)RestoreFixedAvoidance();
    const bool preview=CurrentRules().Hosted() && (state==State::Offering || state==State::Accepted || state==State::PreparingCircuit) &&
        PlannedOpponentRanksReady();
    if (state!=State::Racing && !preview) RestoreOpponentRanks(StateName(state));
    if (state != g_state) {
        Log(std::string("state ") + StateName(g_state) + " -> " + StateName(state));
    }
    g_state = state;
    g_state_entered = Clock::now();
    if(state==State::Racing)TrialBegin();
    if (CurrentRules().Hosted() && g_mary.id) {
        using P = marybeth_dialogue::Phase;
        // Staging is part of the race for dialogue: the host's accepted/start
        // line belongs to the fade, not to the moment the countdown ends.
        const P phase = (state==State::Racing || state==State::PreparingCircuit) ? P::Racing :
            (state==State::Result ? (g_result_phase==ResultPhase::Interrupted ? P::Interrupted : P::Result) :
            (state==State::Departing ? P::Departing : P::Offering));
        if (phase!=g_mary.phase) g_mary.Transition(phase);
        g_mary.won=g_player_won;
    }
}

double StateSeconds()
{
    return std::chrono::duration<double>(Clock::now() - g_state_entered).count();
}

float DistanceSquared(const Vector3& left, const Vector3& right)
{
    const float dx = left.x - right.x;
    const float dy = left.y - right.y;
    const float dz = left.z - right.z;
    return dx * dx + dy * dy + dz * dz;
}

void ShowObjective(const std::string& text)
{
    UILOG::_UILOG_SET_CACHED_OBJECTIVE(i18n::Literal(text));
    UILOG::_UILOG_PRINT_CACHED_OBJECTIVE();
    UILOG::_UILOG_CLEAR_HAS_DISPLAYED_CACHED_OBJECTIVE();
    UILOG::_UILOG_CLEAR_CACHED_OBJECTIVE();
}

void ShowLocalizedObjective(const char* text_key)
{
    UILOG::_UILOG_SET_CACHED_OBJECTIVE(MISC::VAR_STRING(2, text_key));
    UILOG::_UILOG_PRINT_CACHED_OBJECTIVE();
    UILOG::_UILOG_CLEAR_HAS_DISPLAYED_CACHED_OBJECTIVE();
    UILOG::_UILOG_CLEAR_CACHED_OBJECTIVE();
}

bool PlaySpeech(Ped speaker, Ped listener, const char* speech_name, const char* voice_name,
    bool allow_fixed_dialogue = false, int variation = 0, Hash speech_params = 0)
{
    if ((CurrentRules().Hosted() && !allow_fixed_dialogue) || !speech_name || !*speech_name ||
        speaker == 0 || !ENTITY::DOES_ENTITY_EXIST(speaker) || ENTITY::IS_ENTITY_DEAD(speaker)) {
        return false;
    }
    ScriptedSpeechParams params{};
    params.speech_name = speech_name;
    params.voice_name = voice_name;
    params.variation = variation;
    // 0 keeps the set this mod proved on the rival; a caller may name another.
    params.speech_param_hash = speech_params ? speech_params : kConversationalSpeechParams;
    params.listener = listener;
    params.sync_over_network = false;
    params.v7 = 0;
    params.v8 = 0;
    const bool context_exists = AUDIO::DOES_CONTEXT_EXIST_FOR_THIS_PED(speaker, speech_name, false);
    const bool played = AUDIO::PLAY_PED_AMBIENT_SPEECH_NATIVE(speaker, reinterpret_cast<Any*>(&params));
    const char* speaker_role = speaker == PLAYER::PLAYER_PED_ID() ? "player" :
        (speaker == g_opponent_rider ? "rival" : "other");
    Log(std::string("speech ") + speech_name + " speaker=" + speaker_role + " ped=" + std::to_string(speaker) +
        " variation=" + std::to_string(variation) +
        (context_exists ? " context=yes" : " context=no") +
        " speech_params=" + std::to_string(params.speech_param_hash) +
        (played ? " started" : " failed"));
    return played;
}

bool SpeechPlaying(Ped ped)
{
    return ped != 0 && ENTITY::DOES_ENTITY_EXIST(ped) &&
        (AUDIO::IS_SCRIPTED_SPEECH_PLAYING(ped) || AUDIO::IS_ANY_SPEECH_PLAYING(ped));
}

Prompt CreateTargetPrompt(const char* text_key, const char* control)
{
    const Prompt prompt = HUD::_UI_PROMPT_REGISTER_BEGIN();
    HUD::_UI_PROMPT_SET_CONTROL_ACTION(prompt, MISC::GET_HASH_KEY(control));
    HUD::_UI_PROMPT_SET_TEXT(prompt, text_key);
    HUD::_UI_PROMPT_SET_PRIORITY(prompt, 0);
    HUD::_UI_PROMPT_SET_TRANSPORT_MODE(prompt, 0);
    HUD::_UI_PROMPT_SET_ATTRIBUTE(prompt, 18, true);
    HUD::_UI_PROMPT_SET_STANDARD_MODE(prompt, false);
    HUD::_UI_PROMPT_REGISTER_END(prompt);
    HUD::_UI_PROMPT_SET_VISIBLE(prompt, true);
    HUD::_UI_PROMPT_SET_ENABLED(prompt, true);
    return prompt;
}

void SyncRaceHorseInspection();
void TickRaceHorseInspection();

void DestroyOfferPrompts()
{
    // The info action belongs to the actor pair, including its race and exit.
    // Only unregister this offer/result's primary action here; the independent
    // inspection controller supplies its own anchor once that action is gone.
    if ((g_accept_prompt || g_decline_prompt) && g_opponent_rider != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_rider)) {
        MISC::UNREGISTER_INTERACTION_LOCKON_PROMPT(g_opponent_rider);
    }
    if (g_accept_prompt != 0 && HUD::_UI_PROMPT_IS_VALID(g_accept_prompt)) {
        HUD::_UI_PROMPT_DELETE(g_accept_prompt);
    }
    if (g_decline_prompt != 0 && HUD::_UI_PROMPT_IS_VALID(g_decline_prompt)) {
        HUD::_UI_PROMPT_DELETE(g_decline_prompt);
    }
    g_accept_prompt = 0;
    g_decline_prompt = 0;
    g_inspect_prompt = 0;
    g_prompt_group = 0;
}

bool CreateOfferPrompts()
{
    DestroyOfferPrompts();
    if (g_opponent_rider == 0 || !ENTITY::DOES_ENTITY_EXIST(g_opponent_rider)) {
        return false;
    }
    const std::string race_label = FixedRacePromptLabel();
    const auto& rules=CurrentRules();
    g_accept_prompt = CreateTargetPrompt(rules.Hosted() ? i18n::Literal(race_label) : "BEAT_GRT_POS", "INPUT_INTERACT_LOCKON_POS");
    g_decline_prompt = CreateTargetPrompt(rules.Hosted() ? i18n::Literal(FixedPracticePromptLabel()) : "BEAT_GRT_NEG", "INPUT_INTERACT_LOCKON_NEG");
    if (!HUD::_UI_PROMPT_IS_VALID(g_accept_prompt) || !HUD::_UI_PROMPT_IS_VALID(g_decline_prompt)) {
        Log("failed to create race/practice prompts");
        DestroyOfferPrompts();
        return false;
    }
    g_prompt_group = HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(g_opponent_rider);
    HUD::_UI_PROMPT_SET_GROUP(g_accept_prompt, g_prompt_group, 0);
    HUD::_UI_PROMPT_SET_GROUP(g_decline_prompt, g_prompt_group, 0);
    if (rules.Hosted()) PED::_SET_PED_PROMPT_NAME(g_opponent_rider, i18n::Literal(FixedHostName()));
    else PED::_SET_PED_PROMPT_NAME_FROM_GXT_ENTRY(g_opponent_rider, MISC::GET_HASH_KEY("RE_INTER_STRANGER"));
    PED::SET_PED_CONFIG_FLAG(g_opponent_rider, 130, true);
    PED::SET_PED_CONFIG_FLAG(g_opponent_rider, 178, false);
    PED::SET_PED_CONFIG_FLAG(g_opponent_rider, 297, true);
    PED::SET_PED_CONFIG_FLAG(g_opponent_rider, 301, false);
    PED::SET_PED_CONFIG_FLAG(g_opponent_rider, 315, true);
    PED::SET_PED_CONFIG_FLAG(g_opponent_rider, 317, false);
    PED::SET_PED_CONFIG_FLAG(g_opponent_rider, 331, false);
    PED::SET_PED_CONFIG_FLAG(g_opponent_rider, 493, false);
    const bool registered = MISC::REGISTER_INTERACTION_LOCKON_PROMPT(
        g_opponent_rider, "", 15.0F, 15.0F, 32776, 0.0F, 0.0F, g_accept_prompt, false, -1);
    Log(std::string("offer interaction ") + (registered ? "registered" : "registration failed"));
    SyncRaceHorseInspection();
    return registered;
}

bool CreateResultPrompts()
{
    DestroyOfferPrompts();
    if (g_opponent_rider == 0 || !ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_rider)) {
        return false;
    }
    const auto& rules=CurrentRules();
    g_accept_prompt = CreateTargetPrompt(rules.Hosted() ? i18n::Literal(i18n::Text("Talk","交谈")) :
        "RE_INTER_POS", "INPUT_INTERACT_LOCKON_POS");
    if (!rules.Hosted()) g_decline_prompt = CreateTargetPrompt("RE_INTER_ANTAGONIZE", "INPUT_INTERACT_LOCKON_NEG");
    if (!HUD::_UI_PROMPT_IS_VALID(g_accept_prompt) || (!rules.Hosted() && !HUD::_UI_PROMPT_IS_VALID(g_decline_prompt))) {
        DestroyOfferPrompts();
        return false;
    }
    g_prompt_group = HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(g_opponent_rider);
    HUD::_UI_PROMPT_SET_GROUP(g_accept_prompt, g_prompt_group, 0);
    if (g_decline_prompt) HUD::_UI_PROMPT_SET_GROUP(g_decline_prompt, g_prompt_group, 0);
    const bool registered = MISC::REGISTER_INTERACTION_LOCKON_PROMPT(
        g_opponent_rider, "", 15.0F, 15.0F, 32776, 0.0F, 0.0F, g_accept_prompt, false, -1);
    Log(std::string("post-race interaction ") + (registered ? "registered" : "registration failed"));
    SyncRaceHorseInspection();
    return registered;
}

void BeginDeparture();
