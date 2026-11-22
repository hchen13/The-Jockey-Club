#pragma once
#include "race_far_catchup.h"
#include "race_native_follower.h"
#include "race_physical_readback.h"
#include <optional>
#include <type_traits>

namespace horse_races::far_catchup {
inline physical_readback::Result ReadPhysicalReady(Ped horse) {
    return physical_readback::Read(horse!=0,
        [&]{return ENTITY::GET_ENTITY_COLLISION_DISABLED(horse)!=0;},
        [&]{return ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(horse)!=0;},
        [&]{return ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(horse)!=0;},
        [&]{return ENTITY::_IS_ENTITY_FROZEN(horse)!=0;});
}
inline bool PhysicalReady(Ped horse) {return ReadPhysicalReady(horse).ready;}
// The optional kinematic diagnostic mode obeys the same occupied-crossing
// decision BEFORE its coordinate writes. Waiting cannot bank a launch speed.
inline bool HoldForVehicle(Runner& runner,native_follower::Follower& follower,
    Ped rider,bool blocked,double now) {
    if(!blocked)return false;
    runner.speed=runner.handoff_speed=runner.last_awake_speed=0;
    runner.handoff_until=0;runner.last_now=now;
    follower.simulation_speed=0;follower.WaitForVehicle(rider,now);
    return true;
}
// Shared by duel hosts, breed-challenge jockeys and spectator races. The
// bridge owns navigation state, not positions: visible riders are never moved
// or rotated here. Rebuild the native task BEFORE transferring line velocity.
inline bool ObserveHandoff(native_follower::Follower& follower,Ped rider,const Step& step) {
    if(step.entered || step.move)return follower.SuspendForSimulation(rider,step.speed);
    if(follower.simulation_suspended && step.speed>0)follower.simulation_speed=step.speed;
    return true;
}
inline void HoldPendingSpeed(Runner& runner,const native_follower::Follower& follower,double now) {
    // The private runner's short resleep window is not a native-ready signal.
    // If the player looked back for longer than that window, then looks away
    // again before physics/task handoff completes, retain the simulated speed
    // and allow an off-screen resume even inside the ordinary entry radius.
    if(follower.simulation_suspended && !runner.active && follower.simulation_speed>1 && std::isfinite(now)) {
        runner.handoff_speed=follower.simulation_speed;
        runner.handoff_until=now+kAwakeHandoffSeconds;
    }
}
enum class Rejoin {None,Waiting,Started,Failed};
template<class MakeName>
inline Rejoin RejoinNative(native_follower::Follower& follower,Ped rider,
    const course::Path& line,const std::vector<float>& reference,float qualified_along,
    Vector3 position,double now,bool physical_ready,bool loop,MakeName make_name,bool on_screen=false,
    bool vehicle_blocked=false) {
    if(!follower.simulation_suspended)return Rejoin::None;
    const bool speed_enabled=race_speed_policy::Enabled();
    // Visibility may wake physics, but cannot authorize a new running task
    // while the same crossing is still occupied.
    if(speed_enabled && vehicle_blocked)return Rejoin::Waiting;
    // Navigation can run in native coarse simulation while collision sleeps.
    // It must be restored immediately for a visible rider, not gated behind
    // the very physical flag which the distant engine may keep asleep.
    if(speed_enabled && !on_screen && !follower.ReadyToResume(now,physical_ready))return Rejoin::Waiting;
    if(line.points.size()<2)return Rejoin::Failed;
    const float length=reference.size()==line.points.size()?reference.back():line.Length();
    const float local=Runner::LocalAlong(qualified_along,length,loop);
    const auto first=native_follower::EntryIndex(line,local,position,&reference,1.0F);
    const float planned_speed=speed_enabled?follower.simulation_speed:0;
    if(!speed_enabled){follower.simulation_speed=0;follower.handoff_speed=0;follower.handoff_until=0;}
    // Do not allocate a new key or advance the recording serial on every
    // frame: most calls are ordinary riding or waiting for a physical wake.
    const auto name=[&] {
        if constexpr(std::is_invocable_v<MakeName>)return make_name();
        else return std::string(make_name);
    }();
    if(!follower.Start(rider,line,name,first,now,&reference,&position))return Rejoin::Failed;
    follower.handoff_speed=planned_speed;follower.handoff_until=speed_enabled?now+3.0:0;
    return Rejoin::Started;
}
inline bool NativePaused(const Runner& runner,const native_follower::Follower& follower,
    bool /*physical_ready*/) {
    return runner.active || follower.simulation_suspended;
}
inline bool MayRecover(double since_go,double now,const Runner& runner,
    const native_follower::Follower& follower,bool physical_ready) {
    // Starting rear/animation, distant simulation and native scheduling are
    // not stalls. Only judge course progress once physical riding is possible.
    return since_go>=6 && physical_ready && !NativePaused(runner,follower,physical_ready) && !follower.HandoffGrace(now);
}
inline std::optional<Vector3> TakeHandoffVelocity(native_follower::Follower& follower,
    const race_pace::Plan& plan,const Sample& sample,Vector3 forward,bool physical_ready) {
    if(!race_speed_policy::Enabled()){follower.handoff_speed=0;follower.handoff_until=0;return {};}
    if(!(follower.handoff_speed>1))return {};
    if(!std::isfinite(sample.now) || sample.now>=follower.handoff_until || !sample.mounted ||
        !sample.progress_valid || !destination::Finite(sample.horse)) {
        follower.handoff_speed=0;return {};
    }
    if(!physical_ready || sample.asleep || follower.simulation_suspended || !follower.control ||
        (follower.status!=native_follower::Status::Running &&
         follower.status!=native_follower::Status::Starting) || plan.path.points.size()<2)return {};
    const float length=plan.reference.size()==plan.path.points.size()?plan.reference.back():plan.path.Length();
    const float local=Runner::LocalAlong(sample.centre_along,length,sample.loop);
    const auto projected=course::Project(plan.path,sample.horse,std::max(0.0F,local-5),local+25,&plan.reference);
    if(!projected.valid)return {};
    // Use the tangent at the CURRENT position, not the last asleep frame's
    // tangent. No sideways launch while the mount is still turning around.
    const float at=std::clamp(plan.ToLine(projected.along),0.0F,plan.path.Length());
    const auto a=race_pace::At(plan.path,std::max(0.0F,at-2));
    const auto b=race_pace::At(plan.path,std::min(plan.path.Length(),at+2));
    const float tangent_length=std::hypot(b.x-a.x,b.y-a.y),facing_length=std::hypot(forward.x,forward.y);
    if(!(tangent_length>.01F) || !(facing_length>.01F))return {};
    const Vector3 tangent{(b.x-a.x)/tangent_length,(b.y-a.y)/tangent_length,0};
    const float facing=(tangent.x*forward.x+tangent.y*forward.y)/facing_length;
    if(!std::isfinite(facing) || facing<.75F)return {};
    const float speed=std::min(follower.handoff_speed,plan.Allowed(projected.along,follower.handoff_speed));
    if(!(speed>1) || !std::isfinite(sample.physical_speed) || sample.physical_speed<0)return {};
    // Task scheduling may zero velocity again one frame AFTER the first
    // transfer. A one-shot transfer therefore lost all momentum. Hold only
    // the earned, turn-limited speed through scheduling, and retire it after
    // sustained native riding. No position/heading writes and no extra boost.
    if(sample.physical_speed>=speed*.90F) {
        if(follower.handoff_settled_since<0)follower.handoff_settled_since=sample.now;
        if(sample.now-follower.handoff_settled_since>=.5)follower.handoff_speed=0;
        return {};
    }
    follower.handoff_settled_since=-1;
    return Vector3{tangent.x*speed,tangent.y*speed,0};
}
inline bool TransferHandoffVelocity(native_follower::Follower& follower,
    const race_pace::Plan& plan,const Sample& sample,Ped horse,bool physical_ready) {
    if(!(follower.handoff_speed>1))return false;
    // Preserve real jumps/collisions. A velocity bridge must never flatten a
    // fall's vertical motion or propel a ragdoll just because terrain loaded.
    if(ENTITY::IS_ENTITY_IN_AIR(horse,0) || PED::IS_PED_RAGDOLL(horse) || PED::IS_PED_FALLING(horse) ||
        PED::IS_PED_RAGDOLL(follower.rider.horse) || PED::IS_PED_FALLING(follower.rider.horse))return false;
    const auto velocity=TakeHandoffVelocity(follower,plan,sample,
        ENTITY::GET_ENTITY_FORWARD_VECTOR(horse),physical_ready);
    if(!velocity)return false;
    ENTITY::SET_ENTITY_VELOCITY(horse,*velocity);return true;
}
} // namespace horse_races::far_catchup
