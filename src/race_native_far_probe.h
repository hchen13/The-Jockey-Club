#pragma once
#include <cmath>
#include <optional>

namespace horse_races::native_far_probe {
// Diagnostic only: observe an unmodified native waypoint rider while distant.
// The caller owns the navigation task. This class never moves or speeds a ped.
struct Reading {
    double seconds=0;
    float course_metres=0,world_metres=0;
};
struct Probe {
    double since=-1;
    float along=0,x=0,y=0;
    void Reset(){*this={};}
    std::optional<Reading> Observe(double now,float route_along,float world_x,float world_y,bool distant) {
        if(!distant || !std::isfinite(now) || !std::isfinite(route_along) ||
            !std::isfinite(world_x) || !std::isfinite(world_y)){
            Reset();return {};
        }
        if(since<0 || now<since){since=now;along=route_along;x=world_x;y=world_y;return {};}
        if(now-since<3.0)return {};
        const Reading result{now-since,route_along-along,std::hypot(world_x-x,world_y-y)};
        since=now;along=route_along;x=world_x;y=world_y;
        return result;
    }
};
} // namespace horse_races::native_far_probe
