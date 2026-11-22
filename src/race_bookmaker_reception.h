#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

namespace horse_races::bookmaking {
struct ReceptionGround {float z,normal_z;};
struct RecordedGround {float z;bool inferred;};
// F11 records the standing player's origin, roughly one metre above the
// surface. Probe just above that origin: a roof several metres above the
// player must not become the bookie's ground. The height check also rejects
// a rooftop if the native ground query ignores the probe ceiling.
template<class Probe> std::optional<RecordedGround> RecordedReceptionGround(float recorded_z,Probe probe){
    if(!std::isfinite(recorded_z))return {};
    const auto hit=probe(recorded_z+1.0F);
    if(!hit || !std::isfinite(hit->z) || !std::isfinite(hit->normal_z) ||
        hit->normal_z<.8F)return {};
    const float drop=recorded_z-hit->z;
    if(drop>=.45F && drop<=1.55F)return RecordedGround{hit->z,false};
    // The recorder stood on the floor underneath a roof. If the native still
    // reports that roof, use the captured standing pose to reconstruct the
    // floor instead of hiding the receptionist or placing her upstairs.
    if(drop<-.25F)return RecordedGround{recorded_z-1.0F,true};
    return {};
}
inline std::vector<int> ReceptionOutfits(std::span<const int> approved,int count){
    std::vector<int> result;
    if(count<=0 || count>64)return result;
    for(int preset:approved)if(preset>=0 && preset<count &&
        std::find(result.begin(),result.end(),preset)==result.end())result.push_back(preset);
    return result;
}
// A lock-on edge starts a greeting, not the Watch/Bet button. A busy voice can
// retry briefly without a per-frame loop or repeating while focus is held.
struct ReceptionGreeting {
    bool focused=false,pending=false;
    double expires=0,next_try=0,cooldown=0;
    bool Observe(bool focus,double now){
        if(!std::isfinite(now))return false;
        if(focus && !focused && now>=cooldown){pending=true;expires=now+5;next_try=now;}
        focused=focus;
        if(!focus || now>expires)pending=false;
        return focus && pending && now>=next_try;
    }
    void Attempt(double now,bool spoken){
        next_try=now+.8;
        if(spoken){pending=false;cooldown=now+10;}
    }
};
} // namespace horse_races::bookmaking
