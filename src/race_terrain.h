#pragma once
#include "race_course.h"
#include <numeric>

namespace horse_races::terrain {
// Spatial geometry of the user's ride, never a replacement route or a table
// of recorded speeds. Metres remain horizontal centreline metres, as in the
// race judge. No positions, physics flags or elimination rules are written.
inline float Smooth(float value,float low,float high) {
    const float t=std::clamp((value-low)/(high-low),0.F,1.F);
    return t*t*(3-2*t);
}
inline Vector3 At(const course::Path& path,float along) {
    along=std::clamp(along,0.F,path.Length());const auto i=path.Segment(along);
    const float t=(along-path.metres[i])/(path.metres[i+1]-path.metres[i]);
    const auto a=path.points[i],b=path.points[i+1];
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
struct Span {float from=0,to=0;};
struct Profile {
    std::vector<float> metres,severity;
    std::vector<Span> spans;
    bool Build(const course::Path& recorded) {
        *this={};if(recorded.points.size()<2)return false;
        metres=recorded.metres;severity.resize(metres.size());
        for(std::size_t i=0;i<metres.size();++i) {
            const float s=metres[i],left=std::max(0.F,s-8),right=std::min(recorded.Length(),s+8);
            const auto a=At(recorded,left),b=recorded.points[i],c=At(recorded,right);
            const float before=(b.z-a.z)/std::max(.1F,s-left);
            const float after=(c.z-b.z)/std::max(.1F,right-s);
            const float grade=std::max(std::abs(before),std::abs(after));
            // A convex crest can throw a horse even when the mean grade is
            // small. Average over horse-scale chords to reject node jitter.
            const float crest=std::max(0.F,std::atan(before)-std::atan(after))/8.F;
            severity[i]=std::max(Smooth(grade,.25F,.65F),Smooth(crest,.012F,.04F));
            if(severity[i]>.01F) {
                const Span next{std::max(0.F,s-12),std::min(recorded.Length(),s+12)};
                if(!spans.empty() && next.from<=spans.back().to+40)spans.back().to=next.to;
                else spans.push_back(next);
            }
        }
        return true;
    }
    float Severity(float along) const {
        if(metres.size()<2 || !std::isfinite(along))return 0;
        const auto it=std::upper_bound(metres.begin(),metres.end(),along);
        const auto i=std::min(metres.size()-2,static_cast<std::size_t>(std::max<std::ptrdiff_t>(0,it-metres.begin()-1)));
        const float t=std::clamp((along-metres[i])/(metres[i+1]-metres[i]),0.F,1.F);
        return severity[i]*(1-t)+severity[i+1]*t;
    }
    std::vector<float> Caps(float top,float caution) const {
        if(metres.size()<2 || caution<0 || !std::isfinite(caution))return {};
        caution=std::clamp(caution,0.F,1.F);
        // Initial trial values, not a claimed safe-speed measurement. At the
        // steepest slope: 27 km/h steady, 41.4 km/h racing. Flat roads retain
        // exactly the existing ability and burst rules.
        const float floor=std::min(top,11.5F-4.F*caution);
        std::vector<float> caps(metres.size(),top);
        for(std::size_t i=0;i<caps.size();++i)caps[i]=top+(floor-top)*severity[i];
        // Brake BEFORE the crest, using a conservative response model; release
        // gradually after it. Neither envelope depends on frame rate or gaps.
        const float braking=3.5F-caution;
        for(std::size_t i=caps.size()-1;i>0;--i)
            caps[i-1]=std::min(caps[i-1],std::sqrt(caps[i]*caps[i]+2*braking*(metres[i]-metres[i-1])));
        for(std::size_t i=1;i<caps.size();++i)
            caps[i]=std::min(caps[i],std::sqrt(caps[i-1]*caps[i-1]+4*(metres[i]-metres[i-1])));
        return caps;
    }
};
// Balanced draw of strategies, NOT a survivor quota. Host is index zero and
// is shuffled with the other riders. Never redraw when a task is renewed.
template<class Random> std::vector<float> DrawCaution(std::size_t count,Random random) {
    if(count<2 || count>16)return {};
    std::vector<std::size_t> order(count);std::iota(order.begin(),order.end(),0);
    for(std::size_t n=count;n>1;--n) {
        const int pick=random(static_cast<int>(n));if(pick<0 || pick>=static_cast<int>(n))return {};
        std::swap(order[n-1],order[static_cast<std::size_t>(pick)]);
    }
    std::vector<float> result(count,0);
    for(std::size_t i=0;i<count/2;++i)result[order[i]]=1;
    return result;
}
} // namespace horse_races::terrain
