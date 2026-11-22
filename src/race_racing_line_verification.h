#pragma once
#include "race_racing_line_negative.h"

namespace horse_races::racing_line::detail {
// Continuous measured-rail coverage and its endpoint-keyed certificates.
// Owns build-local caches; the caller owns branch fallback and repair policy.
class ChordVerifier {
    const course::Path& centreline;
    const Params& params;
    const std::vector<SectionBound>& measured_rows;
    const std::vector<unsigned char>& recorded;
    const float usable,limit;
    NegativeClearance negative;
    std::vector<strip_geometry::PreparedPatch> patches;
    std::vector<strip_geometry::Range> coverage;
    struct ChordCertificate {Vector3 a{},b{};bool valid=false,covered=false,retained=false;};
    std::vector<ChordCertificate> chord_cache;
    bool Measure(Vector3 a,Vector3 b,std::size_t source_segment) {
        if(!negative.Allows(a,b,source_segment))return false;
        if(!params.obstacles)return true;
        if(recorded[source_segment]) {
            const auto ca=centreline.points[source_segment],cb=centreline.points[source_segment+1];
            return a.x==ca.x&&a.y==ca.y&&a.z==ca.z&&b.x==cb.x&&b.y==cb.y&&b.z==cb.z;
        }
        if(measured_rows.size()<2)return false;
        const float reach=std::max(8.0F,2*usable);
        const float start=centreline.metres[source_segment]-reach,end=centreline.metres[source_segment+1]+reach;
        const auto begin=std::lower_bound(measured_rows.begin()+1,measured_rows.end(),start,
            [](const detail::SectionBound& row,float value){return row.along<value;});
        coverage.clear();
        for(auto it=begin;it!=measured_rows.end()&&(it-1)->along<=end;++it) {
            if(it->incoming_recorded)continue;
            const auto spans=strip_geometry::CoveredIntervals(a,b,patches[static_cast<std::size_t>(it-measured_rows.begin())]);
            for(std::size_t k=0;k<spans.count;++k)coverage.push_back(spans.ranges[k]);
        }
        std::sort(coverage.begin(),coverage.end(),[](auto x,auto y){return x.lo<y.lo;});
        double covered=0;
        for(auto span:coverage) {
            if(span.lo>covered+1e-8)return false;
            covered=std::max(covered,span.hi);if(covered>=1-1e-8)return true;
        }
        return false;

    }
public:
    ChordVerifier(const course::Path& path,const Params& policy,const std::vector<SectionBound>& rows,
        const std::vector<unsigned char>& fallback,float width,float corridor_limit)
        :centreline(path),params(policy),measured_rows(rows),recorded(fallback),usable(width),
         limit(corridor_limit),negative(path,policy),patches(rows.size()) {
        // Tapered rails are quadratic when adjacent normals rotate. Sampling at
        // 1 m can miss a short excursion between samples. Partition each chord
        // at boundary crossings and union the nearby strip coverage. The same
        // 2 mm lateral numerical tolerance as the section solver applies; caps
        // are not extended. Cache patch geometry and broad-phase bounds once.
        for(std::size_t r=1;r<measured_rows.size();++r)if(!measured_rows[r].incoming_recorded)
            patches[r]=strip_geometry::Prepare(detail::Strip(measured_rows[r-1],measured_rows[r]),.002);
        coverage.reserve(64);
        chord_cache.resize(centreline.points.size()-1);

    }
    bool Beyond(Vector3 m,std::size_t segment) const {
        const auto& a=centreline.points[segment];const auto& b2=centreline.points[segment+1];
        const float dx=b2.x-a.x,dy=b2.y-a.y,length2=dx*dx+dy*dy;
        const float t=length2>0?std::clamp(((m.x-a.x)*dx+(m.y-a.y)*dy)/length2,0.0F,1.0F):0.0F;
        const float distance=std::hypot(m.x-a.x-t*dx,m.y-a.y-t*dy);
        return distance>limit;

    }
    bool NegativeAllows(Vector3 a,Vector3 b,std::size_t source_segment) const {
        return negative.Allows(a,b,source_segment);
    }
    bool NegativeAllowsWithReserve(Vector3 a,Vector3 b,std::size_t source_segment) const {
        return negative.AllowsWithReserve(a,b,source_segment);
    }
    bool Measured(Vector3 a,Vector3 b,std::size_t source_segment) {
        auto& cache=chord_cache[source_segment];
        auto same=[](Vector3 p,Vector3 q){return p.x==q.x&&p.y==q.y&&p.z==q.z;};
        if(cache.valid&&cache.retained==bool(recorded[source_segment])&&same(a,cache.a)&&same(b,cache.b))return cache.covered;
        cache.a=a;cache.b=b;cache.valid=true;cache.retained=recorded[source_segment]!=0;
        return cache.covered=Measure(a,b,source_segment);

    }
};
} // namespace horse_races::racing_line::detail
