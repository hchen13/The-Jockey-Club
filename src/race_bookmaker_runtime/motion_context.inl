// Private motion snapshots; included in the bookmaker runtime namespace.
MotionContext MainMotion(){
    MotionContext c;
    if(phase!=Phase::Racing || jockeys.empty())return c;
    const auto& j=jockeys.front();
    c.horse=j.horse;c.rider=j.rider;c.present=Same(j.horse_id)&&Same(j.rider_id);
    c.racing=true;c.town_speed_exempt=true;c.race_token=token;c.npc_braking=j.command.braking;
    c.eliminated=field.Riders().size()>0 && field.Riders()[0].status!=bm::Field::Status::Racing;
    c.host_level_bonus=bm::kBurstLevels;c.host_boost_seconds=static_cast<float>(bm::kBurstDurationSeconds);
    c.host_boost_eligible=!j.command.braking; // Gate itself measures the full three-second hold.
    c.npc_override_intent=c.present && !c.eliminated &&
        horse_motion::OwnedRaceOverrideIntent(j.follower,j.navigation,j.command,j.rider);
    c.real_burst_display_eligible=true; // Every exhibition jockey retains the original earned burst.
    c.catchup_engaged=c.present&&!c.eliminated&&j.chase.Active();
    c.catchup_active=c.catchup_engaged&&(c.npc_override_intent||(!j.command.braking&&!j.command.NativePaceLimited()));
    c.catchup_fraction=c.catchup_active?j.chase.fraction:0;
    c.catchup_gap_valid=c.present&&!c.eliminated&&j.chase.gap_valid;
    c.catchup_gap_m=c.catchup_gap_valid?j.chase.gap:0;
    return c;
}
std::vector<MotionContext> ExtraMotion(){
    std::vector<MotionContext> result;
    if(phase!=Phase::Racing)return result;
    const auto main=MainMotion();
    for(std::size_t i=1;i<jockeys.size();++i){
        const auto& j=jockeys[i];auto c=main;c.horse=j.horse;c.rider=j.rider;
        c.present=Same(j.horse_id)&&Same(j.rider_id);c.npc_braking=j.command.braking;
        c.eliminated=field.Riders().size()>i && field.Riders()[i].status!=bm::Field::Status::Racing;
        c.host_boost_eligible=!j.command.braking;
        c.npc_override_intent=c.present && !c.eliminated &&
            horse_motion::OwnedRaceOverrideIntent(j.follower,j.navigation,j.command,j.rider);
        c.catchup_engaged=c.present&&!c.eliminated&&j.chase.Active();
        c.catchup_active=c.catchup_engaged&&(c.npc_override_intent||(!j.command.braking&&!j.command.NativePaceLimited()));
        c.catchup_fraction=c.catchup_active?j.chase.fraction:0;
        c.catchup_gap_valid=c.present&&!c.eliminated&&j.chase.gap_valid;
        c.catchup_gap_m=c.catchup_gap_valid?j.chase.gap:0;
        result.push_back(c);
    }
    return result;
}
