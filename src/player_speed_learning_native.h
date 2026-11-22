#pragma once
#include "player_speed_learning.h"
#include "horse_motion_native.h"
#include "horse_native_environment_windows.h"
#include "horse_speed_learning_runtime.h"
#include "player_stimulant_native.h"
#include "race_pace_plan.h"
#include "horse_learning_agitation.h"
#include <enums.h>
#include <chrono>

namespace horse_motion::player_speed_learning {
struct NativeSampler {
    Sampler sampler;
    ConsumptionGuard consumption;
    speed_learning::AgitationGuard agitation;
    native_environment::Snapshot environment{};
    ground_snow_candidate::Tracker ground_snow{};
    Binding binding{};
    int frame=-1;
    double wall=0,reported=0,game_time=0;
    bool consumption_supported=false;
    void Reset(){*this=NativeSampler{};}
    void Stop() {
        sampler.context.Reset();sampler.owner={};sampler.rejection=Disabled;
        consumption.Reset();agitation.Reset();ground_snow.Reset();environment={};binding={};wall=game_time=0;frame=-1;consumption_supported=false;
    }
    using EnvironmentReader=native_environment::Snapshot (*)(std::uintptr_t);
    void Observe(Ped player,const NativeFrame& f,bool enabled,bool control,bool experiment,bool burst,
        const horse_races::stimulant::NativeSample* known_consumption=nullptr,
        EnvironmentReader capture=native_environment::Capture) {
        const int current=MISC::GET_FRAME_COUNT();if(current==frame)return;
        if(current<frame || !std::isfinite(f.input.now) || f.input.now<game_time){
            sampler.DiscardPartial();sampler.context.DiscardGaitHistory();consumption.Reset();agitation.Reset();ground_snow.Reset();wall=0;
        }
        frame=current;game_time=f.input.now;
        const auto rider=Identify(player);
        // attributes.current is the raw native rank captured by Observe. The
        // racing stimulant mutates input levels later; it is never a profile key.
        const Binding next{rider.address,player,rider.model,
            {f.identity.address,f.identity.horse,f.identity.model,
             f.attributes.current.Valid()?f.attributes.current.speed+1:0,
             f.attributes.current.Valid()?f.attributes.current.acceleration+1:0}};
        if(next!=binding){binding=next;consumption.Reset();agitation.Reset();ground_snow.Reset();wall=0;}
        const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const float elapsed=wall>0 && now>wall?static_cast<float>(now-wall):0;wall=now;
        const auto horse=f.identity.horse;
        bool healthy=enabled && control && f.valid && next.Valid() && Identify(horse)==f.identity &&
            PED::IS_PED_ON_MOUNT(player) && PED::GET_MOUNT(player)==horse &&
            !ENTITY::IS_ENTITY_DEAD(player) && !PED::IS_PED_RAGDOLL(player) && !PED::IS_PED_FALLING(player) &&
            !HUD::IS_PAUSE_MENU_ACTIVE() && !DLC::GET_IS_LOADING_SCREEN_ACTIVE() &&
            !f.input.environment.unsafe && !f.input.environment.in_water && !f.player_recovery_pending &&
            !ENTITY::_IS_ENTITY_FROZEN(horse) && !ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(horse) &&
            !ENTITY::GET_ENTITY_COLLISION_DISABLED(horse) && !ENTITY::IS_ENTITY_STATIC(horse);
        environment=healthy?capture(f.identity.address):native_environment::Snapshot{};
        bool dose=false;
        if(healthy) {
            const auto observed=known_consumption?*known_consumption:
                horse_races::stimulant::ReadConsumption(horse_races::stimulant::NativeApi{},player);
            consumption_supported=observed.supported;
            dose=consumption.Observe(horse,observed.total,observed.interaction);
            if(observed.status==horse_races::stimulant::SampleStatus::WrongPlayer){healthy=false;environment={};}
        }
        const bool was_blocked=agitation.blocked;
        // Recording already sampled this same frame; otherwise read once so
        // no learning window can finish between stale periodic samples.
        const float agitation_value=healthy?(f.diagnostics?f.diagnostics->agitation:
            PED::_GET_PED_MOTIVATION(horse,3,0)):std::numeric_limits<float>::quiet_NaN();
        const bool agitation_blocked=agitation.Observe(agitation_value,MISC::GET_FRAME_TIME());
        if(agitation_blocked && !was_blocked)sampler.context.DiscardGaitHistory();
        ground_snow_candidate::GroundSample recorded{};
        const bool have_recorded=ground_snow_candidate::frame_samples.Find(current,f.identity.address,
            f.identity.horse,f.identity.model,recorded);
        ground_snow.UpdateWithPermit(healthy && environment.valid,f.identity.address,now,f.position.z,[&] {
            if(have_recorded){ground_snow_candidate::budget.Cancel(f.identity.address);return true;}
            return ground_snow_candidate::budget.Request(current,f.identity.address);
        },[&] {
            if(have_recorded)return recorded;
            float z=0;Hash material=0;int flags=0;
            const bool hit=invoke<BOOL>(0xBBE5B63EFFB08E68,f.position.x,f.position.y,f.position.z+1.5F,129,&z,&material,&flags)!=0;
            return ground_snow_candidate::GroundSample{hit,static_cast<std::uint32_t>(material),z,flags};
        });
        const bool agreed=environment.valid && ground_snow_candidate::Agrees(ground_snow,
            environment.context.current_key,native_environment::kSnowKey,native_environment::kDefaultKey);
        const float gait=f.current_blend_valid && std::isfinite(f.current_blend_x) && std::isfinite(f.current_blend_y)?
            std::hypot(f.current_blend_x,f.current_blend_y):std::numeric_limits<float>::quiet_NaN();
        sampler.Observe({next,{{f.position.x,f.position.y,f.position.z},MISC::GET_FRAME_TIME(),elapsed,
            agreed?environment.context.current_key:0,healthy,true,burst || dose,gait,now,agitation_blocked,
            agreed?ground_snow.material:0},
            horse_races::race_pace::ForAttributes(next.mount.speed,next.mount.acceleration).top_mps,
            // Normal full gallop includes tapping the sprint key. The engine's
            // actual gait and sustained desired gait qualify the ride; a
            // recent same-resource actual-gait P90 selects learning windows.
            // this frame's button state does not interrupt measured travel.
            enabled,healthy,f.input.desired_blend>=2.5F,
            f.input.braking_requested || PAD::IS_CONTROL_PRESSED(0,INPUT_HORSE_STOP),experiment},
            speed_learning::runtime::initialized?&speed_learning::runtime::store:nullptr);
    }
    template<class Logger> void Report(Logger log) {
        const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(now-reported<15)return;reported=now;
        const auto& c=sampler.context;const auto& s=c.sampling;const auto& r=c.reference.Observe();
        log("speed learning source=player enabled="+std::to_string(!(sampler.rejection&Disabled))+
            " reject="+std::to_string(sampler.rejection)+" window_reject="+std::to_string(s.reject_mask)+
            " speed="+std::to_string(binding.mount.speed)+" resource="+std::to_string(c.travel.Resource())+
            " slope="+std::to_string(static_cast<unsigned>(c.travel.Key().slope))+
            " gait="+std::to_string(s.actual_gait)+" minimum_gait="+std::to_string(s.minimum_gait)+" learned="+std::to_string(r.learned)+" base="+std::to_string(r.base)+
            " frames="+std::to_string(sampler.frames)+" eligible="+std::to_string(sampler.eligible_frames)+
            " qualified="+std::to_string(s.qualified)+" accepted="+std::to_string(sampler.accepted)+
            " rejected_windows="+std::to_string(sampler.rejected_windows)+" samples="+std::to_string(r.history_samples)+
            " environment_status="+std::to_string(static_cast<int>(environment.status))+
            " native_resource="+std::to_string(environment.valid?environment.context.current_key:0)+
            " ground_status="+std::to_string(static_cast<unsigned>(ground_snow.status))+
            " ground_material="+std::to_string(ground_snow.material)+
            " ground_resource_agree="+std::to_string(environment.valid && ground_snow_candidate::Agrees(ground_snow,
                environment.context.current_key,native_environment::kSnowKey,native_environment::kDefaultKey))+
            " consumption_supported="+std::to_string(consumption_supported));
    }
};
} // namespace horse_motion::player_speed_learning
