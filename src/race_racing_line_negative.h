#pragma once
#include "race_racing_line_constraints.h"
#include "race_obstacle_link_evidence.h"
#include <memory>

namespace horse_races::racing_line::detail {
// Compatibility entry points for the existing geometry regressions.
inline std::vector<Vec2> NegativeHull(const strip_geometry::Patch& patch) {
    return obstacle_map::ConnectionNegativeHull<Vec2>(patch);
}
inline bool MeetsNegativeHull(Vector3 a,Vector3 b,const std::vector<Vec2>& hull) {
    return obstacle_map::ConnectionHullRange(a,b,hull).has_value();
}
// Positive swept coverage and negative collision evidence have independent
// lifetimes. In particular, a recorded fallback never disables these checks.
class NegativeClearance {
    struct Cut {float along=0;obstacle_map::Section frame;std::vector<obstacle_map::Interval> allowed;};
    const course::Path& path;
    Params policy;
    mutable std::unique_ptr<NegativeClearance> physical_recording;
    float reach=0;
    std::vector<Cut> cuts;
    std::vector<std::pair<float,obstacle_map::ConnectionBlocker>> impacts;
public:
    NegativeClearance(const course::Path& reference,const Params& params):path(reference),policy(params) {
        if(!params.obstacles)return;
        const auto& map=*params.obstacles;
        reach=std::max(60.0F,3*params.Usable()+30);
        const float keep=params.ObstacleReserve();
        for(std::size_t r=0;r<map.rows.size();++r) {
            const auto& row=map.rows[r];
            if((!map.native_mode||!row.HasDetailed())&&row.impacts.empty()&&row.connection_impacts.empty()&&row.authored_sections.empty()&&row.authored_links.empty())continue;
            const float along=map.Along(r);const auto frame=map.SectionFor(path,r);
            auto allowed=NotKnownBlocked(map,r,keep);
            if(allowed.size()!=1||allowed.front().lo> -map.width||allowed.front().hi<map.width) {
                const auto legal=CorridorSpans(path,frame,along,std::max(0.0F,params.Usable()),map.width);
                if(!obstacle_map::SubtractAll(legal,allowed).empty())cuts.push_back({along,frame,std::move(allowed)});
            }
            if(!r)continue;
            const auto from=map.SectionFor(path,r-1);
            if(!row.connection_impacts.empty()||!row.authored_links.empty())impacts.emplace_back(along,
                obstacle_map::ConnectionBlocker(row,from,frame,keep));
        }
    }
    bool AllowsWithReserve(Vector3 a,Vector3 b,std::size_t source) const {
        if(!destination::Finite(a)||!destination::Finite(b))return false;
        const float start=path.metres[source]-reach,end=path.metres[source+1]+reach;
        auto begin=std::lower_bound(cuts.begin(),cuts.end(),start,[](const Cut& cut,float value){return cut.along<value;});
        for(auto it=begin;it!=cuts.end()&&it->along<=end;++it) {
            const auto& frame=it->frame;const double nx=frame.nx,ny=frame.ny;
            const double da=ny*(a.x-frame.centre.x)-nx*(a.y-frame.centre.y);
            const double db=ny*(b.x-frame.centre.x)-nx*(b.y-frame.centre.y);
            if(std::abs(db-da)<1e-10) {
                if(std::abs(da)>.002)continue;
                const float la=static_cast<float>(nx*(a.x-frame.centre.x)+ny*(a.y-frame.centre.y));
                const float lb=static_cast<float>(nx*(b.x-frame.centre.x)+ny*(b.y-frame.centre.y));
                if(!obstacle_map::SubtractAll({{std::min(la,lb),std::max(la,lb)}},it->allowed).empty())return false;
                continue;
            }
            const double t=-da/(db-da);if(t< -1e-7||t>1+1e-7)continue;
            const double l=nx*(a.x+t*(b.x-a.x)-frame.centre.x)+ny*(a.y+t*(b.y-a.y)-frame.centre.y);
            bool permitted=false;for(auto span:it->allowed)if(l>=span.lo-.002&&l<=span.hi+.002){permitted=true;break;}
            if(!permitted)return false;
        }
        auto first=std::lower_bound(impacts.begin(),impacts.end(),start,[](const auto& impact,float value){return impact.first<value;});
        for(auto it=first;it!=impacts.end()&&it->first<=end;++it)
            if(it->second.Blocks(a,b))return false;
        return true;
    }
    bool Allows(Vector3 a,Vector3 b,std::size_t source) const {
        if(AllowsWithReserve(a,b,source))return true;
        const auto same=[](Vector3 x,Vector3 y){return x.x==y.x&&x.y==y.y&&x.z==y.z;};
        // A missing tracking reserve is not a physical collision. Only the
        // unchanged recorded chord may use body clearance alone; even a tiny
        // displacement still needs the complete configured reserve and proof.
        if(policy.margin>0&&same(a,path.points[source])&&same(b,path.points[source+1])) {
            if(!physical_recording) {
                auto body=policy;body.margin=0;
                physical_recording=std::make_unique<NegativeClearance>(path,body);
            }
            return physical_recording->AllowsWithReserve(a,b,source);
        }
        return false;
    }
};
inline bool RecordedNotBlocked(const course::Path& path,const Params& params) {
    NegativeClearance negative(path,params);
    for(std::size_t i=0;i+1<path.points.size();++i)if(!negative.Allows(path.points[i],path.points[i+1],i))return false;
    return true;
}
} // namespace horse_races::racing_line::detail
