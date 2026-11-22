#pragma once

#include "game_natives.h"
#include "race_live_overtake.h"
#include "world/scan.h"
#include <array>
#include <chrono>
#include <vector>

namespace horse_races::live_overtake {
// One shared pool snapshot per quarter-second for all riders. This is a read-
// only input to the private-line planner; no ambient actor is ever deleted.
inline const std::vector<Obstacle>& NearbyEntities(){
    using Clock=std::chrono::steady_clock;
    static Clock::time_point next{};
    static std::vector<Obstacle> items;
    if(Clock::now()<next)return items;
    next=Clock::now()+std::chrono::milliseconds(250);
    items.clear();
    static std::array<int,4096> handles{};
    const int peds=std::clamp(world::scan::Peds(handles.data(),static_cast<int>(handles.size())),0,
        static_cast<int>(handles.size()));
    for(int i=0;i<peds;++i){
        const Ped ped=handles[static_cast<std::size_t>(i)];
        if(!ped || !ENTITY::DOES_ENTITY_EXIST(ped) || ENTITY::IS_ENTITY_DEAD(ped))continue;
        items.push_back({ENTITY::GET_ENTITY_COORDS(ped,true,false),PED::IS_PED_HUMAN(ped)?.65F:1.25F,ped});
    }
    const int vehicles=std::clamp(world::scan::Vehicles(handles.data(),static_cast<int>(handles.size())),0,
        static_cast<int>(handles.size()));
    for(int i=0;i<vehicles;++i){
        const Vehicle vehicle=handles[static_cast<std::size_t>(i)];
        if(!vehicle || !ENTITY::DOES_ENTITY_EXIST(vehicle) || ENTITY::IS_ENTITY_DEAD(vehicle))continue;
        items.push_back({ENTITY::GET_ENTITY_COORDS(vehicle,true,false),2.35F,vehicle});
    }
    return items;
}
} // namespace horse_races::live_overtake
