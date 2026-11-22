#pragma once
#include "race_circuit_start.h"
#include <span>

namespace horse_races::bookmaking {
// The spectator is not a mounted competitor, so the player-input/grid owner
// cannot be reused wholesale. Reuse its game-time timer, stock sounds and
// verified rear actions, without moving the already prepared exhibition grid.
class ExhibitionCountdown {
    struct Rear {Ped horse=0;int action=1,variant=0;double lead=0;bool played=false;};
    circuit_start::Timer timer_;
    std::vector<Rear> rears_;
    int last_digit_=-1;
    bool release_pending_=false,go_played_=false;
    double go_left_=0;
public:
    struct Step {int digit=3;double elapsed=0;bool release=false;};
    void Reset(){timer_={};rears_.clear();last_digit_=-1;release_pending_=go_played_=false;go_left_=0;}
    void Begin(std::span<const Ped> horses) {
        Reset();cues::PrepareAll();
        const auto& settings=cues::Settings();
        if(!settings.rear_enabled)return;
        const int lead_steps=static_cast<int>(std::clamp(settings.npc_rear_lead,0.0F,2.0F)*100);
        for(const auto horse:horses){
            const auto choice=cues::kVerifiedRears[static_cast<std::size_t>(MISC::GET_RANDOM_INT_IN_RANGE(0,4))];
            rears_.push_back({horse,settings.npc_rear_action_override?settings.npc_rear_action_override:choice[0],
                settings.npc_rear_action_override?settings.npc_rear_variant_override:choice[1],
                lead_steps?MISC::GET_RANDOM_INT_IN_RANGE(0,lead_steps+1)/100.0:0,false});
        }
    }
    Step Tick(double dt,bool visible,bool paused) {
        if(release_pending_)return {0,timer_.elapsed,false};
        const int digit=timer_.Step(dt,visible,paused);
        if(!visible || paused)return {digit,timer_.elapsed,false};
        if(digit && digit!=last_digit_){last_digit_=digit;cues::Tick();}
        for(auto& rear:rears_)if(!rear.played && 3-timer_.elapsed<=rear.lead){
            cues::Rear(rear.horse,rear.action,rear.variant);rear.played=true;
        }
        if(!digit){release_pending_=true;return {0,timer_.elapsed,true};}
        return {digit,timer_.elapsed,false};
    }
    bool Go() {
        if(!release_pending_ || go_played_)return false;
        cues::Go();go_played_=true;go_left_=.75;return true;
    }
    void DrawGo(double dt,bool visible,bool paused) {
        if(!visible || paused || go_left_<=0)return;
        countdown_hud::Draw(0,3,go_left_);
        if(std::isfinite(dt))go_left_-=std::clamp(dt,0.0,.1);
    }
};
} // namespace horse_races::bookmaking
