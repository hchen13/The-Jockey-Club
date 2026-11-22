#pragma once
#include "world/identity.h"
#include <algorithm>
#include <cmath>

namespace horse_races::boundary_warning {
// guama2 uses this independent poor-health variant with the strength native.
// PlayerHealthLow belongs to short_update and must never be borrowed or stopped.
inline constexpr const char* kEffect="PlayerHealthPoorGuarma";
inline constexpr float kStrength=.85F;
inline constexpr double kFadeInSeconds=.15,kFadeOutSeconds=.35;

class Effect {
    world::Identity player_;
    bool owned_=false,loading_=false,blocked_=false,fading_=false;
    float strength_=0,submitted_=-1,origin_=0;
    double blend_at_=0,last_time_=-1;

    void WriteStrength(float value) {
        strength_=std::clamp(value,0.F,1.F);
        if(strength_==submitted_)return;
        GRAPHICS::_ANIMPOSTFX_SET_STRENGTH(kEffect,strength_);
        submitted_=strength_;
    }
    void Forget() {
        player_={};owned_=loading_=blocked_=fading_=false;
        strength_=origin_=0;submitted_=-1;blend_at_=0;last_time_=-1;
    }
public:
    Effect()=default;
    Effect(const Effect&)=delete;
    Effect& operator=(const Effect&)=delete;
    bool Active() const {return owned_;}
    float Strength() const {return strength_;}

    // Immediate shutdown/load/death cleanup. It never clears the whole FX stack.
    void Reset() {
        if(owned_ && GRAPHICS::ANIMPOSTFX_IS_RUNNING(kEffect))GRAPHICS::ANIMPOSTFX_STOP(kEffect);
        Forget();
    }
    // Returning inside or resolving the race begins one fade, even if called
    // repeatedly. Continue Tick(player,false,now) outside the Racing state.
    void FadeOut(double now) {
        if(!std::isfinite(now)||(last_time_>=0&&now<last_time_)){Reset();return;}
        loading_=blocked_=false;
        if(!owned_){player_={};return;}
        if(!fading_){fading_=true;origin_=strength_;blend_at_=now;}
    }
    void Tick(Ped player,bool warning,double now) {
        if(!std::isfinite(now)||(last_time_>=0&&now<last_time_)){Reset();return;}
        last_time_=now;
        if(!warning&&!owned_){loading_=blocked_=false;player_={};return;}
        const auto current=world::Identify(player);
        if(!current.Known()||player!=PLAYER::PLAYER_PED_ID()||ENTITY::IS_ENTITY_DEAD(player)){
            Reset();return;
        }
        if(player_.Known()&&player_!=current){Reset();return;}
        player_=current;
        if(owned_&&!GRAPHICS::ANIMPOSTFX_IS_RUNNING(kEffect)) {
            // An external stop/load wins. Do not restart it every frame while
            // this same excursion remains active.
            Forget();player_=current;last_time_=now;blocked_=warning;return;
        }
        if(!warning)FadeOut(now);
        else if(!owned_) {
            if(blocked_)return;
            if(GRAPHICS::ANIMPOSTFX_IS_RUNNING(kEffect)){blocked_=true;loading_=false;return;}
            if(!GRAPHICS::_ANIMPOSTFX_HAS_LOADED(kEffect)) {
                if(!loading_)GRAPHICS::_ANIMPOSTFX_PRELOAD_POSTFX(kEffect);
                loading_=true;return;
            }
            GRAPHICS::ANIMPOSTFX_PLAY(kEffect);
            owned_=true;loading_=fading_=false;origin_=strength_=0;submitted_=-1;blend_at_=now;
        } else if(fading_) {
            // Re-entering the grace window reverses this fade on the same FX.
            fading_=false;origin_=strength_;blend_at_=now;
        }
        if(!owned_)return;
        const double duration=fading_?kFadeOutSeconds:kFadeInSeconds;
        const float fraction=static_cast<float>(std::clamp((now-blend_at_)/duration,0.0,1.0));
        WriteStrength(origin_+((fading_?0.F:kStrength)-origin_)*fraction);
        if(fading_&&fraction>=1)Reset();
    }
};
} // namespace horse_races::boundary_warning
