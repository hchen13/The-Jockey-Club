#pragma once
#include <cstdint>
#include <main.h>
#include "game_natives.h"
#include "horse_attributes.h"
#include "horse_motion_curve.h"
#include "horse_speed_limits.h"
#include "horse_motion_limits.h"
#include "speed_volume_limits.h"
#include "race_breeds.h"
#include "horse_native_constraint_probe_windows.h"
#include "horse_snow_grid_probe.h"
#include "horse_force_trial.h"
#include "horse_ground_snow_candidate.h"

// One native bridge for both the player's mount and our active rival's mount.
// No world-wide ped enumeration; callers provide the two in-scope entities.
namespace horse_motion {
using LimitReader=engine_limits::Snapshot (*)(std::uintptr_t);
inline LimitReader read_motion_limits=engine_limits::Capture;
using ConstraintReader=native_constraint_probe::Snapshot (*)(std::uintptr_t);
inline ConstraintReader read_native_constraints=native_constraint_probe::Capture;
using VolumeReader=speed_volumes::Snapshot (*)(std::uintptr_t,std::uintptr_t,speed_volumes::Point,bool);
inline speed_volumes::Snapshot CaptureSpeedVolumes(std::uintptr_t horse,std::uintptr_t rider,speed_volumes::Point p,bool ignore) {
    static speed_volumes::Cache cache;
    return cache.Capture(horse,rider,p,ignore,[](int handle) {
        return reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(handle));
    });
}
inline VolumeReader read_speed_volumes=CaptureSpeedVolumes;
struct Identity {
    Ped horse=0; Hash model=0; std::uintptr_t address=0;
    bool operator==(const Identity&) const = default;
    bool Valid() const {return horse && model && address;}
};
inline Identity Identify(Ped horse) {
    if(!horse || !ENTITY::DOES_ENTITY_EXIST(horse))return {};
    return {horse,ENTITY::GET_ENTITY_MODEL(horse),reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(horse))};
}
// A town speed volume must not decide a race - for anybody. The roster is the
// player pair followed by one pair per rival, and EVERY actor on it is exempted
// for as long as the race is running.
//
// This briefly exempted only the rivals, on the theory that the town slowdown on
// the player was a track feature. It was not: it made Valentine and Blackwater
// races a handicap that applied to exactly one competitor. Whoever is racing is
// exempt; the volumes return the moment the race ends.
//
// The cap here used to be twelve entities, sized for the six-horse grid this
// began as. A field of five rivals is fourteen, so the whole function bailed on
// its first line and NOBODY was exempted - which is how the host came to be
// pinned to a walking gait through Armadillo while asking for 66 km/h. It is
// now sized by the largest grid a route can declare.
inline constexpr std::size_t kMaxExemptEntities=
    std::max<std::size_t>(20,2*(2+static_cast<std::size_t>(horse_races::field_race::kMaxFieldRivals)));
