#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace horse_races::race_elimination {
enum class Reason { LeftCourse, SkippedSection, Fell, Unseated, RiderDied, HorseDied, Unable };

// A missing identity never proves an accident, and leaving a particular mount
// does not establish a fall. Choose the more specific reason only on evidence.
inline Reason Incident(bool rider_dead,bool horse_dead,bool identity_missing,bool falling) {
    if(identity_missing)return Reason::Unable;
    if(rider_dead)return Reason::RiderDied;
    if(horse_dead)return Reason::HorseDied;
    return falling?Reason::Fell:Reason::Unseated;
}
inline std::string Message(std::string_view name,Reason reason,bool chinese,bool player=false) {
    if(chinese) {
        const std::string who=player?"你":std::string(name);
        switch(reason) {
        case Reason::LeftCourse:return who+"偏离赛道，退出了比赛。";
        case Reason::SkippedSection:return who+"未按赛道行进，退出了比赛。";
        case Reason::Fell:return who+"摔下了马，退出了比赛。";
        case Reason::Unseated:return who+"离开了坐骑，退出了比赛。";
        case Reason::RiderDied:return player?"你已身亡，退出了比赛。":who+"不幸身亡，退出了比赛。";
        case Reason::HorseDied:return player?"你的马不幸身亡，你退出了比赛。":who+"的马不幸身亡，退出了比赛。";
        case Reason::Unable:return who+"无法继续比赛。";
        }
    }
    const std::string who=player?"You":std::string(name);
    switch(reason) {
    case Reason::LeftCourse:return who+(player?" left the course and are out of the race.":" left the course and is out of the race.");
    case Reason::SkippedSection:return who+(player?" missed part of the course and are out of the race.":" missed part of the course and is out of the race.");
    case Reason::Fell:return who+(player?" fell from your horse and are out of the race.":" fell from the saddle and is out of the race.");
    case Reason::Unseated:return who+(player?" left your horse and are out of the race.":" is no longer mounted and is out of the race.");
    case Reason::RiderDied:return player?"You have died and are out of the race.":who+" has died and is out of the race.";
    case Reason::HorseDied:return player?"Your horse has died. You are out of the race.":who+"'s horse has died. They are out of the race.";
    case Reason::Unable:return who+(player?" can no longer continue the race.":" can no longer continue the race.");
    }
    return {};
}

// A frame may have multiple boundary-warning RAII callers. Only an actual
// rising edge in the current race creates a message; returning inside arms
// the next excursion without hiding the native toast already submitted.
struct BoundaryEdge {
    std::uint64_t epoch=0;
    bool active=false;
    bool Update(bool pending,std::uint64_t race_epoch) {
        if(epoch!=race_epoch){epoch=race_epoch;active=false;}
        const bool entered=pending && !active;
        active=pending;return entered;
    }
};
} // namespace horse_races::race_elimination
