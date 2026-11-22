#pragma once

#include "race_system.h"
#include <string>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>

namespace horse_races::bookmaker_runtime {
using LogFn=void (*)(const std::string&);
// A transient stall only requests recovery. A persistent navigation timeout
// is a separate technical DNF, never reported as a boundary or riding foul.
inline const char* RecordingEliminationReason(bool alive,bool identities_valid,bool dead,bool section_jump,int offenses,
    bool navigation_timeout=false){
    if(!alive)return !identities_valid?"entity lost":dead?"death":"dismounted";
    return section_jump?"skipped-section":offenses>=2?"second boundary offense":
        navigation_timeout?"navigation timeout":"route violation";
}
// Observation only; independent of the field's 25/45-second progress watchdog.
// Match EntrantTelemetry::stalled_s: elapsed below0.5m/s, -1 while moving.
struct BelowWalkingTelemetry {
    double since=-1;
    void Observe(double now,float speed,bool racing=true){
        if(!racing || !std::isfinite(now) || !std::isfinite(speed) || speed<0 || speed>=.5F){since=-1;return;}
        if(since<0 || now<since)since=now;
    }
    double Seconds(double now) const {return since>=0 && std::isfinite(now) && now>=since?now-since:-1;}
};
inline float PlannedOffsetTelemetry(const std::vector<float>& reference,const std::vector<float>& offsets,float along){
    if(reference.size()<2 || offsets.size()!=reference.size() || !std::isfinite(along))
        return std::numeric_limits<float>::quiet_NaN();
    const auto upper=std::upper_bound(reference.begin(),reference.end(),along);
    const auto index=std::min(upper==reference.begin()?std::size_t{0}:static_cast<std::size_t>(upper-reference.begin()-1),reference.size()-2);
    const float span=reference[index+1]-reference[index];
    if(!(span>0))return std::numeric_limits<float>::quiet_NaN();
    const float fraction=std::clamp((along-reference[index])/span,0.F,1.F);
    return offsets[index]+fraction*(offsets[index+1]-offsets[index]);
}
void Initialize(const std::string& directory,LogFn log);
void Tick();
bool Active();
// Serialized only when a motion recording opens or the exhibition ends.
std::string RecordingMetadata();
std::string RecordingResult();
std::uint64_t RecordingToken();
MotionContext MainMotion();
std::vector<MotionContext> ExtraMotion();
// Read-only snapshots for the recorder's existing 100 ms sampling boundary.
NavigationTelemetry MainNavigationTelemetry();
std::vector<EntrantTelemetry> ExtraEntrantTelemetry();
void BurstChanged(std::size_t index,bool active,bool announce_start=true);
void SetMotionBoostState(int horse,bool earned,bool awake);
}
