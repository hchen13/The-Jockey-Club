#pragma once
#include "horse_speed_learning.h"

namespace horse_races::catchup {
inline bool casual_mode=false;
inline std::uint64_t policy_revision=0;
inline bool CasualMode(){return casual_mode;}
inline void SetCasualMode(bool enabled){
    if(casual_mode!=enabled){casual_mode=enabled;++policy_revision;}
}
inline std::optional<float> RemainingGap(float player_remaining,float rival_remaining,bool qualified) {
    if(!qualified||!std::isfinite(player_remaining)||!std::isfinite(rival_remaining)||player_remaining<0||rival_remaining<0)return {};
    return rival_remaining-player_remaining;
}
// Race-owner state: navigation/task/LOD restarts do not own this lifetime.
struct State {
    std::uint64_t race=0,revision=0;std::uintptr_t address=0;int horse=0;std::uint32_t model=0;
    float peak=0,fraction=0,gap=0;
    bool gap_valid=false;
    void Reset(){*this={};}
    void Pause(){fraction=gap=0;gap_valid=false;}
    float Update(std::uint64_t token,std::uintptr_t entity,int handle,std::uint32_t hash,
        std::optional<float> observed,bool racing=true) {
        if(CasualMode()||!racing||!token||!entity||!handle){Reset();return 0;}
        if(revision!=policy_revision||race!=token||address!=entity||horse!=handle||model!=hash) {
            Reset();revision=policy_revision;race=token;address=entity;horse=handle;model=hash;
        }
        gap_valid=observed.has_value()&&std::isfinite(*observed);
        gap=gap_valid?*observed:0;fraction=0;
        if(!gap_valid)return 0; // Pause output, retain this race's earned peak.
        if(gap < -5.F)peak=0; // Strictly more than five metres ahead.
        else peak=std::max(peak,horse_motion::speed_learning::CatchupFraction(gap));
        return fraction=peak;
    }
    bool Active() const{return !CasualMode()&&revision==policy_revision&&gap_valid&&fraction>0;}
};
}
