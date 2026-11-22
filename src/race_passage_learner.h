#pragma once
#include "race_obstacle_map.h"
#include "race_strip_geometry.h"
#include "game_natives.h"
#include <deque>
#include <string>

namespace horse_races::passage_learner {
inline constexpr double kSampleSeconds=.1,kSettleSeconds=.6,kMaximumGapSeconds=.35;
inline constexpr float kPhysicalRadius=.5F,kMaximumSpeed=40,kAxisHalfWidth=.12F;
struct Sample {Vector3 at{};double time=0;float speed=0,along=0;bool safe=false;};
struct Crossing {Vector3 at{};std::size_t row=0;float lateral=0;};

// Pure observation chain: keep a short, delayed trail so a jump, collision,
// teleport or streaming interruption cannot immediately paint free space.
// There are no world queries, route edits or driving commands here.
struct Trail {
    std::deque<Sample> pending;
    std::vector<Vector3> between;
    Crossing crossing{};bool have_crossing=false;
    float crossing_radius=0;
    Sample last_observation{};bool have_observation=false;
    std::string key,geometry;float lower=0,upper=0;
    std::uint64_t accepted=0,rejected=0;
    std::uint64_t impact_epoch=0;
    void Break(){pending.clear();between.clear();have_crossing=false;crossing_radius=0;have_observation=false;}
    void Reset(){*this={};}
    static bool Covered(Vector3 a,Vector3 b,const strip_geometry::Patch& patch) {
        if(destination::Distance(a,b)<1e-5F)return strip_geometry::Inside(a,patch,1e-5);
        const auto covered=strip_geometry::CoveredIntervals(a,b,patch);
        // World coordinates are stored as floats. Express the endpoint
        // tolerance in metres so a few micrometres of plane roundoff cannot
        // reject every short render-frame segment at map coordinates.
        const double tolerance=std::min(.01,static_cast<double>(.001F/std::max(.001F,destination::Distance(a,b))));
        double end=0;
        for(std::size_t i=0;i<covered.count;++i) {
            if(covered.ranges[i].lo>end+tolerance)return false;
            end=std::max(end,covered.ranges[i].hi);
        }
        return end>=1-tolerance;
    }
    bool Link(obstacle_map::Map& map,const course::Path& path,const Crossing& next) {
        if(!have_crossing||next.row!=crossing.row+1||between.size()>64)return false;
        const auto a=map.SectionFor(path,crossing.row),b=map.SectionFor(path,next.row);
        const float normal=strip_geometry::MinimumNormalLength(a,b);
        if(normal<.5F||map.Along(next.row)-map.Along(crossing.row)>obstacle_map::kStepMetres+.01F)return false;
        // Spend only the already observed footprint budget. A curved ride
        // need not fit an arbitrary fixed 8 cm axis tube. In a regular patch,
        // continuity supplies an observed point at every longitudinal t;
        // interpolated unit normals have length <=1. Thus axis radius plus
        // trajectory deviation <= crossing_radius stays within that ride's
        // conservative footprint tube, even when the section normals turn.
        if(!std::isfinite(crossing_radius)||crossing_radius<=.02F)return false;
        const float radius=std::min(kAxisHalfWidth,crossing_radius*.5F);
        const float tolerance=crossing_radius-radius;
        const strip_geometry::Patch check{a,b,{crossing.lateral-tolerance,crossing.lateral+tolerance},
            {next.lateral-tolerance,next.lateral+tolerance}};
        // CoveredIntervals may trim a folded patch for ordinary union queries.
        // This proof needs the ORIGINAL whole tube's continuous unique inverse.
        if(!strip_geometry::detail::Regular(check))return false;
        Vector3 previous=crossing.at;
        for(auto p:between){if(!Covered(previous,p,check))return false;previous=p;}
        if(!Covered(previous,next.at,check))return false;
        // At most this small body-width tube was actually occupied. A ride
        // cannot certify the unvisited road shoulders or another lap profile.
        return map.ConfirmConnection(next.row,{{crossing.lateral-radius,crossing.lateral+radius},
            {next.lateral-radius,next.lateral+radius},normal});
    }
    bool Segment(obstacle_map::Map& map,const course::Path& path,const Sample& a,const Sample& b) {
        bool changed=false;
        // The small axis tube lies within the union of two observed .5 m
        // body footprints. Keep that sampling bound, but do not later erode
        // these already certified centre axes by another body width/margin.
        const float radius=kPhysicalRadius-destination::Distance(a.at,b.at)*.5F;
        if(radius<=.01F){have_crossing=false;between.clear();return false;}
        if(have_crossing)crossing_radius=std::min(crossing_radius,radius);
        const auto first=map.Index(std::max(lower,std::min(a.along,b.along)-3));
        const auto last=std::min(map.rows.size()-1,map.Index(std::min(upper,std::max(a.along,b.along)+3))+1);
        for(std::size_t row=first;row<=last;++row) {
            if(map.Along(row)<lower||map.Along(row)>upper)continue;
            const auto section=map.SectionFor(path,row);
            const float tx=section.ny,ty=-section.nx;
            const float start=(a.at.x-section.centre.x)*tx+(a.at.y-section.centre.y)*ty;
            const float end=(b.at.x-section.centre.x)*tx+(b.at.y-section.centre.y)*ty;
            if(start>=0||end<0||end-start<1e-5F)continue;
            const float t=-start/(end-start);
            Crossing next;next.row=row;next.at={a.at.x+t*(b.at.x-a.at.x),a.at.y+t*(b.at.y-a.at.y),a.at.z+t*(b.at.z-a.at.z)};
            next.lateral=(next.at.x-section.centre.x)*section.nx+(next.at.y-section.centre.y)*section.ny;
            if(std::abs(next.lateral)+kPhysicalRadius>map.width){have_crossing=false;between.clear();continue;}
            const float axis_radius=std::min(kAxisHalfWidth,radius);
            changed=map.ConfirmSection(row,{next.lateral-axis_radius,next.lateral+axis_radius})||changed;
            changed=Link(map,path,next)||changed;
            crossing=next;have_crossing=true;crossing_radius=radius;between.clear();
        }
        if(have_crossing) {
            if(between.size()<64)between.push_back(b.at);
            else {have_crossing=false;between.clear();}
        }
        return changed;
    }
    bool Observe(obstacle_map::Map& map,const course::Path& path,Sample input,float from,float to) {
        if(map.rows.empty()||path.points.size()<2||!std::isfinite(from)||!std::isfinite(to)||from<0||to<=from) {Break();return false;}
        if(impact_epoch!=obstacle_map::physical_impact_epoch) {
            Break();impact_epoch=obstacle_map::physical_impact_epoch;
        }
        if(key!=map.key||geometry!=map.geometry_hash||from!=lower||to!=upper) {
            Break();key=map.key;geometry=map.geometry_hash;lower=from;upper=to;
        }
        if(!input.safe||!destination::Finite(input.at)||!std::isfinite(input.time)||!std::isfinite(input.speed)||input.speed<0||input.speed>kMaximumSpeed) {
            Break();++rejected;return false;
        }
        const float search_from=pending.empty()?from:std::max(from,pending.back().along-12);
        const float search_to=pending.empty()?to:std::min(to,pending.back().along+12);
        const auto projected=course::Project(path,input.at,search_from,search_to);
        // Progress projection tolerates stacked roads for race adjudication;
        // positive clearance cannot borrow a ride from another height level.
        if(!projected.valid||projected.distance>map.width||
            std::abs(input.at.z-obstacle_map::Map::At(path,projected.along).z)>2) {Break();++rejected;return false;}
        input.along=projected.along;
        if(have_observation) {
            const auto& previous=last_observation;const double dt=input.time-previous.time;
            const float travel=std::hypot(destination::Distance(previous.at,input.at),input.at.z-previous.at.z);
            const float reachable=std::max(.5F,static_cast<float>(dt)*std::max(previous.speed,input.speed)*1.35F+.25F);
            if(dt<=0||dt>kMaximumGapSeconds||travel>reachable||input.along<previous.along-.2F||
                (previous.speed-input.speed>=4 && input.speed<previous.speed*.6F)) {
                Break();++rejected;pending.push_back(input);last_observation=input;have_observation=true;return false;
            }
        }
        last_observation=input;have_observation=true;
        // At walking pace keep spatially useful footprints, not dozens of
        // identical render-frame positions. Safety still observes every input.
        if(pending.empty()||destination::Distance(pending.back().at,input.at)>=.06F)pending.push_back(input);
        bool changed=false;
        while(pending.size()>1&&input.time-pending[1].time>=kSettleSeconds) {
            changed=Segment(map,path,pending[0],pending[1])||changed;
            pending.pop_front();
        }
        if(pending.size()>256){Break();++rejected;}
        if(changed)++accepted;
        return changed;
    }
};

struct Learner : Trail {
    double next_check=0,last_sample=-1;Ped identity=0;bool stable=false;float speed=0;
    void Reset(){Trail::Reset();next_check=0;last_sample=-1;identity=0;stable=false;speed=0;}
    bool PhysicalAnchor(double now,Vector3& point) const {
        if(!identity||!stable||!have_observation||!last_observation.safe||!std::isfinite(now)||
            now<last_observation.time||now-last_observation.time>kSampleSeconds||!destination::Finite(last_observation.at))return false;
        point=last_observation.at;return true;
    }
    bool PhysicalSample(bool collision_disabled,bool is_static,bool frozen) {
        if(!collision_disabled&&!is_static&&!frozen)return true;
        // These modes can change between the slower environmental checks.
        // Discard the entire delayed trail before any of it can mature.
        Break();stable=false;next_check=0;++rejected;return false;
    }
    bool Tick(obstacle_map::Map& map,const course::Path& path,Ped horse,double now,float from,float to,bool observe,std::string* note=nullptr) {
        if(!observe||!horse){Break();identity=0;stable=false;return false;}
        if(!std::isfinite(now)||now<last_sample){Reset();return false;}
        if(now-last_sample<1.0/120)return false;
        last_sample=now;
        if(horse!=identity){Break();identity=horse;next_check=0;stable=false;}
        if(!PhysicalSample(ENTITY::GET_ENTITY_COLLISION_DISABLED(horse),ENTITY::IS_ENTITY_STATIC(horse),
            ENTITY::_IS_ENTITY_FROZEN(horse)))return false;
        if(now>=next_check) {
            next_check=now+kSampleSeconds;
            stable=ENTITY::DOES_ENTITY_EXIST(horse)&&!ENTITY::IS_ENTITY_DEAD(horse)&&
            ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(horse)&&!ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(horse)&&
            !ENTITY::IS_ENTITY_IN_AIR(horse,false)&&!ENTITY::IS_ENTITY_IN_WATER(horse)&&
            !PED::IS_PED_FALLING(horse)&&!PED::IS_PED_RAGDOLL(horse)&&!PED::IS_PED_JUMPING(horse)&&!PED::IS_PED_SWIMMING(horse);
            if(stable)speed=ENTITY::GET_ENTITY_SPEED(horse);
        }
        if(!stable){Break();++rejected;return false;}
        const bool changed=Observe(map,path,{ENTITY::GET_ENTITY_COORDS(horse,true,false),now,speed,0,true},from,to);
        if(changed&&note)*note="safe passage sections/links updated accepted="+std::to_string(accepted)+" rejected="+std::to_string(rejected);
        return changed;
    }
};
}
