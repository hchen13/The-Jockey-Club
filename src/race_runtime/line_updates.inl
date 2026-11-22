// Private line updates implementation; included by race_system.cpp.
// Live rebuild: the corridor width (or the percent) changed under a running
// race, so every line is recomputed for the width now in force and every
// native driver that is live is restarted on it through the SAME paths a
// creep recovery / an extra's restart use - only the counters differ, so a
// debug width change can never exhaust a rider's recovery budget. A driver
// already on the traffic fallback gets its plan rebuilt and is left there.
// One line takes about 80 ms on a five-kilometre surveyed course, and a rebuild wants the host's
// and one per jockey: together that was a frame of 609 ms, taken as the player crossed the line to
// start a lap (measured 2026-09-22). So a rebuild is a queue, and one line is taken from it per
// frame. A rider whose turn has not come keeps riding the line it already had - which is the line it
// was riding anyway - so nothing is worse for the wait, and the frame is 80 ms instead of 609.
struct LineRebuild {
    struct Inputs {
        std::uint64_t revision=0;
        int percent=0;
        float width=0,margin=0;
        bool required=false,required_refused=false;
        bool operator==(const Inputs&) const=default;
    };
    std::string why;
    bool host=false;
    // Initial host first; later dirty riders, including a dirty host, append
    // here. New evidence must not restart the queue ahead of waiting riders.
    std::vector<Ped> riders;
    std::vector<std::pair<Ped,Inputs>> completed;
    int rebuilt=0,restarted=0,fallen=0;
    const char* host_result="none";
    bool Busy() const {return host || !riders.empty();}
    std::uint64_t race_epoch=0;   // Native route serials change during this same race.
    static Inputs CurrentInputs() {
        return {g_obstacles.revision,g_racing_line_percent,
            g_laps.Frozen()?g_laps.plan.course.strict_half_width:g_recorded_line_half_width,
            g_racing_line_margin,RequiredRecordedLine(),g_line_pins_required_refused};
    }
    bool Append(Ped rider,const Inputs& input) {
        if((host && rider==g_opponent_rider) || std::find(riders.begin(),riders.end(),rider)!=riders.end())return false;
        for(const auto& done:completed)if(done.first==rider && done.second==input)return false;
        riders.push_back(rider);return true;
    }
    void Complete(Ped rider,const Inputs& input) {
        for(auto& done:completed)if(done.first==rider){done.second=input;return;}
        completed.push_back({rider,input});
    }
} g_line_rebuild;
void RebuildRacingLines(const char* why)
{
    if(!CurrentRules().Hosted() || !HasRecordedCourse())return;
    if(g_state!=State::Racing) {
        if(g_start_lines.active) {
            // Preserve the race's speed/terrain settings. Only the existing
            // geometry builds restart, including any partially built field.
            g_start_lines.ready=false;g_start_lines.next=0;g_start_lines.host_failed=false;
            g_start_lines.percent=g_racing_line_percent;
            Log("starting lines invalidated reason="+std::string(why?why:"")+"; rebuilding one rider per frame before GO");
        }
        return;
    }
    const bool merging=g_line_rebuild.Busy() && g_line_rebuild.race_epoch==g_race_epoch;
    const auto input=LineRebuild::CurrentInputs();
    bool added=false;
    if(!merging) {
        g_line_rebuild={};g_line_rebuild.host=true;g_line_rebuild.race_epoch=g_race_epoch;
    } else added=g_line_rebuild.Append(g_opponent_rider,input);
    g_line_rebuild.why=why?why:"";
    if(CurrentRules().Field())for(const auto& e:g_extra_rivals)
        if(!e.out && !e.retiring && e.horse.Valid() && e.rider.Valid())
            added=g_line_rebuild.Append(e.rider.horse,input)||added;
    if(!merging || added)Log(std::string("racing lines ")+(merging?"merged":"queued")+
        " for rebuild reason="+g_line_rebuild.why+": pending="+
        std::to_string(g_line_rebuild.riders.size()+(g_line_rebuild.host?1:0))+", one a frame");
}
// Called every racing frame; does nothing at all unless a rebuild is queued.
void TickLineRebuild()
{
    if(!g_line_rebuild.Busy())return;
    if(g_state!=State::Racing){g_line_rebuild={};return;}
    // A rebuild can be queued near race end: the
    // host is taken on one frame and the race is over on the next, leaving the jockeys queued. The
    // engine recycles ped handles, so running that queue in the next race stops and restarts a live
    // rider nobody asked it to touch.
    if(g_line_rebuild.race_epoch!=g_race_epoch) {
        Log("racing lines queued for rebuild reason="+g_line_rebuild.why+" belonged to the race before this one; dropped");
        g_line_rebuild={};
        return;
    }
    const double now=CircuitNow();
    bool host_turn=g_line_rebuild.host;
    Ped rider=g_opponent_rider;
    if(host_turn)g_line_rebuild.host=false;
    else {
        rider=g_line_rebuild.riders.front();g_line_rebuild.riders.erase(g_line_rebuild.riders.begin());
        host_turn=rider==g_opponent_rider;
    }
    // A pending turn always consumes the latest evidence. A duplicate request
    // cannot repeat a rider already completed for these same frozen inputs.
    g_line_rebuild.Complete(rider,LineRebuild::CurrentInputs());
    if(host_turn) {
        if(g_competitive_follower.control || RequiredRecordedLine()) {
            g_continuous_attempted=false;
            if(!StartOriginalOpponentRoute(true)){g_line_rebuild={};return;}
            g_line_rebuild.host_result=g_competitive_follower.control?"restarted":RequiredRecordedLine()?"retry-pending":"fallback";
        } else if(BuildHostRuntimeLine(ReadRacePaceDynamics()))g_line_rebuild.host_result="replanned";
        else {
            RetireHostLine();g_line_rebuild.host_result="retired";
            g_continuous_attempted=true;
            if(!StartOriginalOpponentRoute())return;
            Log("host line rebuild refused; previous custom plan retired, bounded traffic owner selected");
        }
        if(g_line_rebuild.Busy())return;   // A one-on-one has no jockeys: the host WAS the queue.
    } else {
    for(auto& e:g_extra_rivals) {
        if(e.rider.horse!=rider || e.out || e.retiring || !e.horse.Valid() || !e.rider.Valid())continue;
        if(!BuildEntrantLine(e)) {
            RetireEntrantLine(e);
            if(RequiredRecordedLine()) {
                AbortRace("required recorded passage has no legal entrant line; no wager charged",true,Ender::Course);
                g_line_rebuild={};return;
            }
            Log("entrant line rebuild refused; previous custom plan retired rider="+std::to_string(rider));
            break;
        }
        ++g_line_rebuild.rebuilt;++e.line_rebuilds;
        if(!e.follower.control && !RequiredRecordedLine())break;
        e.follower.StopControl();
        if(StartExtraDriver(e,e.proxy.current.along,now))++g_line_rebuild.restarted;
        else {++g_line_rebuild.fallen;Log("extra native driver not restarted after line rebuild rider="+std::to_string(rider)+
            " along="+std::to_string(e.proxy.current.along)+(RequiredRecordedLine()?"; recording retry pending":"; traffic fallback"));}
        break;
    }
    }
    if(g_line_rebuild.Busy())return;
    Log("racing lines rebuilt reason="+g_line_rebuild.why+" half_width_m="+std::to_string(g_laps.plan.course.strict_half_width)+
        " percent="+std::to_string(g_racing_line_percent)+" host="+g_line_rebuild.host_result+RacingLineSummary(g_racing_line,HostLineAggression())+
        " entrants_rebuilt="+std::to_string(g_line_rebuild.rebuilt)+" restarted="+std::to_string(g_line_rebuild.restarted)+
        (RequiredRecordedLine()?" recording_retries_pending=":" fallback=")+std::to_string(g_line_rebuild.fallen));
    g_line_rebuild={};
}
void SetRacingLinePercent(int percent)
{
    percent=std::clamp(percent,0,100);
    if(percent==g_racing_line_percent)return;
    g_racing_line_percent=percent;
    RebuildRacingLines("racing line percent changed");
}
int GetRacingLinePercent(){return g_racing_line_percent;}
