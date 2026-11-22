// Private invitation implementation; included by race_system.cpp.
bool InvitationActorsValid(Ped player)
{
    return g_player_mount != 0 && ENTITY::DOES_ENTITY_EXIST(g_player_mount) && PED::IS_PED_ON_MOUNT(player) &&
        PED::GET_MOUNT(player) == g_player_mount && g_opponent_rider != 0 &&
        ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) && !ENTITY::IS_ENTITY_DEAD(g_opponent_rider) &&
        g_opponent_horse != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_horse) &&
        !ENTITY::IS_ENTITY_DEAD(g_opponent_horse);
}

void TickApproaching()
{
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!InvitationActorsValid(player)) {
        AbortRace("player mount or opponent was lost during approach", false);
        return;
    }
    if (DistanceSquared(ENTITY::GET_ENTITY_COORDS(player, true, false), g_variant->encounter_anchor) >
        g_offer_abandon_radius * g_offer_abandon_radius) {
        AbortRace("player left invitation area", false);
        return;
    }

    const float distance_squared = DistanceSquared(
        ENTITY::GET_ENTITY_COORDS(player, true, false), ENTITY::GET_ENTITY_COORDS(g_opponent_rider, true, false));
    const float awareness_squared = kOriginalAwarenessRadius * kOriginalAwarenessRadius;
    if (distance_squared > awareness_squared) {
        if (StateSeconds() >= 45.0) {
            AbortRace("opponent could not catch the player for an invitation", false);
        }
        return;
    }
    AddOpponentBlip();
    if (CurrentRules().Hosted() && distance_squared <= 225.0F) { BeginOffering(); return; }
    MaintainOriginalApproachTask(player);
    switch (g_offer_phase) {
    case 0:
        if (g_follow_approach_started &&
            std::chrono::duration<double>(Clock::now() - g_follow_approach_started_at).count() >= 1.0) {
            PlaySpeech(g_opponent_rider, player, g_rival->approach_a, nullptr);
            g_offer_phase = 1;
            g_offer_phase_started = Clock::now();
        }
        break;
    case 1:
        if (OfferLineFinished(4.5)) {
            PlaySpeech(g_opponent_rider, player, g_rival->approach_b, nullptr);
            g_offer_phase = 2;
            g_offer_phase_started = Clock::now();
        }
        break;
    case 2:
        if (OfferLineFinished(4.5)) {
            if (distance_squared <= 144.0F) {
                BeginOffering();
            } else if (PlayerIsRidingAwayFromOpponent(player)) {
                PlaySpeech(g_opponent_rider, player, g_rival->call_over, nullptr);
                g_offer_phase = 3;
                g_offer_phase_started = Clock::now();
            }
        }
        break;
    case 3:
        if (distance_squared <= 144.0F && OfferLineFinished(5.0)) {
            BeginOffering();
        }
        break;
    default:
        break;
    }
    if (StateSeconds() >= 45.0) {
        AbortRace("opponent could not catch the player for an invitation", false);
    }
}

void RefreshFixedPromptLabels()
{
    if (!CurrentRules().Hosted() || g_state != State::Offering) return;
    if (g_accept_prompt && HUD::_UI_PROMPT_IS_VALID(g_accept_prompt)) {
        const std::string label = FixedRacePromptLabel();
        HUD::_UI_PROMPT_SET_TEXT(g_accept_prompt, i18n::Literal(label));
    }
    if (g_decline_prompt && HUD::_UI_PROMPT_IS_VALID(g_decline_prompt))
        HUD::_UI_PROMPT_SET_TEXT(g_decline_prompt, i18n::Literal(FixedPracticePromptLabel()));
    if (g_opponent_rider && ENTITY::DOES_ENTITY_EXIST(g_opponent_rider))
        PED::_SET_PED_PROMPT_NAME(g_opponent_rider, i18n::Literal(FixedHostName()));
}

