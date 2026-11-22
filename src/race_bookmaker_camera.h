#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace horse_races::bookmaking {
struct CameraHold {
    static constexpr double kSeconds=.70;
    double started_at=-1;
    bool ready=false;
    void Reset(){*this={};}
    bool Tick(bool held,double now) {
        if(!held){Reset();return false;}
        if(started_at<0)started_at=now;
        if(!ready && now-started_at>=kSeconds){ready=true;return true;}
        return false;
    }
};
struct CameraPoint {float x=0,y=0,z=0;};
// World-space orbit: the rider translates the pivot but turning the horse
// cannot rotate the camera. Rotation changes only with the spectator's look.
inline CameraPoint FollowPosition(CameraPoint rider,float pitch_degrees,float yaw_degrees){
    constexpr float radians=.01745329252F;
    const float pitch=pitch_degrees*radians,yaw=yaw_degrees*radians;
    const float horizontal=9.5F*std::cos(pitch);
    return {rider.x+horizontal*std::sin(yaw),
        rider.y-horizontal*std::cos(yaw),
        rider.z+1.65F-9.5F*std::sin(pitch)};
}
// A cinematic cut plants a camera beside the course. It must not translate
// with the horse during the shot; only its bearing tracks the subject.
inline CameraPoint DirectorPosition(CameraPoint rider,float heading_degrees,int shot){
    constexpr float radians=.01745329252F;
    const float heading=heading_degrees*radians;
    const float forward_x=std::sin(heading),forward_y=std::cos(heading);
    const float side_x=forward_y,side_y=-forward_x;
    const float forward=(shot&2)?-5.0F:9.0F;
    const float side=(shot&1)?-13.0F:13.0F;
    return {rider.x+forward*forward_x+side*side_x,
        rider.y+forward*forward_y+side*side_y,
        rider.z+((shot&2)?3.0F:2.3F)};
}
// A pool survives release/re-entry and mode changes, but resets for a new race.
// Losing eligibility does not forget an already viewed rider: a temporary
// camera warm-up failure must not give that rider a second turn in this cycle.
struct CameraTurnPool {
    std::vector<std::size_t> viewed;
    void Reset(){viewed.clear();}
    void Remember(std::size_t rider){
        if(std::find(viewed.begin(),viewed.end(),rider)==viewed.end())viewed.push_back(rider);
    }
    template<class Random>
    std::optional<std::size_t> Pick(std::span<const std::size_t> eligible,
        std::span<const std::size_t> preferred,std::optional<std::size_t> previous,
        bool preferred_only,Random&& random) {
        std::vector<std::size_t> choices;
        for(const auto rider:eligible)
            if(std::find(viewed.begin(),viewed.end(),rider)==viewed.end() &&
                std::find(choices.begin(),choices.end(),rider)==choices.end())choices.push_back(rider);
        const bool refill=choices.empty();
        if(refill)for(const auto rider:eligible)
            if(std::find(choices.begin(),choices.end(),rider)==choices.end())choices.push_back(rider);
        if(choices.empty())return {};
        if(previous && choices.size()>1)
            choices.erase(std::remove(choices.begin(),choices.end(),*previous),choices.end());
        std::optional<std::size_t> selected;
        for(const auto rider:preferred)if(std::find(choices.begin(),choices.end(),rider)!=choices.end()){
            selected=rider;break;
        }
        if(!selected){
            // An exciting event cannot bypass unviewed riders merely because
            // its subject has already had a turn. Do not mutate/refill a pool
            // until a cut is actually selected.
            if(preferred_only)return {};
            const int index=std::clamp(random(static_cast<int>(choices.size())),0,
                static_cast<int>(choices.size())-1);
            selected=choices[static_cast<std::size_t>(index)];
        }
        if(refill)viewed.clear();
        Remember(*selected);return selected;
    }
};
// Aim and cinematic keep SEPARATE no-replacement pools. Native cinematic
// shots may change, but their rider stays locked until C or ineligibility.
// Each cinematic entry explicitly starts on the backed rider, or the current
// leader (eligible is in race order). That explicit initial lock is not a
// random draw; C then visits everyone else before refilling the same pool.
struct CameraDirector {
    enum class Cut {None,Follow,Cinematic,Manual,Fallback};
    // 'automatic' denotes native cinematic mode, not automatic rider changes.
    bool automatic=false,aim_was_held=false;
    bool cinematic_entry_pending=false;
    std::optional<std::size_t> target;
    std::optional<std::size_t> previous_follow,previous_cinematic;
    CameraTurnPool follow_pool,cinematic_pool;
    Cut last_cut=Cut::None;
    void Reset(){*this={};}
    template<class Random>
    void Tick(bool aim_held,bool toggle_automatic,bool next_shot,double now,
        std::span<const std::size_t> eligible,Random&& random,
        std::optional<std::size_t> wager_rider={}) {
        last_cut=Cut::None;
        if(!std::isfinite(now))return;
        if(toggle_automatic){
            automatic=!automatic;target.reset();cinematic_entry_pending=automatic;
        }
        const bool following=aim_held && !automatic;
        if(eligible.empty()){target.reset();aim_was_held=following;return;}
        if(!following && !automatic){target.reset();aim_was_held=false;return;}
        const bool stale=target && std::find(eligible.begin(),eligible.end(),*target)==eligible.end();
        const bool pick=!target || stale || next_shot || (following && !aim_was_held);
        auto& pool=automatic?cinematic_pool:follow_pool;
        auto& previous=automatic?previous_cinematic:previous_follow;
        std::optional<std::size_t> selected;
        if(automatic && cinematic_entry_pending){
            const bool backed_live=wager_rider &&
                std::find(eligible.begin(),eligible.end(),*wager_rider)!=eligible.end();
            selected=backed_live?*wager_rider:eligible.front();
            pool.Remember(*selected);cinematic_entry_pending=false;last_cut=Cut::Cinematic;
        }else if(pick){
            const std::span<const std::size_t> preferred=(!automatic && wager_rider)?
                std::span<const std::size_t>(&*wager_rider,1):std::span<const std::size_t>{};
            selected=pool.Pick(eligible,preferred,previous,false,random);
            last_cut=stale?Cut::Fallback:next_shot?Cut::Manual:
                automatic?Cut::Fallback:Cut::Follow;
        }
        if(selected){
            target=selected;previous=selected;
        }
        aim_was_held=following;
    }
};
} // namespace horse_races::bookmaking
