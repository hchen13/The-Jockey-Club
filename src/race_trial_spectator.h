#pragma once
#include "world/identity.h"

namespace horse_races::trial_spectator {
// The same native streaming focus and ped-follow camera used by bookmaker
// spectators. The player stays parked; neither participant is moved to keep
// a remote mount awake. Gameplay camera look remains under native control.
class Session {
    world::Identity player_{},mount_{},rider_{},horse_{};
    bool thaw_player_=false,thaw_mount_=false,focus_=false;
    static bool Hold(const world::Identity& id) {
        if(!id.Known() || ENTITY::_IS_ENTITY_FROZEN(id.entity))return false;
        ENTITY::SET_ENTITY_VELOCITY(id.entity,0,0,0);
        ENTITY::FREEZE_ENTITY_POSITION(id.entity,true);
        return true;
    }
public:
    bool Active() const {return focus_;}
    Ped ObservedHorse() const {
        if(!focus_ || DLC::GET_IS_LOADING_SCREEN_ACTIVE() || NETWORK::NETWORK_IS_SESSION_ACTIVE() ||
            PLAYER::PLAYER_PED_ID()!=player_.entity || !world::Still(player_) ||
            !world::Still(rider_) || !world::Still(horse_))return 0;
        return horse_.entity;
    }
    void Stop() {
        if(focus_) {
            STREAMING::CLEAR_FOCUS();
            if(!DLC::GET_IS_LOADING_SCREEN_ACTIVE() && !NETWORK::NETWORK_IS_SESSION_ACTIVE() &&
                PLAYER::PLAYER_PED_ID()==player_.entity && world::Still(player_))
                CAM::SET_GAMEPLAY_CAM_FOLLOW_PED_THIS_UPDATE(player_.entity);
        }
        // A save load can reuse a handle. Never thaw its replacement, or a
        // participant that another system had already frozen before entry.
        if(thaw_mount_ && world::Still(mount_))ENTITY::FREEZE_ENTITY_POSITION(mount_.entity,false);
        if(thaw_player_ && world::Still(player_))ENTITY::FREEZE_ENTITY_POSITION(player_.entity,false);
        player_=mount_=rider_=horse_={};thaw_player_=thaw_mount_=focus_=false;
    }
    bool Begin(Ped player,Ped mount,Ped rider,Ped horse) {
        Stop();
        player_=world::Identify(player);mount_=world::Identify(mount);
        rider_=world::Identify(rider);horse_=world::Identify(horse);
        if(!player_.Known() || !rider_.Known() || !horse_.Known() ||
            (mount && !mount_.Known()) || player==rider || mount==horse) {
            Stop();return false;
        }
        thaw_player_=Hold(player_);
        if(mount!=player)thaw_mount_=Hold(mount_);
        STREAMING::SET_FOCUS_ENTITY(horse);focus_=true;
        return Tick();
    }
    bool Tick() {
        if(!focus_)return false;
        if(DLC::GET_IS_LOADING_SCREEN_ACTIVE() || NETWORK::NETWORK_IS_SESSION_ACTIVE() ||
            PLAYER::PLAYER_PED_ID()!=player_.entity || !world::Still(player_) ||
            (mount_.Known() && !world::Still(mount_)) ||
            !world::Still(rider_) || !world::Still(horse_)) {
            Stop();return false;
        }
        // Keep collision streaming at the actual NPC, including after a trial
        // redispatch. This does not replace physics with coordinate writes.
        STREAMING::REQUEST_COLLISION_AT_COORD(ENTITY::GET_ENTITY_COORDS(horse_.entity,true,false));
        if(ENTITY::GET_ENTITY_COLLISION_DISABLED(horse_.entity) &&
            ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(horse_.entity) &&
            !ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(horse_.entity) &&
            !ENTITY::_IS_ENTITY_FROZEN(horse_.entity)) {
            // The existing far-rider handoff uses this same loaded-world
            // wake. A camera orbit must not leave our focused horse dormant.
            ENTITY::SET_ENTITY_COLLISION(horse_.entity,true,true);
            PHYSICS::ACTIVATE_PHYSICS(horse_.entity);
        }
        CAM::SET_GAMEPLAY_CAM_FOLLOW_PED_THIS_UPDATE(rider_.entity);
        return true;
    }
};
} // namespace horse_races::trial_spectator
