#pragma once

#include "race_segmented_circuit.h"
#include "race_course_native.h"

namespace horse_races::lap {
// Both display policies consume the expanded, ordered race, never a standalone
// lap. A lap line is an interior point; only the real finish may end the GPS.
struct GuidanceWindow {
    float anchor=0,ahead=0,behind=0;
    bool rolling=false;
};
inline constexpr float kGuideContinuationMetres=50;
inline constexpr float kGuideFallbackAheadMetres=160;
inline constexpr float kGuideFallbackBehindMetres=20;

inline GuidanceWindow GuidanceFor(const Plan& plan,const circuit::Progress& progress){
    GuidanceWindow window;
    const auto& path=plan.course.path;
    if(path.points.size()<2 || plan.offsets.empty())return window;
    const float live=std::clamp(progress.Metres(plan.course),0.0F,path.Length());
    if(plan.guide_ahead>0){
        window={live,plan.guide_ahead,plan.guide_behind,true};
    }else{
        // Keep whole-lap visibility for the existing non-windowed courses,
        // but include an ordered continuation before the rider crosses its end.
        const int current=plan.Current(progress);
        const float start=plan.offsets[current];
        const bool final=current+1==plan.Total();
        const float end=final?path.Length():plan.offsets[current+1];
        const float continuation=final?0:std::min(kGuideContinuationMetres,
            plan.profiles[plan.Profile(current+1)].Length()*.5F);
        window={start,end-start+continuation,kGuideFallbackBehindMetres,false};
        // The native recording cap is per window, not per complete race.
        // If a whole lap plus its continuation will not fit, use a live window
        // rather than failing the start or dropping the cross-lap continuation.
        const auto first=path.Segment(std::max(0.0F,start-window.behind));
        const auto last=std::min(path.points.size()-1,
            path.Segment(std::min(path.Length(),start+window.ahead))+1);
        if(last-first+1>course::kNativeWaypointNodes)
            window={live,kGuideFallbackAheadMetres,kGuideFallbackBehindMetres,true};
    }
    // Even an unusually dense authored window must fit the native resource.
    // Prefer forward nodes; an oversized rear tail must not consume the budget.
    float low=std::max(0.0F,window.anchor-window.behind);
    float high=std::min(path.Length(),window.anchor+window.ahead);
    auto first=path.Segment(low);
    const auto last=std::min(path.points.size()-1,path.Segment(high)+1);
    if(last-first+1>course::kNativeWaypointNodes){
        const auto here=path.Segment(window.anchor);
        const auto tail_budget=course::kNativeWaypointNodes/4;
        first=std::max(first,here>tail_budget?here-tail_budget:0);
        low=std::max(low,path.metres[first]);
        high=std::min(high,path.metres[std::min(path.points.size()-1,
            first+course::kNativeWaypointNodes-2)]);
        window.behind=std::max(0.0F,window.anchor-low);
        window.ahead=std::max(.01F,high-window.anchor);
    }
    return window;
}

struct NativeGuidance {
    course::RollingGps renderer;
    unsigned recoveries=0,repairs=0;
    void Release(){renderer.Release();recoveries=repairs=0;}
    bool Draw(const Plan& plan,const circuit::Progress& progress,Hash colour,
              const std::string& base,std::uint64_t& serial,bool force=false){
        const auto window=GuidanceFor(plan,progress);
        if(window.ahead<=0)return false;
        const auto before_recovery=renderer.channel.recoveries;
        const auto before_repair=renderer.channel.repairs;
        // Rolling anchors are global qualified metres, so a lap crossing does
        // not reset the window or re-register a route at the same world point.
        // Whole-lap anchors advance to the next offset only after qualification.
        const bool okay=renderer.Draw(plan.course.path,window.anchor,colour,
            window.ahead,window.behind,force,base,serial);
        if(renderer.channel.recoveries>before_recovery)
            recoveries+=renderer.channel.recoveries-before_recovery;
        if(renderer.channel.repairs>before_repair)
            repairs+=renderer.channel.repairs-before_repair;
        return okay;
    }
};
} // namespace horse_races::lap
