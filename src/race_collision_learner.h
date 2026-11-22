#pragma once
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "race_collision_evidence.h"
#include "race_obstacle_learning.h"
#include "race_obstacle_survey.h"
#include "world/identity.h"

// One physical impact owns its pre-contact motion and asynchronous results.
// Contact and rider loss survive until classification; current touching is
// deliberately not required after the horse has bounced away from the object.
namespace horse_races::collision_learner {
inline constexpr float kDropMps=4,kDropFraction=.6F;
inline constexpr double kTickSeconds=.5,kCooldownSeconds=5,kProbePatienceSeconds=1;
inline constexpr float kSupportBehindMetres=.75F,kStaticSpeedMps=.1F;
// Reserve one event beside the ordinary survey's four pending capsules.
// The global queue remains bounded even if a native result never completes.
inline constexpr std::size_t kImpactPendingLimit=obstacle_survey::kPendingCapsules+2;
// Other actors keep their full first-hit veto during own-rider/mount
// continuation. Never remove actors from the query mask to find a wall.
inline constexpr int kImpactMask=obstacle_survey::kSweepMask|4|8;
struct Learner {
    float last_speed=0;double last_time=0;Vector3 last_position{},last_forward{};bool have=false;
    double issued_at=0,cooldown_until=0,anchor_age=-1;float probe_from=0,probe_to=INFINITY;
    Vector3 sweep_from{},sweep_to{},probe_direction{},facing_from{},facing_to{},issue_facing{};
    Vector3 motion_from{},motion_to{};
    Vector3 contact_from{},contact_to{},contact_forward{},contact_exit_forward{};double contact_at=0;
    bool contact_anchor=false,last_contact=false;double mounted_at=-INFINITY;
    bool active=false,contact_seen=false,fall_seen=false,event_dismount=false,event_last_contact=false,initial_contact=false;
    world::Identity event_horse,event_rider;
    std::string event_map_key,event_geometry;
    std::array<std::shared_ptr<obstacle_survey::QueryResult>,2> queries{};
    std::array<float,2> query_progress{};
    std::array<Vector3,2> query_from{},query_to{};
    std::array<int,2> self_continuations{};
    const char* self_stop="none";
    struct Trace {
        std::size_t band=0;int continuation=0;Ped ignored=0;float progress=0;
        Vector3 from{},to{};std::shared_ptr<obstacle_survey::QueryResult> query;
    };
    std::vector<Trace> traces;
    int drops=0,learned=0;Outcome outcome=Outcome::None;
    int bounds_layout=0;
    std::uint64_t revision_before=0,revision_after=0;
    // Value copy of the accepted static first hit, valid only on the Tick
    // that finishes this event. Finish releases asynchronous query objects.
    std::optional<Vector3> confirmed_hit;

