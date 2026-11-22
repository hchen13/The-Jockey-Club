#pragma once
#include "race_authored_obstacles.h"
#include <array>
#include <cmath>
#include <vector>

namespace horse_races::bookmaking::strawberry_doors {
// In-game ray probe hit on model 0x6438DB02 during an observed fall. It is
// not asserted to be the exact physical horse-to-door contact point.
inline Vector3 VerifiedRayHit() {return {-1811.953F,-561.666F,156.962F};}
inline std::vector<authored_obstacles::Post> RayHitFallback() {
    return {{VerifiedRayHit(),.65F}};
}
// A model-space rectangle transformed by the engine into the actual world
// orientation. Four points are ordered min/min, max/min, min/max, max/max.
// Approximate the complete rectangle by overlapping discs: the half-step is
// included in the radius, so no corner between samples can be missed.
// This is transient exclusion geometry, never persistent learned evidence.
inline std::vector<authored_obstacles::Post> Footprint(const std::array<Vector3,4>& p) {
    for(const auto& q:p)if(!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z))return {};
    const float ax=p[1].x-p[0].x,ay=p[1].y-p[0].y;
    const float bx=p[2].x-p[0].x,by=p[2].y-p[0].y;
    const float a=std::hypot(ax,ay),b=std::hypot(bx,by);
    if(a<.02F||b<.02F||a>8||b>8||std::min(a,b)>3)return {};
    // Reject an invalid native transform rather than using an accidental
    // unrelated entity or an excessively large rectangular exclusion.
    if(std::hypot(p[3].x-(p[1].x+p[2].x-p[0].x),p[3].y-(p[1].y+p[2].y-p[0].y))>.15F ||
        std::abs(ax*bx+ay*by)>.10F*a*b)return {};
    const bool along_a=a>=b;const float length=along_a?a:b,width=along_a?b:a;
    const float vx=along_a?ax:bx,vy=along_a?ay:by;
    const float wx=along_a?bx:ax,wy=along_a?by:ay;
    const int count=static_cast<int>(std::ceil(length/.5F));
    // The unchanged recorded-line fallback can use body clearance without
    // the configured tracking margin. Keep a small fixed extra buffer at the
    // barn door even in that case; normal optimized lines still use the
    // configured reserve on top of this transient footprint.
    constexpr float kTrackingBuffer=.5F;
    const float radius=std::hypot(.5F*length/count,.5F*width)+.05F+kTrackingBuffer;
    const float z=(p[0].z+p[1].z+p[2].z+p[3].z)*.25F;
    std::vector<authored_obstacles::Post> out;out.reserve(count);
    for(int i=0;i<count;++i) {
        const float t=(i+.5F)/count;
        out.push_back({{p[0].x+.5F*wx+t*vx,p[0].y+.5F*wy+t*vy,z},radius});
    }
    return out;
}
struct Projection {std::size_t section_rows=0,link_rows=0;};
inline Projection Project(obstacle_map::Map& map,const course::Path& path,
    const std::vector<authored_obstacles::Post>& footprint) {
    std::vector<std::vector<obstacle_map::Interval>> sections,links;
    sections.reserve(map.rows.size());links.reserve(map.rows.size());
    for(const auto& row:map.rows){sections.push_back(row.authored_sections);links.push_back(row.authored_links);}
    authored_obstacles::Apply(map,path,footprint);
    Projection out;
    for(std::size_t i=0;i<map.rows.size();++i) {
        out.section_rows+=sections[i]!=map.rows[i].authored_sections;
        out.link_rows+=links[i]!=map.rows[i].authored_links;
    }
    return out;
}
} // namespace horse_races::bookmaking::strawberry_doors
