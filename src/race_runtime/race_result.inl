// Private race result implementation; included by race_system.cpp.

// Once the race is running it ends by a finish, a corridor disqualification, an
// abandonment, the player's own hand, or the one thing that is nobody's fault
// but cannot go on - the game itself taking over. A guard that finds any other
// problem after the flag has dropped reports it; it does not cancel a race the
// player has spent minutes riding. Three evenings of "race cancelled" came from
// preconditions that were right before the start keeping their veto after it,
// so this is the structural form of the rule: a new AbortRace caller cannot
// break it by accident.
enum class Ender {
    Precondition,   // A guard that found a problem. May refuse to START a race; may never end one.
    Player,         // F10, or the player breaking the race (taking the rival's horse).
    Game,           // The game took over: another mission, a lost session, a participant despawned.
    Clock,          // Twenty minutes of GAME time, not wall time.
    Course,         // A mandatory recorded passage has no legal executable line.
};
// The well-known reasons classify themselves so the many existing callers and
// fixtures keep their meaning; anything else is a precondition unless the
// caller says otherwise.
Ender EnderFor(const std::string& reason,Ender declared) {
    if(reason=="manual abort" || reason=="rival horse taken by player" || reason=="additional rival horse taken by player" ||
        reason=="player/session changed or rival horse taken")return Ender::Player;
    if(reason=="fixed race interrupted by another mission/session" ||
        reason=="opponent entity unavailable or identity changed; not an accident win")return Ender::Game;
    if(reason=="fixed station race timed out")return Ender::Clock;
    return declared;
}
bool MayEndRunningRace(const std::string& reason,Ender declared=Ender::Precondition) {
    return EnderFor(reason,declared)!=Ender::Precondition;
}
void AbortRace(const std::string& reason, bool notify, Ender ender=Ender::Precondition)
{
    if(g_state==State::Racing && !MayEndRunningRace(reason,ender)) {
        static std::string last;   // Once per distinct problem, not once per frame.
        if(last!=reason){last=reason;Log("running race, NOT cancelled: "+reason);}
        return;
    }
    horse_diagnostics::Stage("abort: stop navigation");
    horse_ledger::runtime::CancelRace();
    Log("abort requested: "+reason+" notify="+std::to_string(notify));
    // A camp race that falls over before the starting scene takes the screen
    // must give it back, or the player is left sitting in the dark.
    g_camp_choice=false;g_camp_horse=0;g_camp_moved=false;g_camp_mount_tasked=false;
    if(g_camp_fade){CAM::DO_SCREEN_FADE_IN(500);g_camp_fade=false;Log("camp race: blackout released");}
    StopNavigation();
    horse_diagnostics::Stage("abort: restore ranks and mission flag");
    ReleaseMissionFlag();
    horse_diagnostics::Stage("abort: stop race audio");
    StopRaceAudio(true);
    horse_diagnostics::Stage("abort: settle wager");
    // F10 and F11 share "manual abort": cancel the open wager without charging.
    // Stakes are settled after the race, so cancellation needs no refund.
    SettleFixedWager(reason == "rival horse taken by player" ? wager::Outcome::Loss : wager::Outcome::Void);
    Log("aborted: " + reason);
    if (notify) {
        horse_diagnostics::Stage("abort: cancellation objective");
        if(reason=="manual abort")
            ShowObjective(i18n::Text("The race has been called off.", "比赛已取消。"));
        else if(reason.starts_with("circuit isolation audit failed:"))
            ShowObjective(i18n::Text("The track is not ready. The race has been called off. No wager was taken. Please try again.", "赛道尚未准备妥当，比赛已取消，未收取赌注。请稍后再试。"));
        else if(reason.starts_with("circuit preparation failed:"))
            ShowObjective(i18n::Text("The course is not ready. The race has been called off. No wager was taken. Please try again.", "赛道尚未准备妥当，比赛已取消，未收取赌注。请稍后再试。"));
        else if(reason.starts_with("circuit starting grid failed:"))
            ShowObjective(i18n::Text("The starting positions are not ready. The race has been called off. No wager was taken. Please try again.", "起跑位置尚未准备妥当，比赛已取消，未收取赌注。请稍后再试。"));
        else ShowObjective(i18n::Text("The race has been called off. Please try again.", "比赛已取消。请稍后再试。"));
    }
    horse_diagnostics::Stage("abort: retire actors and reset");
    ReturnToReady();
    horse_diagnostics::Stage("abort: complete");
    Log("abort cleanup complete");
}

