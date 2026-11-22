#pragma once

#include "race_obstacle_learning.h"
#include "race_obstacle_profiles.h"

namespace horse_races::obstacle_profiles {
// Transfer only accepted physical impact evidence, once per event. Each
// qualified local branch gets its own bounded projection: a circuit may pass
// the same world point at both its beginning and its end.
// This reuses the existing collision classifier, which cannot prove an object
// remains permanently fixed. Asynchronous impacts and later passages across
// profiles do not carry a global physical-event ordering in persisted maps.
inline int TransferConfirmedImpact(Bank& bank,int source,Vector3 hit,float shoulder,
    float source_from=0,float source_to=INFINITY) {
    constexpr float kRoadSeparation=5.0F,kLayerSeparation=1.0F,kDirectionDot=.5F;
    constexpr std::size_t kMaximumBranches=12;
    if(source<0 || source>=static_cast<int>(bank.maps.size()) || !destination::Finite(hit) ||
        !std::isfinite(shoulder) || shoulder<0 || !std::isfinite(source_from) || std::isnan(source_to))return 0;
    struct Local {Vector3 centre;float dx=0,dy=0,distance=0;};
    const auto local=[&](const course::Path& path,std::size_t segment,Local& out) {
        const auto a=path.points[segment],b=path.points[segment+1];
        const float dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
        if(!(length>.01F))return false;
        const float t=std::clamp(((hit.x-a.x)*dx+(hit.y-a.y)*dy)/(length*length),0.0F,1.0F);
        out.centre={a.x+t*dx,a.y+t*dy,a.z+t*(b.z-a.z)};
        out.dx=dx/length;out.dy=dy/length;
        out.distance=std::hypot(hit.x-out.centre.x,hit.y-out.centre.y);
        return hit.z>=out.centre.z-1.0F && hit.z<=out.centre.z+3.5F;
    };
    const auto& origin_path=bank.paths[source];const auto& origin_map=bank.maps[source];
    if(origin_map.key.empty() || origin_map.rows.empty() || origin_path.points.size()<2)return 0;
    const auto projection=course::Project(origin_path,hit,source_from,source_to);
    Local origin;
    if(!projection.valid || projection.distance>origin_map.width ||
        !local(origin_path,projection.segment,origin))return 0;
    int changed=0;
    for(int target=0;target<static_cast<int>(bank.maps.size());++target) {
        auto& map=bank.maps[target];const auto& path=bank.paths[target];
        if(map.key.empty() || map.rows.empty() || path.points.size()<2)continue;
        struct Branch {std::size_t first,last;};
        std::vector<Branch> branches;
        for(std::size_t segment=0;segment+1<path.points.size();++segment) {
            Local other;
            if(!local(path,segment,other) || other.distance>map.width ||
                std::hypot(origin.centre.x-other.centre.x,origin.centre.y-other.centre.y)>kRoadSeparation ||
                std::abs(origin.centre.z-other.centre.z)>kLayerSeparation ||
                origin.dx*other.dx+origin.dy*other.dy<kDirectionDot)continue;
            if(!branches.empty()&&branches.back().last+1==segment)branches.back().last=segment;
            else branches.push_back({segment,segment});
            // Ambiguous repeated geometry is safer to leave untouched. This
            // also bounds the event's later per-branch projection work.
            if(branches.size()>kMaximumBranches)break;
        }
        if(branches.size()>kMaximumBranches)continue;
        for(std::size_t i=0;i<branches.size();++i) {
            const auto branch=branches[i];
            // The learner already visited this branch using its frozen
            // source window. Other disjoint branches of the same recorded
            // shape still need their own local projection.
            if(target==source && projection.segment>=branch.first && projection.segment<=branch.last)continue;
            // Include the measured section on either side of the qualified
            // segment; its incoming link can cross the physical footprint.
            float from=std::max(0.0F,path.metres[branch.first]-obstacle_map::kStepMetres);
            float to=std::min(path.Length(),path.metres[branch.last+1]+obstacle_map::kStepMetres);
            if(i)from=std::max(from,(path.metres[branches[i-1].last+1]+path.metres[branch.first])*.5F);
            if(i+1<branches.size())to=std::min(to,(path.metres[branch.last+1]+path.metres[branches[i+1].first])*.5F);
            const auto chosen=course::Project(path,hit,from,to);
            if(!chosen.valid || chosen.segment<branch.first || chosen.segment>branch.last)continue;
            changed+=obstacle_learning::NarrowAt(map,path,hit,shoulder,nullptr,from,to);
        }
    }
    return changed;
}
} // namespace horse_races::obstacle_profiles
