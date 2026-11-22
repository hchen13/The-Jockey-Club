#pragma once
#include <cmath>
#include <array>
#include <cstdint>
#include <limits>

namespace horse_motion::ground_snow_candidate {
// This is an empirical, experimental ground-material candidate, not the
// engine's hoof-contact class or a universal snow-depth measurement.
inline constexpr std::uint32_t kAppaloosaObservedMaterial=0x6ce4d538u;
enum class Status : unsigned {Inactive=0,Pending=1,NoHit=2,OtherMaterial=3,SnowCandidate=4,InvalidHeight=5,InvalidMaterial=6};
struct GroundSample {bool hit=false;std::uint32_t material=0;float height=0;int flags=0;};
// A fixed-size, script-thread-only FIFO permits at most one additional ground
// cast per frame across player and NPC learners. Waiting requests survive
// traversal order; stale requesters expire rather than blocking the queue.
struct FrameBudget {
    struct Requester {std::uintptr_t entity=0;int requested=0,seen=0;};
    std::array<Requester,32> waiting{};
    int last_frame=-1,granted=-1;
    void Clear(){*this={};}
    void Cancel(std::uintptr_t entity){for(auto& item:waiting)if(item.entity==entity)item={};}
    bool Request(int frame,std::uintptr_t entity){
        if(!entity || frame<0)return false;
        if(frame<last_frame)Clear();
        last_frame=frame;
        for(auto& item:waiting)if(item.entity && frame-item.seen>2)item={};
        Requester* current=nullptr;Requester* free=nullptr;
        for(auto& item:waiting){if(item.entity==entity)current=&item;if(!item.entity && !free)free=&item;}
        if(!current){if(!free)return false;current=free;*current={entity,frame,frame};}
        current->seen=frame;
        if(granted==frame)return false;
        auto* first=current;
        for(auto& item:waiting)if(item.entity && item.requested<first->requested)first=&item;
        if(first!=current)return false;
        granted=frame;*current={};return true;
    }
};
inline FrameBudget budget;
struct FrameSamples {
    struct Entry {std::uintptr_t entity=0;int horse=0;std::uint32_t model=0;int frame=-1;GroundSample sample{};};
    std::array<Entry,32> entries{};
    int last_frame=-1;
    void Advance(int frame){if(frame<last_frame)entries={};last_frame=frame;}
    void Remember(int frame,std::uintptr_t entity,int horse,std::uint32_t model,GroundSample sample){
        Advance(frame);
        if(entity && horse && model)entries[(entity>>12)%entries.size()]={entity,horse,model,frame,sample};
    }
    bool Find(int frame,std::uintptr_t entity,int horse,std::uint32_t model,GroundSample& out) {
        Advance(frame);
        const auto& entry=entries[(entity>>12)%entries.size()];
        if(!entity || entry.entity!=entity || entry.horse!=horse || entry.model!=model || entry.frame!=frame)return false;
        out=entry.sample;return true;
    }
};
inline FrameSamples frame_samples;
struct Tracker {
    Status status=Status::Inactive;
    std::uint32_t material=0;
    float height_delta=0;
    int flags=0;
    double next=0;
    double sampled_at=0;
    bool initialized=false;
    bool Snow() const {return status==Status::SnowCandidate;}
    void Reset(){*this={};}
    template<class Permit,class Reader> void UpdateWithPermit(bool eligible,std::uintptr_t entity,double now,float horse_z,Permit permit,Reader read){
        if(!eligible || !std::isfinite(now) || !std::isfinite(horse_z)){Reset();return;}
        if(!initialized){
            initialized=true;status=Status::Pending;
            // Spread first queries across at most 0.35 seconds; subsequent
            // reads are at most 2 Hz per horse and globally limited per frame.
            next=now+double((entity>>12)%8)*.05;
        }
        // A globally deferred cast must never leave a previous location's
        // material active indefinitely during a low-frame-rate backlog.
        if(sampled_at>0 && (now<sampled_at || now-sampled_at>.75)){
            status=Status::Pending;material=0;flags=0;height_delta=0;sampled_at=0;
        }
        if(now<next || !permit())return;
        next=now+.5;
        sampled_at=now;
        const auto sample=read();material=sample.material;flags=sample.flags;
        height_delta=sample.height-horse_z;
        if(!sample.hit){status=Status::NoHit;return;}
        if(!sample.material){status=Status::InvalidMaterial;return;}
        if(!std::isfinite(sample.height) || !std::isfinite(height_delta) ||
            std::abs(height_delta)>3.F || height_delta>1.5F){status=Status::InvalidHeight;return;}
        status=material==kAppaloosaObservedMaterial?Status::SnowCandidate:Status::OtherMaterial;
    }
    template<class Reader> void Update(bool eligible,std::uintptr_t entity,double now,float horse_z,bool permit,Reader read){
        UpdateWithPermit(eligible,entity,now,horse_z,[&]{return permit;},read);
    }
};
// A whitelist hit is compatible only with native Snow. For other sampled
// materials, preserve every non-Snow native resource in its own speed bucket;
// non-whitelisted does not prove the ground is physically snow-free.
inline bool Agrees(const Tracker& ground,std::uint32_t native_resource,std::uint32_t snow,std::uint32_t normal){
    (void)normal;
    return (ground.status==Status::SnowCandidate && native_resource==snow) ||
        (ground.status==Status::OtherMaterial && native_resource && native_resource!=snow);
}
} // namespace horse_motion::ground_snow_candidate
