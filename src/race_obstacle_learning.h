#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include "race_course.h"
#include "race_obstacle_map.h"
#include "race_racing_line.h"
#include "race_strip_geometry.h"

// What the survey cannot promise, the ride teaches. Two things here.
//
// 1. Room caps. The line keeps margin + clearance (3 m) from whatever the
//    survey found; a narrowing on one side is the line's to move away
//    from, and it does. Where the gap between the two sides is narrower
//    than twice that the line can only thread the middle - the recorded
//    ride proves it passable. Passable at the pace it was recorded at: the
//    native follower's tracking error at a gallop is up to 2.2 m, and a
//    horse driven through a 5 m gap between trunks at 60 km/h hits one
//    (Clemens Point, 2026-09-19, every race). So the pace plan is capped
//    where the line threads a gap: full speed with kRoomFullMetres of room
//    either side (a 6 m gap), kCrawlMps with none, linear between. The
//    first cut capped every node near any one-sided narrowing - 67 of the
//    one-on-one's 654, and the host never regained speed on a tree-lined
//    road - so only the gap's half-width counts. Rows the survey never
//    reached cap nothing: the line is on the centreline there already.
//
// 2. Learning from a collision. When a driven horse loses most of its speed
//    in one tick and a probe ahead of it finds something that does not
//    move, the map's rows there are narrowed to the hit, the file is saved
//    and the lines are rebuilt: whatever the survey missed - a trunk that
//    had not streamed in when its row was surveyed at range, a rock below
//    the capsule's height - is known from then on.
namespace horse_races::obstacle_learning {
inline constexpr float kRoomFullMetres=3;
inline constexpr float kCrawlMps=6;            // 21.6 km/h: the follower tracks to well under a metre at that.
inline constexpr float kRoomWindowMetres=4;    // Rows either side of a node that its chords may cross.

// Per node of `line`: the fastest the plan may ride it for the room the map
// leaves it. top_mps where the map is silent or the room is ample.
inline std::vector<float> RoomCaps(const racing_line::Result& line,const obstacle_map::Map& map,float top_mps,float crawl_mps=kCrawlMps,const course::Path* reference_path=nullptr) {
    std::vector<float> caps(line.offsets.size(),top_mps);
    if(map.rows.empty() || line.reference.size()!=line.offsets.size() || !(crawl_mps>0) || !(top_mps>crawl_mps))return caps;
    // Offsets at two different normals are not interchangeable. Find the
    // actual planned chord crossing of each measured section once, outside
    // the driving loop. Without world geometry only legacy widths can be used.
    const bool geometry=reference_path && line.path.points.size()==line.reference.size();
    std::vector<float> crossings(map.rows.size(),NAN);
    if(geometry)for(std::size_t r=0;r<map.rows.size();++r) {
        const auto section=map.SectionFor(*reference_path,r);
        const auto source=line.Index(map.Along(r));double nearest=1e30;
        for(std::size_t k=source>2?source-2:0;k<=source+2 && k+1<line.path.points.size();++k) {
            const auto a=line.path.points[k],b=line.path.points[k+1];
            const double tx=section.ny,ty=-section.nx,dx=b.x-a.x,dy=b.y-a.y;
            const double denom=tx*dx+ty*dy;if(std::abs(denom)<1e-8)continue;
            const double t=-(tx*(a.x-section.centre.x)+ty*(a.y-section.centre.y))/denom;
            if(t<0 || t>1)continue;
            const double distance=std::abs(line.reference[k]+t*(line.reference[k+1]-line.reference[k])-map.Along(r));
            if(distance>=nearest)continue;nearest=distance;
            crossings[r]=static_cast<float>(section.nx*(a.x+t*dx-section.centre.x)+section.ny*(a.y+t*dy-section.centre.y));
        }
    }
    for(std::size_t i=0;i<caps.size();++i) {
        const float along=line.reference[i];
        const std::size_t a=map.Index(std::max(0.0F,along-kRoomWindowMetres)),b=map.Index(along+kRoomWindowMetres);
        float room=1e9F;bool known=false;   // Half the narrowest gap the node's chords cross.
        for(std::size_t r=a;r<=b && r<map.rows.size();++r) {
            const auto& row=map.rows[r];
            if(!row.Surveyed() || (row.HasDetailed()&&!geometry))continue;
            const float lateral=geometry?crossings[r]:
                line.canonical_offsets.size()==line.offsets.size()?line.canonical_offsets[i]:line.offsets[i];
            if(!std::isfinite(lateral))continue;
            const auto clear=row.ClearIntervals();
            const auto physical=row.MeasuredIntervals();
            const bool actual_axis=row.passage_axes&&obstacle_map::Contains(row.PassedIntervals(),lateral);
            // A safe centre trajectory measures its own axes, not the road's
            // available width. Its narrow evidence band is no reason to slow
            // every future rider to a crawl when native room is unresolved.
            if(actual_axis)continue;
            // Negative evidence in an otherwise unscanned row does not turn
            // its unresolved centre into a measured zero-width passage.
            if(obstacle_map::Contains(row.unknown,lateral)&&!obstacle_map::Contains(clear,lateral))continue;
            known=true;
            float gap=0;
            for(const auto interval:physical)
                if(lateral>=interval.lo && lateral<=interval.hi)
                    gap=std::max(gap,(interval.hi-interval.lo)/2);
            // A free island on each bank is not necessarily one connected
            // passage. Both actual crossings must fit the same incoming strip.
            if(geometry && r && row.HasConnections() && std::isfinite(crossings[r-1]) &&
                !obstacle_map::Contains(row.unknown,crossings[r-1]) && !obstacle_map::Contains(row.unknown,lateral)) {
                float link_gap=0;
                bool actual_link=false;
                if(row.passage_axes)for(const auto& p:row.passages)
                    actual_link|=crossings[r-1]>=p.from.lo&&crossings[r-1]<=p.from.hi&&lateral>=p.to.lo&&lateral<=p.to.hi;
                if(!actual_link) {
                    const float normal=strip_geometry::MinimumNormalLength(map.SectionFor(*reference_path,r-1),map.SectionFor(*reference_path,r));
                    for(const auto& p:map.ConnectionStrips(r))
                        if(crossings[r-1]>=p.from.lo&&crossings[r-1]<=p.from.hi&&lateral>=p.to.lo&&lateral<=p.to.hi)
                            link_gap=std::max(link_gap,std::min(p.from.hi-p.from.lo,p.to.hi-p.to.lo)*normal*.5F+row.connection_radius);
                    gap=std::min(gap,link_gap);
                }
            }
            room=std::min(room,gap);
        }
        if(!known || room>=kRoomFullMetres)continue;
        caps[i]=crawl_mps+(top_mps-crawl_mps)*std::clamp(room,0.0F,kRoomFullMetres)/kRoomFullMetres;
    }
    return caps;
}

struct Learned {std::size_t row=0;float lateral=0,free=0;bool left=false;int rows=0;};
// Cut a detailed section only where the hit's footprint meets its plane,
// and cut the incoming links that physically contain the hit. Legacy rows
// retain their old conservative side-width update until remeasured. Unknown
// rows retain negative evidence without acquiring free clearance. Returns the number of changed rows/links; zero for a
// hit off the course.
inline int NarrowAt(obstacle_map::Map& map,const course::Path& path,Vector3 hit,float shoulder,Learned* out=nullptr,
                    float from=0,float to=INFINITY) {
    if(!std::isfinite(from)||std::isnan(to))return 0;
    from=std::max(0.0F,from);to=std::min(path.Length(),to);if(!(to>from))return 0;
    const auto p=course::Project(path,hit,from,to);
    if(!p.valid || map.rows.empty() || p.distance>map.width || p.segment+1>=path.points.size() || !std::isfinite(shoulder) || shoulder<0)return 0;
    const auto& a=path.points[p.segment];const auto& b=path.points[p.segment+1];
    const float dx=b.x-a.x,dy=b.y-a.y;
    const float lateral=dx*(hit.y-a.y)-dy*(hit.x-a.x);   // + left of travel.
    const bool left=lateral>=0;
    const float free=std::max(0.0F,p.distance-shoulder);
    const std::size_t r=map.Index(p.along);
    int rows=0;
    // A hit can meet a nearby rotated section, but cannot turn its projection
    // onto every neighbouring normal into a fictitious wall at that row.
    const auto first=map.Index(p.along-map.width-shoulder),last=map.Index(p.along+map.width+shoulder)+1;
    for(std::size_t k=first;k<=last && k<map.rows.size();++k) {
        if(map.Along(k)<from||map.Along(k)>to)continue;
        auto& row=map.rows[k];
        if(row.HasDetailed()||!row.Surveyed()) {
            const auto section=map.SectionFor(path,k);
            const float offset=(hit.x-section.centre.x)*section.nx+(hit.y-section.centre.y)*section.ny;
            const float longitudinal=(hit.x-section.centre.x)*section.ny-(hit.y-section.centre.y)*section.nx;
            if(std::abs(longitudinal)<=shoulder && map.Block(k,offset-shoulder,offset+shoulder))++rows;
        } else if(k+1>=r && k<=r+1) {
            auto next=row;float& side=left?next.left:next.right;
            if(side>free) {
                side=free;next.reason|=obstacle_map::kImpact;
                if(map.ReplaceRow(k,std::move(next)))++rows;
            }
        }
    }
    // Incoming links are swept through the space BETWEEN their two sections.
    // Invert that ruled strip for the hit, retaining its actual local lateral
    // coordinate. Endpoint support also covers a body's longitudinal radius.
    for(std::size_t k=std::max<std::size_t>(1,first);k<=last && k<map.rows.size();++k) {
        if(map.Along(k-1)<from||map.Along(k)>to)continue;
        const auto entry=map.SectionFor(path,k-1),exit=map.SectionFor(path,k);
        const double px=hit.x-entry.centre.x,py=hit.y-entry.centre.y,delta_x=exit.centre.x-entry.centre.x,delta_y=exit.centre.y-entry.centre.y;
        const double nx=entry.nx,ny=entry.ny,ex=exit.nx-nx,ey=exit.ny-ny;
        const double qa=-(delta_x*ey-delta_y*ex),qb=px*ey-py*ex-delta_x*ny+delta_y*nx,qc=px*ny-py*nx;
        double roots[2]{};int count=0;
        if(std::abs(qa)<1e-10){if(std::abs(qb)>1e-10)roots[count++]=-qc/qb;}
        else {
            const double disc=qb*qb-4*qa*qc;
            if(disc>=0) {
                const double q=-.5*(qb+std::copysign(std::sqrt(disc),qb));
                if(std::abs(q)>1e-14){roots[count++]=q/qa;roots[count++]=qc/q;}
                else roots[count++]=-qb/(2*qa);
            }
        }
        bool changed=false;
        for(int n=0;n<count;++n) {
            const double t=roots[n];if(t<0 || t>1)continue;
            const double vx=nx+t*ex,vy=ny+t*ey,norm=vx*vx+vy*vy;if(norm<.01)continue;
            const float at=static_cast<float>(((px-t*delta_x)*vx+(py-t*delta_y)*vy)/norm);
            const float support=shoulder/static_cast<float>(std::sqrt(norm));
            changed=map.BlockConnection(k,at-support,at+support)||changed;
        }
        for(const auto endpoint:{entry,exit}) {
            const float hx=hit.x-endpoint.centre.x,hy=hit.y-endpoint.centre.y;
            if(std::abs(hx*endpoint.ny-hy*endpoint.nx)>shoulder+map.rows[k].connection_radius)continue;
            const float at=hx*endpoint.nx+hy*endpoint.ny;
            changed=map.BlockConnection(k,at-shoulder,at+shoulder)||changed;
        }
        if(changed)++rows;
    }
    if(out)*out={r,lateral,free,left,rows};
    return rows;
}
} // namespace horse_races::obstacle_learning
