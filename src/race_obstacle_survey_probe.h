#pragma once
#include "race_obstacle_map.h"
#include "game_natives.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

// One resumable survey per map, not per racer. Every native query, candidate
// scan and publish is an individual budgeted step. A native already executing
// cannot be preempted: max_step_ms and budget_overruns expose that exception.
namespace horse_races::obstacle_survey {
inline constexpr float kSurveyHalfWidth=20,kRangeMetres=120,kEdgeMetres=.35F;
inline constexpr float kChestMetres=1.2F,kShinMetres=.45F,kHeadMetres=2.4F;
inline constexpr int kSurveyVersion=obstacle_map::kDetailedVersion;
inline constexpr float kCapsuleRadius=obstacle_map::kMaxConnectionRadius,kCapsuleCentre=.95F,kHeadCapsuleCentre=2.2F;
inline constexpr int kCapsuleOptions=4,kCapsulesPerFrame=16,kSweepPatienceFrames=300;
inline constexpr std::size_t kPendingRows=4,kPendingCapsules=4;
inline constexpr float kShoulderMetres=.3F,kProbeAbove=3,kProbeBelow=4;
inline constexpr double kFrameBudgetMs=.4,kFreezeBudgetMs=kFrameBudgetMs;
inline constexpr double kOnlineBudgetMs=kFrameBudgetMs-.05; // Reserve bounded publication into race copies.
inline constexpr int kWorld=1,kVehicles=2,kObjects=16,kFoliage=256;
inline constexpr int kProbeMask=kWorld|kObjects|kVehicles,kSweepMask=kWorld|kObjects|kVehicles,kProbeOptions=7;
inline constexpr int kCells=40,kNodes=81;
inline constexpr float kGridPitch=.5F,kGroundFollowTolerance=.08F,kMinimumNormalZ=.65F;
inline constexpr float kGroundProbeAboveRiding=.25F; // Keep overhead roofs out of the floor query.
inline constexpr float kMaximumRecordedFloorDrop=2.5F; // A different level cannot seed the measured road.
inline constexpr float kStaticSpeedMps=.1F,kSupportContactTolerance=.12F;

struct Probe {bool hit=false,traffic=false;Vector3 at{},normal{};Entity entity=0;int status=0;ScrHandle pending=0;};
inline ScrHandle StartCapsuleProbe(Vector3 from,Vector3 to) {
    return SHAPETEST::START_SHAPE_TEST_CAPSULE(from.x,from.y,from.z,to.x,to.y,to.z,
        kCapsuleRadius,kSweepMask,0,kCapsuleOptions);
}
inline bool Transient(Entity entity) {
    // Entity zero is a world collider, not a missing/soft object. A parked
    // wagon, ped, or moving prop is not permanent course geometry either.
    if(!entity)return false;
    if(!ENTITY::DOES_ENTITY_EXIST(entity)||ENTITY::IS_ENTITY_A_PED(entity)||ENTITY::IS_ENTITY_A_VEHICLE(entity))return true;
    const float speed=ENTITY::GET_ENTITY_SPEED(entity);return !std::isfinite(speed)||speed>kStaticSpeedMps;
}
inline Probe Line(Vector3 from,Vector3 to,int flags) {
    Probe out;const auto handle=SHAPETEST::START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE(from.x,from.y,from.z,to.x,to.y,to.z,flags,0,kProbeOptions);
    if(!handle)return out;
    BOOL hit=false;out.status=SHAPETEST::GET_SHAPE_TEST_RESULT(handle,&hit,&out.at,&out.normal,&out.entity);
    if(out.status==1)out.pending=handle;
    if(out.status==2&&hit){out.traffic=Transient(out.entity);out.hit=!out.traffic;}return out;
}
inline bool Ground(float x,float y,float riding_z,float& ground) {
    const auto p=Line({x,y,riding_z+kProbeAbove},{x,y,riding_z-kProbeBelow},kWorld|kObjects);
    if(!p.hit||!std::isfinite(p.at.z))return false;ground=p.at.z;return true;
}
inline bool UnderWater(float x,float y,float ground) {
    float water=0;return WATER::GET_WATER_HEIGHT(x,y,ground+1,&water)&&std::isfinite(water)&&water>ground+.3F;
}
struct Sample {
    Vector3 at{},normal{};bool valid=false,water=false;
    bool HasNormal() const {return std::isfinite(normal.x)&&std::isfinite(normal.y)&&std::isfinite(normal.z)&&normal.z>.05F;}
};
inline Vector3 Above(const Sample& p,float height){return {p.at.x,p.at.y,p.at.z+height};}
inline Sample Blend(const Sample& a,const Sample& b,float t) {
    Sample out;out.at={a.at.x+(b.at.x-a.at.x)*t,a.at.y+(b.at.y-a.at.y)*t,a.at.z+(b.at.z-a.at.z)*t};return out;
}
enum class Verdict {Clear,Blocked,Unknown,Refine};
struct Edge {
    Sample a,b;std::array<Sample,3> inside{};bool refined=false,cross=false,ray=false;
    std::uint32_t reason=0;
};
inline Verdict SegmentVerdict(const Sample& a,const Sample& b,std::uint32_t& reason,bool may_refine) {
    if(!a.valid||!b.valid){reason|=obstacle_map::kGroundUnknown;return Verdict::Unknown;}
    if(a.water||b.water){reason|=obstacle_map::kWater;return Verdict::Blocked;}
    if((a.HasNormal()&&a.normal.z<kMinimumNormalZ)||(b.HasNormal()&&b.normal.z<kMinimumNormalZ)) {
        reason|=obstacle_map::kSteep;return Verdict::Blocked;
    }
    const float dx=b.at.x-a.at.x,dy=b.at.y-a.at.y,dz=b.at.z-a.at.z;
    if(std::abs(dz)<=kEdgeMetres)return Verdict::Clear;
    if(a.HasNormal()&&b.HasNormal()) {
        const float predicted=-.5F*((a.normal.x*dx+a.normal.y*dy)/a.normal.z+(b.normal.x*dx+b.normal.y*dy)/b.normal.z);
        const float dot=a.normal.x*b.normal.x+a.normal.y*b.normal.y+a.normal.z*b.normal.z;
        if(dot>.65F&&std::abs(dz-predicted)<=.08F+.04F*std::hypot(dx,dy))return Verdict::Clear;
    }
    if(may_refine)return Verdict::Refine;
    reason|=obstacle_map::kStep;return Verdict::Blocked;
}
inline Verdict EdgeVerdict(Edge& edge) {
    if(!edge.refined)return SegmentVerdict(edge.a,edge.b,edge.reason,true);
    Sample previous=edge.a;bool unknown=false;
    for(int i=0;i<4;++i) {
        const auto& next=i<3?edge.inside[static_cast<std::size_t>(i)]:edge.b;
        const auto v=SegmentVerdict(previous,next,edge.reason,false);
        if(v==Verdict::Blocked)return v;if(v==Verdict::Unknown)unknown=true;previous=next;
    }
    return unknown?Verdict::Unknown:Verdict::Clear;
}
inline bool Finite(Vector3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline bool UnitNormal(Vector3 n) {
    if(!Finite(n))return false;const float length=n.x*n.x+n.y*n.y+n.z*n.z;
    return length>.75F&&length<1.25F;
}
enum class Contact {Blocked,Supporting,Unknown};
inline Contact ClassifyContact(Vector3 hit,Vector3 normal,const Sample& a,const Sample& b,float radius) {
    if(!Finite(hit)||!UnitNormal(normal))return Contact::Unknown;
    if(normal.z<kMinimumNormalZ)return Contact::Blocked;
    if(!a.valid||!b.valid||a.water||b.water||!Finite(a.at)||!Finite(b.at)||
       !UnitNormal(a.normal)||!UnitNormal(b.normal)||a.normal.z<kMinimumNormalZ||b.normal.z<kMinimumNormalZ)return Contact::Blocked;
    // Compare with the ORIGINAL measured support, not a new high ray that
    // could land on top of the very rock/box being classified. An upward
    // normal alone cannot distinguish that top from continuous terrain.
    const float dx=b.at.x-a.at.x,dy=b.at.y-a.at.y,length=dx*dx+dy*dy;
    const float t=length>1e-6F?std::clamp(((hit.x-a.at.x)*dx+(hit.y-a.at.y)*dy)/length,0.0F,1.0F):0;
    if(std::hypot(hit.x-a.at.x-t*dx,hit.y-a.at.y-t*dy)>radius+.05F)return Contact::Blocked;
    for(const auto& p:{a,b}) {
        const float height=p.at.z-(p.normal.x*(hit.x-p.at.x)+p.normal.y*(hit.y-p.at.y))/p.normal.z;
        const float aligned=normal.x*p.normal.x+normal.y*p.normal.y+normal.z*p.normal.z;
        if(aligned<.9F||std::abs(hit.z-height)>kSupportContactTolerance)return Contact::Blocked;
    }
    // The first contact hides the rest of this sweep. Supporting means
    // unresolved, never a certificate that no solid object lies behind it.
    return Contact::Supporting;
}
struct Cell {bool open=true,link=true,unknown=false,link_unknown=false;std::uint32_t reason=0;};
struct QueryResult {
    ScrHandle handle=0,raw_handle=0;int status=1,native_status=1;BOOL hit=false;
    Vector3 at{},normal{};Entity entity=0;bool handle_conflict=false;
    std::uint64_t issue_frame=0,first_poll_frame=0,last_poll_frame=0,polls=0;
};
// The queue, rather than a race/profile, owns each live native handle. Any
// survey (or idle maintenance) can finish it. Results outlive the handle for
// their original consumer; abandoning a race never abandons engine queries.
class QueryQueue {
    inline static std::vector<std::shared_ptr<QueryResult>> pending_;
    inline static std::size_t cursor_=0;
    inline static double spent_ms_=0;
    inline static int issued_=0;
    inline static std::uint64_t frame_=0;
public:
    static bool Full(){return pending_.size()>=kPendingCapsules;}
    static std::size_t Total(){return pending_.size();}
    static bool CanIssue(){return issued_<kCapsulesPerFrame;}
    static void Issued(){++issued_;}
    static std::shared_ptr<QueryResult> Track(ScrHandle handle){
        auto q=std::make_shared<QueryResult>();q->handle=q->raw_handle=handle;q->issue_frame=frame_;
        if(!handle){q->status=q->native_status=0;return q;}
        for(const auto& owned:pending_)if(owned->handle==handle) {
            // A reused live handle cannot certify two different geometries.
            // Keep the original owner only to retire it, and reject BOTH uses.
            owned->handle_conflict=q->handle_conflict=true;owned->status=q->status=0;
            q->handle=0;return q;
        }
        pending_.push_back(q);return q;
    }
    static bool PollOne(){
        if(pending_.empty())return false;cursor_%=pending_.size();auto& q=*pending_[cursor_];
        q.native_status=SHAPETEST::GET_SHAPE_TEST_RESULT(q.handle,&q.hit,&q.at,&q.normal,&q.entity);
        if(!q.polls)q.first_poll_frame=frame_;q.last_poll_frame=frame_;++q.polls;
        q.status=q.handle_conflict?0:q.native_status;
        // The native only defines its pointer outputs for status RESULTS_READY.
        if(q.status!=2){q.hit=false;q.at={};q.normal={};q.entity=0;}
        if(q.native_status!=1){q.handle=0;pending_.erase(pending_.begin()+static_cast<std::ptrdiff_t>(cursor_));}
        else ++cursor_;return true;
    }
    static void BeginFrame(){
        // 1491 query maintenance clears a keep-alive bit each frame, and frees
        // an unpolled query on the next maintenance pass (default grace is 0).
        // GET_RESULT(PENDING) sets that bit. Poll EVERY live handle, including
        // abandoned owners, before discretionary survey work. One round-robin
        // poll per frame silently loses the other simultaneous capsule results.
        ++frame_;const auto live=pending_.size();
        const auto start=std::chrono::steady_clock::now();
        for(std::size_t i=0;i<live;++i)PollOne();
        spent_ms_=live?std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count():0;issued_=0;
    }
    static void Debit(double elapsed){if(std::isfinite(elapsed)&&elapsed>0)spent_ms_+=elapsed;}
    static double RemainingBudget(double requested){return std::max(0.0,std::min(requested,kFrameBudgetMs-spent_ms_));}
    struct Charge {
        std::chrono::steady_clock::time_point started;
        ~Charge(){Debit(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());}
    };
};
} // namespace horse_races::obstacle_survey