void TickFixedGreeting(Ped player)
{
    TickMaryGreeting(player);
}

bool InspectionPairClaimed(const race_horses::Identity& rider,const race_horses::Identity& horse)
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !horse.Valid() || HorseIdentity(horse.horse)!=horse)return true;
    return PED::GET_MOUNT(player)==horse.horse || PED::_GET_RIDER_OF_MOUNT(horse.horse,true)==player ||
        PLAYER::_GET_ACTIVE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID())==horse.horse ||
        PLAYER::_GET_TEMP_PLAYER_HORSE(PLAYER::PLAYER_ID())==horse.horse ||
        (rider.Valid() && HorseIdentity(rider.horse)==rider && PED::GET_PEDS_JACKER(rider.horse)==player);
}

std::vector<inspection::Subject> RaceInspectionSubjects()
{
    std::vector<inspection::Subject> subjects;
    const bool active_phase=g_accept_prompt || g_state==State::Offering || g_state==State::Accepted ||
        g_state==State::PreparingCircuit || g_state==State::Racing || g_state==State::Result || g_state==State::Departing;
    if(CurrentRules().Hosted() && active_phase && !g_opponent_horse_claimed) {
        const auto rider=HorseIdentity(g_opponent_rider),horse=HorseIdentity(g_opponent_horse);
        const bool rider_owned=(!g_opponent_damage[0].identity.Valid() || g_opponent_damage[0].identity==rider) &&
            (!g_mary.rider.Valid() || MaryIdentity(g_opponent_rider)==g_mary.rider);
        const bool horse_owned=!g_opponent_damage[1].identity.Valid() || g_opponent_damage[1].identity==horse;
        if(rider_owned && horse_owned && !InspectionPairClaimed(rider,horse)) {
            // Only the unaccepted preview is tied to the planned rank lease.
            // Racing and retired cards read the actual current horse attributes.
            const bool ready=g_state!=State::Offering || PlannedOpponentRanksReady();
            subjects.push_back({rider,horse,horse_catalog::current.Labels(g_fixed_selected_horse_name),
                FixedHostName(),g_accept_prompt,ready});
        }
    }
    for(const auto& e:g_extra_rivals)if((g_extra_grid_ready || g_state==State::Racing ||
            g_state==State::Result || g_state==State::Departing || e.retiring) && !ExtraHorseClaimed(e)) {
        subjects.push_back({e.rider,e.horse,horse_catalog::current.Labels(e.choice.name?e.choice.name:""),
            e.inspection_name,0,true});
    }
    for(const auto& old:g_retired_opponents)if(!old.claimed && !old.inspection_model.empty() &&
        !InspectionPairClaimed(old.rider,old.horse))
        subjects.push_back({old.rider,old.horse,horse_catalog::current.Labels(old.inspection_model),
            old.inspection_name,0,true});
    return subjects;
}

void SyncRaceHorseInspection()
{
    g_race_inspection.Sync(RaceInspectionSubjects(),HorseIdentity);
    g_inspect_prompt=g_race_inspection.PromptFor(g_opponent_rider);
}

