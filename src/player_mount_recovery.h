#pragma once
#include "horse_motion_native.h"
#include "player_physics_handoff.h"

namespace horse_motion {
// A teleport/stable hand-back can leave the occupied player horse kinematic.
// SET_ENTITY_COLLISION alone acknowledges its flag for this frame, but that is
// not evidence that the next engine update retains a live physics instance.
// Never relocate a horse, clear its tasks, touch an NPC, or write during a fade.
class PlayerMountRecovery {
public:
    struct Result {
        bool pending=false,started=false,requested=false,recovered=false,teleported=false;
        bool collision_disabled=false,is_static=false,world_ready=false;
        unsigned attempts=0;
        player_physics::Result handoff{};
    };
private:
    Identity owner_{};
    bool pending_=false;
    std::optional<double> safe_since_,healthy_since_,last_request_,last_time_;
    std::optional<Vector3> last_position_;
    unsigned attempts_=0;
public:
    void Reset(){*this={};}
    using Repair=player_physics::Result (*)(const Identity&);
    Result Tick(Ped player,Ped mount,double now,bool control_ready,const Vector3* position=nullptr,Repair repair=nullptr) {
        Result result;
        if(!std::isfinite(now) || !player || player!=PLAYER::PLAYER_PED_ID() ||
            !ENTITY::DOES_ENTITY_EXIST(player) || ENTITY::IS_ENTITY_DEAD(player) ||
            !PED::IS_PED_ON_MOUNT(player) || PED::GET_MOUNT(player)!=mount) {
            Reset();return result;
        }
        const auto identity=Identify(mount);
        if(!identity.Valid() || ENTITY::IS_ENTITY_DEAD(mount)){Reset();return result;}
        if(identity!=owner_ || (last_time_ && now<*last_time_)) {Reset();owner_=identity;}
        // Native Trainer's waypoint teleport does not fade or take control.
        // Its ground search moves the mount through heights with 100 ms waits.
        // Detect discontinuity BEFORE waiting for a collision fault so the
        // speed curve stays out of that placement/streaming transaction.
        if(position && std::isfinite(position->x) && std::isfinite(position->y) && std::isfinite(position->z)) {
            if(last_position_ && last_time_ && now-*last_time_<=2) {
                const double distance=std::hypot(double(position->x)-last_position_->x,
                    double(position->y)-last_position_->y,double(position->z)-last_position_->z);
                result.teleported=distance>30+60*(now-*last_time_);
            }
            last_position_=*position;
        } else last_position_.reset();
        last_time_=now;
        result.collision_disabled=ENTITY::GET_ENTITY_COLLISION_DISABLED(mount)!=0;
        result.is_static=ENTITY::IS_ENTITY_STATIC(mount)!=0;
        if((result.collision_disabled || result.teleported) && !pending_) {
            pending_=result.started=true;attempts_=0;
            safe_since_.reset();healthy_since_.reset();last_request_.reset();
        }
        result.pending=pending_;result.attempts=attempts_;
        if(!pending_)return result; // A healthy resting horse is not ours to wake.
        if(result.teleported){safe_since_.reset();healthy_since_.reset();}

        // An intentional freeze, loading screen, jump/fall or water transition
        // remains under its owner's control. In particular, do not fight a
        // trainer while it is still moving/placing the player during a teleport.
        const bool safe=control_ready && !HUD::IS_PAUSE_MENU_ACTIVE() &&
            !ENTITY::_IS_ENTITY_FROZEN(mount) && !ENTITY::IS_ENTITY_IN_AIR(mount,0) &&
            !PED::IS_PED_FALLING(mount) && !PED::IS_PED_JUMPING(mount) &&
            !PED::IS_PED_RAGDOLL(mount) && !PED::IS_PED_SWIMMING(mount) &&
            !ENTITY::IS_ENTITY_IN_WATER(mount);
        result.world_ready=ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(mount) &&
            !ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(mount);
        if(!safe || !result.world_ready) {
            safe_since_.reset();healthy_since_.reset();return result;
        }
        if(!safe_since_)safe_since_=now;
        if(!result.collision_disabled && !result.is_static) {
            if(!healthy_since_)healthy_since_=now;
            // Read back across engine updates, not immediately after a native.
            if(now-*healthy_since_>=.5) {
                result.pending=pending_=false;result.recovered=true;
                safe_since_.reset();healthy_since_.reset();last_request_.reset();
            }
            return result;
        }
        healthy_since_.reset();
        if(now-*safe_since_<.25 || (last_request_ && now-*last_request_<.5))return result;
        // Recheck ownership immediately before the small, player-only native
        // transaction. No yield, task clearing, position or velocity write.
        if(Identify(mount)!=owner_ || PED::GET_MOUNT(player)!=mount)return result;
        // Superficial collision/dynamic/ACTIVATE calls already failed hundreds
        // of times in the incident. Only the validated upstream engine mode
        // transition can retire its recurring collision-disable decision.
        if(repair)result.handoff=repair(owner_);
        last_request_=now;result.requested=true;result.attempts=++attempts_;
        return result; // Even a successful immediate read is still unverified.
    }
};
} // namespace horse_motion
