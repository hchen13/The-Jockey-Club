#pragma once
#include "race_course.h"
#include "race_terrain.h"
#include "race_pace_trace.h"
#include "horse_speed_priors.h"
#include <array>

namespace horse_races::race_pace {
// Speed planning ON the line it is given: the plan itself moves no point,
// deletes none, resamples nothing and changes no gate. The SOURCE may be the
// recorded centreline or a racing line built on the centreline's nodes
// (race_racing_line.h); then `reference` carries the centreline metres of
// node i so callers keep speaking centreline along.
struct Dynamics {
    float top_mps=70.35F/3.6F,acceleration=3,braking=6,lateral=14;
    bool Valid() const {return std::isfinite(top_mps) && top_mps>=5 && top_mps<=50 &&
        std::isfinite(acceleration) && acceleration>0 && acceleration<=30 && std::isfinite(braking) && braking>0 && braking<=30 &&
        std::isfinite(lateral) && lateral>0 && lateral<=30;}
};
inline Dynamics ForAttributes(int speed,int acceleration) {
    // Rank-dependent planning priors, not claimed exact live terminal speeds.
    // Native execution and the unchanged shared horse curve set actual ability.
    constexpr std::array<float,10> seconds{18,15,12.5F,10.5F,8.5F,7,5.5F,4,2.3F,.5F};
    if(speed<1 || speed>10 || acceleration<1 || acceleration>10)return {0};
    const float top=horse_motion::speed_learning::PriorTopMps(speed);
    return {top,std::clamp(top/seconds[acceleration-1],.25F,8.0F),6,14};
}
inline Vector3 At(const course::Path& path,float along) {
    along=std::clamp(along,0.0F,path.Length());const auto i=path.Segment(along);
    const float t=(along-path.metres[i])/(path.metres[i+1]-path.metres[i]);
    const auto a=path.points[i],b=path.points[i+1];
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
struct Plan {
    course::Path path;
    Dynamics dynamics;
    std::vector<float> ceiling;
    std::vector<float> curvature;   // Per node, as computed below.
    float peak_curvature=0;
    double nominal_seconds=0; // Model estimate, NEVER a measured lap time.
    // Centreline metres of node i when the source is an offset line; empty
    // when the source IS the centreline. Allowed()/Straight() take centreline
    // along either way and map it here.
    std::vector<float> reference;
    std::vector<float> terrain_ceiling; // Same planned nodes, separate from bend/room caps.
    float terrain_caution=-1; // -1: unchanged policy; 0: racing; 1: steady.
    // `caps`, one per node: the fastest the node may be ridden for reasons
    // the geometry does not show (race_obstacle_learning.h); none by default.
    bool Build(const course::Path& source,const Dynamics& parameters={},const std::vector<float>& centre_metres={},
        const std::vector<float>* caps=nullptr,const terrain::Profile* terrain=nullptr,float caution=-1) {
        *this={};if(!parameters.Valid() || !path.Build(source.points))return false;
        if(centre_metres.size()==path.points.size() && centre_metres!=path.metres)reference=centre_metres;
        dynamics=parameters;ceiling.assign(path.points.size(),parameters.top_mps);
        curvature.assign(path.points.size(),0.0F);
        for(std::size_t i=1;i+1<path.points.size();++i) {
            // Estimate heading change over a horse-scale distance rather than
            // differentiating tiny/noisy adjacent recording samples. Positions
            // submitted to the native remain EXACTLY the original points.
            const auto a=At(path,path.metres[i]-8),b=path.points[i],c=At(path,path.metres[i]+8);
            const float ab=destination::Distance(a,b),bc=destination::Distance(b,c),ac=destination::Distance(a,c);
            const float cross=std::abs((b.x-a.x)*(c.y-b.y)-(b.y-a.y)*(c.x-b.x));
            const float k=2*cross/std::max(.01F,ab*bc*ac);
            curvature[i]=k;
            peak_curvature=std::max(peak_curvature,k);
            ceiling[i]=std::min(parameters.top_mps,std::sqrt(parameters.lateral/std::max(.0001F,k)));
        }
        // Room caps: where the survey leaves the line little room the plan
        // slows before the braking envelope is laid back over it, so the
        // approach brakes for the gap as it would for a bend.
        if(caps && caps->size()==ceiling.size())
            for(std::size_t i=0;i<ceiling.size();++i)ceiling[i]=std::min(ceiling[i],std::max(1.0F,(*caps)[i]));
        if(terrain && caution>=0 && !terrain->metres.empty()) {
            const auto safety=terrain->Caps(parameters.top_mps,caution);
            if(safety.size()!=terrain->metres.size() || safety.size()<2)return false;
            terrain_caution=caution;terrain_ceiling.resize(ceiling.size());
            for(std::size_t i=0;i<ceiling.size();++i) {
                const float s=reference.empty()?path.metres[i]:reference[i];
                const auto it=std::upper_bound(terrain->metres.begin(),terrain->metres.end(),s);
                const auto j=std::min(safety.size()-2,static_cast<std::size_t>(std::max<std::ptrdiff_t>(0,it-terrain->metres.begin()-1)));
                const float t=std::clamp((s-terrain->metres[j])/(terrain->metres[j+1]-terrain->metres[j]),0.F,1.F);
                terrain_ceiling[i]=safety[j]*(1-t)+safety[j+1]*t;
                ceiling[i]=std::min(ceiling[i],terrain_ceiling[i]);
            }
        }
        // Backward braking envelope: account for ALL subsequent bends, not
        // only the next node. Intermediate points and finish are never stops.
        for(std::size_t i=ceiling.size()-1;i>0;--i)
            ceiling[i-1]=std::min(ceiling[i-1],std::sqrt(ceiling[i]*ceiling[i]+2*parameters.braking*(path.metres[i]-path.metres[i-1])));
        auto predicted=ceiling;predicted.front()=0;
        for(std::size_t i=1;i<predicted.size();++i)
            predicted[i]=std::min(predicted[i],std::sqrt(predicted[i-1]*predicted[i-1]+2*parameters.acceleration*(path.metres[i]-path.metres[i-1])));
        for(std::size_t i=1;i<predicted.size();++i)
            nominal_seconds+=2*(path.metres[i]-path.metres[i-1])/std::max(1.0F,predicted[i]+predicted[i-1]);
        return true;
    }
    // Geometry only, deliberately independent of the dynamics prior: whether
    // the recorded line ahead is actually straight, not whether the planner's
    // cornering model happens to call it one. A gate built on the planner would
    // move every time the cornering envelope is tuned.
    // Centreline along -> along on the planned line, piecewise linear over the
    // shared nodes; identity when the plan is on the centreline itself.
    float ToLine(float centre_along) const {
        if(reference.size()<2 || reference.size()!=path.metres.size() || !std::isfinite(centre_along))return centre_along;
        if(centre_along<=reference.front())return path.metres.front();
        if(centre_along>=reference.back())return path.metres.back();
        const auto it=std::upper_bound(reference.begin(),reference.end(),centre_along);
        const std::size_t i=static_cast<std::size_t>(it-reference.begin()-1);
        const float span=reference[i+1]-reference[i];
        return path.metres[i]+(path.metres[i+1]-path.metres[i])*(span>0?(centre_along-reference[i])/span:0);
    }
    bool Straight(float from,float metres,float max_curvature) const {
        if(curvature.empty() || path.points.empty() || !(metres>0) || !(max_curvature>0))return false;
        const float begin=std::clamp(ToLine(from),0.0F,path.Length());
        const float end=std::clamp(ToLine(from+metres),0.0F,path.Length());
        if(!(end>begin))return false;
        for(std::size_t i=0;i<curvature.size() && i<path.metres.size();++i) {
            if(path.metres[i]<begin)continue;
            if(path.metres[i]>end)break;
            if(curvature[i]>max_curvature)return false;
        }
        return true;
    }
    float Allowed(float along,float speed,Trace* observation=nullptr) const {
        if(ceiling.size()!=path.points.size() || ceiling.size()<2 || !std::isfinite(along) || !std::isfinite(speed))return 0;
        // Reserve a 0.4 s planning allowance before the bend, not a native
        // response guarantee; keep this horizon while a destination task brakes.
        along=std::clamp(ToLine(along)+std::max(0.0F,speed)*.4F+2,0.0F,path.Length());
        const auto i=path.Segment(along);const float t=(along-path.metres[i])/(path.metres[i+1]-path.metres[i]);
        if(observation && diagnostics_enabled) {
            observation->lookahead_line_m=along;
            if(curvature.size()==ceiling.size())
                observation->lookahead_curvature=curvature[i]*(1-t)+curvature[i+1]*t;
        }
        return std::sqrt(ceiling[i]*ceiling[i]*(1-t)+ceiling[i+1]*ceiling[i+1]*t);
    }
    bool TerrainLimited(float along,float speed) const {
        if(terrain_ceiling.size()!=path.points.size() || terrain_ceiling.size()<2 ||
            !std::isfinite(along) || !std::isfinite(speed))return false;
        const float s=std::clamp(ToLine(along)+std::max(0.F,speed)*.4F+2,0.F,path.Length());
        const auto i=path.Segment(s);
        return std::min(terrain_ceiling[i],terrain_ceiling[i+1])<dynamics.top_mps-.25F;
    }
};
inline float Effort(int skill_percent) {
    return 2.5F+.5F*static_cast<float>(std::clamp(skill_percent,0,100))/100.0F;
}
struct Command {
    bool braking=false;
    float target_mps=0,effort=3;
    bool terrain_limited=false;
    bool following_limited=false;
    int following_horse=0;
    float following_gap_m=0;
    bool vehicle_blocked=false;
    float catchup_fraction=0; // Currently qualified output of the race-owner peak latch.
    bool vehicle_limited=false; // Approach cap; vehicle_blocked owns only the final pause.
    bool NativePaceLimited() const {return terrain_limited || following_limited || vehicle_blocked || vehicle_limited;}
    mutable Trace trace;
};
struct Controller {
    bool braking=false;
    Command Update(const Plan& plan,float along,float speed,int skill=100) {
        Command out;Trace* observation=diagnostics_enabled?&out.trace:nullptr;
        if(observation) {
            observation->controller_evaluated=true;
            observation->input_speed_mps=speed;observation->input_along_m=along;
            observation->top_mps=plan.dynamics.top_mps;observation->lateral_mps2=plan.dynamics.lateral;
        }
        const auto record=[&] {
            if(!observation)return;
            observation->plan_target_mps=out.target_mps;observation->plan_effort=out.effort;
            observation->plan_braking=out.braking;observation->plan_terrain_limited=out.terrain_limited;
        };
        out.effort=Effort(skill);out.target_mps=plan.Allowed(along,speed,observation);
        if(!std::isfinite(speed) || speed<0 || out.target_mps<=0){braking=false;record();return out;}
        out.terrain_limited=plan.TerrainLimited(along,speed);
        if(out.terrain_limited) {
            // Withhold custom propulsion throughout the local safety envelope,
            // even below its ceiling. Otherwise force / learned catch-up fights
            // the native's deliberate slope pace. Same absolute native request
            // for awake and sleeping mounts; gait is the traffic fallback only.
            braking=true;out.braking=true;
            out.effort=std::min(out.effort,std::clamp(3.F*out.target_mps/15.F,1.5F,3.F));
            record();return out;
        }
        // The speed-level prior must NOT become a new top-speed limiter. Only
        // a geometric bend can request braking; clear immediately on exit.
        const bool bend=out.target_mps<plan.dynamics.top_mps-.25F;
        if(observation)observation->plan_bend=bend;
        const float error=speed-out.target_mps;
        if(!bend || error<.1F)braking=false;
        else if(error>1.2F)braking=true;
        out.braking=braking;
        if(braking)out.effort=std::min(out.effort,std::clamp(3.0F-.22F*std::max(0.0F,error),1.5F,2.8F));
        record();return out;
    }
};
} // namespace horse_races::race_pace
