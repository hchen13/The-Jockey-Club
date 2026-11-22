#pragma once
#include "race_racing_line_geometry.h"

namespace horse_races::racing_line::detail {
// Missing swept-link certificates are not collision observations. Native
// section gaps, except the explicit unknown layer, and actual impacts are
// negative evidence even when a chord loses its positive certificate.
inline std::vector<obstacle_map::Interval> NotKnownBlocked(const obstacle_map::Map& map,
    std::size_t r,float keep) {
    const auto& row=map.rows[r];
    return obstacle_map::SubtractAll({{-1e6F,1e6F}},row.BlockedIntervals(map.width,keep,map.native_mode));
}
// Intersect an actual survey cross-section with the union of nearby legal
// road capsules. A section offset is not a nearest-road distance at a bend.
inline std::vector<obstacle_map::Interval> CorridorSpans(const course::Path& path,
    const obstacle_map::Section& cut,float along,float width,float extent) {
    std::vector<obstacle_map::Interval> spans;
    const double nx=cut.nx,ny=cut.ny,nn=nx*nx+ny*ny;
    if(!(nn>.01)||!(width>0))return spans;
    auto circle=[&](Vector3 p) {
        const double dx=cut.centre.x-p.x,dy=cut.centre.y-p.y;
        const double b=nx*dx+ny*dy,c=dx*dx+dy*dy-width*width;
        const double discriminant=b*b-nn*c;if(discriminant<0)return;
        const double root=std::sqrt(discriminant);
        spans.push_back({static_cast<float>((-b-root)/nn),static_cast<float>((-b+root)/nn)});
    };
    const float window=std::max(60.0F,3*width+30);
    const auto first=path.Segment(along-window),last=path.Segment(along+window);
    bool previous_valid=false;
    for(std::size_t i=first;i<=last&&i+1<path.points.size();++i) {
        const auto a=path.points[i],b=path.points[i+1];
        const double dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
        if(length<1e-8){previous_valid=false;continue;}
        // The previous valid segment already added this shared vertex.
        // Skipped segments break adjacency, even when their ends are close.
        if(!previous_valid)circle(a);
        circle(b);previous_valid=true;
        const double tx=dx/length,ty=dy/length;
        const double px=cut.centre.x-a.x,py=cut.centre.y-a.y;
        double lo=-extent,hi=extent;
        auto intersect=[&](double value,double slope,double lower,double upper) {
            if(std::abs(slope)<1e-12)return value>=lower&&value<=upper;
            double x=(lower-value)/slope,y=(upper-value)/slope;if(x>y)std::swap(x,y);
            lo=std::max(lo,x);hi=std::min(hi,y);return lo<=hi;
        };
        if(intersect(px*tx+py*ty,nx*tx+ny*ty,0,length)&&
            intersect(-px*ty+py*tx,-nx*ty+ny*tx,-width,width))
            spans.push_back({static_cast<float>(lo),static_cast<float>(hi)});
    }
    return obstacle_map::Intersect(obstacle_map::Normalize(std::move(spans)),{{-extent,extent}});
}
} // namespace horse_races::racing_line::detail