void TickRaceHorseInspection()
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    const bool allowed=player && ENTITY::DOES_ENTITY_EXIST(player) && !ENTITY::IS_ENTITY_DEAD(player) &&
        !HUD::IS_PAUSE_MENU_ACTIVE() && !NETWORK::NETWORK_IS_SESSION_ACTIVE() &&
        !CAM::IS_SCREEN_FADED_OUT() && !CAM::IS_SCREEN_FADING_OUT() && !showcase::Active();
    // The camp shares the native card but retains its own interaction policy.
    // Drain that card even when its owner has already requested Close().
    if(!g_race_inspection.OwnsCard())g_horse_info_card.Tick(allowed && g_camp_inspecting,HorseIdentity);
    SyncRaceHorseInspection();
    const auto attempts=g_race_inspection.OpenAttempts();
    const bool was_open=g_race_inspection.OwnsCard() && g_horse_info_card.Active();
    g_race_inspection.Tick(g_horse_info_card,player,allowed && !g_camp_inspecting,HorseIdentity);
    if(g_race_inspection.OpenAttempts()!=attempts) {
        Log(std::string("native race horse info ")+(g_race_inspection.LastOpened()?"opened":"unavailable")+
            " horse="+std::to_string(g_race_inspection.OwnerHorse().horse)+
            (g_race_inspection.LastOpened()?"":" reason=")+g_horse_info_card.LastOpenReason());
        if(!g_race_inspection.LastOpened())ShowObjective(i18n::Text("Horse info unavailable right now. Please try again.",
            "暂时无法查看马匹属性，请稍后重试。"));
    }
    if(was_open && !g_horse_info_card.Active())Log(std::string("native race horse info closed: ")+g_race_inspection.LastCloseReason());
    if(g_camp_inspecting && !g_horse_info_card.Active() && !g_horse_info_card.Busy()) {
        g_camp_inspecting=false;
        Log(std::string("camp horse info closed: ")+g_horse_info_card.LastCloseReason());
    }
}

bool TryHandoffFixedHost(Vector3 position);

