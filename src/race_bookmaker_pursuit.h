#pragma once
#include "race_bookmaker_field.h"
#include "race_catchup.h"
#include <array>

namespace horse_races::bookmaking {
// One shared progress snapshot, at most ten entrants. No world queries or
// route projections: use only each rider's already-qualified ordered metres.
inline std::array<std::optional<float>,kExhibitionRacers> PursuitGaps(
    std::span<const Field::Rider> riders,const circuit::Layout& layout,std::span<const bool> alive) {
    std::array<std::optional<float>,kExhibitionRacers> positions{},gaps{};
    if(riders.size()>positions.size() || alive.size()!=riders.size())return gaps;
    for(std::size_t i=0;i<riders.size();++i) {
        const auto& r=riders[i];
        if(!alive[i] || r.status!=Field::Status::Racing || r.progress.total_laps<=0 ||
            r.progress.off_course || r.progress.continuity_lost)continue;
        const float metres=r.progress.Metres(layout);
        if(std::isfinite(metres))positions[i]=metres;
    }
    for(std::size_t i=0;i<riders.size();++i)if(positions[i]) {
        std::optional<float> other;
        for(std::size_t j=0;j<riders.size();++j)if(i!=j && positions[j] && (!other || *positions[j]>*other))
            other=positions[j];
        // Excluding self allows a genuine lead of >5 m to release the same
        // owner-latched pursuit used in hosted races. Finished/OUT horses are
        // not moving targets, and the last remaining racer has no pursuit.
        if(other)gaps[i]=*other-*positions[i];
    }
    return gaps;
}
}
