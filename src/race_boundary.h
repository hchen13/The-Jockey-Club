#pragma once
#include <algorithm>
#include <cmath>

namespace horse_races::boundary {
inline constexpr double kOutsideSeconds=2.0;
struct Event {bool offense=false,disqualified=false;double at=0;};
// One race, one rider. A continuous excursion can produce at most one
// offense; only a measured return inside arms the next episode.
class Tracker {
    int limit_=1,offenses_=0;
    double outside_since_=-1,last_at_=-1;
    bool outside_=false,counted_=false;
public:
    void Reset(int limit=1){*this={};limit_=std::max(1,limit);}
    int Limit() const {return limit_;}
    int Offenses() const {return offenses_;}
    bool Outside() const {return outside_;}
    bool Pending() const {return outside_&&!counted_;}
    bool EpisodeCounted() const {return counted_;}
    double OutsideElapsed(double now) const {
        return outside_&&std::isfinite(now)?std::max(0.0,now-outside_since_):0;
    }
    Event Observe(bool inside,double now) {
        if(!std::isfinite(now)||now<0||now<last_at_)return {};
        last_at_=now;
        if(inside){outside_=counted_=false;outside_since_=-1;return {};}
        if(!outside_){outside_=true;counted_=false;outside_since_=now;}
        if(counted_||now-outside_since_+1e-9<kOutsideSeconds)return {};
        counted_=true;++offenses_;
        return {true,offenses_>=limit_,outside_since_+kOutsideSeconds};
    }
};
} // namespace horse_races::boundary
