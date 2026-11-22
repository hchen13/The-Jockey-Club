#pragma once
#include "race_destination.h"
#include <limits>
#include <vector>

namespace horse_races::course {
// One immutable centreline for navigation, progress, dialogue and abandonment.
// Nodes are spatial samples, NOT recorded velocities or a teleport playback.
// A recorded line in a route file holds this many nodes too (race_route_config.h reads the limit
// from here); a native waypoint recording is registered 1024 nodes at a time (race_course_native.h).
// The 4 m line the rivals ride on the 11 km camp duel is over 3000 nodes,
// and until 2026-09-20 this limit too was 1024: the subdivision silently
// fell back to the recorded nodes, 17 m apart, on every course over 4 km,
// and the line bent only at those nodes.
inline constexpr std::size_t kMaxNodes=16384;
struct Path {
    std::vector<Vector3> points;
    std::vector<float> metres;
    float Length() const { return metres.empty()?0:metres.back(); }
    bool Build(const std::vector<Vector3>& input,float minimum_length=50) {
        points.clear();metres.clear();
        if(input.size()<2 || input.size()>kMaxNodes || !std::isfinite(minimum_length) ||
            minimum_length<0 || minimum_length>50)return false;
        float total=0;
        for(std::size_t i=0;i<input.size();++i) {
            if(!destination::Finite(input[i]))return false;
            if(i) {
                const float length=destination::Distance(input[i-1],input[i]);
                if(length<.05F || length>250.0F)return false;
                total+=length;
            }
            points.push_back(input[i]);metres.push_back(total);
        }
        return total>=minimum_length && total<=100000;
    }
    std::size_t Segment(float along) const {
        const auto it=std::upper_bound(metres.begin(),metres.end(),along);
        return std::min(points.size()-2,static_cast<std::size_t>(std::max<std::ptrdiff_t>(0,it-metres.begin()-1)));
    }
};
struct Projection {
    float along=0, distance=std::numeric_limits<float>::infinity();
    std::size_t segment=0;
    bool valid=false;
};
inline Projection Project(const Path& path,Vector3 position,float minimum=0,float maximum=100000,
    const std::vector<float>* ordered_metres=nullptr) {
    Projection best;
    if(path.points.size()<2 || !destination::Finite(position))return best;
    const auto& metres=ordered_metres && ordered_metres->size()==path.points.size()?*ordered_metres:path.metres;
    for(std::size_t i=0;i+1<path.points.size();++i) {
        if(metres[i+1]<minimum || metres[i]>maximum)continue;
        const auto& a=path.points[i];const auto& b=path.points[i+1];
        const float dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
        const float t=std::clamp(((position.x-a.x)*dx+(position.y-a.y)*dy)/length2,0.0F,1.0F);
        const float along=metres[i]+t*(metres[i+1]-metres[i]);
        if(along<minimum || along>maximum)continue;
        const float horizontal=std::hypot(position.x-a.x-t*dx,position.y-a.y-t*dy);
        // Distinguish a bridge from the river under it without penalising the
        // rider's cached coordinate being above the horse/road by a few metres.
        const float height=std::max(0.0F,std::abs(position.z-a.z-t*(b.z-a.z))-6.0F);
        const float distance=std::hypot(horizontal,height);
        if(distance<best.distance)best={along,distance,i,true};
    }
    return best;
}
struct Progress {
    Projection current;
    Projection nearest; // Geometry only; never restricted by the old progress anchor.
    float furthest=0;
    Vector3 previous{};
    bool ready=false;
    bool continuity_lost=false;
    void Reset(){*this={};}
    void Update(const Path& path,Vector3 position) {
        if(!destination::Finite(position))return;
        const float step=ready?std::hypot(destination::Distance(previous,position),previous.z-position.z):0;
        nearest=Project(path,position);
        // Prefer the nearby branch at a crossing, but don't lock a continuously
        // ridden A-to-B shortcut to the old segment after rejoining the course.
        // Lap/checkpoint qualification is a separate rule, not this projection.
        const float ahead=ready?std::min(120.0F,std::max(50.0F,step*1.5F+15.0F)):100.0F;
        const auto local=Project(path,position,std::max(0.0F,furthest-150.0F),furthest+ahead);
        current=local.valid && (!nearest.valid || local.distance<=nearest.distance+2.0F)?local:nearest;
        const bool jumped=ready && step>100.0F;
        const bool valid_start=local.valid && local.distance<=35.0F && current.along<=100.0F;
        if(jumped || (!ready && !valid_start))continuity_lost=true;
        // A jump cannot win on the NEXT stationary frame either. It must first
        // return to the last legitimately reached section to restore continuity.
        if(continuity_lost && !jumped) {
            const auto recovery=Project(path,position,std::max(0.0F,furthest-50.0F),furthest+35.0F);
            if(recovery.valid && recovery.distance<=35.0F)continuity_lost=false;
        }
        if(current.valid && current.distance<=35.0F && !continuity_lost &&
            (ready || valid_start))
            furthest=std::max(furthest,current.along);
        previous=position;ready=true;
    }
    bool MayFinish(const Path& path) const {
        return ready && !continuity_lost && current.valid && current.distance<35 &&
            current.along>=path.Length()-50 && furthest>=path.Length()-50;
    }
};
// Traffic AI receives a nearby goal on the same line. Do not request the far
// finish and let it choose a different valley. Retarget before braking begins.
inline float LookAheadMetres(float speed_mps,bool rolling=false) {
    const float speed=std::isfinite(speed_mps)?std::max(0.0F,speed_mps):0.0F;
    return rolling?std::clamp(speed*5.0F,65.0F,110.0F):std::clamp(speed*3.5F,35.0F,85.0F);
}
inline float HandoffMetres(float speed_mps,bool rolling=false) {
    const float speed=std::isfinite(speed_mps)?std::max(0.0F,speed_mps):0.0F;
    return rolling?std::clamp(speed*3.0F,35.0F,65.0F):std::clamp(speed*1.8F,15.0F,32.0F);
}
struct CommandPace {
    float peak_mps=0;
    void Observe(float speed) {if(std::isfinite(speed))peak_mps=std::max(peak_mps,speed);}
    void Reset(float speed=0) {peak_mps=0;Observe(speed);}
};
// 2026-09-10 trail telemetry: lateral motion changed the safe local horizon
// from node 186 back to 185, without any missed gate. Retargeting to that closer
// endpoint restarts approach handling. Only an explicit gate recovery may retreat.
inline int HandoffDecision(std::size_t current,std::size_t candidate,bool gate_recovery) {
    if(candidate==current)return 3;
    if(candidate<current && !gate_recovery)return 5;
    return 4;
}
// Rolling goals are initially limited to the cleared circuit. Destination races
// keep their accepted horizon. Bend protection and gate qualification are unchanged.
inline std::size_t LookAhead(const Path& path,const Progress& progress,float speed_mps,bool rolling=false) {
    const float along=progress.current.valid?progress.current.along:progress.furthest;
    const float distance=LookAheadMetres(speed_mps,rolling);
    std::size_t target=std::min(path.points.size()-1,path.Segment(along+distance)+1);
    // Keep the first substantial bend as a goal instead of skipping across it.
    const std::size_t begin=std::min(path.points.size()-2,path.Segment(along)+1);
    for(std::size_t i=begin;i<target && i+1<path.points.size();++i) {
        if(path.metres[i]-along<25)continue;
        const auto a=path.points[i-1],b=path.points[i],c=path.points[i+1];
        const float dot=(b.x-a.x)*(c.x-b.x)+(b.y-a.y)*(c.y-b.y);
        const float lengths=destination::Distance(a,b)*destination::Distance(b,c);
        if(lengths>.01F && dot/lengths<.82F){target=i;break;}
    }
    return target;
}
} // namespace horse_races::course