void TickFixedHost()
{
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!PlayerInSelectedSpawnBand() || !g_opponent_rider || !ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_rider) || !g_opponent_horse || !ENTITY::DOES_ENTITY_EXIST(g_opponent_horse) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_horse)) {
        const Vector3 position=player && ENTITY::DOES_ENTITY_EXIST(player)?ENTITY::GET_ENTITY_COORDS(player,true,false):Vector3{};
        Log("host cleanup detail player="+std::to_string(player)+" mount="+std::to_string(player?PED::GET_MOUNT(player):0)+
            " horse="+std::to_string(g_opponent_horse)+" horse_exists="+std::to_string(ENTITY::DOES_ENTITY_EXIST(g_opponent_horse))+
            " rider="+std::to_string(g_opponent_rider)+" rider_exists="+std::to_string(ENTITY::DOES_ENTITY_EXIST(g_opponent_rider))+
            " distance="+std::to_string(std::sqrt(DistanceSquared(position,g_variant->encounter_anchor))));
        AbortRace("station host unavailable or player left pickup area", false); return;
    }
    if(TryHandoffFixedHost(ENTITY::GET_ENTITY_COORDS(player,true,false)))return;
    const bool host_ready=MaintainFixedHostMount();
    HUD::_UI_PROMPT_SET_VISIBLE(g_accept_prompt,host_ready);
    HUD::_UI_PROMPT_SET_VISIBLE(g_decline_prompt,host_ready);
    if(host_ready)TickFixedGreeting(player);
    if (g_horse_plan_ready && !PlannedOpponentRanksReady()) {
        AbortRace("fixed horse changed since offer; refusing mismatched preview/price/race ranks",false);return;
    }
    // The host stays even when the visitor is on foot/changes horse; eligibility
    // is checked at the explicit choice, not latched on first entering the area.
    const bool mounted = PED::IS_PED_ON_MOUNT(player) && PED::GET_MOUNT(player) != 0 &&
        ENTITY::DOES_ENTITY_EXIST(PED::GET_MOUNT(player)) && !ENTITY::IS_ENTITY_DEAD(PED::GET_MOUNT(player));
    const bool breed_ok=!CurrentRules().breed || (mounted && field_race::EligiblePlayerModel(CurrentBreed(),ENTITY::GET_ENTITY_MODEL(PED::GET_MOUNT(player))));
    if(CurrentRules().breed && !breed_ok && MaryFriendlyFocus(player)) {
        const auto& breed=CurrentBreed();
        HUD::_UI_PROMPT_SET_TEXT(g_decline_prompt,i18n::Literal(i18n::Chinese()?
            "练习（必须骑乘"+std::string(breed.name_zh)+"参赛）":
            "Practice (requires "+std::string(breed.name)+")"));
    }
    else RefreshFixedPromptLabels();
    const bool ready = host_ready && mounted && breed_ok && PED::GET_MOUNT(g_opponent_rider) == g_opponent_horse &&
        !HUD::IS_PAUSE_MENU_ACTIVE() && g_fixed_wager.CanBegin() &&
        (CampChoicePending() ||
            DistanceSquared(ENTITY::GET_ENTITY_COORDS(player, true, false),
                ENTITY::GET_ENTITY_COORDS(g_opponent_rider, true, false)) <= 15.0F*15.0F);
    const bool can_pay = ready && wager::ValidCash(g_fixed_stake_cents) && wager::ValidCash(g_reward_cents) &&
        MONEY::_MONEY_GET_CASH_BALANCE() >= g_fixed_stake_cents;
    HUD::_UI_PROMPT_SET_ENABLED(g_accept_prompt, can_pay);
    HUD::_UI_PROMPT_SET_ENABLED(g_decline_prompt, ready);
    PED::SET_PED_RESET_FLAG(g_opponent_rider, 184, true);
    // A camp prompt is pressed before the stake exists - it is priced from the
    // horse the host draws, and no horse is drawn while the player is only
    // looking at Arthur. So the paid race is the one place the answer can turn
    // out to be unaffordable, and it is said out loud rather than left pending.
    if (g_camp_choice && !g_camp_practice && ready && !can_pay) {
        g_camp_choice = false;
        // Two different refusals, and the player is told which: a race that
        // carries no purse at these ranks, or a purse they cannot cover. The
        // first was reported as the second on 2026-09-17.
        const bool unpriced=!wager::ValidCash(g_fixed_stake_cents) || !wager::ValidCash(g_reward_cents);
        ShowObjective(unpriced?
            i18n::Text("There is no purse for this race. You can still ride for practice.","这场比赛没有奖金，你仍可以参加练习赛。"):
            i18n::Text("You don't have enough money to cover the wager.","你的钱不够支付这场比赛的赌注。"));
        AbortRace(unpriced?"camp race chosen but it is unpriced at these ranks":"camp race chosen without the cash for its entry fee", false);
        return;
    }
    if (!ready) return;
    // A race chosen at the camp is already answered: the player pressed at
    // Arthur, or at one of his horses, and this host is the same offer.
    const bool camp_race=g_camp_choice && !g_camp_practice && can_pay;
    const bool camp_practice=g_camp_choice && g_camp_practice && ready;
    const bool race_pressed = camp_race || (can_pay && HUD::_UI_PROMPT_IS_PRESSED(g_accept_prompt));
    const bool practice_pressed = !race_pressed && (camp_practice || HUD::_UI_PROMPT_IS_PRESSED(g_decline_prompt));
    if(camp_race || camp_practice)g_camp_choice=false;
    if (!race_pressed && !practice_pressed) return;
    g_fixed_practice = practice_pressed;
    if(!g_fixed_wager.Begin(practice_pressed ? 0 : g_fixed_stake_cents, practice_pressed,
        practice_pressed ? 0 : g_reward_cents))return;
    g_player_mount = PED::GET_MOUNT(player);
    Log(std::string("fixed explicit choice=") + (practice_pressed ? "practice" : "paid") +
        " speed=" + std::to_string(g_opponent_speed_level) + " agreed_stake_cents=" + std::to_string(g_fixed_wager.stake)+
        "; no cash debited at start");
    AUDIO::START_AUDIO_SCENE("horse_race_scene");
    StartRaceImmediately(); // Identical route, AI, finish rules and calibrated curve.
}