// A bare false told the recording only that nobody was exempted. The oversized
// roster above went undiagnosed for an entire race because "false" and "not
// applicable" looked identical in the log; every refusal now says which.
enum class ExemptionRefusal {None,Inactive,RosterShape,MissingActor,DuplicateActor,NotMounted,PlayerOnly};
inline const char* ExemptionRefusalName(ExemptionRefusal why) {
    switch(why) {
    case ExemptionRefusal::Inactive:return "not-open-circuit";
    case ExemptionRefusal::RosterShape:return "roster-size";
    case ExemptionRefusal::MissingActor:return "actor-missing-or-dead";
    case ExemptionRefusal::DuplicateActor:return "actor-listed-twice";
    case ExemptionRefusal::NotMounted:return "rider-not-on-their-mount";
    case ExemptionRefusal::PlayerOnly:return "nobody-to-exempt";
    default:return "none";
    }
}
inline bool ApplyCircuitSpeedExemptionToRoster(bool active,const std::vector<Ped>& actors,
    ExemptionRefusal* why=nullptr) {
    const auto refuse=[&](ExemptionRefusal reason){if(why)*why=reason;return false;};
    if(why)*why=ExemptionRefusal::None;
    if(!active)return refuse(ExemptionRefusal::Inactive);
    if(actors.size()<2 || actors.size()>kMaxExemptEntities || actors.size()%2)
        return refuse(ExemptionRefusal::RosterShape);
    for(Ped p:actors) {
        if(!p || !ENTITY::DOES_ENTITY_EXIST(p) || ENTITY::IS_ENTITY_DEAD(p))
            return refuse(ExemptionRefusal::MissingActor);
        if(std::count(actors.begin(),actors.end(),p)!=1)return refuse(ExemptionRefusal::DuplicateActor);
    }
    for(std::size_t i=0;i<actors.size();i+=2)
        if(!PED::IS_PED_ON_MOUNT(actors[i]) || PED::GET_MOUNT(actors[i])!=actors[i+1])
            return refuse(ExemptionRefusal::NotMounted);
    for(std::size_t i=0;i<actors.size();++i)PED::SET_PED_RESET_FLAG(actors[i],154,true);
    return true;
}
struct NativeFrame {
    struct PhysicsActivation {
        bool eligible=false,requested=false,dynamic_requested=false,collision_loaded=false,load_collision_flagged=false;
        std::uint64_t requests=0;
        std::optional<bool> static_before,static_after,collision_disabled_before,collision_disabled_after;
    };
    struct Diagnostics {
        float stamina=0,max_stamina=0,normalized_stamina=0,rider_desired_blend=0;
        float fear=-1,agitation=-1;             // eMotivationState 1 and 3, 0..1; read only.
        std::optional<std::uint32_t> ground_material; // The engine's material cast under the horse.
        std::optional<ground_snow_candidate::GroundSample> ground_sample; // Same-frame in-memory reuse only.
        int stamina_core=0,health=0,max_health=0,bonding_rank=-1;
        bool on_screen=false,visible=false,is_static=false,frozen=false,collision_disabled=false;
        Vector3 rider_position{};
        native_constraint_probe::Snapshot native_constraints;
        std::optional<snow_grid_probe::Sample> snow_grid_shadow;
    };
    Identity identity;
    Input input;
    Vector3 velocity{}, position{}, forward{};
    horse_attributes::Snapshot attributes;
    engine_limits::Snapshot limits;
    speed_volumes::Snapshot volumes;
    float current_blend_x=0, current_blend_y=0, submerged=0;
    int rider=0;
    bool horse_reset_317=false; // Read only for sampled diagnostics.
    std::array<bool,4> reset_flags{}; // horse154, horse174, rider154, rider174; observations only.
    bool current_blend_valid=false, valid=false,simulation_lease=false,player_recovery_pending=false;
    std::optional<Diagnostics> diagnostics;
    PhysicsActivation physics_activation;
};
inline NativeFrame Observe(Ped horse,double now,bool accelerate,bool diagnostics=false,bool query_volumes=false) {
    NativeFrame f;f.identity=Identify(horse);f.input.now=now;f.input.acceleration_requested=accelerate;
    if(!f.identity.Valid() || ENTITY::IS_ENTITY_DEAD(horse))return f;
    f.velocity=ENTITY::GET_ENTITY_VELOCITY(horse,0);
    f.forward=ENTITY::GET_ENTITY_FORWARD_VECTOR(horse);
    f.attributes.model=f.identity.model;
    f.attributes.current={ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,5),ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,6)};
    f.input.speed_level=f.attributes.current.Valid()?f.attributes.current.speed+1:0;
    f.input.acceleration_level=f.attributes.current.Valid()?f.attributes.current.acceleration+1:0;
    f.input.speed_kmh=std::hypot(f.velocity.x,f.velocity.y)*3.6F;
    f.input.desired_blend=TASK::GET_PED_DESIRED_MOVE_BLEND_RATIO(horse);
    f.input.environment.unsafe=HUD::IS_PAUSE_MENU_ACTIVE() || ENTITY::IS_ENTITY_IN_AIR(horse,0) ||
        PED::IS_PED_FALLING(horse) || PED::IS_PED_JUMPING(horse) || PED::IS_PED_RAGDOLL(horse) || PED::IS_PED_SWIMMING(horse);
    f.input.environment.in_water=ENTITY::IS_ENTITY_IN_WATER(horse);
    f.position=ENTITY::GET_ENTITY_COORDS(horse,true,false);
    f.limits=read_motion_limits(f.identity.address);
    if(diagnostics) {
        f.horse_reset_317=PED::GET_PED_RESET_FLAG(horse,317);
        f.attributes=horse_attributes::Read(horse);
        f.submerged=ENTITY::GET_ENTITY_SUBMERGED_LEVEL(horse);
    }
    if(diagnostics || query_volumes) {
        f.rider=PED::_GET_RIDER_OF_MOUNT(horse,true);
        f.reset_flags[0]=PED::GET_PED_RESET_FLAG(horse,154);
        f.reset_flags[1]=PED::GET_PED_RESET_FLAG(horse,174);
        if(f.rider && ENTITY::DOES_ENTITY_EXIST(f.rider)) {
            f.reset_flags[2]=PED::GET_PED_RESET_FLAG(f.rider,154);
            f.reset_flags[3]=PED::GET_PED_RESET_FLAG(f.rider,174);
            f.volumes=read_speed_volumes(f.identity.address,
                reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(f.rider)),
                {f.position.x,f.position.y,f.position.z},f.reset_flags[0] || f.reset_flags[2]);
        }
    }
    // The gait is read every frame: the host bonus gate holds it across
    // frames, and a read only on recording frames (every 0.1 s) left every
    // frame between them ineligible - the hold restarted ten times a second.
    f.current_blend_valid=PED::GET_PED_CURRENT_MOVE_BLEND_RATIO(horse,&f.current_blend_x,&f.current_blend_y);
    if(diagnostics) {
        NativeFrame::Diagnostics d;
        d.native_constraints=read_native_constraints(f.identity.address);
        d.stamina=PED::_GET_PED_STAMINA(horse);d.max_stamina=PED::_GET_PED_MAX_STAMINA(horse);
        d.fear=PED::_GET_PED_MOTIVATION(horse,1,0);d.agitation=PED::_GET_PED_MOTIVATION(horse,3,0);
        {
            float z=0;Hash material=0;int flags=0;
            const bool hit=invoke<BOOL>(0xBBE5B63EFFB08E68,f.position.x,f.position.y,f.position.z+1.5F,129,&z,&material,&flags)!=0;
            d.ground_sample=ground_snow_candidate::GroundSample{hit,static_cast<std::uint32_t>(material),z,flags};
            if(hit)
                d.ground_material=static_cast<std::uint32_t>(material);
        }
        d.normalized_stamina=PED::_GET_PED_STAMINA_NORMALIZED(horse);
        d.stamina_core=ATTRIBUTE::_GET_ATTRIBUTE_CORE_VALUE(horse,1);
        d.health=ENTITY::GET_ENTITY_HEALTH(horse);d.max_health=ENTITY::GET_ENTITY_MAX_HEALTH(horse,false);
        d.bonding_rank=ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,7);
        d.on_screen=ENTITY::IS_ENTITY_ON_SCREEN(horse);d.visible=ENTITY::IS_ENTITY_VISIBLE(horse);
        d.is_static=ENTITY::IS_ENTITY_STATIC(horse);d.frozen=ENTITY::_IS_ENTITY_FROZEN(horse);
        d.collision_disabled=ENTITY::GET_ENTITY_COLLISION_DISABLED(horse);
        if(f.rider && ENTITY::DOES_ENTITY_EXIST(f.rider)) {
            d.rider_position=ENTITY::GET_ENTITY_COORDS(f.rider,true,false);
            d.rider_desired_blend=TASK::GET_PED_DESIRED_MOVE_BLEND_RATIO(f.rider);
        }
        f.diagnostics=d;
    }
    f.valid=Identify(horse)==f.identity;
    if(f.valid && f.diagnostics && f.diagnostics->ground_sample)
        ground_snow_candidate::frame_samples.Remember(MISC::GET_FRAME_COUNT(),f.identity.address,
            f.identity.horse,f.identity.model,*f.diagnostics->ground_sample);
    return f;
}
// Apply only to NPC race frames AFTER the caller supplies its explicit braking
// flag. Low native desired gait alone is not a brake while an owned, positively
// verified race speed override is active. Outside that scope keep the original
// native-intent threshold; the earned burst also checks that threshold itself.
inline constexpr float kNpcFastRequestBlend=2.5F;
// Cached state of the race-owned task; never polls the world or projects a path.
// Absolute override is measured in metres per second, gait override in [0,3].
// A positive race override can retain propulsion even during a pace reduction;
// an actual vehicle stop or suspended/invalid task still withdraws it.
template<class Follower,class Recovery,class Command>
bool OwnedRaceOverrideIntent(const Follower& f,const Recovery& r,const Command& c,int rider) {
    const bool request_valid=std::isfinite(f.override_request) && f.override_request>0 &&
        f.override_request<=(f.override_absolute?speed_limits::kRequestedMps:3.F);
    return rider && f.rider.horse==rider && f.control && f.playback_readback &&
        f.status==decltype(f.status)::Running && !f.simulation_suspended && !f.vehicle_paused &&
        !r.propulsion_suspended && !c.vehicle_blocked && request_valid;
}
inline void RespectNativeNpcIntent(NativeFrame& f,bool owned_race_override=false) {
    const bool fast=f.valid && std::isfinite(f.input.desired_blend) &&
        (f.input.desired_blend>=kNpcFastRequestBlend || owned_race_override);
    f.input.acceleration_requested=f.input.acceleration_requested && fast;
    // The shared curve already respects braking before desired-gait assistance
    // AND positive force. Reuse that path while retaining all rank gait caps.
    f.input.braking_requested=f.input.braking_requested || !fast;
}
// A scoped wake-up request, not a persistent physics/collision override. Native
// static readback after the call is evidence; submitting the call is not proof.
// No global LOD edits, no forced velocity, no collision/freeze writes or retries
// inside a frame. Both participant roles use this identical bridge.
struct RacePhysicsKeeper {
    Identity owner;
    std::uint64_t token=0,requests=0;
    std::optional<double> last_request;
    bool last_activation_failed=false,load_collision_flagged=false;
    NativeFrame::PhysicsActivation Apply(const NativeFrame& f,Ped rider,bool active,std::uint64_t race_token) {
        NativeFrame::PhysicsActivation out;
        if(!active || !f.valid || !f.identity.Valid()) {owner={};token=requests=0;last_request.reset();last_activation_failed=false;load_collision_flagged=false;return out;}
        if(owner!=f.identity || token!=race_token) {owner=f.identity;token=race_token;requests=0;last_request.reset();last_activation_failed=false;load_collision_flagged=false;}
        out.load_collision_flagged=load_collision_flagged;
        out.requests=requests;
        if(f.input.environment.unsafe || f.input.environment.in_water ||
            !std::isfinite(f.input.speed_kmh) || f.input.speed_kmh<3 ||
            !rider || !ENTITY::DOES_ENTITY_EXIST(rider) || ENTITY::IS_ENTITY_DEAD(rider) ||
            !PED::IS_PED_ON_MOUNT(rider) || PED::GET_MOUNT(rider)!=owner.horse ||
            Identify(owner.horse)!=owner || ENTITY::IS_ENTITY_DEAD(owner.horse))return out;
        out.static_before=ENTITY::IS_ENTITY_STATIC(owner.horse);
        if(ENTITY::_IS_ENTITY_FROZEN(owner.horse))return out;
        out.eligible=true;
        if(!*out.static_before)return out;
        if(last_request && f.input.now>=*last_request && f.input.now-*last_request<.5)return out;
        out.collision_disabled_before=ENTITY::GET_ENTITY_COLLISION_DISABLED(owner.horse);
        if(Identify(owner.horse)!=owner)return out;
        // Merely waking a kinematic horse was ineffective in the real trace.
        // Request normal dynamic simulation only after a failed wake and only
        // with local collision loaded. Never force collision off/on, unfreeze,
        // relocate the horse or write its velocity. Same policy for both roles.
        out.collision_loaded=ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(owner.horse);
        // A racing horse 200 m or more from the player has no collision loaded
        // around it - the world streams around the player - so its physics
        // cannot wake and the force that makes its rank never lands: three
        // jockeys held 56 km/h all race on 2026-09-14 while the four beside the
        // player ran 70. The game's own way to keep a mission ped simulated
        // away from the player is this flag; once per horse per race.
        if(!out.collision_loaded && !load_collision_flagged) {
            ENTITY::SET_ENTITY_LOAD_COLLISION_FLAG(owner.horse,true);
            load_collision_flagged=out.load_collision_flagged=true;
        }
        if(last_activation_failed && out.collision_loaded) {
            ENTITY::SET_ENTITY_DYNAMIC(owner.horse,true);out.dynamic_requested=true;
        }
        PHYSICS::ACTIVATE_PHYSICS(owner.horse);
        last_request=f.input.now;
        out.requested=true;out.requests=++requests;
        if(Identify(owner.horse)==owner) {
            out.static_after=ENTITY::IS_ENTITY_STATIC(owner.horse);
            out.collision_disabled_after=ENTITY::GET_ENTITY_COLLISION_DISABLED(owner.horse);
            last_activation_failed=*out.static_after;
        }
        return out;
    }
};
struct AppliedFrame {
    Output requested;
    float force_applied=0;
    bool max_written=false, desired_written=false;
    bool volume_mirrored=false;
};
struct NativeController {
    State curve;
    Identity owner;
    horse_attributes::Ranks levels;
    bool owns_max=false;
    float previous_max=0,last_max=0;
    bool baseline_known=false;

