#pragma once
#include "race_vehicle_clearance.h"
#include "race_pace_plan.h"
#include "horse_motion_native.h"
#include "world/scan.h"
#include <array>
#include <unordered_map>

namespace horse_races::vehicle_clearance {
inline Point PointOf(Vector3 p){return {p.x,p.y,p.z};}
// One bounded vehicle inventory for the whole field, four times per game
// second. Model bounds and detailed transforms are fetched only for nearby
// vehicles and shared by all riders. No raycasts or per-rider world scans.
struct NativeSnapshot {
    struct Item {int entity=0;Vector3 origin{};bool read=false;Vehicle body;};
    struct Bounds {Vector3 low{},high{};bool valid=false;};
    std::array<Item,1024> items{};
    std::size_t count=0;
    std::unordered_map<unsigned,Bounds> models;
    double sampled=-1;
    bool complete=false;
    unsigned scans=0;
    void Refresh(double now) {
        if(sampled>=0 && now>=sampled && now-sampled<.25)return;
        sampled=now;count=0;complete=false;++scans;
        std::array<int,1024> handles{};
        const int found=world::scan::Vehicles(handles.data(),static_cast<int>(handles.size()));
        if(found<0 || found>static_cast<int>(handles.size()))return;
        complete=found<static_cast<int>(handles.size());
        for(int i=0;i<found;++i) {
            const auto entity=handles[static_cast<std::size_t>(i)];
            if(!entity || !ENTITY::DOES_ENTITY_EXIST(entity) || ENTITY::GET_ENTITY_COLLISION_DISABLED(entity))continue;
            const auto origin=ENTITY::GET_ENTITY_COORDS(entity,true,false);
            if(!destination::Finite(origin)){complete=false;continue;}
            items[count++]={entity,origin,false,{}};
        }
    }
    const Vehicle& Read(Item& item) {
        if(item.read)return item.body;
        item.read=true;auto& body=item.body;
        body.entity=item.entity;body.bounds_valid=false;
        if(!ENTITY::DOES_ENTITY_EXIST(item.entity))return body;
        body.model=ENTITY::GET_ENTITY_MODEL(item.entity);
        auto found=models.find(body.model);
        if(found==models.end()) {
            Bounds bounds;MISC::GET_MODEL_DIMENSIONS(body.model,&bounds.low,&bounds.high);
            bounds.valid=destination::Finite(bounds.low) && destination::Finite(bounds.high) &&
                bounds.high.x>bounds.low.x && bounds.high.y>bounds.low.y && bounds.high.z>bounds.low.z;
            if(!bounds.valid)return body; // Streaming can make bounds temporarily unavailable; retry next snapshot.
            if(models.size()>=256)models.clear();
            found=models.emplace(body.model,bounds).first;
        }
        const auto& bounds=found->second;if(!bounds.valid)return body;
        Vector3 right{},forward{},up{},origin{};
        ENTITY::GET_ENTITY_MATRIX(item.entity,&right,&forward,&up,&origin);
        const auto velocity=ENTITY::GET_ENTITY_VELOCITY(item.entity,0);
        if(!destination::Finite(right) || !destination::Finite(forward) ||
            !destination::Finite(up) || !destination::Finite(origin) || !destination::Finite(velocity))return body;
        const float heading=std::hypot(forward.x,forward.y);
        if(heading<.5F)return body;
        const Vector3 local{(bounds.low.x+bounds.high.x)*.5F,(bounds.low.y+bounds.high.y)*.5F,
            (bounds.low.z+bounds.high.z)*.5F};
        const Vector3 half{(bounds.high.x-bounds.low.x)*.5F,(bounds.high.y-bounds.low.y)*.5F,
            (bounds.high.z-bounds.low.z)*.5F};
        const Vector3 center{origin.x+right.x*local.x+forward.x*local.y+up.x*local.z,
            origin.y+right.y*local.x+forward.y*local.y+up.y*local.z,
            origin.z+right.z*local.x+forward.z*local.y+up.z*local.z};
        const float fx=forward.x/heading,fy=forward.y/heading;
        // Project all three model axes: a pitched carriage's height contributes
        // to its footprint, and an off-centre model origin is not its centre.
        const auto extent=[&](float x,float y) {
            return std::abs(right.x*x+right.y*y)*half.x+
                std::abs(forward.x*x+forward.y*y)*half.y+std::abs(up.x*x+up.y*y)*half.z;
        };
        const float height=std::abs(right.z)*half.x+std::abs(forward.z)*half.y+std::abs(up.z)*half.z;
        body.center=PointOf(center);body.forward={fx,fy,0};
        body.half_length=extent(fx,fy);body.half_width=extent(-fy,fx);
        body.min_z=center.z-height;body.max_z=center.z+height;body.velocity=PointOf(velocity);
        body.bounds_valid=true;return body;
    }
};
inline NativeSnapshot shared_vehicles;

struct NativeGuard {
    Observer observer;
    Decision decision{};
    horse_motion::Identity horse;
    double sampled=-1;
    bool changed=false;
    int blocking_vehicle=0;
    unsigned blocking_model=0;
    Point vehicle_position{},vehicle_velocity{};
    float remaining=0;
    Vector3 last_position{};
    bool position_valid=false;
    float Ceiling() const {
        return decision.blocked?ApproachSpeed(remaining):std::numeric_limits<float>::infinity();
    }
    bool Holding() const {return decision.blocked && Ceiling()<=0;}
    std::array<Vehicle,256> local{};
    bool Update(Ped mount,const race_pace::Plan& plan,float along,Vector3 position,double now,float simulated_speed=-1) {
        const auto identity=horse_motion::Identify(mount);
        if(identity!=horse) {*this={};horse=identity;}
        changed=false;
        if(!identity.Valid() || !std::isfinite(now))return false;
        // Evaluate reports horizontal physical metres, not centreline/reference
        // progress. Consume actual XY travel between calls in those same units.
        // Reverse/lateral motion is conservative until the next body snapshot.
        if(decision.blocked&&position_valid&&destination::Finite(position))
            remaining=std::max(0.F,remaining-std::hypot(position.x-last_position.x,position.y-last_position.y));
        position_valid=destination::Finite(position);last_position=position;
        if(sampled>=0 && now>=sampled && now-sampled<.25)return decision.blocked;
        sampled=now;
        shared_vehicles.Refresh(now);
        const float speed=std::max(ENTITY::GET_ENTITY_SPEED(mount),simulated_speed);
        const float lookahead=observer.Lookahead(speed);
        std::array<Point,64> path{};std::size_t nodes=1;path[0]=PointOf(position);
        float cursor=std::clamp(plan.ToLine(along),0.0F,plan.path.Length());
        float covered=0;
        // Coverage is measured from the ACTUAL position in the same XY units
        // as Evaluate. Arc length alone overstates sampled chord coverage on a
        // bend and used to turn an empty curved course into "unknown".
        while(nodes<path.size() && covered<lookahead && cursor<plan.path.Length()) {
            cursor=std::min(plan.path.Length(),cursor+4);
            const auto next=PointOf(race_pace::At(plan.path,cursor));
            covered+=std::hypot(next.x-path[nodes-1].x,next.y-path[nodes-1].y);
            path[nodes++]=next;
        }
        std::size_t vehicles=0;bool complete=shared_vehicles.complete;
        for(std::size_t i=0;i<shared_vehicles.count;++i) {
            auto& item=shared_vehicles.items[i];
            // Larger than the bounded stopping horizon plus a carriage length.
            // Filtering is per rider; each nearby body is still read only once.
            if(destination::Distance(position,item.origin)>160)continue;
            if(vehicles==local.size()){complete=false;break;}
            local[vehicles++]=shared_vehicles.Read(item);
        }
        const auto result=Evaluate(std::span(path.data(),nodes),speed,
            {std::span(local.data(),vehicles),complete,static_cast<float>(now-shared_vehicles.sampled)},lookahead,
            cursor>=plan.path.Length());
        // A partial scan can prove a closer obstruction, never that an older
        // close body disappeared. Only complete coverage may replace it with
        // a farther nearest body; the corridor stays restricted at that cap.
        // Fully clear samples still require the observer's 0.75s debounce.
        if(result.known&&result.blocked)
            remaining=!decision.blocked || result.complete?result.distance:std::min(remaining,result.distance);
        if(result.blocked)for(std::size_t i=0;i<vehicles;++i)if(local[i].entity==result.vehicle) {
            blocking_vehicle=local[i].entity;blocking_model=local[i].model;
            vehicle_position=local[i].center;vehicle_velocity=local[i].velocity;break;
        }
        const bool before=decision.blocked;
        decision=observer.Update(result,now);changed=before!=decision.blocked;
        if(!decision.blocked)remaining=0;
        return decision.blocked;
    }
};
} // namespace horse_races::vehicle_clearance
