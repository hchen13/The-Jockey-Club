#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace horse_races::vehicle_clearance {
inline constexpr std::size_t kMaxPathPoints=64,kMaxVehicles=256;
inline constexpr float kHorseMargin=1.5F,kPredictionSeconds=.25F;
// A 250 ms sampler runs on rendered game frames (e.g. 266.7 ms at 60 fps).
// Missing a whole additional sampling period breaks continuous-clear evidence.
inline constexpr double kClearSeconds=.75,kMaximumObservationGap=.5;
struct Point {float x=0,y=0,z=0;};
// Native collection supplies a world-space bounding-box centre (including
// the model's local centre offset), horizontal OBB half extents and absolute
// world Z bounds. Pitch/roll must be projected into these enclosing extents
// by the collector; silently dropping a model axis is not a valid bound.
struct Vehicle {
    int entity=0;
    std::uint32_t model=0;
    Point center{},forward{0,1,0};
    float half_length=0,half_width=0,min_z=0,max_z=0;
    Point velocity{};
    bool bounds_valid=true;
};
struct Snapshot {
    std::span<const Vehicle> vehicles{};
    bool complete=true;
    float age_seconds=0;
};
struct Result {
    bool known=false,blocked=true;
    int vehicle=0;
    float distance=0,lookahead=0;
    std::size_t pairs=0;
    bool complete=false; // A known obstruction need not be the nearest body in a partial snapshot.
};
inline constexpr float kReactionSeconds=.5F,kBrakingMps2=4.F,kStopClearance=5.F;
inline float Horizon(float speed) {
    if(!std::isfinite(speed) || speed<0)return std::numeric_limits<float>::quiet_NaN();
    const double v=speed,d=kReactionSeconds*v+v*v/(2.*kBrakingMps2)+kStopClearance;
    return d<=std::numeric_limits<float>::max()?static_cast<float>(d):
        std::numeric_limits<float>::quiet_NaN();
}
// Invert the same stopping envelope. Entering the horizon starts a gradual
// native speed cap; it is not an instruction to pause a horse sixty metres away.
inline float ApproachSpeed(float distance) {
    if(!std::isfinite(distance))return 0;
    const double room=std::max(0.F,distance-kStopClearance);
    const double reaction=kBrakingMps2*kReactionSeconds;
    return static_cast<float>(std::sqrt(reaction*reaction+2*kBrakingMps2*room)-reaction);
}
namespace detail {
inline bool Finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
struct Segment {Point a{},b{};double start=0,length=0;};
inline bool Slab(double a,double delta,double low,double high,double& enter,double& leave) {
    if(std::abs(delta)<1.e-12)return a>=low && a<=high;
    double first=(low-a)/delta,last=(high-a)/delta;
    if(first>last)std::swap(first,last);
    enter=std::max(enter,first);leave=std::min(leave,last);
    return enter<=leave;
}
}
// The path starts at the actual horse position and covers the requested
// horizontal braking horizon. A shorter path is unknown unless it genuinely
// ends at the race destination. No path/actor is moved by this module.
inline Result Evaluate(std::span<const Point> path,float speed,const Snapshot& snapshot,
    float lookahead_override=0,bool path_ends=false) {
    Result out;
    const float horizon=Horizon(speed);
    if(!std::isfinite(horizon) || !std::isfinite(lookahead_override) || lookahead_override<0)return out;
    out.lookahead=std::max(horizon,lookahead_override);
    if(path.size()<2 || path.size()>kMaxPathPoints ||
        !std::isfinite(snapshot.age_seconds) || snapshot.age_seconds<0 ||
        snapshot.age_seconds>kPredictionSeconds)return out;
    for(const auto p:path)if(!detail::Finite(p))return out;
    std::array<detail::Segment,kMaxPathPoints-1> segments{};
    std::size_t count=0;double distance=0;
    for(std::size_t i=1;i<path.size() && distance<out.lookahead;++i) {
        const auto a=path[i-1],b=path[i];
        const double length=std::hypot(double(b.x)-a.x,double(b.y)-a.y);
        if(length<1.e-6){if(std::abs(double(b.z)-a.z)>.01)return out;continue;}
        segments[count++]={a,b,distance,length};distance+=length;
    }
    if(!count || (!path_ends && distance+1.e-4<out.lookahead))return out;
    out.blocked=false;out.distance=out.lookahead;
    bool complete=snapshot.complete && snapshot.vehicles.size()<=kMaxVehicles;
    const auto vehicles=snapshot.vehicles.first(std::min(snapshot.vehicles.size(),kMaxVehicles));
    const double forecast=double(snapshot.age_seconds)+kPredictionSeconds;
    for(const auto& v:vehicles) {
        if(!v.entity || !v.model || !v.bounds_valid ||
            !detail::Finite(v.center) || !detail::Finite(v.forward) || !detail::Finite(v.velocity) ||
            !std::isfinite(v.half_length) || !std::isfinite(v.half_width) || v.half_length<=0 || v.half_width<=0 ||
            !std::isfinite(v.min_z) || !std::isfinite(v.max_z) || v.min_z>=v.max_z ||
            std::hypot(double(v.forward.x),double(v.forward.y))<.001) {
            complete=false;continue;
        }
        const double norm=std::hypot(double(v.forward.x),double(v.forward.y));
        const double fx=v.forward.x/norm,fy=v.forward.y/norm,rx=fy,ry=-fx;
        const double along_motion=(v.velocity.x*fx+v.velocity.y*fy)*forecast;
        const double cross_motion=(v.velocity.x*rx+v.velocity.y*ry)*forecast;
        const double vertical_motion=v.velocity.z*forecast;
        // Enclose the whole translation sweep, not only its endpoint. Keeping
        // the axes separate avoids turning parallel traffic into a large circle.
        const double low_f=-v.half_length-kHorseMargin+std::min(0.,along_motion);
        const double high_f=v.half_length+kHorseMargin+std::max(0.,along_motion);
        const double low_r=-v.half_width-kHorseMargin+std::min(0.,cross_motion);
        const double high_r=v.half_width+kHorseMargin+std::max(0.,cross_motion);
        // Path Z is the horse entity origin. Its body/rider occupy -0.5..+2.5m.
        const double low_z=v.min_z+std::min(0.,vertical_motion)-2.5;
        const double high_z=v.max_z+std::max(0.,vertical_motion)+.5;
        for(std::size_t i=0;i<count;++i) {
            const auto& s=segments[i];
            if(s.start>out.distance)break;
            ++out.pairs;
            const double ax=double(s.a.x)-v.center.x,ay=double(s.a.y)-v.center.y;
            const double dx=double(s.b.x)-s.a.x,dy=double(s.b.y)-s.a.y;
            double enter=0,leave=std::min(1.,(double(out.lookahead)-s.start)/s.length);
            if(detail::Slab(ax*fx+ay*fy,dx*fx+dy*fy,low_f,high_f,enter,leave) &&
                detail::Slab(ax*rx+ay*ry,dx*rx+dy*ry,low_r,high_r,enter,leave) &&
                detail::Slab(s.a.z,double(s.b.z)-s.a.z,low_z,high_z,enter,leave)) {
                const float hit=static_cast<float>(s.start+enter*s.length);
                if(!out.blocked || hit<out.distance || (hit==out.distance && v.entity<out.vehicle)) {
                    out.blocked=true;out.distance=hit;out.vehicle=v.entity;
                }
            }
        }
    }
    // One valid body can prove an obstruction even when other evidence is
    // missing. Proving clearance requires every candidate to be observed and
    // valid; a skipped body or bounded-prefix scan must never grant permission.
    out.complete=complete;out.known=out.blocked || complete;
    if(!out.known)out.blocked=true;
    return out;
}
struct Decision {bool blocked=false,clearing=false;Result observation{};};
class Observer {
    bool held_=false;
    double clear_since_=-1,last_time_=-1;
    float held_lookahead_=0;
public:
    void Reset(){*this=Observer{};}
    float Lookahead(float speed) const {
        const float current=Horizon(speed);
        return std::isfinite(current)?std::max(current,held_lookahead_):current;
    }
    Decision Update(const Result& sample,double now) {
        Decision out;out.observation=sample;
        if(!std::isfinite(now) || now<0) {
            clear_since_=last_time_=-1;out.observation.known=false;out.blocked=held_;return out;
        }
        if(last_time_>=0 && (now<last_time_ || now-last_time_>kMaximumObservationGap)) {
            clear_since_=-1;
        }
        last_time_=now;
        // A caller must inspect the retained horizon even after slowing to a
        // standstill. A shorter clear query is not evidence that the formerly
        // occupied corridor cleared; fail closed even if the adapter forgot
        // to pass Lookahead() back into Evaluate().
        if(held_ && sample.lookahead+1.e-4F<held_lookahead_)
            out.observation.known=false;
        if(!out.observation.known) {
            // No observation cannot prove that an existing obstruction cleared,
            // nor does it prove a new obstruction on an otherwise open course.
            clear_since_=-1;
        } else if(sample.blocked) {
            held_=true;clear_since_=-1;
        } else if(held_) {
            if(clear_since_<0)clear_since_=now;
            if(now-clear_since_>=kClearSeconds){held_=false;clear_since_=-1;held_lookahead_=0;}
        }
        if(held_ && std::isfinite(sample.lookahead) && sample.lookahead>0)
            held_lookahead_=std::max(held_lookahead_,sample.lookahead);
        out.blocked=held_;out.clearing=held_ && out.observation.known && !sample.blocked;
        return out;
    }
};
} // namespace horse_races::vehicle_clearance