    bool ReleaseCap() {
        if(!owns_max)return true;
        if(owner.Valid() && Identify(owner.horse)==owner) {
            const auto current=read_motion_limits(owner.address).script_max;
            if(!current && baseline_known)return false; // Retain verified ownership and retry.
            // Unsupported engine layouts retain the PREEXISTING native-only
            // compatibility behaviour. Do not disable the user's calibrated
            // low-level horses merely because new read-only diagnostics failed.
            // That fallback is NOT a verified native-cap restoration.
            if(!current || std::abs(*current-last_max)<.001F)
                PED::SET_PED_MAX_MOVE_BLEND_RATIO(owner.horse,previous_max);
        }
        owns_max=false;return true;
    }
    bool Reset() {curve={};if(!ReleaseCap())return false;owner={};levels={};return true;}

    AppliedFrame Apply(const Config& config,const NativeFrame& f,bool enabled,bool mirror_player_volume_control=false) {
        AppliedFrame result;
        if(!f.valid || (!enabled && !mirror_player_volume_control)) {Reset();return result;}
        if(owner!=f.identity || levels!=f.attributes.current) {
            if(!Reset())return result;owner=f.identity;levels=f.attributes.current;
        }
        if(enabled)result.requested=Evaluate(config,curve,f.input);
        else {
            curve={};
            result.requested.suppressed=f.input.environment.unsafe || f.input.environment.in_water;
        }
        if(result.requested.suppressed) {ReleaseCap();return result;}
        // Only the AI lacks the game's player-rider volume-control branch.
        // Keep the player's accepted additive-force behaviour exactly intact;
        // this is a gait ceiling, NOT a new km/h ceiling or boost suppression.
        if(mirror_player_volume_control && f.volumes.status==speed_volumes::Status::Known &&
            f.volumes.max_blend && std::isfinite(*f.volumes.max_blend) &&
            *f.volumes.max_blend>=0 && *f.volumes.max_blend<3 && f.limits.script_max) {
            const float external=owns_max && std::abs(*f.limits.script_max-last_max)<.001F?
                previous_max:*f.limits.script_max;
            const float cap=std::min(*f.volumes.max_blend,external);
            result.requested.max_blend=std::min(result.requested.max_blend.value_or(cap),cap);
            if(result.requested.desired_blend)result.requested.desired_blend=std::min(*result.requested.desired_blend,cap);
            result.volume_mirrored=true;
        }
        // The rider's gait, for an AI racer: equal force, equal gait, equal speed.
        if(enabled && config.rider_gait_cap>0 && std::isfinite(config.rider_gait_cap) && config.rider_gait_cap<3) {
            const float cap=config.rider_gait_cap;
            result.requested.max_blend=std::min(result.requested.max_blend.value_or(cap),cap);
            if(result.requested.desired_blend)result.requested.desired_blend=std::min(*result.requested.desired_blend,cap);
        }
        // Or the AI racer rides the sprint gait and gives its extra speed back
        // as force, so the rider sits the sprint the player's does.
        if(enabled && config.rider_force_offset>0 && std::isfinite(config.rider_force_offset) && result.requested.forward_force>0)
            result.requested.forward_force=std::max(0.0F,result.requested.forward_force-config.rider_force_offset);
        if(result.requested.max_blend) {
            if(!owns_max) {
                baseline_known=f.limits.script_max.has_value();
                previous_max=f.limits.script_max.value_or(12.2F); // Existing native-only compatibility fallback.
            } else if(!f.limits.script_max && baseline_known) {result.requested.suppressed=true;return result;}
            // Preserve the last externally supplied value for cleanup. Do not
            // introduce another active-town clamp: the user's accepted player
            // calibration (including the native slowdown) stays unchanged.
            if(f.limits.script_max && std::abs(*f.limits.script_max-last_max)>=.001F) {
                previous_max=*f.limits.script_max;baseline_known=true;
            }
            last_max=*result.requested.max_blend;
            PED::SET_PED_MAX_MOVE_BLEND_RATIO(owner.horse,last_max);
            owns_max=result.max_written=true;
        } else if(!ReleaseCap()) {result.requested.suppressed=true;return result;}
        if(result.requested.desired_blend) {
            TASK::SET_PED_DESIRED_MOVE_BLEND_RATIO(owner.horse,*result.requested.desired_blend);
            result.desired_written=true;
        }
        const float length=std::hypot(f.forward.x,f.forward.y);
        const float motion=f.forward.x*f.velocity.x+f.forward.y*f.velocity.y;
        // A race override may continue an already established force after a
        // horse is stopped or briefly pushed back. It cannot seed a new ramp
        // while standing or change player/non-race behaviour.
        const bool held_force=f.input.hold_race_override_force && curve.force_started.has_value();
        if(result.requested.forward_force>.0001F && std::isfinite(length) && length>.01F &&
            std::isfinite(motion) && (motion>0 || held_force)) {
            const float force=result.requested.forward_force;
            force_trial::probe.Submit({owner.horse,owner.model,owner.address},f.rider,
                {f.forward.x/length*force,f.forward.y/length*force,0.0F},f.input.now,
                [&](force_trial::Vec v) {
                    ENTITY::APPLY_FORCE_TO_ENTITY(owner.horse,1,v.x,v.y,v.z,
                        0.0F,0.0F,0.0F,0,false,true,true,false,true);
                },[&](force_trial::Vec v) {
                    // F11 trial lease only. Its final false must not recurse
                    // into attached children (the rider). No magnitude or Z
                    // change; normal races always take the original ABI above.
                    ENTITY::APPLY_FORCE_TO_ENTITY_CENTER_OF_MASS(owner.horse,1,v.x,v.y,v.z,0,false,true,false);
                });
            result.force_applied=force;
        }
        return result;
    }
};
} // namespace horse_motion
