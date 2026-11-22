#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace horse_races::obstacle_map {
struct Interval {float lo=0,hi=0;bool operator==(const Interval&) const=default;};
// Physical clearance observed along an actual ride, expressed in the two
// original section frames. Keeping both ends avoids inventing a straight,
// constant-offset lane when the horse travelled diagonally through a bend.
struct Passage {Interval from,to;float min_normal=1;bool operator==(const Passage&) const=default;};
inline std::vector<Interval> Normalize(std::vector<Interval> input) {
    std::erase_if(input,[](Interval v){return !std::isfinite(v.lo)||!std::isfinite(v.hi)||v.lo>v.hi;});
    std::sort(input.begin(),input.end(),[](Interval a,Interval b){return a.lo<b.lo||(a.lo==b.lo&&a.hi<b.hi);});
    std::vector<Interval> out;out.reserve(input.size());
    for(auto v:input) {
        if(!out.empty()&&v.lo<=out.back().hi+1e-5F)out.back().hi=std::max(out.back().hi,v.hi);
        else out.push_back(v);
    }
    return out;
}
inline std::vector<Interval> Erode(const std::vector<Interval>& input,float clearance) {
    std::vector<Interval> out;if(!std::isfinite(clearance)||clearance<0)return out;
    for(auto v:input)if(v.lo+clearance<=v.hi-clearance)out.push_back({v.lo+clearance,v.hi-clearance});
    return out;
}
inline std::vector<Interval> Intersect(const std::vector<Interval>& a,const std::vector<Interval>& b) {
    std::vector<Interval> out;
    for(auto x:a)for(auto y:b){const float lo=std::max(x.lo,y.lo),hi=std::min(x.hi,y.hi);if(lo<=hi)out.push_back({lo,hi});}
    return Normalize(std::move(out));
}
inline std::vector<Interval> Subtract(const std::vector<Interval>& input,float lo,float hi) {
    if(lo>hi)std::swap(lo,hi);std::vector<Interval> out;
    for(auto v:input) {
        if(hi<=v.lo||lo>=v.hi){out.push_back(v);continue;}
        if(lo>v.lo)out.push_back({v.lo,lo});if(hi<v.hi)out.push_back({hi,v.hi});
    }
    return out;
}
inline bool Contains(const std::vector<Interval>& spans,float lateral) {
    if(!std::isfinite(lateral))return false;
    for(auto v:spans)if(lateral>=v.lo-1e-5F&&lateral<=v.hi+1e-5F)return true;return false;
}
inline std::vector<Interval> Union(std::vector<Interval> a,const std::vector<Interval>& b) {
    a.insert(a.end(),b.begin(),b.end());return Normalize(std::move(a));
}
inline std::vector<Interval> SubtractAll(std::vector<Interval> a,const std::vector<Interval>& b,float radius=0) {
    for(auto v:b)a=Subtract(a,v.lo-radius,v.hi+radius);return a;
}
inline bool Touches(const Passage& p,float lo,float hi) {
    return std::max(p.from.hi,p.to.hi)>lo && std::min(p.from.lo,p.to.lo)<hi;
}
inline bool Contains(const Passage& a,const Passage& b) {
    return a.from.lo<=b.from.lo&&a.from.hi>=b.from.hi&&a.to.lo<=b.to.lo&&a.to.hi>=b.to.hi;
}
inline bool UnionCompatible(const Passage& a,const Passage& b,Passage& out) {
    // All four rails are affine in the strip parameter. Endpoint overlap
    // therefore proves overlap for the whole strip. Each exterior rail must
    // also have the SAME owner at both ends, otherwise its min/max would be
    // piecewise linear and joining endpoint bounds would invent a hull.
    if(a.from.lo>b.from.hi||b.from.lo>a.from.hi||a.to.lo>b.to.hi||b.to.lo>a.to.hi)return false;
    const bool lower=(a.from.lo<=b.from.lo&&a.to.lo<=b.to.lo)||(b.from.lo<=a.from.lo&&b.to.lo<=a.to.lo);
    const bool upper=(a.from.hi>=b.from.hi&&a.to.hi>=b.to.hi)||(b.from.hi>=a.from.hi&&b.to.hi>=a.to.hi);
    if(!lower||!upper)return false;
    out={{std::min(a.from.lo,b.from.lo),std::max(a.from.hi,b.from.hi)},
        {std::min(a.to.lo,b.to.lo),std::max(a.to.hi,b.to.hi)},std::min(a.min_normal,b.min_normal)};
    return true;
}
inline void AddPassage(std::vector<Passage>& values,Passage p) {
    for(const auto& old:values)if(Contains(old,p)&&old.min_normal>=p.min_normal)return;
    std::size_t insertion=values.size();
    for(std::size_t i=0;i<values.size();) {
        Passage merged;
        if(!UnionCompatible(values[i],p,merged)){++i;continue;}
        if(merged==values[i])return;
        p=merged;insertion=std::min(insertion,i);
        values.erase(values.begin()+static_cast<std::ptrdiff_t>(i));
        // A newly joined neighbour may now bridge another compatible strip.
        // At most the fixed 64 stored strips can be consumed by this scan.
        i=0;
    }
    if(values.size()>=64){values.erase(values.begin());if(insertion)--insertion;}
    values.insert(values.begin()+static_cast<std::ptrdiff_t>(std::min(insertion,values.size())),p);
}
} // namespace horse_races::obstacle_map
