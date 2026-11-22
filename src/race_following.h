#pragma once
#include "race_pace_plan.h"
#include <span>

namespace horse_races::following {
struct Mount {
    int horse=0;
    Vector3 position{},forward{};
    float along=0,speed=0;
    float route_height=0;
    bool route_height_valid=false;
};
struct Limit {
    int horse=0;float gap=0,target=0;
    float lead_mps=race_pace::kUnobserved,closing_mps=race_pace::kUnobserved,lateral_m=race_pace::kUnobserved;
    float self_mps=race_pace::kUnobserved;
};
// Body-centre clearances, not a lane reservation. These are control priors,
// not measured guarantees about native collision shapes or braking response.
inline constexpr float kLateralClearance=1.4F,kBodyGap=3.5F;
inline constexpr float kResponseSeconds=.4F,kRelativeDeceleration=3.F;
inline constexpr float kSpeedTolerance=.1F;
// Only an actual horse ahead in the same lane can constrain an NPC. Course
// order rejects crossings; world lane/height and heading reject parallel
// paths, bridges and opposing traffic. No route or position is modified.
inline Limit Ahead(const Mount& self,std::span<const Mount> roster,float desired,float loop_length=0) {
    Limit result;
    if(!self.horse || !destination::Finite(self.position) || !destination::Finite(self.forward) ||
        !std::isfinite(self.speed) || self.speed<0 || !std::isfinite(self.along) ||
        !std::isfinite(desired) || desired<=0)return result;
    const float length=std::hypot(self.forward.x,self.forward.y);
    if(length<.5F)return result;
    const float fx=self.forward.x/length,fy=self.forward.y/length;
    for(const auto& lead:roster) {
        if(!lead.horse || lead.horse==self.horse || !destination::Finite(lead.position) ||
            !destination::Finite(lead.forward) || !std::isfinite(lead.along) ||
            !std::isfinite(lead.speed) || lead.speed<0)continue;
        float order_gap=lead.along-self.along;
        if(order_gap<0 && std::isfinite(loop_length) && loop_length>0)order_gap+=loop_length;
        if(order_gap<-.5F || order_gap>65)continue;
        // Compare height residuals about the recorded route, not raw height
        // difference: a legitimate steep descent changes Z by many metres
        // across the anticipation gap. Bridges/parallel levels still differ.
        const bool heights=self.route_height_valid && lead.route_height_valid &&
            std::isfinite(self.route_height) && std::isfinite(lead.route_height);
        const float height_delta=lead.position.z-self.position.z-
            (heights?lead.route_height-self.route_height:0);
        if(std::abs(height_delta)>3.5F)continue;
        const float heading=std::hypot(lead.forward.x,lead.forward.y);
        if(heading<.5F)continue;
        const float alignment=(fx*lead.forward.x+fy*lead.forward.y)/heading;
        if(alignment<.65F)continue;
        const float dx=lead.position.x-self.position.x,dy=lead.position.y-self.position.y;
        const float gap=dx*fx+dy*fy,signed_lateral=dx*fy-dy*fx,lateral=std::abs(signed_lateral);
        if(gap<=0 || gap>65)continue;
        // A diagonal rider can have equal scalar speed while closing our
        // longitudinal clearance. Resolve both motion components in our frame.
        const float lead_speed=lead.speed*alignment;
        const float closing=self.speed-lead_speed;
        // Only observed closing needs this EXTRA brake. Equal-speed/opening
        // gaps keep native acceleration, including the 4.2 m starting rows.
        if(closing<=.75F)continue;
        // Heading * measured speed is only a short-horizon motion estimate,
        // not a prediction of a pass through a rider's current world point.
        // Include an imminent cut-in, and release a separating neighbour if
        // it clears our body width before longitudinal contact could begin.
        const float lateral_speed=lead.speed*(lead.forward.x*fy-lead.forward.y*fx)/heading;
        const float response_lateral=signed_lateral+lateral_speed*kResponseSeconds;
        if(lateral>kLateralClearance) {
            if(signed_lateral*response_lateral>0 &&
                std::abs(response_lateral)>kLateralClearance)continue;
        } else if(signed_lateral*lateral_speed>0) {
            const float contact_time=std::clamp((gap-kBodyGap)/closing,0.F,kResponseSeconds);
            if(std::abs(signed_lateral+lateral_speed*contact_time)>kLateralClearance)continue;
        }
        // Stop the RELATIVE closing speed over the remaining body clearance:
        // v * response + v^2 / (2 * deceleration) <= gap - body.
        // The old headway formula requested less than the leader's speed
        // well before contact, opening a new gap every time a pass began.
        const float available=gap-kBodyGap;
        const float response_speed=kRelativeDeceleration*kResponseSeconds;
        const float allowed_closing=std::sqrt(response_speed*response_speed+
            2.F*kRelativeDeceleration*std::max(0.F,available))-response_speed;
        // Once the body buffer is already consumed, withdraw below lead pace
        // to separate. This is an emergency request, not a collision proof.
        const float separation=std::max(0.F,-available)/kResponseSeconds;
        const float target=std::max(1.5F,lead_speed+allowed_closing-separation);
        if(target>=desired-.25F || (result.horse && target>=result.target))continue;
        result={lead.horse,gap,target};result.self_mps=self.speed;
        if(race_pace::diagnostics_enabled){result.lead_mps=lead_speed;result.closing_mps=closing;result.lateral_m=lateral;}
    }
    return result;
}
inline void Apply(race_pace::Command& command,const Limit& limit) {
    race_pace::Trace* observation=race_pace::diagnostics_enabled?&command.trace:nullptr;
    if(observation) {
        observation->following_evaluated=true;observation->following_applied=false;
        observation->following_horse=limit.horse;
        observation->following_input_target_mps=command.target_mps;
        observation->following_input_effort=command.effort;observation->following_input_braking=command.braking;
        observation->following_cap_mps=limit.horse?limit.target:race_pace::kUnobserved;
        observation->following_gap_m=limit.horse?limit.gap:race_pace::kUnobserved;
        observation->following_lead_mps=limit.lead_mps;
        observation->following_closing_mps=limit.closing_mps;observation->following_lateral_m=limit.lateral_m;
        observation->following_target_mps=command.target_mps;
        observation->following_effort=command.effort;observation->following_braking=command.braking;
    }
    if(!limit.horse || !std::isfinite(limit.target) || limit.target<=0 ||
        limit.target>=command.target_mps)return;
    const bool overspeed=!std::isfinite(limit.self_mps) || limit.target<limit.self_mps-kSpeedTolerance;
    // A forecast below free pace but ABOVE measured speed is not a brake.
    // Even with braking=false, publishing following_limited selected absolute
    // native speed, retired momentum and removed pursuit in every downstream
    // driver. Let the native race task close/pass until observed closing really
    // needs intervention. Keep recording the forecast for diagnosis.
    if(!overspeed)return;
    command.following_limited=true;command.following_horse=limit.horse;command.following_gap_m=limit.gap;
    command.target_mps=limit.target;command.braking=true;
    // Playback receives the absolute ceiling; the fallback only accepts gait.
    // Reduce that gait in proportion to ACTUAL overspeed. A slower requested
    // ceiling is not itself a reason to turn a controlled approach into a
    // trot. Extra force is withheld once actual overspeed needs braking.
    if(overspeed && std::isfinite(limit.self_mps) && limit.self_mps>limit.target)
        command.effort=std::min(command.effort,
            std::clamp(3.F*limit.target/limit.self_mps,1.F,3.F));
    if(observation) {
        observation->following_applied=true;observation->following_target_mps=command.target_mps;
        observation->following_effort=command.effort;observation->following_braking=command.braking;
    }
}
template<class Follower> inline void RetireHandoff(Follower& follower,const race_pace::Command& command) {
    if(!command.NativePaceLimited())return;
    follower.handoff_speed=0;follower.handoff_until=0;follower.handoff_settled_since=-1;
}
} // namespace horse_races::following
