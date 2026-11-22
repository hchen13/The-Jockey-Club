#pragma once
#include <cstdint>
#include <limits>

namespace horse_races::race_pace {
inline bool diagnostics_enabled=false;
inline constexpr float kUnobserved=std::numeric_limits<float>::quiet_NaN();
enum class SupplementReason : std::uint8_t {
    NotEvaluated,Compensation,SafetyPace,Disabled,ProgressInvalid,HorseMissing,RiderMissing,
    DistanceInvalid,DistanceNear,CommandBraking,EffortLow,EffortInvalid,EffortHigh,
    PlanInvalid,DynamicsInvalid,TargetInvalid,TargetNonPositive,TargetTooHigh,RankIneligible,
    SleepingRequired,UnsafeInput,PlayerRider,HorseUnavailable,RiderUnavailable,RiderNotMounted,
    MountMismatch,PlayerMount,Paused,HorseDead,RiderDead,HorseFrozen,AwaitingCollision,
    HorseAirborne,HorseWater,HorseFalling,HorseRagdoll,RiderFalling,RiderRagdoll,HorseJumping,HorseSwimming
};
inline const char* Name(SupplementReason reason) {
    switch(reason) {
    case SupplementReason::NotEvaluated:return "not_evaluated";
    case SupplementReason::Compensation:return "compensation";
    case SupplementReason::SafetyPace:return "safety_pace";
    case SupplementReason::Disabled:return "disabled";
    case SupplementReason::ProgressInvalid:return "progress_invalid";
    case SupplementReason::HorseMissing:return "horse_missing";
    case SupplementReason::RiderMissing:return "rider_missing";
    case SupplementReason::DistanceInvalid:return "distance_invalid";
    case SupplementReason::DistanceNear:return "distance_near";
    case SupplementReason::CommandBraking:return "command_braking";
    case SupplementReason::EffortLow:return "effort_low";
    case SupplementReason::EffortInvalid:return "effort_invalid";
    case SupplementReason::EffortHigh:return "effort_high";
    case SupplementReason::PlanInvalid:return "plan_invalid";
    case SupplementReason::DynamicsInvalid:return "dynamics_invalid";
    case SupplementReason::TargetInvalid:return "target_invalid";
    case SupplementReason::TargetNonPositive:return "target_nonpositive";
    case SupplementReason::TargetTooHigh:return "target_too_high";
    case SupplementReason::RankIneligible:return "rank_ineligible";
    case SupplementReason::SleepingRequired:return "sleeping_required";
    case SupplementReason::UnsafeInput:return "unsafe_input";
    case SupplementReason::PlayerRider:return "player_rider";
    case SupplementReason::HorseUnavailable:return "horse_unavailable";
    case SupplementReason::RiderUnavailable:return "rider_unavailable";
    case SupplementReason::RiderNotMounted:return "rider_not_mounted";
    case SupplementReason::MountMismatch:return "mount_mismatch";
    case SupplementReason::PlayerMount:return "player_mount";
    case SupplementReason::Paused:return "paused";
    case SupplementReason::HorseDead:return "horse_dead";
    case SupplementReason::RiderDead:return "rider_dead";
    case SupplementReason::HorseFrozen:return "horse_frozen";
    case SupplementReason::AwaitingCollision:return "awaiting_collision";
    case SupplementReason::HorseAirborne:return "horse_airborne";
    case SupplementReason::HorseWater:return "horse_water";
    case SupplementReason::HorseFalling:return "horse_falling";
    case SupplementReason::HorseRagdoll:return "horse_ragdoll";
    case SupplementReason::RiderFalling:return "rider_falling";
    case SupplementReason::RiderRagdoll:return "rider_ragdoll";
    case SupplementReason::HorseJumping:return "horse_jumping";
    case SupplementReason::HorseSwimming:return "horse_swimming";
    }
    return "unknown";
}
// Observations only. None of these values is read to choose a driving output.
// NaN means that the existing decision chain did not evaluate that quantity.
struct Trace {
    double evaluated_game_s=std::numeric_limits<double>::quiet_NaN();
    bool controller_evaluated=false,plan_braking=false,plan_bend=false,plan_terrain_limited=false;
    float input_speed_mps=kUnobserved,input_along_m=kUnobserved;
    float top_mps=kUnobserved,lateral_mps2=kUnobserved;
    float lookahead_line_m=kUnobserved,lookahead_curvature=kUnobserved;
    float plan_target_mps=kUnobserved,plan_effort=kUnobserved;
    bool following_evaluated=false,following_applied=false,following_input_braking=false,following_braking=false;
    int following_horse=0;
    float following_input_target_mps=kUnobserved,following_input_effort=kUnobserved,following_cap_mps=kUnobserved;
    float following_gap_m=kUnobserved,following_lead_mps=kUnobserved;
    float following_closing_mps=kUnobserved,following_lateral_m=kUnobserved;
    float following_target_mps=kUnobserved,following_effort=kUnobserved;
    bool supplement_evaluated=false,supplement_enabled=false,supplement_progress_valid=false;
    bool supplement_table_trial=false,supplement_gap_valid=false,supplement_safety=false,supplement_input_braking=false;
    unsigned supplement_calls=0;
    int supplement_source=0,supplement_speed_level=0; // Source: 1 recorded plan, 2 native traffic.
    SupplementReason supplement_reason=SupplementReason::NotEvaluated;
    float supplement_distance_m=kUnobserved,supplement_input_target_mps=kUnobserved;
    float supplement_input_effort=kUnobserved,supplement_learned_base_mps=kUnobserved;
    float supplement_gap_m=kUnobserved,supplement_burst_levels=kUnobserved;
    float supplement_catchup_fraction=0;
    float supplement_target_mps=kUnobserved;
};
// Preserve the original left-to-right short circuit while recording its first
// failing predicate. Callers pass only values the original branch evaluated.
inline bool Rejected(bool condition,SupplementReason reason,Trace* trace) {
    if(condition && trace)trace->supplement_reason=reason;
    return condition;
}
inline void Returned(Trace* trace,SupplementReason reason,float target) {
    if(trace){trace->supplement_reason=reason;trace->supplement_target_mps=target;}
}
} // namespace horse_races::race_pace
