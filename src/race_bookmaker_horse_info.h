#pragma once
#include "horse_inspection.h"
#include <cstddef>
#include <optional>

namespace horse_races::bookmaking {
// A Q toggle owns only the stock information card for the visible follow
// target. Releasing Aim, changing target/mode, or ending the show closes it;
// asynchronous native cleanup continues even after the target is forgotten.
class RiderHorseInfo {
    horse_inspection::Card card_;
    std::optional<std::size_t> rider_;
public:
    bool Active() const{return card_.Active();}
    bool Busy() const{return card_.Busy();}
    const char* LastOpenReason() const{return card_.LastOpenReason();}
    bool Toggle(std::size_t rider,race_horses::Identity horse,const story_horses::Labels* labels){
        if(card_.Active()){Close();return true;}
        if(!card_.Open(horse,labels))return false;
        rider_=rider;return true;
    }
    void Close(){card_.Close();rider_.reset();}
    template<class Identify>
    void Tick(std::optional<std::size_t> visible_rider,Identify identify,
        horse_time::GameClock::time_point now=horse_time::GameClock::now()){
        card_.Tick(rider_ && visible_rider==rider_,identify,now);
        if(!card_.Busy())rider_.reset();
    }
};
} // namespace horse_races::bookmaking
