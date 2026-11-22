#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "horse_motion_curve.h"

// A field host's edge over the player, expressed as a fraction of a speed level
// rather than a fixed km/h. She races the rank she drew - whatever the
// difficulty settings gave her - and is simply a little faster at that rank
// than the same rank is for anyone else: a drawn 7 beats our 7, loses to our 8.
// Her acceleration is left entirely to the difficulty settings.
//
// The bonus is NOT always on. It appears only once she has been at full
// flight for a sustained stretch. A timed burst then lasts its full duration;
// an untimed bonus continues only while the horse holds its gait.
// "Full flight" is her own gait, not a speed: the game's move blend ratio,
// 0 standing, 1 walking, 2 cantering, 3 galloping, blended in between. A
// speed bar was wrong twice over - the same rank's speed differs uphill and
// on sand, and a bar tied to the route's straightness was reachable on 1% of
// New Austin - whereas a task-driven horse holds gait 3.0 up a hill at
// 70 km/h and drops to 2.0 only where the controller brakes for a bend. She
// cannot use it to pull away out of a bend or off the line; it is the reward
// for already galloping flat out, and the corners stay the player's.
namespace horse_races::host_boost {
inline constexpr double kHoldSeconds=5.0;
// The camp one-on-one gives Arthur a longer recovery between timed bursts.
inline constexpr double kArthurDuelCooldownSeconds=30.0;
// The gait she must be at: the gallop fully blended in. A task-driven horse
// sits at exactly 3.0; the player's own mount tops out at 2.9.
// 2.85: at full flight her blend hovers between 2.94 and 2.96 (2026-09-14, the
// bonus came and went thirty times in a minute at 2.95); the player's mount
// tops out at 2.9, which is beside the point - the bar is about her.
inline constexpr float kGaitBar=2.85F;
// The blend can flicker for a frame or two as the animation re-plants; only
// a dip that outlasts a stride (~0.44 s) counts as having fallen off.
inline constexpr double kDropGrace=0.45;
// Two ticks more than half a second apart are not a continuous hold: a pause, a
// frame hitch or a loading screen must not be credited as time at speed.
inline constexpr double kMaxGapSeconds=0.5;
inline constexpr float kMaxLevelBonus=2.0F;

// Force for `level` plus `fraction` of a level. The shipped curve is linear in
// terminal speed, so a fraction of a level is that fraction of the way to the
// next force - no fitted constants, and it stays correct if the ini is edited.
// At the top level there is no neighbour above, so the gap below is continued.
inline float BonusForce(const std::array<float,5>& forces,int displayed_level,float fraction) {
    if(displayed_level<6 || displayed_level>10 || !(fraction>0))return 0;
    const auto index=static_cast<std::size_t>(displayed_level-6);
    const float step=displayed_level<10?forces[index+1]-forces[index]:forces[4]-forces[3];
    const float force=forces[index]+step*fraction;
    return force>0?force:0;
}

// Apply to the curve input only: native attribute ranks and acceleration stay
// untouched. A fresh frame/config each tick makes withdrawal immediate.
inline bool ApplyNearBonus(horse_motion::Config& config,horse_motion::Input& input,
    bool racing,bool valid,bool awake,bool catchup,float catchup_fraction,
    bool earned,float earned_fraction) {
    if(!racing || !valid || !awake || input.speed_level<1 || input.speed_level>10 ||
        input.braking_requested || !input.acceleration_requested ||
        input.environment.unsafe || input.environment.in_water ||
        (input.environment.max_blend && *input.environment.max_blend<config.desired_blend) ||
        (input.environment.max_speed_kmh && input.speed_kmh>=*input.environment.max_speed_kmh))return false;
    const bool chase=catchup && std::isfinite(catchup_fraction) && catchup_fraction>0;
    const float bonus=std::max(chase?1.0F:0.0F,
        earned && std::isfinite(earned_fraction)?std::clamp(earned_fraction,0.0F,kMaxLevelBonus):0.0F);
    if(!(bonus>0))return false;
    const int level=input.speed_level;
    if(chase && level<=5)input.speed_level=level+1;
    if(level>=6)config.high_speed_forces[static_cast<std::size_t>(level-6)]=
        BonusForce(config.high_speed_forces,level,bonus);
    // Existing earned bonuses are defined only on the high-speed ladder.
    return chase || level>=6;
}
// Red means an earned burst belonging to an eligible rider. Near pursuit can
// apply the same curve without earning a burst; it never owns presentation.
// A sleeping mount uses its earned far bonus instead of the near curve; a
// timed earned burst stays visible for its full lifetime when force is paused.
inline bool DisplayActive(bool racing,bool valid,bool display_eligible,
    bool near_applied,bool awake,bool earned,bool timed=false) {
    // A timed burst remains active even when a genuine physical restriction
    // temporarily withholds its force. Untimed Sadie retains the old display.
    return racing && valid && display_eligible && earned && (timed || near_applied || !awake);
}

enum class Reason {Ready,Inactive,Identity,FrameInvalid,PaceBraking,Unsafe,Water,
    GaitUnavailable,NativeIntent,GaitBelow,Clock,Configuration,Count};
inline const char* ReasonName(Reason reason) {
    switch(reason) {
    case Reason::Ready:return "ready";case Reason::Inactive:return "inactive";
    case Reason::Identity:return "identity";case Reason::FrameInvalid:return "frame_invalid";
    case Reason::PaceBraking:return "pace_braking";case Reason::Unsafe:return "unsafe";
    case Reason::Water:return "water";case Reason::GaitUnavailable:return "gait_unavailable";
    case Reason::NativeIntent:return "native_intent";case Reason::GaitBelow:return "gait_below";
    case Reason::Clock:return "clock";case Reason::Configuration:return "configuration";
    default:return "unknown";
    }
}
struct Conditions {
    bool racing=false,actor_valid=false,frame_valid=false,pace_braking=false;
    bool unsafe=false,water=false,gait_valid=false,native_fast=false;
};
inline Reason Classify(const Conditions& c) {
    if(!c.racing)return Reason::Inactive;
    if(!c.actor_valid)return Reason::Identity;
    if(!c.frame_valid)return Reason::FrameInvalid;
    if(c.pace_braking)return Reason::PaceBraking;
    if(c.unsafe)return Reason::Unsafe;
    if(c.water)return Reason::Water;
    if(!c.gait_valid)return Reason::GaitUnavailable;
    if(!c.native_fast)return Reason::NativeIntent;
    return Reason::Ready;
}
inline bool Transient(Reason reason) {
    return reason==Reason::GaitUnavailable || reason==Reason::NativeIntent;
}
// Count qualified game time. Missing native evidence cannot earn time or
// extra force, but one missing frame must not erase a five-second gallop.
class Gate {
    double held_seconds_=0,unqualified_since_=-1;
    double active_since_=-1,last_tick_=-1,cooldown_until_=-1;
    bool holding_=false,qualified_last_=false,active_=false,started_=false;
    unsigned drops_=0;
    std::array<unsigned,static_cast<std::size_t>(Reason::Count)> blocked_{},resets_{};
    Reason reason_=Reason::Ready;
    void ClearHold() {
        held_seconds_=0;unqualified_since_=active_since_=-1;
        holding_=qualified_last_=active_=started_=false;
    }
    void Drop(Reason why) {
        if(holding_){++drops_;++resets_[static_cast<std::size_t>(why)];}
        ClearHold();
    }
public:
    void Reset(){ClearHold();last_tick_=cooldown_until_=-1;reason_=Reason::Ready;}
    bool Active() const {return active_;}
    bool Started() const {return started_;}
    unsigned Drops() const {return drops_;}
    double HeldFor(double) const {return held_seconds_;}
    unsigned Blocked(Reason reason) const {return blocked_[static_cast<std::size_t>(reason)];}
    unsigned Resets(Reason reason) const {return resets_[static_cast<std::size_t>(reason)];}
    Reason LastReason() const {return reason_;}
    bool Update(double now,float gait,float bar,bool eligible=true,
        double hold_seconds=kHoldSeconds,double burst_seconds=0,double cooldown_seconds=0) {
        // The legacy false contract is real safety ineligibility. Only an
        // explicit native-evidence reason can retain charge through a gap.
        return Update(now,gait,bar,eligible?Reason::Ready:Reason::PaceBraking,hold_seconds,burst_seconds,cooldown_seconds);
    }
    bool Update(double now,float gait,float bar,Reason reason,
        double hold_seconds=kHoldSeconds,double burst_seconds=0,double cooldown_seconds=0) {
        started_=false;
        if(static_cast<std::size_t>(reason)>=static_cast<std::size_t>(Reason::Count))reason=Reason::Configuration;
        if(!std::isfinite(now))reason=Reason::Clock;
        else if(reason==Reason::Ready && !std::isfinite(gait))reason=Reason::GaitUnavailable;
        if((reason==Reason::Ready || Transient(reason)) && (!(bar>0) || !std::isfinite(bar) ||
            !std::isfinite(hold_seconds) || hold_seconds<0 ||
            !std::isfinite(burst_seconds) || burst_seconds<0 ||
            !std::isfinite(cooldown_seconds) || cooldown_seconds<0))reason=Reason::Configuration;
        reason_=reason;
        if(reason!=Reason::Ready)++blocked_[static_cast<std::size_t>(reason)];
        // Once earned, a timed burst belongs to its race/horse for the whole
        // configured interval. Gait dips and temporary unavailable samples do
        // not cancel or restart its clock. The physical actuator still owns
        // its independent safety and environment checks. A departed race or
        // lost identity never keeps a live bonus.
        if(active_since_>=0 && burst_seconds>0 && reason!=Reason::Inactive &&
            reason!=Reason::Identity && reason!=Reason::Clock && reason!=Reason::Configuration) {
            if(last_tick_>=0 && now<last_tick_) {
                reason_=Reason::Clock;Drop(reason_);last_tick_=-1;return false;
            }
            last_tick_=now;
            if(now-active_since_<burst_seconds){active_=true;return true;}
            // Cooldown begins at the actual timed expiry, even if a late
            // observation finds that the burst has already ended. Charge can
            // accumulate concurrently, but cannot start during cooldown.
            cooldown_until_=active_since_+burst_seconds+cooldown_seconds;
            ClearHold();holding_=reason==Reason::Ready && gait>=bar;
            qualified_last_=reason==Reason::Ready;return false;
        }
        if(reason!=Reason::Ready && !Transient(reason)) {
            Drop(reason);last_tick_=-1;return false;
        }
        const bool had_tick=last_tick_>=0;
        const bool continuous=had_tick && now>=last_tick_ && now-last_tick_<=kMaxGapSeconds;
        const double dt=continuous?now-last_tick_:0;
        last_tick_=now;
        if(!continuous) {
            if(had_tick){reason_=Reason::Clock;++blocked_[static_cast<std::size_t>(reason_)];}
            Drop(Reason::Clock);
            holding_=reason==Reason::Ready && gait>=bar;
            qualified_last_=reason==Reason::Ready;return false;
        }
        if(Transient(reason)) {
            if(unqualified_since_<0)unqualified_since_=now;
            if(now-unqualified_since_>=kDropGrace)Drop(reason);
            active_=qualified_last_=false;return false;
        }
        // Only actual full gait restores the stride window. Alternating a
        // missing read with low gait must not restart two separate graces.
        if(gait>=bar)unqualified_since_=-1;
        else {
            if(unqualified_since_<0)unqualified_since_=now;
            if(now-unqualified_since_>=kDropGrace) {
                reason_=Reason::GaitBelow;++blocked_[static_cast<std::size_t>(reason_)];
                Drop(reason_);qualified_last_=true;return false;
            }
            // Preserve the established sub-stride ACTUAL gait tolerance. It
            // never overrides an explicit brake or unavailable native input.
            if(holding_ && qualified_last_)held_seconds_+=dt;
            qualified_last_=true;active_=active_since_>=0;return active_;
        }
        const bool was_holding=holding_;holding_=true;
        // Both interval endpoints must be qualified: the first good read may
        // not credit the time whose native evidence was missing.
        if(was_holding && qualified_last_)held_seconds_+=dt;
        qualified_last_=true;
        if(held_seconds_+1.e-9<hold_seconds){active_=false;active_since_=-1;return false;}
        if(burst_seconds>0 && now<cooldown_until_){active_=false;return false;}
        if(active_since_<0){active_since_=now;started_=true;}
        active_=true;return true;
    }
};
// Identity equality is supplied by the existing native frame, including its
// model/address so a recycled script handle cannot inherit another horse's hold.
template<class Identity> struct Scope {
    Identity horse{};
    int rider=0;
    std::uint64_t race_token=0;
    bool operator==(const Scope&) const = default;
};
template<class Identity> class ScopedGate : public Gate {
    Scope<Identity> scope_{};
    bool bound_=false;
public:
    // A failed native identity read is not a new owner. Keep a timed gate
    // only while the race still names the same horse handle, rider and token;
    // a later valid identity must still pass Bind's full model/address check.
    bool CanRetain(int horse,int rider,std::uint64_t race_token) const {
        return bound_ && horse && scope_.horse.horse==horse && scope_.rider==rider && scope_.race_token==race_token;
    }
    bool Bind(const Scope<Identity>& scope) {
        if(bound_ && scope_==scope)return false;
        Reset();scope_=scope;bound_=true;return true;
    }
};
} // namespace horse_races::host_boost