void TickOffering()
{
    if (CurrentRules().Hosted()) { TickFixedHost(); return; }
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (!InvitationActorsValid(player)) {
        AbortRace("player mount or opponent was lost during invitation", false);
        return;
    }
    if (DistanceSquared(ENTITY::GET_ENTITY_COORDS(player, true, false), g_variant->encounter_anchor) >
        g_offer_abandon_radius * g_offer_abandon_radius) {
        AbortRace("player left invitation area", false);
        return;
    }
    MaintainOriginalApproachTask(player, true);
    PED::SET_PED_RESET_FLAG(g_opponent_rider, 184, true);
    switch (g_offer_phase) {
    case 1:
        if (OfferLineFinished()) {
            if (!CreateOfferPrompts()) {
                ShowObjective(i18n::Text("The race is unavailable right now. Please try again later.", "现在暂时无法参加这场比赛。请稍后再试。"));
                AbortRace("offer interaction unavailable", false);
                return;
            }
            g_offer_phase = 2;
            g_offer_phase_started = Clock::now();
            Log("original-style invitation ready; lock on to choose accept or decline");
        }
        break;
    case 2:
        if (HUD::_UI_PROMPT_IS_PRESSED(g_accept_prompt)) {
            DestroyOfferPrompts();
            PlaySpeech(player, g_opponent_rider, g_rival->player_accept, nullptr);
            Log("player accepted invitation");
            SetState(State::Accepted);
        } else if (HUD::_UI_PROMPT_IS_PRESSED(g_decline_prompt)) {
            DestroyOfferPrompts();
            PlaySpeech(player, g_opponent_rider, g_rival->player_decline, nullptr);
            g_offer_phase = 0;
            Log("player declined invitation");
            SetState(State::Declined);
        } else {
            const double waiting_seconds =
                std::chrono::duration<double>(Clock::now() - g_offer_phase_started).count();
            if (g_waiting_line_index == 0 && waiting_seconds >= 7.0 && !SpeechPlaying(g_opponent_rider)) {
                PlaySpeech(g_opponent_rider, player, g_rival->waiting_a, nullptr);
                g_waiting_line_index = 1;
                g_offer_phase_started = Clock::now();
            } else if (g_waiting_line_index == 1 && waiting_seconds >= 2.0 &&
                       !SpeechPlaying(g_opponent_rider)) {
                PlaySpeech(g_opponent_rider, player, g_rival->waiting_b, nullptr);
                g_waiting_line_index = 2;
                g_offer_phase_started = Clock::now();
            } else if (g_waiting_line_index == 2 && waiting_seconds >= 3.0 &&
                       !SpeechPlaying(g_opponent_rider)) {
                DestroyOfferPrompts();
                PlaySpeech(g_opponent_rider, player, g_rival->outro, nullptr);
                g_offer_phase = 1;
                g_offer_phase_started = Clock::now();
                SetState(State::Declined);
            }
        }
        break;
    default:
        break;
    }
}

void TickAccepted()
{
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (g_player_mount == 0 || !ENTITY::DOES_ENTITY_EXIST(g_player_mount) || !PED::IS_PED_ON_MOUNT(player) ||
        PED::GET_MOUNT(player) != g_player_mount) {
        AbortRace("player changed mount after accepting", false);
        return;
    }
    if ((StateSeconds() >= 0.35 && !SpeechPlaying(player)) || StateSeconds() >= 1.75) {
        StartRaceImmediately();
    }
}

void TickDeclined()
{
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (g_offer_phase == 0 && ((StateSeconds() >= 0.5 && !SpeechPlaying(player)) || StateSeconds() >= 2.5)) {
        PlaySpeech(g_opponent_rider, player, g_rival->outro, nullptr);
        g_offer_phase = 1;
        g_offer_phase_started = Clock::now();
        return;
    }
    if (g_offer_phase == 1 && OfferLineFinished(4.0)) {
        StopRaceAudio(false);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_opponent_rider, false);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_opponent_horse, false);
        MoveInTraffic(g_opponent_rider, 1.25F, 8704);
        g_departure_task_started = true;
        SetState(State::Departing);
    }
}
