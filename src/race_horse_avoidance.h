#pragma once
#include "world/identity.h"

namespace horse_races::race_avoidance {
// Keep the established assistance mode. These modes are not a monotonic
// collision/braking-strength scale; lowering the enum did not solve traffic.
inline constexpr int kRaceLevel=2;
inline constexpr const char* kRaceName="FULLASSIST(2)";
// beat_horse_race.c func_77 -> func_235 refreshes this on the racing horse.
// It suppresses horse evasion, not physical collision or all navigation avoidance.
inline constexpr int kDisableHorseEvasion=317;
struct Lease {
    world::Identity owner;
    world::Identity rider_owner;
    bool Begin(Ped horse) {
        const auto current=world::Identify(horse);
        const Ped player=PLAYER::PLAYER_PED_ID();
        if(!current.Known() || horse==player || (player && PED::GET_MOUNT(player)==horse))return false;
        if(owner==current)return true;
        Reset();owner=current;
        PED::SET_HORSE_AVOIDANCE_LEVEL(horse,kRaceLevel);
        return true;
    }
    bool Tick(Ped rider) {
        if(!world::Still(owner))return false;
        const Ped player=PLAYER::PLAYER_PED_ID();
        if(!rider || rider==player || owner.entity==player ||
           (player && PED::GET_MOUNT(player)==owner.entity))return false;
        const auto current=world::Identify(rider);
        if(!current.Known() || (rider_owner.Known() && rider_owner!=current) ||
           ENTITY::IS_ENTITY_DEAD(rider) || ENTITY::IS_ENTITY_DEAD(owner.entity) ||
           !PED::IS_PED_ON_MOUNT(rider) || PED::GET_MOUNT(rider)!=owner.entity)return false;
        rider_owner=current;
        PED::SET_PED_RESET_FLAG(owner.entity,kDisableHorseEvasion,true);
        return true;
    }
    void Reset() {
        if(world::Still(owner))PED::RESET_HORSE_AVOIDANCE_LEVEL_TO_DEFAULT(owner.entity);
        // Reset flags expire naturally; writing false could cancel another script.
        owner={};rider_owner={};
    }
};
} // namespace horse_races::race_avoidance
