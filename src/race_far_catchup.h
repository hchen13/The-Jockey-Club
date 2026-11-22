#pragma once
#include "race_pace_plan.h"
#include "race_speed_policy.h"

namespace horse_races::far_catchup {
// RDR2 drops collision/physics for distant peds. The ordinary horse-force
// curve then cannot move an off-screen opponent at its quoted speed. Never
// move an entity kinematically while it is on screen: its rider and mount can
// render at different orientations/heights until collision wakes.
inline constexpr float kEnterDistance=65.0F;
// Hysteresis: an off-screen horse stays on simulated navigation until well
// inside the engine's observed ~30 m wake boundary. A transient collision flag
// farther away must not bounce it between two controllers. Visibility always
// wins: never move a rider kinematically in front of the player.
inline constexpr float kExitDistance=15.0F;
inline constexpr double kMaxStepSeconds=.20;
// A distant horse can report collision awake for only a few frames and then
// sleep again. That is not a new start: retain its line speed across the
// transient when an early/visible physical handoff briefly sleeps again.
inline constexpr double kAwakeHandoffSeconds=.9;
struct Sample {
    double now=0;
    Vector3 horse{},player{};
    float centre_along=0,physical_speed=0;
    bool progress_valid=false,asleep=false,on_screen=false,mounted=false,loop=false;
    float speed_ceiling=std::numeric_limits<float>::infinity();
};
struct Step {
    bool move=false,entered=false,left=false,resumed=false,visible_bridge_started=false,wake_requested=false;
    Vector3 position{},velocity{};
    float heading=0,speed=0;
    unsigned moves=0;
    const char* reason=nullptr;
};
struct Runner {
    bool active=false,visible_bridge=false;
    float line_along=0,speed=0,last_awake_speed=0;
    float handoff_speed=0;
    double last_now=0,retry_after=0,handoff_until=0,last_wake_request=0;
    unsigned moves=0;
    void Reset(){*this={};}
    void Suspend(double now){Reset();retry_after=std::isfinite(now)?now+5.0:0;}
    static float LocalAlong(float along,float length,bool loop) {
        if(!std::isfinite(along) || !(length>0))return 0;
        if(!loop)return std::clamp(along,0.0F,length);
        const float local=std::fmod(std::max(0.0F,along),length);
        return local<0?local+length:local;
    }
    Step Tick(const race_pace::Plan& plan,const Sample& s) {
        Step out;
        if(!race_speed_policy::Enabled()){Reset();return out;}
        if(std::isfinite(s.now) && s.now<retry_after)return out;
        const float length=plan.path.Length();
        const float centre_length=plan.reference.size()==plan.path.metres.size() && !plan.reference.empty()?
            plan.reference.back():length;
        const bool valid=plan.path.points.size()>=2 && plan.ceiling.size()==plan.path.points.size() &&
            length>50 && centre_length>50 && std::isfinite(centre_length) && std::isfinite(s.now) &&
            destination::Finite(s.horse) && destination::Finite(s.player) &&
            std::isfinite(s.physical_speed) && s.physical_speed>=0 && std::isfinite(s.centre_along) &&
            s.progress_valid && s.mounted;
        const float distance=valid?destination::Distance(s.horse,s.player):0;
        // Do not replace the simulated speed with a transient zero-speed
        // physics readback. Sustained awake riding still supplies the next
        // ordinary entry speed once the handoff window has expired.
        if(valid && !active && !s.asleep && s.physical_speed>1 && s.now>=handoff_until)
            last_awake_speed=s.physical_speed;
        const char* leave=nullptr;
        if(!valid)leave="invalid participant/line";
        // Looking back must return task ownership even while the collision bit
        // flickers. Keeping active=true here prohibited RejoinNative forever:
        // a visible mount stood idle with NO task for as long as it was watched.
        else if(active && s.on_screen)leave="visible native riding";
        else if(!s.asleep && (!active || distance<=kExitDistance))leave="physics awake";
        if(leave) {
            out.left=active;out.reason=leave;out.speed=speed;out.moves=moves;
            if(!active) {
                if(!valid)Reset();
                else if(s.now>=handoff_until) {
                    handoff_speed=0;handoff_until=0;
                }
                return out;
            }
            if(valid && active) {
                const float at=std::clamp(line_along,0.0F,length);
                const auto a=race_pace::At(plan.path,std::max(0.0F,at-2));
                const auto b=race_pace::At(plan.path,std::min(length,at+2));
                const float d=std::hypot(b.x-a.x,b.y-a.y);
                if(d>.01F && speed>1)out.velocity={speed*(b.x-a.x)/d,speed*(b.y-a.y)/d,0};
            }
            // A brief collision-awake frame is not a new start, even if the
            // camera is facing the horse. Preserve speed for a quick resleep.
            const bool brief_awake=valid && speed>1.0F;
            const float brief_awake_speed=speed;
            out.visible_bridge_started=valid && s.on_screen;
            out.wake_requested=valid && s.on_screen;
            Reset();
            if(out.wake_requested)last_wake_request=s.now;
            if(brief_awake) {
                handoff_speed=brief_awake_speed;
                handoff_until=s.now+kAwakeHandoffSeconds;
            }
            return out;
        }
        // Progress is measured on the recorded centreline. The NPC's racing
        // line can be shorter, so wrap in centreline metres before ToLine().
        const float local=LocalAlong(s.centre_along,centre_length,s.loop);
        const float measured_line=std::clamp(plan.ToLine(local),0.0F,length);
        if(s.on_screen) {
            out.visible_bridge_started=active && !visible_bridge;
            visible_bridge=true;
            // Pause the private clock as well as movement. A camera turn must
            // not produce a catch-up jump on the first off-screen frame.
            last_now=s.now;out.speed=speed;
            if(last_wake_request==0 || s.now-last_wake_request>=.5) {
                out.wake_requested=true;last_wake_request=s.now;
            }
            return out;
        }
        visible_bridge=false;
        if(active && s.asleep && distance<=kExitDistance &&
            (last_wake_request==0 || s.now-last_wake_request>=.5)) {
            out.wake_requested=true;last_wake_request=s.now;
        }
        if(!active) {
            const bool quick_resume=handoff_speed>1 && s.now<handoff_until;
            if(distance<kEnterDistance && !quick_resume)return out;
            active=true;out.entered=true;
            out.resumed=quick_resume;
            line_along=measured_line;
            // The last physically awake sample avoids an instant acceleration
            // from an already-sleeping 4 m/s gait to a 10-speed horse's top.
            speed=std::clamp(quick_resume?handoff_speed:
                (last_awake_speed>1?last_awake_speed:s.physical_speed),0.0F,
                plan.dynamics.top_mps);
            handoff_speed=0;handoff_until=0;
            out.speed=speed;
            last_now=s.now;
            return out;
        }
        // A changed/rebuilt line or a game-side relocation must not be bridged
        // with a large unqualified jump. Re-anchor to the measured position.
        const float discrepancy=std::abs(measured_line-line_along);
        if(discrepancy>30 && (!s.loop || discrepancy<length-30)) {
            line_along=measured_line;speed=std::min(speed,plan.dynamics.top_mps);
            last_now=s.now;out.reason="progress resynchronised";return out;
        }
        const double dt=std::clamp(s.now-last_now,0.0,kMaxStepSeconds);
        last_now=s.now;
        if(dt<=0)return out;
        // A lap gate is a physical crossing, not a modular arithmetic event.
        // Visit the exact final point for one judged sample, then start the
        // next lap only after its progress tracker has acknowledged the gate.
        if(line_along>=length-.001F) {
            if(!s.loop)return out;
            if(local>centre_length-10.0F)return out;
            line_along=0;
        }
        const float allowed=std::min(std::clamp(plan.Allowed(local,speed),0.0F,plan.dynamics.top_mps),
            std::max(0.F,s.speed_ceiling));
        const float seconds=static_cast<float>(dt);
        speed=allowed>=speed?std::min(allowed,speed+plan.dynamics.acceleration*seconds):
            std::max(allowed,speed-plan.dynamics.braking*seconds);
        const float advance=std::min(5.0F,std::max(0.0F,speed*seconds));
        if(advance<.01F)return out;
        line_along=std::min(line_along+advance,length);
        const float before=std::max(0.0F,line_along-2),after=std::min(length,line_along+2);
        const auto a=race_pace::At(plan.path,before),b=race_pace::At(plan.path,after);
        const float horizontal=std::hypot(b.x-a.x,b.y-a.y);
        if(horizontal<.01F)return out;
        out.position=race_pace::At(plan.path,line_along);
        out.heading=std::fmod(std::atan2(-(b.x-a.x),b.y-a.y)*180.0F/3.14159265F+720.0F,360.0F);
        out.speed=speed;out.move=true;++moves;
        return out;
    }
};
} // namespace horse_races::far_catchup
