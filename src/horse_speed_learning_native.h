#pragma once
#include "horse_speed_learning_runtime.h"
#include "horse_speed_context_learning.h"
#include "horse_motion_native.h"
#include "race_pace_plan.h"
#include "horse_native_environment_windows.h"
#include "horse_learning_agitation.h"
#include "horse_ground_snow_candidate.h"
#include <chrono>

namespace horse_motion::speed_learning {
// Script-thread only. Every NPC sampler in the same game frame sees the same
// pause/load observation, without paying two native calls per rider per frame.
inline bool LearningUiReady(int frame) {
    struct State {int frame=-1;bool paused=true,loading=true;};
    static State state;
    if(state.frame!=frame) {
        state.frame=frame;
        state.paused=HUD::IS_PAUSE_MENU_ACTIVE();
        state.loading=DLC::GET_IS_LOADING_SCREEN_ACTIVE();
    }
    return !state.paused && !state.loading;
}
// Script-thread only. Only owned NPCs in awake simulation may teach the
// sampler. Sleeping NPCs may separately query a bounded ground candidate.
struct Session : ContextReference {
    using EnvironmentReader=native_environment::Snapshot (*)(std::uintptr_t);
    int frame=-1;
    double wall=0;
    ground_snow_candidate::Tracker ground_snow{};
    AgitationGuard agitation{};
    native_environment::Snapshot environment{};
    void Reset(){*this=Session{};}
    void Observe(Ped horse,Ped rider,bool active,float effort,bool braking,bool burst,std::string_view owner={},
        EnvironmentReader capture=native_environment::Capture) {
        const auto id=Identify(horse);
        if(!active || !id.Valid() || !rider || rider==PLAYER::PLAYER_PED_ID() ||
            !ENTITY::DOES_ENTITY_EXIST(rider) || !PED::IS_PED_ON_MOUNT(rider) || PED::GET_MOUNT(rider)!=horse ||
            PED::GET_MOUNT(PLAYER::PLAYER_PED_ID())==horse) {BreakTravel();DiscardGaitHistory();agitation.Reset();ground_snow.Reset();wall=0;return;}
        const int speed=ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,5)+1;
        const int accel=ATTRIBUTE::GET_ATTRIBUTE_RANK(horse,6)+1;
        const Key next{id.address,id.horse,id.model,speed,accel};
        if(next!=live){wall=0;frame=-1;ground_snow.Reset();agitation.Reset();}
        const int current=MISC::GET_FRAME_COUNT();
        if(current<frame){BreakTravel();DiscardGaitHistory();reference.Break();agitation.Reset();ground_snow.Reset();wall=0;}
        if(current==frame)return;
        const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const float wall_dt=wall>0 && now>wall?static_cast<float>(now-wall):0;
        const float dt=MISC::GET_FRAME_TIME();
        frame=current;wall=now;
        // Coarse simulation reports STATIC while its real positions continue
        // moving. That still supplies resource/grade context for far output;
        // only awake, non-static motion may teach terminal capability below.
        const bool safe=LearningUiReady(current) &&
            !ENTITY::_IS_ENTITY_FROZEN(horse) &&
            !ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(horse) &&
            !ENTITY::IS_ENTITY_DEAD(horse) && !ENTITY::IS_ENTITY_DEAD(rider) &&
            !ENTITY::IS_ENTITY_IN_AIR(horse,false) && !ENTITY::IS_ENTITY_IN_WATER(horse) &&
            !PED::IS_PED_FALLING(horse) && !PED::IS_PED_RAGDOLL(horse) && !PED::IS_PED_JUMPING(horse) &&
            !PED::IS_PED_FALLING(rider) && !PED::IS_PED_RAGDOLL(rider);
        environment=safe?capture(id.address):native_environment::Snapshot{};
        const bool awake=safe && !ENTITY::GET_ENTITY_COLLISION_DISABLED(horse) && !ENTITY::IS_ENTITY_STATIC(horse);
        const bool was_blocked=agitation.blocked;
        const float agitation_value=awake?PED::_GET_PED_MOTIVATION(horse,3,0):std::numeric_limits<float>::quiet_NaN();
        const bool agitation_blocked=agitation.Observe(agitation_value,dt);
        if(agitation_blocked && !was_blocked)DiscardGaitHistory();
        // Full effort and the recent same-resource actual-gait P90 select
        // learning windows; do not assume a fixed snow or ground gait.
        float blend_x=0,blend_y=0;
        const float gait=awake && PED::GET_PED_CURRENT_MOVE_BLEND_RATIO(horse,&blend_x,&blend_y) &&
            std::isfinite(blend_x) && std::isfinite(blend_y)?std::hypot(blend_x,blend_y):
            std::numeric_limits<float>::quiet_NaN();
        const auto p=ENTITY::GET_ENTITY_COORDS(horse,true,false);
        const speed_learning::Position position{p.x,p.y,p.z};
        // Near and far use the same material classification. Reuse this
        // horse's validated same-frame diagnostic cast whenever available;
        // otherwise all learners share one fair added-cast budget per frame.
        ground_snow_candidate::GroundSample recorded{};
        const bool have_recorded=ground_snow_candidate::frame_samples.Find(current,id.address,id.horse,id.model,recorded);
        ground_snow.UpdateWithPermit(safe && environment.valid,
            id.address,now,p.z,[&] {
                if(have_recorded){ground_snow_candidate::budget.Cancel(id.address);return true;}
                return ground_snow_candidate::budget.Request(current,id.address);
            },[&] {
                if(have_recorded)return recorded;
                float z=0;Hash material=0;int flags=0;
                const bool hit=invoke<BOOL>(0xBBE5B63EFFB08E68,p.x,p.y,p.z+1.5F,129,&z,&material,&flags)!=0;
                return ground_snow_candidate::GroundSample{hit,static_cast<std::uint32_t>(material),z,flags};
            });
        const auto resource=!awake && ground_snow.Snow()?native_environment::kSnowKey:
            environment.valid?environment.context.current_key:0;
        const bool agreed=environment.valid && ground_snow_candidate::Agrees(ground_snow,
            environment.context.current_key,native_environment::kSnowKey,native_environment::kDefaultKey);
        const bool known_ground=ground_snow.status==ground_snow_candidate::Status::SnowCandidate ||
            ground_snow.status==ground_snow_candidate::Status::OtherMaterial;
        // Only publish a material from this horse's current bounded sample.
        // An awake learner also requires the native resource to agree; the
        // sleeping rider uses its material directly for pace lookup.
        const std::uint32_t pace_material=known_ground && (!awake || agreed)?ground_snow.material:0;
        ObserveFrame(next,owner,horse_races::race_pace::ForAttributes(speed,accel).top_mps,
            runtime::initialized?&runtime::store:nullptr,
            {position,dt,wall_dt,resource,safe,
             awake && agreed && !agitation_blocked && !braking && std::isfinite(effort) && effort>=2.99F && effort<=3,burst,gait,now,agitation_blocked,pace_material});
    }
};
} // namespace horse_motion::speed_learning
