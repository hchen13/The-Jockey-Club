#pragma once

#include "race_course.h"
#include "race_obstacle_map.h"
#include "race_pace_plan.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace horse_races::live_overtake {
struct Obstacle {Vector3 position{};float radius=1.0F;int entity=0;};
struct Result {bool changed=false;float at=0;int side=0;int entity=0;};
inline float DistanceXY(Vector3 a,Vector3 b){return std::hypot(a.x-b.x,a.y-b.y);}
inline Vector3 AtReference(const course::Path& line,const std::vector<float>& reference,float along){
    const auto it=std::upper_bound(reference.begin(),reference.end(),along);
    const std::size_t i=std::min(line.points.size()-2,static_cast<std::size_t>(
        std::max<std::ptrdiff_t>(0,it-reference.begin()-1)));
    const float span=reference[i+1]-reference[i];
    const float t=span>0?std::clamp((along-reference[i])/span,0.0F,1.0F):0.0F;
    const auto a=line.points[i],b=line.points[i+1];
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
// A transient obstacle creates one smooth, bounded lateral bulge in a private
// copy of a rider's line. It never changes the recorded course, gate order or
// legal corridor. The native recording is then restarted on this new line.
struct Session {
    course::Path original;
    course::Path path;
    std::vector<float> reference;
    double next_check=0;
    float last_target=-1000;
    float active_until=-1;
    bool Reset(const course::Path& source,const std::vector<float>& order={}){
        original=path=source;reference=order.size()==source.points.size()?order:source.metres;
        next_check=0;last_target=-1000;active_until=-1;
        return path.points.size()>=2 && reference.size()==path.points.size();
    }
    Result Try(const course::Path& legal,float half_width,float qualified_along,
        const std::vector<Obstacle>& obstacles,double now,int self_horse,int self_rider,
        const obstacle_map::Map* surveyed=nullptr){
        if(now<next_check || path.points.size()<2 || reference.size()!=path.points.size() ||
            legal.points.size()<2 || !std::isfinite(qualified_along) || half_width<5)return {};
        next_check=now+.25;
        // A detour is a single bounded manoeuvre, not a new centreline on top
        // of which the next moving horse may generate another detour. The old
        // code repeatedly deformed the same line and restarted native riding
        // every few metres in a packed field.
        if(active_until>=0){
            if(qualified_along<active_until)return {};
            path=original;active_until=-1;next_check=now+.5;
            return {true,qualified_along,0,0};
        }
        const float maximum=std::min(reference.back()-35,qualified_along+45);
        if(maximum<qualified_along+8)return {};
        const auto rider_at=AtReference(path,reference,qualified_along);
        const Obstacle* blocker=nullptr;float blocker_at=0;
        for(const auto& item:obstacles){
            if(!item.entity || item.entity==self_horse || item.entity==self_rider ||
                !destination::Finite(item.position) || item.radius<=0 ||
                DistanceXY(item.position,rider_at)>65.0F)continue;
            const auto proj=course::Project(path,item.position,qualified_along+8,maximum,&reference);
            if(!proj.valid || proj.distance>item.radius+1.25F ||
                std::abs(item.position.z-AtReference(path,reference,proj.along).z)>3.5F ||
                std::abs(proj.along-last_target)<12.0F)continue;
            if(!blocker || proj.along<blocker_at){blocker=&item;blocker_at=proj.along;}
        }
        if(!blocker)return {};
        const float begin=std::max(qualified_along+1,blocker_at-25);
        const float end=std::min(reference.back(),blocker_at+30);
        if(end-begin<35)return {};
        const float amplitude=std::clamp(blocker->radius+2.4F,2.9F,4.8F);
        float best=-std::numeric_limits<float>::infinity();course::Path selected;
        int selected_side=0;
        for(int side:{-1,1}){
            std::vector<Vector3> points=path.points;
            for(std::size_t i=0;i<points.size();++i){
                const float along=reference[i];if(along<=begin || along>=end)continue;
                const float t=(along-begin)/(end-begin);
                const float wave=std::sin(3.14159265358979323846F*t);
                const auto a=AtReference(path,reference,std::max(reference.front(),along-3));
                const auto b=AtReference(path,reference,std::min(reference.back(),along+3));
                const float dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy);
                if(length<.1F)continue;
                const float offset=static_cast<float>(side)*amplitude*wave*wave;
                points[i].x-=dy/length*offset;points[i].y+=dx/length*offset;
            }
            course::Path candidate;if(!candidate.Build(points))continue;
            bool legal_line=true;float nearest=1000;
            for(float along=begin;along<=end;along+=2.0F){
                const auto p=AtReference(candidate,reference,along);
                const auto corridor=course::Project(legal,p,std::max(0.0F,along-12),along+12);
                if(!corridor.valid || corridor.distance>half_width-2.0F){legal_line=false;break;}
                if(surveyed && !surveyed->rows.empty()){
                    const auto& row=surveyed->rows[surveyed->Index(along)];
                    if(row.Surveyed()){
                        const auto before=race_pace::At(legal,std::max(0.0F,along-3));
                        const auto after=race_pace::At(legal,std::min(legal.Length(),along+3));
                        const auto middle=race_pace::At(legal,along);
                        const float dx=after.x-before.x,dy=after.y-before.y;
                        const float signed_offset=(dx*(p.y-middle.y)-dy*(p.x-middle.x))/std::max(.1F,std::hypot(dx,dy));
                        const float free=signed_offset>=0?row.left:row.right;
                        if(std::abs(signed_offset)>free-.5F){legal_line=false;break;}
                    }
                }
                for(const auto& item:obstacles)if(item.entity && item.entity!=self_horse && item.entity!=self_rider &&
                    std::abs(item.position.z-p.z)<3.5F && DistanceXY(item.position,p)<12.0F)
                    nearest=std::min(nearest,DistanceXY(item.position,p)-item.radius);
            }
            if(!legal_line || nearest<1.1F)continue;
            if(nearest>best){best=nearest;selected=std::move(candidate);selected_side=side;}
        }
        if(!selected_side)return {};
        path=std::move(selected);last_target=blocker_at;active_until=end;next_check=now+1.0;
        return {true,blocker_at,selected_side,blocker->entity};
    }
};
} // namespace horse_races::live_overtake
