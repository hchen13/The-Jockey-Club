#pragma once

#include "game_natives.h"

namespace horse_races::bookmaker_blips {
// The same small friendly/urgent-alert dot as the hosted race rival, not a
// second venue marker. Colour modifiers are owned only by this session.
inline constexpr Hash kFriendlyStyle=4205537449U;
inline constexpr Hash kRivalAlert=580546400U;
inline constexpr Hash kSelected=0xE80A86F4; // BLIP_MODIFIER_OBJECTIVE (yellow)
inline constexpr Hash kBurst=0x382616F3;    // BLIP_MODIFIER_ENEMY (red)
enum class Colour {Companion,Selected,Burst};
inline constexpr Colour Desired(bool selected,bool burst) {
    return burst?Colour::Burst:selected?Colour::Selected:Colour::Companion;
}
// Shared with breed-challenge hosts: red temporarily replaces yellow, and
// removing the burst restores the base colour without touching the alert/name.
inline bool ApplyColour(Blip blip,Colour desired) {
    MAP::BLIP_REMOVE_MODIFIER(blip,kSelected);
    MAP::BLIP_REMOVE_MODIFIER(blip,kBurst);
    return desired==Colour::Companion || MAP::BLIP_ADD_MODIFIER(blip,
        desired==Colour::Burst?kBurst:kSelected);
}
class Dot {
    Blip blip_=0;
    Ped rider_=0;
    Colour colour_=Colour::Companion;
    bool colour_ready_=false;
public:
    void Reset() {
        if(blip_ && MAP::DOES_BLIP_EXIST(blip_))MAP::REMOVE_BLIP(&blip_);
        blip_=0;rider_=0;colour_ready_=false;
    }
    // Caller supplies only an identity-checked rider, or zero to retire it.
    bool Update(Ped rider,bool selected,bool burst,const char* name) {
        if(!rider){Reset();return true;}
        if(rider!=rider_ || (blip_ && !MAP::DOES_BLIP_EXIST(blip_)))Reset();
        if(!blip_) {
            blip_=MAP::BLIP_ADD_FOR_ENTITY(kFriendlyStyle,rider);
            if(!blip_ || !MAP::DOES_BLIP_EXIST(blip_)){blip_=0;return false;}
            rider_=rider;
            MAP::BLIP_ADD_MODIFIER(blip_,kRivalAlert);
            MAP::_SET_BLIP_NAME(blip_,MISC::VAR_STRING(10,"LITERAL_STRING",name));
        }
        const auto desired=Desired(selected,burst);
        if(!colour_ready_ || colour_!=desired) {
            colour_ready_=false;
            if(!ApplyColour(blip_,desired))return false;
            colour_=desired;colour_ready_=true;
        }
        return true;
    }
    Blip Handle() const {return blip_;}
};
} // namespace horse_races::bookmaker_blips
