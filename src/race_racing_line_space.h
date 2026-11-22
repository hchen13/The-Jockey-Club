#pragma once
#include "race_racing_line_negative.h"
#include <optional>

namespace horse_races::racing_line::detail {
// Independent continuous positive proof across ALL measured branches. The
// selected optimizer branch and a recorded/frozen mask cannot supply proof.
// Prepared lazily for a bounded repair or a missing final chord certificate.
class PositiveClearance {
    struct Patch {float from=0,to=0;strip_geometry::PreparedPatch shape;};
    const course::Path& path;
    const Params& params;
    std::vector<Patch> patches;
    std::vector<strip_geometry::Range> coverage;
public:
    PositiveClearance(const course::Path& reference,const Params& policy):path(reference),params(policy) {
        if(!params.obstacles)return;
        const auto& map=*params.obstacles;const float keep=params.ProofClearance();
        if(map.rows.size()>1) {
            auto from=map.SectionFor(path,0);
            auto entry=map.rows[0].ClearIntervals(keep);
            for(std::size_t r=1;r<map.rows.size();++r) {
                const auto to=map.SectionFor(path,r);
                auto exit=map.rows[r].ClearIntervals(keep);
                for(const auto& strip:map.ConnectionStrips(r,keep)) {
                    for(auto a:obstacle_map::Intersect({strip.from},entry))for(auto b:obstacle_map::Intersect({strip.to},exit)) {
                        const auto prepared=strip_geometry::Prepare({from,to,a,b},.002);
                        if(prepared.regular)patches.push_back({map.Along(r-1),map.Along(r),prepared});
                    }
                }
                from=to;entry=std::move(exit);
            }
        }
        coverage.reserve(64);
    }
    bool Covers(Vector3 a,Vector3 b,std::size_t source) {
        if(!params.obstacles)return true;
        const float reach=std::max(8.0F,2*params.Usable());
        const float start=path.metres[source]-reach,end=path.metres[source+1]+reach;
        const auto first=std::lower_bound(patches.begin(),patches.end(),start,
            [](const Patch& patch,float value){return patch.to<value;});
        coverage.clear();
        for(auto it=first;it!=patches.end()&&it->from<=end;++it) {
            const auto spans=strip_geometry::CoveredIntervals(a,b,it->shape);
            for(std::size_t k=0;k<spans.count;++k)coverage.push_back(spans.ranges[k]);
        }
        std::sort(coverage.begin(),coverage.end(),[](auto x,auto y){return x.lo<y.lo;});
        double end_covered=0;
        for(const auto span:coverage) {
            if(span.lo>end_covered+1e-8)return false;
            end_covered=std::max(end_covered,span.hi);if(end_covered>=1-1e-8)return true;
        }
        return false;
    }
};
} // namespace horse_races::racing_line::detail