    bool Busy() const{return active;}
    void ResetMotion(){have=false;contact_anchor=false;last_contact=false;mounted_at=-INFINITY;}
    // QueryQueue still owns every unfinished handle after cancellation/reset.
    void Cancel(){queries={};traces.clear();active=false;outcome=Outcome::Cancelled;ResetMotion();}
    void Reset(){*this={};}
    static bool Permanent(Entity entity) {
        if(!entity)return true;
        if(!ENTITY::DOES_ENTITY_EXIST(entity)||ENTITY::IS_ENTITY_A_PED(entity)||ENTITY::IS_ENTITY_A_VEHICLE(entity))return false;
        const float speed=ENTITY::GET_ENTITY_SPEED(entity);
        return std::isfinite(speed)&&speed<=kStaticSpeedMps;
    }
    static bool Mounted(Ped rider,Ped horse) {
        return rider&&PED::IS_PED_ON_MOUNT(rider)&&PED::GET_MOUNT(rider)==horse;
    }
    static bool RiderFell(Ped rider) {
        return rider&&(PED::IS_PED_FALLING(rider)||PED::IS_PED_RAGDOLL(rider)||ENTITY::IS_ENTITY_DEAD(rider));
    }
    Outcome Obstruction(Vector3 hit,Vector3 normal) const {
        if(!obstacle_survey::UnitNormal(normal))return Outcome::InvalidNormal;
        if(normal.z<obstacle_survey::kMinimumNormalZ)return Outcome::Learned;
        // One support observation per actual first hit. Unknown service or
        // continuous terrain cannot become persistent obstacle evidence.
        if(obstacle_survey::QueryQueue::Total()>=kImpactPendingLimit||!obstacle_survey::QueryQueue::CanIssue())return Outcome::GroundUnknown;
        obstacle_survey::QueryQueue::Charge charge{std::chrono::steady_clock::now()};
        obstacle_survey::QueryQueue::Issued();
        const Vector3 behind{hit.x-probe_direction.x*kSupportBehindMetres,
            hit.y-probe_direction.y*kSupportBehindMetres,hit.z};
        const auto ground=obstacle_survey::Line({behind.x,behind.y,hit.z+obstacle_survey::kGroundProbeAboveRiding},
            {behind.x,behind.y,hit.z-obstacle_survey::kProbeBelow},obstacle_survey::kWorld|obstacle_survey::kObjects);
        if(ground.pending)obstacle_survey::QueryQueue::Track(ground.pending);
        if(ground.status!=2||!ground.hit||!Permanent(ground.entity))return Outcome::GroundUnknown;
        obstacle_survey::Sample a,b;a.at=ground.at;a.normal=ground.normal;a.valid=Finite(ground.at);
        b.at=hit;b.normal=normal;b.valid=Finite(hit);
        if(!a.HasNormal()||!b.HasNormal())return Outcome::GroundUnknown;
        std::uint32_t reason=0;
        const auto verdict=obstacle_survey::SegmentVerdict(a,b,reason,false);
        return verdict==obstacle_survey::Verdict::Blocked?Outcome::Learned:
            verdict==obstacle_survey::Verdict::Clear?Outcome::SupportingSurface:Outcome::GroundUnknown;
    }
    bool Finish(Outcome result,const obstacle_map::Map& map,double now,std::string* note) {
        outcome=result;revision_after=map.revision;active=false;ResetMotion();
        if(note)*note="event="+std::to_string(drops)+" result="+Name(result)+
            " contact="+std::to_string(contact_seen)+" rider_fall="+std::to_string(fall_seen)+
            " contact_initial_raw="+std::to_string(initial_contact)+" contact_latest_raw="+
            std::to_string(event_last_contact)+" fresh_anchor_age="+std::to_string(anchor_age)+
            " bounds_layout="+std::to_string(bounds_layout)+
            " self_continuations="+std::to_string(self_continuations[0]+self_continuations[1])+
            " self_stop="+self_stop+
            " unscaled_game_seconds="+std::to_string(now-issued_at)+" revision="+std::to_string(revision_before)+
            "->"+std::to_string(revision_after)+" sweep=("+std::to_string(sweep_from.x)+","+
            std::to_string(sweep_from.y)+","+std::to_string(sweep_from.z)+")->("+
            std::to_string(sweep_to.x)+","+std::to_string(sweep_to.y)+","+std::to_string(sweep_to.z)+")";
        if(note)for(const auto& trace:traces)if(trace.query) {
            const auto& q=*trace.query;int type=0;Hash model=0;
            if(q.hit&&q.entity&&ENTITY::DOES_ENTITY_EXIST(q.entity)) {
                type=ENTITY::GET_ENTITY_TYPE(q.entity);model=ENTITY::GET_ENTITY_MODEL(q.entity);
            }
            *note+=" band="+std::to_string(trace.band)+" continuation="+std::to_string(trace.continuation)+
                " ignore="+std::to_string(trace.ignored)+" advance_m="+std::to_string(trace.progress)+
                " raw_handle="+std::to_string(q.raw_handle)+
                " handle_conflict="+std::to_string(q.handle_conflict)+" issue_frame="+std::to_string(q.issue_frame)+
                " first_poll_frame="+std::to_string(q.first_poll_frame)+" last_poll_frame="+
                std::to_string(q.last_poll_frame)+" polls="+std::to_string(q.polls)+
                " native_status="+std::to_string(q.native_status)+" status="+std::to_string(q.status)+" hit="+
                std::to_string(q.hit)+" entity="+std::to_string(q.entity)+" type="+std::to_string(type)+
                " model="+std::to_string(model)+" at=("+std::to_string(q.at.x)+","+
                std::to_string(q.at.y)+","+std::to_string(q.at.z)+") normal=("+
                std::to_string(q.normal.x)+","+std::to_string(q.normal.y)+","+std::to_string(q.normal.z)+")";
        }
        if(note&&(self_continuations[0]||self_continuations[1]))
            *note+=" static_scope=original-sweep-remainder impact_object_attributed=0";
        if(note)*note+=" facing_from=("+std::to_string(facing_from.x)+","+std::to_string(facing_from.y)+","+
            std::to_string(facing_from.z)+") facing_to=("+std::to_string(facing_to.x)+","+
            std::to_string(facing_to.y)+","+std::to_string(facing_to.z)+") issue_facing=("+
            std::to_string(issue_facing.x)+","+std::to_string(issue_facing.y)+","+std::to_string(issue_facing.z)+")";
        queries={};traces.clear();
        return result==Outcome::Learned;
    }
    bool Issue(std::size_t band,Vector3 from,Vector3 to,Ped ignored,float progress,int continuation) {
        if(obstacle_survey::QueryQueue::Total()>=kImpactPendingLimit||!obstacle_survey::QueryQueue::CanIssue())return false;
        obstacle_survey::QueryQueue::Charge charge{std::chrono::steady_clock::now()};
        obstacle_survey::QueryQueue::Issued();
        const auto raw=SHAPETEST::START_SHAPE_TEST_CAPSULE(from.x,from.y,from.z,to.x,to.y,to.z,
            obstacle_survey::kCapsuleRadius,kImpactMask,ignored,obstacle_survey::kCapsuleOptions);
        queries[band]=obstacle_survey::QueryQueue::Track(raw);query_progress[band]=progress;
        query_from[band]=from;query_to[band]=to;
        self_continuations[band]=continuation;
        traces.push_back({band,continuation,ignored,progress,from,to,queries[band]});
        return true;
    }
    bool Drain(obstacle_map::Map& map,const course::Path& path,Ped horse,Ped rider,double now,std::string* note) {
        if(map.key!=event_map_key||map.geometry_hash!=event_geometry||
            horse!=event_horse.entity||rider!=event_rider.entity||
            !world::Still(event_horse)||!world::Still(event_rider))
            return Finish(Outcome::OwnerChanged,map,now,note);
        const bool contact=ENTITY::HAS_ENTITY_COLLIDED_WITH_ANYTHING(horse)!=0;
        if(!contact_seen&&contact&&!event_last_contact&&now>=issued_at&&now-issued_at<=kContactWindowSeconds) {
            const auto p=ENTITY::GET_ENTITY_COORDS(horse,true,false);
            contact_seen=SegmentDistance(p,motion_from,motion_to)<=obstacle_survey::kCapsuleRadius;
        }
        event_last_contact=contact;
        fall_seen|=RiderFell(rider)||(event_dismount&&!Mounted(rider,horse));
        bool pending=false;
        for(const auto& q:queries) {
            if(!q||q->status==0)return Finish(Outcome::QueryUnavailable,map,now,note);
            if(q->status==1)pending=true;
            else if(q->status!=2)return Finish(Outcome::QueryUnavailable,map,now,note);
        }
        const bool timeout=!std::isfinite(now)||now-issued_at>kProbePatienceSeconds;
        if(pending)return timeout?Finish(Outcome::QueryTimeout,map,now,note):false;
        bool any=false,self=false;
        // Inspect both FIRST contacts before accepting either static result.
        for(std::size_t i=0;i<queries.size();++i) {
            const auto& q=*queries[i];if(!q.hit)continue;
            if(q.entity==horse||q.entity==rider){self=true;continue;}
            if(!Permanent(q.entity))return Finish(Outcome::DynamicContact,map,now,note);
            const float height=i?obstacle_survey::kHeadCapsuleCentre:obstacle_survey::kCapsuleCentre;
            auto a=sweep_from,b=sweep_to;a.z+=height;b.z+=height;
            if(SegmentDistance(q.at,a,b)>obstacle_survey::kCapsuleRadius+.001F||
                SegmentDistance(q.at,query_from[i],query_to[i])>obstacle_survey::kCapsuleRadius+.001F)
                return Finish(Outcome::OutsideSweep,map,now,note);
            any=true;
        }
        // Only our own two identities may be passed. Keep static bands while
        // the occluded band continues, so a newly exposed other actor can
        // still veto the whole event before any permanent map update.
        if(self) {
            if(timeout){self_stop="deadline";return Finish(Outcome::SelfContact,map,now,note);}
            for(std::size_t i=0;i<queries.size();++i) {
                const auto& q=*queries[i];if(!q.hit||(q.entity!=horse&&q.entity!=rider))continue;
                const float height=i?obstacle_survey::kHeadCapsuleCentre:obstacle_survey::kCapsuleCentre;
                auto a=sweep_from,b=sweep_to;a.z+=height;b.z+=height;
                float progress=0;Vector3 next{};
                if(self_continuations[i]>=kMaximumSelfContinuations) {
                    self_stop="step-limit";return Finish(Outcome::SelfContact,map,now,note);
                }
                if(!ContinuePastSelf(a,b,q.at,obstacle_survey::kCapsuleRadius,query_progress[i],progress,next)) {
                    self_stop="axis-end";return Finish(Outcome::SelfContact,map,now,note);
                }
                const auto ignored=q.entity;const int continuation=self_continuations[i]+1;
                if(!Issue(i,next,b,ignored,progress,continuation))return Finish(Outcome::QueryBudget,map,now,note);
                if(queries[i]->status==0)return Finish(Outcome::QueryUnavailable,map,now,note);
            }
            return false;
        }
        if(!any)return Finish(Outcome::NoHit,map,now,note);
        if(!contact_seen)return timeout?Finish(Outcome::NoContact,map,now,note):false;
        if(!fall_seen)return timeout?Finish(Outcome::NoRiderFall,map,now,note):false;
        Outcome rejected=Outcome::SupportingSurface;
        std::optional<Vector3> already_excluded;
        for(const auto& ptr:queries) {
            const auto& q=*ptr;if(!q.hit||q.entity==horse||q.entity==rider)continue;
            const auto classification=Obstruction(q.at,q.normal);
            if(classification!=Outcome::Learned){rejected=classification;continue;}
            obstacle_learning::Learned detail;
            // This confirms a static obstruction in the sampled physical
            // footprint. After passing self, the omitted overlap cannot be
            // attributed to a particular collision partner; do not claim the
            // static surface caused the fall or that every hidden actor was
            // observed. Other actor results themselves are never continued.
            const int rows=obstacle_learning::NarrowAt(map,path,q.at,obstacle_survey::kShoulderMetres,&detail,probe_from,probe_to);
            if(rows>0){confirmed_hit=q.at;++learned;return Finish(Outcome::Learned,map,now,note);}
            const auto projected=course::Project(path,q.at,probe_from,probe_to);
            if(projected.valid && projected.distance<=map.width)already_excluded=q.at;
            rejected=Outcome::AlreadyExcluded;
        }
        if(already_excluded){confirmed_hit=*already_excluded;return Finish(Outcome::AlreadyExcluded,map,now,note);}
        return Finish(rejected,map,now,note);
    }
    // Two bands, with bounded self-contact continuations; never a query per
    // observation and never more than two event queries pending at once.
    bool Tick(obstacle_map::Map& map,const course::Path& path,Ped horse,double now,std::string* note,
        float from=0,float to=INFINITY,bool observe=true,Ped rider=0) {
        confirmed_hit.reset();
        if(active)return Drain(map,path,horse,rider,now,note);
        if(!observe||!horse||!rider||!ENTITY::DOES_ENTITY_EXIST(horse)||!ENTITY::DOES_ENTITY_EXIST(rider)) {
            ResetMotion();return false;
        }
        const Vector3 p=ENTITY::GET_ENTITY_COORDS(horse,true,false);
        const Vector3 forward=ENTITY::GET_ENTITY_FORWARD_VECTOR(horse);
        const float speed=ENTITY::GET_ENTITY_SPEED(horse);
        const bool contact=ENTITY::HAS_ENTITY_COLLIDED_WITH_ANYTHING(horse)!=0,mounted=Mounted(rider,horse);
        if(mounted)mounted_at=now;
        const bool recently_mounted=now>=mounted_at&&now-mounted_at<=kContactWindowSeconds;
        const bool motion=have&&ShortObservedSweep(last_position,p,now-last_time)&&std::isfinite(speed);
        const bool fallen=RiderFell(rider);
        if(!motion)contact_anchor=false;
        if(contact&&motion&&!last_contact) {
            contact_from=last_position;contact_to=p;contact_forward=last_forward;
            contact_exit_forward=forward;contact_at=now;contact_anchor=true;
        }
        const bool sudden=motion&&last_speed-speed>=kDropMps&&speed<last_speed*kDropFraction;
        const bool event=motion&&recently_mounted&&(sudden||(contact&&fallen))&&now>=cooldown_until;
        if(event) {
            ++drops;cooldown_until=now+kCooldownSeconds;issued_at=now;
            queries={};query_progress={};query_from={};query_to={};self_continuations={};self_stop="none";traces.clear();
            probe_from=from;probe_to=to;revision_before=revision_after=map.revision;
            const bool anchored=contact_anchor&&now>=contact_at&&now-contact_at<=kContactWindowSeconds;
            contact_seen=anchored;initial_contact=event_last_contact=contact;anchor_age=anchored?now-contact_at:-1;
            event_dismount=sudden&&anchored;fall_seen=fallen||(event_dismount&&!mounted);
            const Vector3 origin_from=anchored?contact_from:last_position,origin_to=anchored?contact_to:p;
            motion_from=origin_from;motion_to=origin_to;
            facing_from=anchored?contact_forward:last_forward;facing_to=anchored?contact_exit_forward:forward;
            issue_facing=forward;
            alignas(16) std::array<float,8> raw_low,raw_high;
            raw_low.fill(NAN);raw_high.fill(NAN);
            MISC::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(horse),
                reinterpret_cast<Vector3*>(raw_low.data()),reinterpret_cast<Vector3*>(raw_high.data()));
            Vector3 low{},high{};bounds_layout=ModelBound(raw_low,low);
            if(!bounds_layout||ModelBound(raw_high,high)!=bounds_layout)
                return Finish(Outcome::InvalidFootprint,map,now,note);
            if(!FrontSweep(origin_from,origin_to,facing_from,facing_to,low,high,obstacle_survey::kCapsuleRadius,sweep_from,sweep_to))
                return Finish(Outcome::InvalidFootprint,map,now,note);
            const float dx=origin_to.x-origin_from.x,dy=origin_to.y-origin_from.y,length=std::hypot(dx,dy);
            const float facing_length=std::hypot(facing_from.x,facing_from.y);
            probe_direction=length>.01F?Vector3{dx/length,dy/length,0}:
                facing_length>.01F?Vector3{facing_from.x/facing_length,facing_from.y/facing_length,0}:Vector3{};
            event_horse=world::Identify(horse);event_rider=world::Identify(rider);active=true;
            event_map_key=map.key;event_geometry=map.geometry_hash;
            if(obstacle_survey::QueryQueue::Total()+queries.size()>kImpactPendingLimit)
                return Finish(Outcome::QueryBudget,map,now,note);
            for(std::size_t i=0;i<queries.size();++i) {
                const float height=i?obstacle_survey::kHeadCapsuleCentre:obstacle_survey::kCapsuleCentre;
                auto a=sweep_from,b=sweep_to;a.z+=height;b.z+=height;
                if(!Issue(i,a,b,horse,0,0))return Finish(Outcome::QueryBudget,map,now,note);
                if(queries[i]->status==0)return Finish(Outcome::QueryUnavailable,map,now,note);
            }
            return false;
        }
        have=Finite(p)&&Finite(forward)&&std::isfinite(speed)&&std::isfinite(now);
        last_position=p;last_forward=forward;last_speed=speed;last_time=now;
        last_contact=contact;
        return false;
    }
};
} // namespace horse_races::collision_learner
