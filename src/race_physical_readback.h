#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

// Diagnostic values retain the short-circuit semantics of PhysicalReady.
// A null field means the corresponding native was NOT called, not false.
namespace horse_races::physical_readback {
struct Result {
    bool attempted=false,ready=false;
    std::optional<bool> collision_disabled,collision_loaded,waiting,frozen;
};
template<class Disabled,class Loaded,class Waiting,class Frozen>
Result Read(bool horse,Disabled disabled,Loaded loaded,Waiting waiting,Frozen frozen) {
    Result r;
    if(!horse)return r;
    r.attempted=true;
    r.collision_disabled=disabled();if(*r.collision_disabled)return r;
    r.collision_loaded=loaded();if(!*r.collision_loaded)return r;
    r.waiting=waiting();if(*r.waiting)return r;
    r.frozen=frozen();r.ready=!*r.frozen;return r;
}
struct Identity {
    int horse=0;std::uint32_t model=0;std::uintptr_t address=0;
    bool operator==(const Identity&)const=default;
    bool Valid()const{return horse && model && address;}
};
struct Sample {
    Result result;
    Identity horse;
    std::uint64_t race_token=0;
    double now=0;
    std::optional<float> distance_m,along_m;
    bool trial=false,trial_focus_lease_active=false;
};
// Memory-only fixed ring. Sampling is throttled, and formatting is left to the
// caller at an incident. Bound to a full identity and race; no old world data
// survives an identity change or clock reversal.
class History {
    static constexpr std::size_t kCapacity=24;
    std::array<Sample,kCapacity> samples_{};
    std::size_t next_=0,count_=0;
    std::uint64_t token_=0;
    Identity identity_{};
    double last_=0;
public:
    void Clear(){next_=count_=0;token_=0;identity_={};last_=0;}
    bool Due(std::uint64_t token,Identity horse,double now)const {
        return token!=token_ || !(horse==identity_) || !count_ || !std::isfinite(now) ||
            now<last_ || now-last_>=.1;
    }
    bool Add(const Sample& s) {
        if(!s.race_token || !s.horse.Valid() || !std::isfinite(s.now)) {Clear();return false;}
        if(s.race_token!=token_ || !(s.horse==identity_) || (count_ && s.now<last_)) {
            Clear();token_=s.race_token;identity_=s.horse;
        }
        if(!Due(s.race_token,s.horse,s.now))return false;
        samples_[next_]=s;next_=(next_+1)%kCapacity;
        if(count_<kCapacity)++count_;
        last_=s.now;return true;
    }
    std::size_t Size()const{return count_;}
    const Sample* Latest(std::uint64_t token,Identity horse,double now,double max_age=.25)const {
        if(!count_ || token!=token_ || !(horse==identity_) || !std::isfinite(now) ||
            !std::isfinite(max_age) || max_age<0 || now<last_ || now-last_>max_age)return nullptr;
        return &samples_[(next_+kCapacity-1)%kCapacity];
    }
    // On a hitch the incident can arrive >250 ms after the last sample. The
    // recorder must not use it as current state, but the historical evidence
    // remains useful with its true age. Bound the incident window to 10 s.
    template<class F> void ForEach(std::uint64_t token,Identity horse,double now,F f)const {
        if(!count_ || token!=token_ || !(horse==identity_) || !std::isfinite(now) ||
            now<last_ || now-last_>10)return;
        for(std::size_t i=0;i<count_;++i) {
            const auto& s=samples_[(next_+kCapacity-count_+i)%kCapacity];
            if(now>=s.now && now-s.now<=10)f(s);
        }
    }
};
} // namespace horse_races::physical_readback