std::string RewardLabel()
{
    std::ostringstream value;
    value << '$' << (g_reward_cents / 100) << '.' << std::setfill('0') << std::setw(2)
          << (g_reward_cents % 100);
    return value.str();
}

bool QueueRaceVictory()
{
    if((CurrentRules().Hosted() && g_fixed_practice) || GetAutoSurveyState().running)return false;
    if(g_victory_toast_queued)return true;
    const auto* station=StationOf(g_variant);
    auto text=progression::Victory(station?progression::Find(station->key):std::nullopt,i18n::Chinese());
    g_victory_toast_queued=progression_toast::Show(std::move(text.title),std::move(text.body),text.duration_ms);
    if(g_victory_toast_queued)Log("race victory toast queued station="+
        (station?std::string(station->key):"encounter")+" duration_ms=4000");
    return g_victory_toast_queued;
}

void GrantRaceReward()
{
    if (CurrentRules().wagered) { SettleFixedWager(wager::Outcome::Win); return; }
    if (g_reward_granted) {
        return;
    }
    const bool queued = g_fixed_wager.QueueRewardOnly(g_reward_cents);
    g_reward_granted = queued;
    if(queued){
        g_cash_timeline = g_race_cash_timeline;
        horse_ledger::runtime::PaymentQueued(g_cash_timeline);
        g_cash_retry={};FlushFixedCredit();
    }
    Log(std::string("encounter reward ") + (queued ? "queued " : "failed to queue ") + RewardLabel() +
        " for difficulty=" + kDifficultyTiers[static_cast<std::size_t>(g_difficulty_tier)].name);
}

void FinishRace(bool player_won)
{
    horse_ledger::runtime::Outcome(player_won);
    // A lost open race on a recorded line leaves the player still riding to
    // the finish for the result scene: keep the line and the finish marker as
    // guidance until they get there or the host departs. The winner's line and
    // every circuit (whose guidance is the lap engine's) go now, as before.
    const auto& rules=CurrentRules();
    const bool keep_guidance=!player_won && rules.has_recorded_line && !rules.uses_lap_engine &&
        HasRecordedCourse() && g_course_gps.ready && !PlayerAtFinish();
    StopNavigation(keep_guidance);
    if(keep_guidance) {
        g_guidance_retained=true;
        Log("recorded-course guidance retained for the player's finish approach; released at the finish area or departure");
    }
    ReleaseMissionFlag();
    StopRaceAudio(true);
    g_player_won = player_won;
    g_result_phase = ResultPhase::Arrival;
    g_result_started_at = Clock::time_point{};
    g_result_next_log = Clock::time_point{};
    g_result_scripted_line_attempted = false;
    g_result_scripted_line_at = Clock::time_point{};
    g_result_interaction_registered = false;
    g_result_interaction_positive = false;
    g_result_response_started = false;
    // Queue the actual race result before settlement emits newly available
    // races. Repeat wins still celebrate; F11 progress edits never call here.
    const bool victory_toast=player_won && QueueRaceVictory();
    if (player_won) {
        GrantRaceReward();
        if(!victory_toast)ShowLocalizedObjective("HORSE_RACE_WON");
    } else {
        SettleFixedWager(wager::Outcome::Loss);
        ShowLocalizedObjective("HORSE_RACE_LOST");
    }
    Log(player_won ? "result: player won" : "result: opponent won");
    if (CurrentRules().wagered && !victory_toast) {
        ShowObjective(g_fixed_practice ? i18n::Text(player_won ? "You won the practice race. No wager was made." : "You lost the practice race. No wager was made.",
            player_won ? "你赢下了练习赛，本场没有下注。" : "你输掉了练习赛，本场没有下注。") :
            std::string(i18n::Text(player_won ? "You won the race and earned $" : "You lost the race. Your stake of $",
                player_won ? "你赢得比赛，净赢 $" : "你输掉比赛，损失赌注 $")) +
                    std::to_string((player_won?g_reward_cents:g_fixed_stake_cents) / 100) +
                    i18n::Text(player_won ? "." : " was lost.", "。"));
    }
    PlaySpeech(PLAYER::PLAYER_PED_ID(), g_opponent_rider,
        player_won ? g_rival->player_win_reaction : g_rival->player_lose_reaction, nullptr);
    SetState(State::Result);
}

