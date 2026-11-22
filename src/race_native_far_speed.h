#pragma once
#include "race_pace_envelope.h"
#include "race_speed_policy.h"
#include "horse_motion_native.h"
#include "horse_speed_learning_native.h"

namespace horse_races::native_far_speed {
using SupplementReason=race_pace::SupplementReason;
inline void ObserveLearning(horse_motion::speed_learning::Session& learning,const race_pace::Command& command,
    Ped horse,Ped rider,bool active,float earned_bonus,std::string_view owner,
    horse_motion::speed_learning::Session::EnvironmentReader capture=horse_motion::native_environment::Capture) {
    // An anticipatory following ceiling need not brake yet, but its native
    // absolute request must never teach capped speed as natural capability.
    // This veto is sampling-only: preserve the real brake/earned-burst state.
    learning.Observe(horse,rider,active,command.effort,command.braking || command.NativePaceLimited(),
        earned_bonus>0 || command.catchup_fraction>0,owner,capture);
}
inline race_pace::Trace* BeginTrace(const race_pace::Command& command,int source,bool enabled,
    float distance,bool progress_valid,float earned_bonus,bool table_trial,std::optional<float> gap) {
    if(!race_pace::diagnostics_enabled)return nullptr;
    auto& trace=command.trace;
    trace.supplement_evaluated=true;trace.supplement_source=source;++trace.supplement_calls;
    trace.supplement_enabled=enabled;trace.supplement_progress_valid=progress_valid;
    trace.supplement_distance_m=distance;trace.supplement_table_trial=table_trial;
    trace.supplement_input_target_mps=command.target_mps;trace.supplement_input_effort=command.effort;
    trace.supplement_input_braking=command.braking;trace.supplement_safety=command.NativePaceLimited();
    trace.supplement_gap_valid=gap.has_value();trace.supplement_gap_m=gap.value_or(race_pace::kUnobserved);
    trace.supplement_burst_levels=earned_bonus;trace.supplement_learned_base_mps=race_pace::kUnobserved;
    trace.supplement_catchup_fraction=command.catchup_fraction;
    trace.supplement_reason=SupplementReason::NotEvaluated;trace.supplement_target_mps=0;
    trace.supplement_speed_level=0;
    return &trace;
}
// WAYPOINT_PLAYBACK_OVERRIDE_SPEED has two input units. p4=0 clamps a gait
// request to [0,3]; p4=1 stores an absolute speed (movement parameter type 2).
// Verified read-only in 1491.50 and used by train_robbery4.c::func_1801.
// Type2 bypasses the native gait-to-speed conversion and its gait bounds.
// Production vector compensation keeps native gait input; the owned output
// filter supplies ability/burst compensation separately. Explicit safety caps
// and legacy non-vector experiments can still request absolute speed.
inline constexpr bool kDefaultVectorSpeed=true;
inline constexpr bool VectorSpeedEnabled(int configured,int legacy=-1) {
    return configured>=0?configured!=0:legacy>=0?legacy!=0:kDefaultVectorSpeed;
}
inline constexpr float kMaxRequestMps=race_pace::kMaxRequestedMps; // Ability/catch-up requests may reach 135 km/h; safety ceilings remain 25 m/s.
inline constexpr float kWaypointTransitionRate=.4F;
inline constexpr float WaypointTransitionRate(bool absolute,bool safety_cap) {
    // 1491's positive rate slews its current locomotion request each engine
    // step; it is NOT a duration. Nonpositive copies the complete target/type
    // immediately. Safety limits must reach that target without a second ramp.
    return absolute&&safety_cap?0.F:kWaypointTransitionRate;
}
struct WaypointCommand {
    float speed=0,vector_target=0;
    bool absolute=false;
    float transition_rate=kWaypointTransitionRate;
};
inline WaypointCommand SelectWaypointCommand(float effort,float request,bool vector_compensation,bool safety_cap=false,
    bool previous_safety_absolute=false){
    const bool valid=std::isfinite(request)&&request>0&&request<=kMaxRequestMps;
    // The native task owns a safety cap in absolute units. Sleeping vector
    // output can retain the same positive cap without changing the task type.
    const bool vector_output=vector_compensation;
    const bool absolute=valid&&(!vector_output||safety_cap);
    // Releasing our own immediate absolute cap must also replace its current
    // parameter/type immediately. Otherwise gait 3 still slews from e.g. 1.5
    // m/s for seconds. This changes the native request, never world velocity.
    const bool restore=previous_safety_absolute&&!safety_cap&&!absolute&&
        std::isfinite(effort)&&effort>0&&effort<=3;
    return {absolute?request:effort,valid&&vector_output?request:0,absolute,
        restore?0.F:WaypointTransitionRate(absolute,safety_cap)};
}
inline float DynamicsTarget(const race_pace::Dynamics& dynamics,const race_pace::Command& command,
    bool enabled,bool asleep,float distance,bool unsafe,int displayed_speed,float earned_bonus=0,bool table_trial=false,
    float learned_base=0,std::optional<float> gap={},race_pace::Trace* observation=nullptr) {
    const auto reject=[&](bool condition,SupplementReason reason){return race_pace::Rejected(condition,reason,observation);};
    if(observation){observation->supplement_learned_base_mps=learned_base;observation->supplement_speed_level=displayed_speed;}
    if(reject(!enabled,SupplementReason::Disabled) || reject(!asleep,SupplementReason::SleepingRequired) ||
        reject(unsafe,SupplementReason::UnsafeInput) || reject(!std::isfinite(distance),SupplementReason::DistanceInvalid) ||
        reject(distance<(table_trial?25.0F:65.0F),SupplementReason::DistanceNear) ||
        reject(!dynamics.Valid(),SupplementReason::DynamicsInvalid) ||
        reject(!std::isfinite(command.target_mps),SupplementReason::TargetInvalid) ||
        reject(command.target_mps<=0,SupplementReason::TargetNonPositive) ||
        reject(!std::isfinite(command.effort),SupplementReason::EffortInvalid) ||
        reject(command.effort<2.99F,SupplementReason::EffortLow) || reject(command.effort>3,SupplementReason::EffortHigh) ||
        reject(command.braking||command.NativePaceLimited(),SupplementReason::CommandBraking) ||
        reject(displayed_speed<(command.catchup_fraction>0?1:6) || displayed_speed>10,SupplementReason::RankIneligible))return 0;
    const float result=race_pace::RequestedTarget(dynamics,command,displayed_speed,earned_bonus,learned_base,gap);
    race_pace::Returned(observation,SupplementReason::Compensation,result);return result;
}
inline float Target(const race_pace::Plan& plan,const race_pace::Command& command,
    bool enabled,bool asleep,float distance,bool unsafe,int displayed_speed,float earned_bonus=0,bool table_trial=false,
    float learned_base=0,std::optional<float> gap={},race_pace::Trace* observation=nullptr) {
    if(observation){observation->supplement_learned_base_mps=learned_base;observation->supplement_speed_level=displayed_speed;}
    if(plan.path.points.size()<2 || plan.ceiling.size()!=plan.path.points.size()) {
        race_pace::Returned(observation,SupplementReason::PlanInvalid,0);return 0;
    }
    return DynamicsTarget(plan.dynamics,command,enabled,asleep,distance,unsafe,displayed_speed,earned_bonus,table_trial,learned_base,gap,observation);
}
inline bool SafeRacingMount(Ped horse,Ped rider,race_pace::Trace* observation=nullptr) {
    const auto reject=[&](bool condition,SupplementReason reason){return race_pace::Rejected(condition,reason,observation);};
    if(reject(!horse,SupplementReason::HorseMissing) || reject(!rider,SupplementReason::RiderMissing) ||
        reject(rider==PLAYER::PLAYER_PED_ID(),SupplementReason::PlayerRider) ||
        reject(!ENTITY::DOES_ENTITY_EXIST(horse),SupplementReason::HorseUnavailable) ||
        reject(!ENTITY::DOES_ENTITY_EXIST(rider),SupplementReason::RiderUnavailable) ||
        reject(!PED::IS_PED_ON_MOUNT(rider),SupplementReason::RiderNotMounted) ||
        reject(PED::GET_MOUNT(rider)!=horse,SupplementReason::MountMismatch) ||
        reject(PED::GET_MOUNT(PLAYER::PLAYER_PED_ID())==horse,SupplementReason::PlayerMount))return false;
    return !reject(HUD::IS_PAUSE_MENU_ACTIVE(),SupplementReason::Paused) &&
        !reject(ENTITY::IS_ENTITY_DEAD(horse),SupplementReason::HorseDead) &&
        !reject(ENTITY::IS_ENTITY_DEAD(rider),SupplementReason::RiderDead) &&
        !reject(ENTITY::_IS_ENTITY_FROZEN(horse),SupplementReason::HorseFrozen) &&
        !reject(ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(horse),SupplementReason::AwaitingCollision) &&
        !reject(ENTITY::IS_ENTITY_IN_AIR(horse,false),SupplementReason::HorseAirborne) &&
        !reject(ENTITY::IS_ENTITY_IN_WATER(horse),SupplementReason::HorseWater) &&
        !reject(PED::IS_PED_FALLING(horse),SupplementReason::HorseFalling) &&
        !reject(PED::IS_PED_RAGDOLL(horse),SupplementReason::HorseRagdoll) &&
        !reject(PED::IS_PED_FALLING(rider),SupplementReason::RiderFalling) &&
        !reject(PED::IS_PED_RAGDOLL(rider),SupplementReason::RiderRagdoll) &&
        !reject(PED::IS_PED_JUMPING(horse),SupplementReason::HorseJumping) &&
        !reject(PED::IS_PED_SWIMMING(horse),SupplementReason::HorseSwimming);
}
inline bool SafeSleepingMount(Ped horse,Ped rider,race_pace::Trace* observation=nullptr) {
    return SafeRacingMount(horse,rider,observation) &&
        !race_pace::Rejected(!ENTITY::GET_ENTITY_COLLISION_DISABLED(horse),SupplementReason::SleepingRequired,observation);
}
inline float ForRider(const race_pace::Plan& plan,const race_pace::Command& command,
    bool enabled,Ped horse,Ped rider,float distance,bool progress_valid,float earned_bonus=0,bool table_trial=false,
    horse_motion::speed_learning::Session* learning=nullptr,std::optional<float> gap={},std::string_view owner={}) {
    if(!race_speed_policy::Enabled())return 0;
    auto* observation=BeginTrace(command,1,enabled,distance,progress_valid,earned_bonus,table_trial,gap);
    const auto reject=[&](bool condition,SupplementReason reason){return race_pace::Rejected(condition,reason,observation);};
    // MUST run while awake too; the sleeping-output hook must never teach its
    // own catch-up speed back into the normal ability estimate.
    if(learning) {
        ObserveLearning(*learning,command,horse,rider,enabled && progress_valid,earned_bonus,owner);
        learning->reference.SetTarget(gap,0,command.catchup_fraction);
    }
    // A safety pace remains an absolute native request in BOTH simulation
    // modes. The sleeping vector may retain its gain below that same ceiling;
    // it never fabricates or scales a Z velocity.
    if(command.NativePaceLimited()) {
        if(reject(command.vehicle_blocked,SupplementReason::CommandBraking) ||
            reject(!progress_valid,SupplementReason::ProgressInvalid) ||
            reject(!std::isfinite(command.target_mps),SupplementReason::TargetInvalid) ||
            reject(command.target_mps<=0,SupplementReason::TargetNonPositive) ||
            reject(command.target_mps>horse_motion::speed_limits::kNativeMps,SupplementReason::TargetTooHigh) ||
            !SafeRacingMount(horse,rider,observation))return 0;
        race_pace::Returned(observation,SupplementReason::SafetyPace,command.target_mps);
        return command.target_mps;
    }
    if(reject(!enabled,SupplementReason::Disabled) || reject(!progress_valid,SupplementReason::ProgressInvalid) ||
        reject(!horse,SupplementReason::HorseMissing) || reject(!rider,SupplementReason::RiderMissing) ||
        reject(!std::isfinite(distance),SupplementReason::DistanceInvalid) ||
        reject(distance<(table_trial?25.0F:65.0F),SupplementReason::DistanceNear) ||
        reject(command.braking,SupplementReason::CommandBraking) || reject(command.effort<2.99F,SupplementReason::EffortLow) ||
        !SafeSleepingMount(horse,rider,observation))return 0;
    const float target=Target(plan,command,true,true,distance,false,ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,5)+1,
        earned_bonus,table_trial,learning?learning->FlatBase():0,gap,observation);
    if(learning)learning->reference.SetTarget(gap,target,command.catchup_fraction);
    return target;
}
inline float ForNativeTraffic(const race_pace::Command& command,bool enabled,
    Ped horse,Ped rider,float distance,float earned_bonus=0,
    horse_motion::speed_learning::Session* learning=nullptr,std::string_view owner={},std::optional<float> gap={}) {
    if(!race_speed_policy::Enabled())return 0;
    auto* observation=BeginTrace(command,2,enabled,distance,gap.has_value(),earned_bonus,true,gap);
    const auto reject=[&](bool condition,SupplementReason reason){return race_pace::Rejected(condition,reason,observation);};
    // Bare A->B duels (Mary-Beth/Pearson) have NO recorded geometry/pace plan.
    // Do not fabricate a straight route or take over native road selection.
    // Use their own ranks; the original-vector slow/bend/vertical filter still
    // decides whether any output can be increased. No per-frame INI reads.
    if(learning) {
        ObserveLearning(*learning,command,horse,rider,enabled,earned_bonus,owner);
        learning->reference.SetTarget(gap,0,command.catchup_fraction);
    }
    if(reject(!enabled,SupplementReason::Disabled) || reject(!std::isfinite(distance),SupplementReason::DistanceInvalid) ||
        reject(distance<25,SupplementReason::DistanceNear) || reject(command.braking||command.NativePaceLimited(),SupplementReason::CommandBraking) ||
        reject(command.effort<2.99F,SupplementReason::EffortLow) || !SafeSleepingMount(horse,rider,observation))return 0;
    const int speed=ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,5)+1;
    const auto dynamics=race_pace::ForAttributes(speed,ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,6)+1);
    auto desired=command;desired.target_mps=dynamics.top_mps;
    const float target=DynamicsTarget(dynamics,desired,true,true,distance,false,speed,
        earned_bonus,true,learning?learning->FlatBase():0,gap,observation);
    if(learning)learning->reference.SetTarget(gap,target,command.catchup_fraction);
    return target;
}
// Common entry for hosts, field entrants, bookmaker jockeys and original
// encounter recordings. The caller supplies ONLY its owned racing rider.
// Keep the proven sleeping-only treatment: no awake speed seed or teleport.
template<class VectorLease>
inline bool ApplyVector(VectorLease& lease,Ped rider,float request,bool enabled,bool pace_limited=false) {
    if(!race_speed_policy::Enabled()){lease.Release();return false;}
    const bool valid=std::isfinite(request) && request>0 && request<=kMaxRequestMps;
    const Ped mount=enabled && rider && rider!=PLAYER::PLAYER_PED_ID() &&
        ENTITY::DOES_ENTITY_EXIST(rider) && PED::IS_PED_ON_MOUNT(rider)?PED::GET_MOUNT(rider):0;
    if(mount && SafeSleepingMount(mount,rider)) {
        // A zero request is this frame's pace veto, not the end of this horse's
        // race. Immediately hide its task from the output hook without making
        // every short brake restart the already established ability ramp.
        if(request==0){lease.Suspend(mount);return false;}
        if(valid)return pace_limited?lease.UpdateCapped(mount,request):lease.Update(mount,request);
    }
    lease.Release();return false;
}
} // namespace horse_races::native_far_speed
