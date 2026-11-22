#pragma once
#include "race_bookmaking.h"
#include <cmath>

namespace horse_races::bookmaking {
struct SpectatorExitDecision {
    Finish finish=Finish::Lost;
    int place=0;
    bool forfeits_wager=false;
};
inline SpectatorExitDecision SpectatorExit(bool started,bool wagered,bool selected_finished,
    bool selected_out,int place){
    if(!started)return {Finish::AccidentalVoid,0,false};
    if(wagered && selected_finished && place>0)return {Finish::Placed,place,false};
    return {Finish::Lost,0,wagered && !selected_out};
}
// Use the native hold ring, but require a fresh release and a complete hold
// after a pause, card or fade. A stale native completion cannot abandon a bet.
struct SpectatorLeaveHold {
    bool armed=false,completed_seen=false;
    double began=-1;
    void Reset(){*this={};}
    bool Tick(bool ready,bool held,bool native_completed,double now){
        if(!ready || !std::isfinite(now) || (began>=0 && now<began)){Reset();return false;}
        if(!held){armed=true;began=-1;completed_seen=false;return false;}
        if(!armed)return false;
        if(began<0)began=now;
        completed_seen|=native_completed;
        if(completed_seen && now-began>=1.0){Reset();return true;}
        return false;
    }
};
} // namespace horse_races::bookmaking