void WinRaceForOpponentAccident(const std::string& reason)
{
    if (g_state != State::Racing) return; // A settled accident cannot pay twice.
    horse_ledger::runtime::Outcome(true, true);
    RemoveBlip(g_opponent_blip);
    StopNavigation();
    ReleaseMissionFlag();
    StopRaceAudio(true);
    g_player_won = true;
    const bool victory_toast=QueueRaceVictory();
    GrantRaceReward();
    if(!victory_toast)ShowLocalizedObjective("HORSE_RACE_WON");
    if (CurrentRules().wagered && !victory_toast) {
        ShowObjective(g_fixed_practice ?
            i18n::Text("Your rival is out. You won the practice race. No wager was made.", "对手退出比赛，你赢了练习赛。本场没有下注。") :
            std::string(i18n::Text("Your rival is out. You won $", "对手退出比赛，你净赢 $")) +
                std::to_string(g_reward_cents / 100) + i18n::Text(".", "。"));
    }
    Log("result: player won after opponent accident: " + reason);
    // No finish-line scene, celebration, remount-to-resume or post-race Talk
    // for an accident. ReleaseMissionFlag already restores temporary ranks.
    g_result_phase = ResultPhase::Interrupted;
    SetState(State::Result);
}

void EndRaceWithoutWinner(const char* objective_key, const std::string& reason)
{
    horse_ledger::runtime::CancelRace();
    g_player_won = false;
    RemoveBlip(g_opponent_blip);
    StopNavigation();
    ReleaseMissionFlag();
    StopRaceAudio(true);
    SettleFixedWager(wager::Outcome::Loss);
    ShowLocalizedObjective(objective_key);
    Log("result: " + reason);
    g_result_phase = ResultPhase::Interrupted;
    SetState(State::Result);
}

void FinishCircuitDisqualification(circuit::Outcome outcome)
{
    if(g_state!=State::Racing)return;
    const bool won=outcome==circuit::Outcome::PlayerWon,tied=outcome==circuit::Outcome::Tie;
    // Name the actual offence: a wide corner leaves the corridor, a shortcut
    // stays on tarmac but abandons the ordered line of the lap being ridden.
    const auto offence=[](const circuit::Progress& p) {
        return p.off_course?(p.section_jump?" (skipped section at "+std::to_string(p.offence_along)+
            " m)":" (left corridor at "+std::to_string(p.offence_along)+" m)"):std::string();
    };
    Log("circuit disqualification: player="+std::to_string(g_laps.player.off_course)+offence(g_laps.player)+
        " opponent="+std::to_string(g_laps.opponent.off_course)+offence(g_laps.opponent)+
        " half_width_m="+std::to_string(g_laps.plan.course.strict_half_width)+
        " result="+(tied?std::string("void"):(won?std::string("player won"):std::string("player lost"))));
    RemoveBlip(g_opponent_blip);StopNavigation();ReleaseMissionFlag();StopRaceAudio(true);
    g_player_won=won;
    horse_ledger::runtime::Outcome(won);
    if(won)QueueRaceVictory();
    SettleFixedWager(tied?wager::Outcome::Void:(won?wager::Outcome::Win:wager::Outcome::Loss));
    // Keep the objective about what happened. Settlement already displays the
    // actual cash change through the native transaction feed.
    if(tied) {
        ShowObjective(i18n::Text("Both riders were disqualified. The race was called off.","双方均被取消资格，比赛就此作罢。"));
    } else if(won) {
        ShowObjective(g_laps.opponent.section_jump ?
            i18n::Text("Your rival missed part of the course. You won the race.","对手未按赛道行进，你赢得了比赛。") :
            i18n::Text("Your rival left the course. You won the race.","对手偏离赛道，你赢得了比赛。"));
    } else {
        ShowObjective(g_laps.player.section_jump ?
            i18n::Text("You missed part of the course and were disqualified.","你未按赛道行进，已被取消资格。") :
            i18n::Text("You left the course and lost the race.","你偏离赛道，输掉了比赛。"));
    }
    // Early infractions have no finish-line arrival scene. Ordinary protected
    // departure/road restoration still runs with the wager already closed.
    g_result_phase=ResultPhase::Interrupted;SetState(State::Result);
}
