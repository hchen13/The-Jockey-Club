#pragma once

#include "../game_natives.h"

namespace horse_races::world {
// This native permits vanilla UI to slow time; it is not a time-scale lease
// belonging to our menu. Closing our native app releases the menu itself.
// Disabling this permission on exit would also disable the wheel and satchel.
class MenuTimeRequest {
    bool active_=false;
public:
    void Begin(){
        if(active_)return;
        HUD::_ENABLE_REDUCED_MENU_TIME_SCALE();
        active_=true;
    }
    void End(){active_=false;}
    bool Active() const{return active_;}
};
} // namespace horse_races::world
