#pragma once
#include "horse_world_speed_feedback.h"
#include "horse_speed_learning.h"
#include "horse_native_environment.h"
#include "horse_speed_limits.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace horse_motion::native_vector {
inline std::atomic<bool> diagnostics_enabled{false};
// Native output acceleration and continuous on-screen return accepted in 0.28.38.
// Keep that tested ramp; do not seed momentum on entry to sleeping simulation.
// No route, position, heading, physics, task or animation ownership here.
struct Key {
    std::uintptr_t task=0,entity=0;
    std::uint32_t generation=0;
    bool operator==(const Key&) const=default;
    bool Valid() const {return task>=0x10000 && entity>=0x10000;}
};
struct Vec {float x=0,y=0,z=0,w=0;};
struct Observation {
    int state=0; // idle, unavailable, armed, released
    std::uint64_t calls=0,modified=0;
    float target=0,before=0,after=0,multiplier=1,native_reference=0;
    float normal_reference=0,environment_ratio=1,unrestricted_target=0;
    bool environment_valid=false,environment_limited=false;
    int environment_status=0;
    std::uint32_t environment_resource_key=0,environment_normal_key=0;
    std::uint32_t pace_resource=0,pace_material=0;
    bool snow_inferred=false;
    unsigned snow_evidence_source=0,snow_weather_status=0,snow_evidence_status=0;
    unsigned ground_snow_status=0;
    std::uint32_t ground_snow_material=0;
    int ground_snow_flags=0;
    float ground_snow_height_delta=0;
    float snow_evidence_distance=0,snow_evidence_age=0;
    unsigned pace_slope=0,pace_source=0;
    float pace_grade=0,pace_measured=0,pace_flat_base=0,pace_ability_gain=1,pace_native_terrain_ratio=0;
    std::uint64_t callbacks=0,rejected_output=0,rejected_step=0;
    std::uint64_t rejected_environment=0;
    float step_seconds=0;
    float world_target=0,world_speed=0,feedback_gain=1,feedback_filtered=0,feedback_settled=0;
    float feedback_native_interval=0,feedback_sample_age=0;
    WorldSpeedWindow feedback_window{};
    speed_learning::Observation learning{};
    speed_learning::SamplingObservation sampling{};
    float pace_terrain_flat=0,pace_preset=0;
    int checkpoint=0; // capture, install, registration, publish, armed
    std::uint64_t registration=0,resets=0,suspensions=0;
    int reset_reason=0,bind_reason=0,release_reason=0;
    // GetTickCount64 clock of the existing publication/callback, not game time.
    // A retained observation must not be mistaken for this frame's output.
    std::uint64_t request_tick_ms=0,output_tick_ms=0;
    std::array<float,3> input_xyz{},output_xyz{};
};
enum class ReleaseReason {None,Ineligible,Awake,TaskChanged,CaptureFailed,InstallFailed,AcquireFailed,PublishFailed,Stopped,EnvironmentUnavailable,PaceSuppressed};
struct Token {std::size_t slot=16;std::uint64_t serial=0;bool Valid() const{return slot<16 && serial;}};
inline bool Adjust(Vec& value,float native_top,float target,float multiplier,bool environment_limited=false) {
    if(!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
        !std::isfinite(native_top) || native_top<=0 || native_top>25 ||
        !std::isfinite(target) || target<=0 || target>speed_limits::kRequestedMps ||
        !std::isfinite(multiplier) || multiplier<=0 || multiplier>speed_limits::kMaxOutputGain)return false;
    const float speed=std::hypot(value.x,value.y);
    // Preserve direction, vertical motion and the accepted high-Z safety
    // boundary. Restore a reduced native locomotion resource's ceiling BEFORE
    // applying this horse's already-ramped ability compensation.
    if(speed<=.001F || std::abs(value.z)>3)return false;
    const float native_speed=environment_limited?std::min(speed,native_top):speed;
    // The measured environmental pace is a terminal ceiling. In particular,
    // do not use target/native_top as a second slope/snow reduction. A lower
    // ceiling is enforced if a sleeping vector omitted the environment response.
    // A slow native vector alone does not withdraw an otherwise valid racing
    // override. Zero-speed or actual-stop vetoes suspend publication before this callback.
    const float adjusted=std::min(target,native_speed*multiplier);
    if(std::abs(adjusted-speed)<=.00001F*speed)return false;
    const float scale=adjusted/speed;
    value.x*=scale;value.y*=scale;return true;
}
class Registry {
    struct Slot {
        std::atomic<std::uintptr_t> visible_task{0};
        std::mutex mutex;
        Key key{};
        std::uint64_t serial=0,updated=0,expires=0;
        float native_top=0,multiplier=1;
        native_environment::Snapshot environment{};
        Observation stats{};
    };
    std::array<Slot,16> slots_{};
    std::uint64_t serial_=0; // Registration is owned by the ASI script thread.
public:
    bool HasTask(std::uintptr_t task) const {
        if(!task)return false;
        for(const auto& s:slots_)if(s.visible_task.load(std::memory_order_acquire)==task)return true;
        return false;
    }
    Token Acquire(Key key) {
        if(!key.Valid())return {};
        // A suspended slot is invisible to the worker, but still owns its task.
        // Registration runs only on the script thread; workers never change keys.
        for(auto& s:slots_){
            std::lock_guard lock(s.mutex);
            if(s.key.task==key.task)return {};
        }
        for(std::size_t i=0;i<slots_.size();++i){
            auto& s=slots_[i];std::lock_guard lock(s.mutex);
            if(s.key.task)continue;
            s.key=key;s.serial=++serial_;s.updated=s.expires=0;s.multiplier=1;s.stats={};s.environment={};
            s.visible_task.store(key.task,std::memory_order_release);return {i,s.serial};
        }
        return {};
    }
    bool Suspend(Token token,Key key,std::uint64_t now) {
        if(!token.Valid())return false;
        auto& s=slots_[token.slot];std::lock_guard lock(s.mutex);
        if(s.serial!=token.serial || s.key!=key || !s.key.Valid())return false;
        s.visible_task.store(0,std::memory_order_release);
        if(!s.updated || now<s.updated || now>s.expires) {
            s.stats.reset_reason=!s.updated?1:(now<s.updated?3:2);
            ++s.stats.resets;s.multiplier=1;
        }
        if(s.stats.state!=3)++s.stats.suspensions;
        // Revalidated sleeping owner, no output permission. Keep only its
        // established ability coefficient; never grow it while withholding
        // propulsion. No speed, position or previous target is replayed.
        s.updated=now;s.expires=now+200;
        if(diagnostics_enabled.load(std::memory_order_relaxed))s.stats.request_tick_ms=now;
        s.stats.state=3;s.stats.target=0;s.stats.multiplier=s.multiplier;
        return true;
    }
    bool Publish(Token token,float top,float target,std::uint64_t now,bool environment_limited=false,
        const native_environment::Snapshot* environment=nullptr,float ability_gain=0){
        if(!token.Valid() || !std::isfinite(top) || top<=0 || top>25 ||
            !std::isfinite(target) || target<=0 || target>speed_limits::kRequestedMps ||
            !std::isfinite(ability_gain) || ability_gain<0 || ability_gain>speed_limits::kMaxOutputGain)return false;
        auto& s=slots_[token.slot];std::lock_guard lock(s.mutex);
        if(s.serial!=token.serial || !s.key.Valid())return false;
        if(environment && (!environment->valid || environment->context.entity!=s.key.entity ||
            environment->context.generation!=s.key.generation || environment->current_native_mps!=top ||
            environment->Limited()!=environment_limited))return false;
        const bool continuous=s.updated && now>=s.updated && now<=s.expires;
        const float dt=continuous?std::min(.1F,float(now-s.updated)/1000.F):0;
        const float want=ability_gain>0?ability_gain:std::clamp(target/top,1.F,speed_limits::kMaxOutputGain);
        if(!continuous){
            s.stats.reset_reason=!s.updated?1:(now<s.updated?3:2);
            ++s.stats.resets;s.multiplier=1;
        }
        // The accepted 0.28.38 ramp, including its brief entry slowdown.
        s.multiplier=std::min(want,s.multiplier+.5F*dt);
        s.updated=now;s.expires=now+200;s.native_top=top;
        if(diagnostics_enabled.load(std::memory_order_relaxed))s.stats.request_tick_ms=now;
        s.stats.state=2;s.stats.checkpoint=5;s.stats.registration=s.serial;
        s.stats.target=target;s.stats.multiplier=s.multiplier;s.stats.native_reference=top;
        s.stats.environment_limited=environment_limited;
        s.environment=environment?*environment:native_environment::Snapshot{};
        // Re-arm only after the complete fresh target/resource publication.
        s.visible_task.store(s.key.task,std::memory_order_release);
        return true;
    }
    template<class Validate> bool ApplyVerified(Key key,std::uint64_t now,Vec& output,float dt,
        bool output_valid,Validate validate){
        for(auto& s:slots_){
            if(s.visible_task.load(std::memory_order_acquire)!=key.task)continue;
            // Never block an engine worker on the script/telemetry thread.
            std::unique_lock lock(s.mutex,std::try_to_lock);
            if(!lock || s.visible_task.load(std::memory_order_acquire)!=key.task ||
                s.key!=key || !s.expires || now<s.updated || now>s.expires)return false;
            ++s.stats.callbacks;s.stats.step_seconds=dt;
            if(!output_valid){++s.stats.rejected_output;return false;}
            if(!std::isfinite(dt) || dt<=0 || dt>.25F){++s.stats.rejected_step;return false;}
            if(s.environment.valid && !validate(s.environment)){++s.stats.rejected_environment;return false;}
            ++s.stats.calls;s.stats.before=std::hypot(output.x,output.y);
            const bool trace=diagnostics_enabled.load(std::memory_order_relaxed);
            if(trace){s.stats.output_tick_ms=now;s.stats.input_xyz={output.x,output.y,output.z};}
            const bool changed=Adjust(output,s.native_top,s.stats.target,s.multiplier,s.stats.environment_limited);
            s.stats.after=std::hypot(output.x,output.y);if(changed)++s.stats.modified;
            if(trace)s.stats.output_xyz={output.x,output.y,output.z};
            return changed;
        }
        return false;
    }
    bool Apply(Key key,std::uint64_t now,Vec& output,float dt=1.F/60,bool output_valid=true){
        return ApplyVerified(key,now,output,dt,output_valid,[](const auto&){return true;});
    }
    Observation Observe(Token token){
        if(!token.Valid())return {};
        auto& s=slots_[token.slot];std::lock_guard lock(s.mutex);
        return s.serial==token.serial?s.stats:Observation{};
    }
    Observation Release(Token token){
        if(!token.Valid())return {};
        auto& s=slots_[token.slot];std::lock_guard lock(s.mutex);
        if(s.serial!=token.serial)return {};
        s.visible_task.store(0,std::memory_order_release);s.key={};s.expires=0;
        s.stats.state=3;return s.stats;
    }
};
} // namespace horse_motion::native_vector
