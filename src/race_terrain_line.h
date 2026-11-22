#pragma once
#include "race_terrain.h"
#include "race_line_pins.h"

namespace horse_races::terrain {
inline bool FollowRecorded(const course::Path& recorded,const racing_line::Params& params,
    const Profile& terrain,racing_line::Result& line) {
    std::vector<line_pins::Stretch> stretches;
    for(const auto& span:terrain.spans) {
        line_pins::Stretch s;s.from=span.from;s.to=span.to;s.note="terrain: user's recorded line";
        s.pins={{s.from,0},{s.to,0}};stretches.push_back(std::move(s));
    }
    // Reuse the existing smooth 20 m blend and chord/order verification.
    // These zero-offset pins are in memory only; route and user pins unchanged.
    return line_pins::Apply(recorded,params,stretches,line).ok;
}
} // namespace horse_races::terrain
