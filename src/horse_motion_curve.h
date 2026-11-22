#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

// Role-blind controller: no player/NPC, race placement, opponent distance,
// character name, or world-coordinate exceptions enter the calculation.
// Unknown engine environmental limits remain UNKNOWN, never inferred from km/h.
namespace horse_motion {
struct Config {
    std::array<float,4> low_speed_blends{2.41F,2.55F,2.68F,2.84F};
    std::array<float,5> high_speed_forces{1.70F,3.18F,4.77F,6.44F,8.22F};
    std::array<float,4> low_accel_seconds{12,8,5,2.5F};
    std::array<float,5> high_accel_responses{.01F,.02F,.04F,.08F,.15F};
    std::array<float,10> force_ramp_seconds{18,15,12.5F,10.5F,8.5F,7,5.5F,4,2.3F,.5F};
    float ramp_start_blend=1.5F, open_blend=3.2F, desired_blend=2.9F, force_start_kmh=40;
    // The gait an AI racer may reach; 0 = none. The same curve drives the
    // player's mount and every AI horse, but a rider's spurs take the mount
    // to a move blend ratio of 2.9 while a task-driven horse holds 3.0 - and
    // that alone made a jockey of the player's rank 2 km/h faster (70.2
    // against 68.0 km/h, 2026-09-14). Set on the AI roles only.
    float rider_gait_cap=0;
    // Force taken from an AI racer's push, in the units of high_speed_forces;
    // 0 = none. For riding the AI at the sprint gait after all: at 3.0 the
    // rider sits the sprint (the crouch and the spurs a player's mount shows)
    // and the mount is 2.2 km/h faster than at 2.9 - about 1.1 of force at
    // the top of the ladder (8.22 buys 70.35 over the vanilla 53.5). Set on
    // the AI roles only; the player's force is untouched (2026-09-17).
    float rider_force_offset=0;
    // Engaging and releasing on the same number costs the whole force on a
    // single bad velocity read, and discarding the ramp costs it again on the
    // way back: at acceleration 8 that is four seconds bought back for a tenth
    // of a second under the line. Measured on a 189 s race: 15.9 s at zero
    // force plus 20.4 s of partial ramp, from dips with unrelated causes.
    // Hold the force down to force_hold_kmh; only a SUSTAINED dip below
    // force_reset_kmh discards what has been earned.
    float force_hold_kmh=33, force_reset_kmh=30;
    double force_reset_seconds=1.0;
};
struct Environment {
    bool unsafe=false; // Airborne, falling, jumping, ragdoll, swimming, pause.
    bool in_water=false; // Wading too: never propel through native water drag.
    // Populated only by a verified engine provider, NOT town circles or guesses.
    std::optional<float> max_blend, max_speed_kmh;
};
struct Input {
    double now=0;
    int speed_level=5, acceleration_level=5; // Displayed 1..10; invalid is not clamped.
    float speed_kmh=0, desired_blend=0;
    bool acceleration_requested=false;
    Environment environment;
    bool braking_requested=false; // Explicit rider intent, not a role/position exception.
    // A verified, positive NPC race speed override may retain an already
    // established force ramp through a speed dip. It never starts one early.
    bool hold_race_override_force=false;
};
struct State {
    std::optional<double> acceleration_started, force_started, last_tick;
    std::optional<double> force_low_since; // When the current sustained dip began.
    void ResetRamps() { acceleration_started.reset();force_started.reset();force_low_since.reset(); }
};
struct Output {
    std::optional<float> max_blend, desired_blend;
    float forward_force=0;
    bool suppressed=false;
    bool operator==(const Output&) const = default;
};
inline float Smooth(float value) {value=std::clamp(value,0.0F,1.0F);return value*value*(3-2*value);}
// A dip only discards the ramp once it has lasted. A momentary under-read of
// velocity must not cost seconds of rebuilding.
inline void DecayForceRamp(const Config& c,State& state,const Input& in) {
    if(in.speed_kmh>=c.force_reset_kmh){state.force_low_since.reset();return;}
    if(!state.force_low_since)state.force_low_since=in.now;
    if(in.now-*state.force_low_since>=c.force_reset_seconds)state.force_started.reset();
}
inline bool ValidCap(const std::optional<float>& v) {return !v || (std::isfinite(*v) && *v>=0);}
inline Output Evaluate(const Config& c, State& state, const Input& in)
{
    Output out;
    const bool invalid=!std::isfinite(in.now) || !std::isfinite(in.speed_kmh) || in.speed_kmh<0 ||
        !std::isfinite(in.desired_blend) || in.speed_level<1 || in.speed_level>10 ||
        in.acceleration_level<1 || in.acceleration_level>10 ||
        !ValidCap(in.environment.max_blend) || !ValidCap(in.environment.max_speed_kmh);
    if(invalid || in.environment.unsafe || in.environment.in_water) {
        state.ResetRamps();state.last_tick.reset();out.suppressed=true;return out;
    }
    // Do not count a pause, missing entity, or long unobserved interval as acceleration.
    if(state.last_tick && (in.now<*state.last_tick || in.now-*state.last_tick>.5))state.ResetRamps();
    state.last_tick=in.now;
    float acceleration_progress=1;
    if(in.acceleration_level<=4) {
        if(in.speed_kmh<3)state.acceleration_started.reset();
        else if(!state.acceleration_started)state.acceleration_started=in.now;
        acceleration_progress=state.acceleration_started ? Smooth(static_cast<float>(in.now-*state.acceleration_started)/
            c.low_accel_seconds[static_cast<std::size_t>(in.acceleration_level-1)]) : 0;
    }
    if(in.speed_level<=4 || in.acceleration_level<=4) {
        float cap=in.speed_level<=4?c.low_speed_blends[static_cast<std::size_t>(in.speed_level-1)]:c.open_blend;
        if(in.acceleration_level<=4)cap=std::min(cap,c.ramp_start_blend+(c.open_blend-c.ramp_start_blend)*acceleration_progress);
        out.max_blend=cap;
    }
    if(in.environment.max_blend)out.max_blend=std::min(out.max_blend.value_or(*in.environment.max_blend),*in.environment.max_blend);
    const bool native_restricted=(in.environment.max_blend && *in.environment.max_blend<c.desired_blend) ||
        (in.environment.max_speed_kmh && in.speed_kmh>=*in.environment.max_speed_kmh);
    // Do not add propulsion or acceleration assistance against either rider's
    // explicit braking command. Existing rank caps/ramp rules remain shared.
    if(in.braking_requested) {DecayForceRamp(c,state,in);return out;}
    if(in.acceleration_level>=6 && in.acceleration_requested && in.speed_kmh>=3 && !native_restricted) {
        const float target=std::min(c.desired_blend,in.environment.max_blend.value_or(c.desired_blend));
        if(in.desired_blend<target)out.desired_blend=in.desired_blend+(target-in.desired_blend)*
            c.high_accel_responses[static_cast<std::size_t>(in.acceleration_level-6)];
    }
    if(native_restricted) {state.force_started.reset();return out;}
    // Engage at force_start_kmh; once engaged, keep pulling down to
    // force_hold_kmh. The gap is the hysteresis that a one-sample velocity
    // error cannot cross.
    if(in.speed_level>=6 && (in.speed_kmh>=c.force_start_kmh ||
        (state.force_started && (in.speed_kmh>=c.force_hold_kmh || in.hold_race_override_force)))) {
        state.force_low_since.reset();
        if(!state.force_started)state.force_started=in.now;
        const float progress=Smooth(static_cast<float>(in.now-*state.force_started)/
            c.force_ramp_seconds[static_cast<std::size_t>(in.acceleration_level-1)]);
        out.forward_force=c.high_speed_forces[static_cast<std::size_t>(in.speed_level-6)]*progress;
    } else DecayForceRamp(c,state,in);
    return out;
}
} // namespace horse_motion
