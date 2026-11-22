#pragma once

#include "../game_natives.h"

namespace horse_races::world {
// The spectator camera owns only its own fades. A race-start or race-end
// blackout belongs to the circuit scene and must not be lifted here.
class CameraBlackout {
    bool owned_=false;
    bool revealing_=false;
public:
    void StartOut(int milliseconds){
        if(owned_ || CAM::IS_SCREEN_FADED_OUT() || CAM::IS_SCREEN_FADING_OUT())return;
        CAM::DO_SCREEN_FADE_OUT(milliseconds);owned_=true;revealing_=false;
    }
    bool Black() const {return CAM::IS_SCREEN_FADED_OUT();}
    bool FadingOut() const {return CAM::IS_SCREEN_FADING_OUT();}
    bool FullyVisible() const {return CAM::IS_SCREEN_FADED_IN() && !CAM::IS_SCREEN_FADING_IN();}
    void StartIn(int milliseconds){
        if(!owned_)return;
        CAM::DO_SCREEN_FADE_IN(milliseconds);
        owned_=false;revealing_=true;
    }
    // Recover only the fade-in this instance started and which the engine
    // still reports as revealing. An external fade-out/blackout is not ours.
    void RecoverIn(int milliseconds){
        if(!revealing_)return;
        revealing_=false;
        if(CAM::IS_SCREEN_FADING_IN() && !CAM::IS_SCREEN_FADING_OUT())
            CAM::DO_SCREEN_FADE_IN(milliseconds);
    }
    void Restore(){StartIn(350);}
};
} // namespace horse_races::world
