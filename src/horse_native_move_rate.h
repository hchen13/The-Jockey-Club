#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace horse_motion::native_move_rate {
// 1491.50 RVA fe6b60 RETURNS WITHOUT WRITING above 1.15. In particular the
// historical 1.6 trial was not a sustained, accepted 1.6 (or 1.15) override.
inline constexpr float kMaximum=1.15F;
enum class State {Idle,Unavailable,Baseline,Treatment,Released,Conflict,Rejected};
struct Key {
    std::uintptr_t entity=0,motion=0;
    std::uint32_t generation=0,model=0;
    int horse=0;
    bool operator==(const Key&) const=default;
};
struct Snapshot {Key key;float rate=1,native_top=0;bool valid=false;};
struct Observation {
    int state=0,phase=-1;
    float requested=1;
    std::optional<float> before,after;
    bool accepted=false;
    double phase_seconds=0;
};
inline bool Same(float a,float b){return std::isfinite(a) && std::isfinite(b) && std::abs(a-b)<.0001F;}
template<class Api> struct Trial {
    Api api{};
    Key owner{};
    bool owned=false,blocked=false;
    float last=1;
    double started=-1,previous=-1;
    Observation observation{};
    void Release(){
        if(owned){
            const auto current=api.Capture(owner.horse);
            // The native override is reset to 1 by the engine each frame.
            // Never overwrite a recycled entity or somebody else's override.
            if(current.valid && current.key==owner && Same(current.rate,last) && !Same(current.rate,1))
                api.Write(current,1);
            observation.state=static_cast<int>(State::Released);
            observation.accepted=false;
            const auto after=api.Capture(owner.horse);
            observation.after=after.valid && after.key==owner?std::optional<float>(after.rate):std::nullopt;
        }
        owned=false;blocked=false;owner={};started=previous=-1;
    }
    bool Update(int horse,float target_mps,double now){
        if(!horse || !std::isfinite(target_mps) || target_mps<=0 || target_mps>25 || !std::isfinite(now)){
            Release();return false;
        }
        auto current=api.Capture(horse);
        if(!current.valid || !std::isfinite(current.rate) || !std::isfinite(current.native_top) ||
            current.native_top<10 || current.native_top>25){
            Release();observation={};observation.state=static_cast<int>(State::Unavailable);return false;
        }
        if(owned && current.key!=owner){Release();current=api.Capture(horse);if(!current.valid)return false;}
        if(blocked)return false;
        if(!Same(current.rate,1) && !(owned && Same(current.rate,last))){
            observation={};observation.state=static_cast<int>(State::Conflict);observation.before=current.rate;
            blocked=true;return false;
        }
        // No distance-earned boost: even this bounded diagnostic cannot ask
        // for more than the existing race-plan speed relative to natural gait.
        const float treatment=std::clamp(target_mps/current.native_top,1.0F,kMaximum);
        if(treatment<=1.001F){Release();return false;}
        if(started<0 || now<previous || (previous>=0 && now-previous>.5))started=now;
        previous=now;owner=current.key;owned=true;
        const double elapsed=now-started;
        const int phase=static_cast<int>(std::fmod(std::floor(elapsed/6.0),2.0));
        const float request=phase?treatment:1.0F;
        observation={};observation.phase=phase;observation.phase_seconds=std::fmod(elapsed,6.0);
        observation.requested=request;observation.before=current.rate;
        observation.state=static_cast<int>(phase?State::Treatment:State::Baseline);
        if(!api.Write(current,request)){
            observation.state=static_cast<int>(State::Rejected);blocked=true;return false;
        }
        last=request;
        const auto after=api.Capture(horse);
        if(after.valid && after.key==owner)observation.after=after.rate;
        observation.accepted=observation.after && Same(*observation.after,request);
        if(!observation.accepted){observation.state=static_cast<int>(State::Rejected);blocked=true;}
        return observation.accepted;
    }
};
} // namespace horse_motion::native_move_rate
