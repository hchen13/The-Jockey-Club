#pragma once
#include "race_obstacle_map.h"
#include <filesystem>
#include <fstream>

namespace horse_races::authored_obstacles {
// Source: observed entity origin/hit. Radius is a conservative footprint,
// not the measured model bounds; rider clearance is applied separately.
struct Post {Vector3 centre{};float radius=0;};
inline bool Apply(obstacle_map::Map& map,const course::Path& path,const std::vector<Post>& posts) {
    using namespace obstacle_map;
    bool all=true;
    for(const auto& post:posts) {
        bool linked=false;
        for(std::size_t k=0;k<map.rows.size();++k) {
        auto& row=map.rows[k];const auto e=map.SectionFor(path,k);
        // LOS hit is near rider height; reject other levels at the same XY.
        if(std::abs(post.centre.z-e.centre.z)>2.0F)continue;
        const float hx=post.centre.x-e.centre.x,hy=post.centre.y-e.centre.y;
        const float lateral=hx*e.nx+hy*e.ny,longitudinal=hx*e.ny-hy*e.nx;
        if(std::abs(longitudinal)<=post.radius&&std::abs(lateral)<=map.width+post.radius)
            row.authored_sections=Union(std::move(row.authored_sections),{{lateral-post.radius,lateral+post.radius}});
        if(!k)continue;
        const auto a=map.SectionFor(path,k-1);
        if(std::abs(post.centre.z-a.centre.z)>2.0F)continue;
        const double px=post.centre.x-a.centre.x,py=post.centre.y-a.centre.y,dx=e.centre.x-a.centre.x,dy=e.centre.y-a.centre.y;
        const double nx=a.nx,ny=a.ny,ex=e.nx-nx,ey=e.ny-ny;
        const double qa=-(dx*ey-dy*ex),qb=px*ey-py*ex-dx*ny+dy*nx,qc=px*ny-py*nx;
        double roots[2]{};int count=0;
        if(std::abs(qa)<1e-10){if(std::abs(qb)>1e-10)roots[count++]=-qc/qb;}
        else {const double disc=qb*qb-4*qa*qc;if(disc>=0){const double q=-.5*(qb+std::copysign(std::sqrt(disc),qb));
            if(std::abs(q)>1e-14){roots[count++]=q/qa;roots[count++]=qc/q;}else roots[count++]=-qb/(2*qa);}}
        const auto add=[&](float offset,float support) {
            if(std::abs(offset)<=map.width+support){
                row.authored_link_min_normal=std::min(row.authored_link_min_normal,
                    std::sqrt(std::max(0.0F,(1.0F+a.nx*e.nx+a.ny*e.ny)*.5F)));
                row.authored_links=Union(std::move(row.authored_links),{{offset-support,offset+support}});linked=true;
            }
        };
        for(int i=0;i<count;++i){const double t=roots[i];if(t<0||t>1)continue;
            const double vx=nx+t*ex,vy=ny+t*ey,norm=vx*vx+vy*vy;if(norm<.01)continue;
            add(static_cast<float>(((px-t*dx)*vx+(py-t*dy)*vy)/norm),post.radius/static_cast<float>(std::sqrt(norm)));}
        for(const auto endpoint:{a,e}){const float x=post.centre.x-endpoint.centre.x,y=post.centre.y-endpoint.centre.y;
            if(std::abs(x*endpoint.ny-y*endpoint.nx)<=post.radius)add(x*endpoint.nx+y*endpoint.ny,post.radius);}
        }
        all=all&&linked;
    }
    return all;
}
} // namespace horse_races::authored_obstacles
