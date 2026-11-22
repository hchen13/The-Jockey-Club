// Ordinary exit remains available before GO and in every spectator camera.
// The same return path owns camera, mission, actors, player lock and money.
bool TickLeaveRace(){
    const bool started=phase==Phase::Racing;
    const bool preparing=phase==Phase::Loading || phase==Phase::Preparing || phase==Phase::Countdown;
    if(!started && !preparing)return false;
    const bool wagered=candidate_bet && ticket.Stake()>0;
    bool selected_finished=false,selected_out=false;int place=0;
    if(started && wagered){
        for(std::size_t i=0;i<jockeys.size() && i<field.Riders().size();++i)
            if(jockeys[i].profile->id==ticket.Selection()){
                selected_finished=field.Riders()[i].status==bm::Field::Status::Finished;
                selected_out=field.Riders()[i].status==bm::Field::Status::Out;
                place=field.Place(i);break;
            }
    }
    const auto decision=bm::SpectatorExit(started,wagered,selected_finished,selected_out,place);
    const char* label=decision.forfeits_wager?
        tr::Text("Leave Race (Forfeit Wager)","离开赛场（放弃赌注）"):
        tr::Text("Leave Race","离开赛场");
    const bool ready=Foreground() && PlayerOkay() && !HUD::IS_PAUSE_MENU_ACTIVE() &&
        horse_races::BookmakerReturnIsVisible() && camera_blackout.FullyVisible() && !rider_horse_info.Busy();
    NativeSpectatorPromptApi api;
    const Hash group=MISC::GET_HASH_KEY("TJC_SPECTATOR_CAMERA");
    bm::UpdateSpectatorLeavePrompt(api,leave_race_prompt,group,INPUT_CONTEXT_B,label,ready);
    if(ready && api.Valid(leave_race_prompt))api.ActivateGroup(group,tr::Text("SPECTATOR","观赛"));
    const bool held=PAD::IS_CONTROL_PRESSED(0,INPUT_CONTEXT_B) ||
        PAD::IS_DISABLED_CONTROL_PRESSED(0,INPUT_CONTEXT_B);
    const bool completed=ready && api.Valid(leave_race_prompt) &&
        HUD::_UI_PROMPT_HAS_HOLD_MODE_COMPLETED(leave_race_prompt);
    if(!leave_race_hold.Tick(ready,held,completed,Now()))return false;
    // This is voluntary departure, never sabotage or a technical refund.
    // A selection that already finished retains its earned place and payout.
    ReturnFromRace(decision.finish,decision.place,started?"spectator left race":"spectator cancelled before start");
    return true;
}
