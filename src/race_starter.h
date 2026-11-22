#pragma once
#include "race_destination.h"
#include "game_natives.h"
#include "race_localization.h"
#include <main.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace horse_races::starter {
// A race starter standing beside the grid. He raises a Cattleman during the
// countdown and fires one shot straight up as the field is released.
//
// This exists because no frontend sound set carries a general revolver report:
// every gunshot literal in the shipped scripts belongs to a mission's own
// soundset, and weapon fire is engine side. Firing a real weapon is therefore
// the only way to get the actual sound, and it also reads correctly on screen.
inline constexpr std::array<const char*,4> kModels{
    "A_M_M_BLWTOWNFOLK_01","A_M_M_RHDTOWNFOLK_01","A_M_M_STRTOWNFOLK_01","A_M_M_VALTOWNFOLK_01"};
inline constexpr float kSideMetres=9.0F;   // Clear of the racing line, still in shot.
inline constexpr float kBackMetres=2.5F;
inline constexpr float kAimMetres=45.0F;   // Straight up; never at a person.

struct Starter {
    Ped ped=0;
    Hash model=0;
    BYTE* address=nullptr;
    bool requested=false,fired=false,aimed=false;
    std::string error;

    bool Alive() const {
        return ped && address && ENTITY::DOES_ENTITY_EXIST(ped) &&
            getScriptHandleBaseAddress(ped)==address && ENTITY::GET_ENTITY_MODEL(ped)==model &&
            !ENTITY::IS_ENTITY_DEAD(ped);
    }
    void Request(int pick) {
        if(requested)return;
        model=MISC::GET_HASH_KEY(kModels[static_cast<std::size_t>(std::clamp(pick,0,3))]);
        if(!STREAMING::IS_MODEL_VALID(model)){error="starter model invalid";return;}
        STREAMING::REQUEST_MODEL(model,false);requested=true;
    }
    // Only ever placed beside the line the field is about to leave, never on it.
    bool Create(Vector3 start,float heading) {
        if(ped || !requested || !model)return false;
        if(!STREAMING::HAS_MODEL_LOADED(model))return false;
        if(!destination::Finite(start) || !std::isfinite(heading))
            {error="starter grid pose unavailable";return false;}
        const float radians=heading*.01745329252F;
        const Vector3 forward{-std::sin(radians),std::cos(radians),0};
        Vector3 spot{start.x-forward.y*kSideMetres-forward.x*kBackMetres,
            start.y+forward.x*kSideMetres-forward.y*kBackMetres,start.z};
        STREAMING::REQUEST_COLLISION_AT_COORD(spot);
        float z=0;Vector3 normal{};
        if(!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(spot.x,spot.y,spot.z+3,&z,&normal) ||
            !std::isfinite(z) || std::abs(z-spot.z)>2.5F || normal.z<.85F)
            {error="no safe ground beside the grid";return false;}
        spot.z=z;
        const Ped created=PED::CREATE_PED(model,spot,heading+90,false,true,true,true);
        if(!created || !ENTITY::DOES_ENTITY_EXIST(created)){error="starter creation failed";return false;}
        ped=created;address=getScriptHandleBaseAddress(created);
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped,true,true);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped,true);
        PED::SET_PED_KEEP_TASK(ped,true);
        ENTITY::SET_ENTITY_INVINCIBLE(ped,true);
        PED::_SET_PED_PROMPT_NAME(ped,i18n::Literal(i18n::Text("Starter","发令员")));
        WEAPON::GIVE_WEAPON_TO_PED(ped,MISC::GET_HASH_KEY("WEAPON_REVOLVER_CATTLEMAN"),
            12,true,false,0,false,.5F,1.0F,0,false,0,false);
        WEAPON::SET_PED_INFINITE_AMMO(ped,true,MISC::GET_HASH_KEY("WEAPON_REVOLVER_CATTLEMAN"));
        PED::SET_PED_ACCURACY(ped,0); // He is shooting at the sky, not at anything.
        return true;
    }
    Vector3 Sky() const {
        const auto p=ENTITY::GET_ENTITY_COORDS(ped,true,false);
        return {p.x,p.y,p.z+kAimMetres};
    }
    // Gun comes up on the first visible countdown second, so the shot itself
    // lands on GO instead of starting to draw there.
    void Aim() {
        if(aimed || !Alive())return;
        WEAPON::SET_CURRENT_PED_WEAPON(ped,MISC::GET_HASH_KEY("WEAPON_REVOLVER_CATTLEMAN"),true,0,false,false);
        TASK::TASK_AIM_GUN_AT_COORD(ped,Sky(),6000,false,false);
        aimed=true;
    }
    void Fire() {
        if(fired || !Alive())return;
        TASK::TASK_SHOOT_AT_COORD(ped,Sky(),1200,MISC::GET_HASH_KEY("FIRING_PATTERN_SINGLE_SHOT"),1);
        fired=true;
    }
    // Never vanish in front of the player: the same rule the departing racers use.
    bool Retire() {
        if(!ped)return true;
        if(!Alive()){ped=0;address=nullptr;return true;}
        if(!CAM::IS_SCREEN_FADED_OUT() && ENTITY::IS_ENTITY_VISIBLE(ped) &&
            ENTITY::IS_ENTITY_ON_SCREEN(ped))return false;
        Ped doomed=ped;PED::DELETE_PED(&doomed);ped=0;address=nullptr;return true;
    }
    void Release() {
        if(model)STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
        ped=0;address=nullptr;model=0;requested=fired=aimed=false;error.clear();
    }
};
} // namespace horse_races::starter
