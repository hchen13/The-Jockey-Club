#pragma once
#include "horse_speed_context_learning.h"
#include "player_stimulant.h"

namespace horse_motion::player_speed_learning {
// A read-only source of the SAME reusable speed/resource/grade evidence as
// NPC racing. Player and mount identities only protect unfinished samples.
struct Binding {
    std::uintptr_t player_address=0;
    int player=0;
    std::uint32_t player_model=0;
    speed_learning::Key mount{};
    bool operator==(const Binding&) const=default;
    bool Valid() const {return player_address && player>0 && player_model &&
        mount.address && mount.handle>0 && mount.model && mount.speed>=1 && mount.speed<=10 &&
        mount.acceleration>=1 && mount.acceleration<=10;}
};
struct Frame {
    Binding binding{};
    speed_learning::ContextReference::Frame motion{};
    float fallback=0;
    bool enabled=false,healthy=false,sprinting=false,braking=false,experiment=false;
};
enum Reject : unsigned {Disabled=1,Identity=2,Unsafe=4,NotSprinting=8,Braking=16,
    Experiment=32,Burst=64,Clock=128,Resource=256,Gait=512,Discontinuity=1024,Agitation=2048};
struct Sampler {
    speed_learning::ContextReference context;
    Binding owner{};
    std::uint64_t frames=0,eligible_frames=0,accepted=0,rejected_windows=0;
    unsigned rejection=0;
    void Reset(){*this=Sampler{};}
    void DiscardPartial() {
        context.BreakTravel();context.reference.Break();
    }
    void Observe(const Frame& f,speed_learning::Store* store) {
        if(f.binding!=owner){context.Reset();owner=f.binding;}
        ++frames;rejection=0;
        if(!f.enabled)rejection|=Disabled;
        if(!f.binding.Valid())rejection|=Identity;
        if(!f.healthy || !f.motion.grounded)rejection|=Unsafe;
        if(!f.sprinting)rejection|=NotSprinting;
        if(f.braking)rejection|=Braking;
        if(f.experiment)rejection|=Experiment;
        if(f.motion.burst || context.burst_tail>0)rejection|=Burst;
        if(f.motion.agitation_blocked)rejection|=Agitation;
        if(!std::isfinite(f.motion.dt) || f.motion.dt<=0 || f.motion.dt>.25F ||
            !std::isfinite(f.motion.wall_dt) || f.motion.wall_dt<=0 || f.motion.wall_dt>.5F)rejection|=Clock;
        if(!f.motion.resource)rejection|=Resource;
        if(!speed_learning::ContextReference::ValidGait(f.motion.gait))rejection|=Gait;
        if(rejection&Clock)context.DiscardGaitHistory();
        // No partial terminal windows cross control handoffs, doses, low gait,
        // pause/load, a dismount or a disabled interval. Saved store is untouched.
        if(rejection)DiscardPartial();
        if(rejection&(Disabled|Identity)) {context.Reset();return;}
        auto motion=f.motion;
        motion.grounded=motion.grounded && f.healthy;
        motion.eligible=!rejection;
        const auto revision=store?store->Revision():0;
        const auto before=context.sampling.windows;
        context.ObserveFrame(f.binding.mount,"player",f.fallback,store,motion);
        if(!context.travel.Resource()) {DiscardPartial();rejection|=Discontinuity;}
        if(context.sampling.windows>before && (context.sampling.reject_mask&speed_learning::ContextReference::ClockMismatch)){
            DiscardPartial();rejection|=Clock;
        }
        if(!rejection)++eligible_frames;
        if(store && store->Revision()!=revision)++accepted;
        if(context.sampling.windows>before && context.sampling.reject_mask)++rejected_windows;
    }
};
// Exclusion only: never activates the race stimulant tracker. The native
// interaction can outlive its consumption marker; a retained state-7 marker
// must not restart exclusion forever. ContextReference supplies its normal 1s
// tail after an observed action/increment, not a guessed drug-duration timer.
struct ConsumptionGuard {
    int total=-1;
    horse_races::stimulant::Interaction previous{};
    void Reset(){*this={};}
    bool Observe(int mount,int next,const horse_races::stimulant::Interaction& action) {
        using namespace horse_races::stimulant;
        const bool increment=total>=0 && next>total;
        total=next;
        const bool item=action.known && action.mount==mount && ItemTier(action.item)!=Tier::None;
        const bool active=item && (action.state==5 || action.state==6);
        const bool completed=item && action.consumed &&
            (!previous.known || !previous.consumed || previous.mount!=action.mount ||
             previous.item!=action.item || previous.started_ms!=action.started_ms);
        previous=action;return active || completed || increment;
    }
};
} // namespace horse_motion::player_speed_learning
