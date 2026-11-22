#pragma once

#include "race_course.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace horse_races::bookmaker_grid {
inline constexpr int kColumns=5;
inline constexpr float kLaneSpacing=1.8F;
inline constexpr float kRowSpacing=4.2F;
inline constexpr float kInitialSetback=3.0F;
// Slots are left-to-right as viewed looking down the race direction, then
// front-to-back. Even-sized rows are centred too, not offset by half a lane.
inline float Lane(int slot,int columns=kColumns){return (columns-1)*.5F-slot%columns;}
inline int Row(int slot,int columns=kColumns){return slot/columns;}
// A venue may move the complete standing grid without editing the shared
// recording, lap gates, finish or another event that uses the same course.
inline Vector3 Origin(const course::Path& centre,float forward_metres=0){
    if(centre.points.size()<2 || !std::isfinite(forward_metres))return {};
    const auto& a=centre.points.front();const auto& b=centre.points[1];
    const float dx=b.x-a.x,dy=b.y-a.y,len=std::max(.1F,std::hypot(dx,dy));
    return {a.x+dx/len*forward_metres,a.y+dy/len*forward_metres,a.z};
}
inline Vector3 Position(const course::Path& centre,int slot,int columns=kColumns,float forward_metres=0){
    if(slot<0 || columns<1 || columns>kColumns || centre.points.size()<2 ||
        !std::isfinite(forward_metres))return {};
    const auto a=Origin(centre,forward_metres);const auto& first=centre.points.front();
    const auto& b=centre.points[1];
    const float dx=b.x-first.x,dy=b.y-first.y,len=std::max(.1F,std::hypot(dx,dy));
    const float lateral=Lane(slot,columns)*kLaneSpacing;
    const float back=kInitialSetback+Row(slot,columns)*kRowSpacing;
    return {a.x-dx/len*back-dy/len*lateral,a.y-dy/len*back+dx/len*lateral,a.z};
}
// A recorded starting point can fall on a curb, prop, or collision seam even
// though its horizontal position is inside the course. Search only within the
// original slot: under half a lane sideways and under half a row lengthwise.
// No rider changes rows/columns, and every accepted ground pose is checked by
// the caller against the original strict course corridor.
struct Surface {float z=0;Vector3 normal{};};
enum class GroundReject {None,InvalidGrid,Unavailable,NonFinite,Steep,Height,Corridor};
inline const char* GroundRejectName(GroundReject reason){
    switch(reason){
    case GroundReject::None:return "none";
    case GroundReject::InvalidGrid:return "invalid_grid";
    case GroundReject::Unavailable:return "unavailable";
    case GroundReject::NonFinite:return "nonfinite";
    case GroundReject::Steep:return "steep";
    case GroundReject::Height:return "height";
    case GroundReject::Corridor:return "corridor";
    }
    return "unknown";
}
struct GroundResult {
    std::optional<Vector3> position;
    GroundReject first_rejection=GroundReject::None,last_rejection=GroundReject::None;
    int candidates=0,probes=0,selected=-1;
};
template<class Probe,class InCorridor>
GroundResult GroundedPosition(const course::Path& centre,int slot,int columns,float forward_metres,
    Probe probe,InCorridor in_corridor){
    GroundResult result;
    if(slot<0 || columns<1 || columns>kColumns || centre.points.size()<2 ||
        !std::isfinite(forward_metres)){
        result.first_rejection=result.last_rejection=GroundReject::InvalidGrid;return result;
    }
    const auto base=Position(centre,slot,columns,forward_metres);
    const auto& a=centre.points[0];const auto& b=centre.points[1];
    const float dx=b.x-a.x,dy=b.y-a.y,len=std::hypot(dx,dy);
    if(!std::isfinite(len) || len<.1F){
        result.first_rejection=result.last_rejection=GroundReject::InvalidGrid;return result;
    }
    // Longitudinal alternatives preserve the 4.2 m row ordering. Lateral
    // alternatives remain at least 1.1 m from a neighbour, even if both move.
    constexpr std::array<std::array<float,2>,7> offsets{{
        {0,0},{.7F,0},{-.7F,0},{1.35F,0},{-1.35F,0},{0,.35F},{0,-.35F}
    }};
    constexpr std::array<float,2> probe_heights{3.0F,1.5F};
    const auto reject=[&](GroundReject why){
        if(result.first_rejection==GroundReject::None)result.first_rejection=why;
        result.last_rejection=why;
    };
    for(std::size_t index=0;index<offsets.size();++index){
        const auto [ahead,side]=offsets[index];
        Vector3 candidate{base.x+dx/len*ahead-dy/len*side,
            base.y+dy/len*ahead+dx/len*side,base.z};
        ++result.candidates;
        for(float height:probe_heights){
            ++result.probes;
            const auto surface=probe(candidate,candidate.z+height);
            if(!surface){reject(GroundReject::Unavailable);continue;}
            if(!std::isfinite(surface->z) || !std::isfinite(surface->normal.x) ||
                !std::isfinite(surface->normal.y) || !std::isfinite(surface->normal.z)){
                reject(GroundReject::NonFinite);continue;
            }
            if(surface->normal.z<.8F){reject(GroundReject::Steep);continue;}
            if(std::abs(surface->z-candidate.z)>3.0F){reject(GroundReject::Height);continue;}
            auto grounded=candidate;grounded.z=surface->z;
            if(!in_corridor(grounded)){reject(GroundReject::Corridor);continue;}
            result.position=grounded;result.selected=static_cast<int>(index);
            return result;
        }
    }
    return result;
}
// Separate *standing* slots do not imply separate navigation lines. Parallel
// offsets throughout the butcher/stable street sent the outer horses through
// static storefront props. The game gets the original centre recording and
// chooses a native approach from each narrower starting slot.
inline bool BuildLine(const course::Path& centre,int slot,course::Path& line,std::vector<float>& reference){
    line={};reference.clear();
    if(slot<0 || slot>=2*kColumns || centre.points.size()<2 || centre.Length()<20)return false;
    line=centre;reference=centre.metres;
    return true;
}
} // namespace horse_races::bookmaker_grid
