#pragma once

#include "race_bookmaking.h"
#include <cmath>
#include <numeric>
#include <span>

namespace horse_races::bookmaking {
// Field size and the horse-attribute policy are independent. A smaller
// Handicap draws distinct hosts just as a smaller WFA does.
template<class Random> std::vector<characters::Id> SelectRoster(
    characters::Id protagonist,std::size_t count,Random random) {
    return DrawRoster(protagonist,count,random);
}

// Pace estimates come from each frozen personal line, speed, acceleration,
// corridor utilization and cornering. Slowest starts first. Shuffle only
// resolves exactly equal estimates; the returned order is used by BOTH the
// catalog and the physical grid, never re-drawn during blackout preparation.
template<class Random> std::vector<std::size_t> StartingOrder(
    std::span<const double> predicted_seconds,Random random) {
    if(predicted_seconds.size()<2 || predicted_seconds.size()>kExhibitionRacers)return {};
    for(const double seconds:predicted_seconds)
        if(!std::isfinite(seconds) || seconds<=0)return {};
    std::vector<std::size_t> order(predicted_seconds.size());
    std::iota(order.begin(),order.end(),0);
    for(std::size_t i=order.size();i>1;--i){
        const int drawn=random(0,static_cast<int>(i));
        if(drawn<0 || drawn>=static_cast<int>(i))return {};
        std::swap(order[i-1],order[static_cast<std::size_t>(drawn)]);
    }
    std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){
        return predicted_seconds[a]>predicted_seconds[b];
    });
    return order;
}
} // namespace horse_races::bookmaking
