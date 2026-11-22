#pragma once
#include "race_pace_plan.h"
#include "horse_speed_learning.h"
#include "horse_speed_limits.h"

namespace horse_races::race_pace {
inline constexpr float kMaxRequestedMps=horse_motion::speed_limits::kRequestedMps;
// Shared prediction of the requested pace, not a physical velocity guarantee.
// Eligibility and native execution remain with the caller.
inline float RequestedTarget(const Dynamics& dynamics,const Command& command,int displayed_speed,
    float earned_bonus=0,float learned_base=0,std::optional<float> gap={}) {
    if(command.braking||command.NativePaceLimited())return std::isfinite(command.target_mps)?
        std::clamp(command.target_mps,0.F,horse_motion::speed_limits::kNativeMps):0;
    const float base=std::isfinite(learned_base)&&learned_base>=3&&learned_base<=25?
        learned_base:dynamics.top_mps;
    float target=command.target_mps>=dynamics.top_mps-.25F?
        base*std::min(1.F,command.target_mps/dynamics.top_mps):std::min(base,command.target_mps);
    (void)gap; // Gap is diagnostic; only the owner-latched qualified fraction controls pace.
    float extra=0;
    if(command.target_mps>=dynamics.top_mps-.25F&&!command.braking&&!command.NativePaceLimited()&&
       std::isfinite(command.catchup_fraction))
        extra=base*std::clamp(command.catchup_fraction,0.F,horse_motion::speed_learning::kCatchupLimit);
    if(command.target_mps>=dynamics.top_mps-.25F&&std::isfinite(earned_bonus)&&earned_bonus>0) {
        const float here=ForAttributes(displayed_speed,10).top_mps;
        const float neighbour=ForAttributes(displayed_speed==10?9:displayed_speed+1,10).top_mps;
        extra=std::max(extra,std::abs(neighbour-here)*std::min(earned_bonus,2.F));
    }
    return std::clamp(target+extra,0.F,kMaxRequestedMps);
}
// Lateral decisions need the free pace INCLUDING the user's fixed near +1
// pursuit level. This is an intention estimate only; it never applies force
// or replaces the actual near/far controllers.
inline float PassingTarget(const Plan& plan,const Command& command,int displayed_speed,float earned_bonus=0) {
    auto free=command;free.catchup_fraction=0;
    const float bonus=std::max(command.catchup_fraction>0?1.F:0.F,earned_bonus);
    return RequestedTarget(plan.dynamics,free,displayed_speed,bonus);
}

}
