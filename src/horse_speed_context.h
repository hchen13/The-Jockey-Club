#pragma once
#include "horse_speed_limits.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace horse_motion::speed_learning {
// Native locomotion resource, NOT weather, venue, coat or a guessed material
// coefficient. Unknown resources remain separate keys too (including modded
// resources). Grade is measured from travel, never sleeping velocity.z or the
// stale native slope blackboard. Thresholds are rise / horizontal distance.
enum class Slope : unsigned {Unknown,Flat,GentleUp,SteepUp,GentleDown,SteepDown};
struct ContextKey {
    std::uint32_t resource=0;
    Slope slope=Slope::Unknown;
    std::uint32_t material=0; // Zero identifies imported legacy, resource-only data.
    bool operator==(const ContextKey&) const=default;
    bool Valid() const{return resource && slope>=Slope::Flat && slope<=Slope::SteepDown;}
};
inline Slope Grade(float grade) {
    if(!std::isfinite(grade))return Slope::Unknown;
    if(std::abs(grade)<=.07F)return Slope::Flat;
    if(grade>0)return grade<=.20F?Slope::GentleUp:Slope::SteepUp;
    return grade>=-.20F?Slope::GentleDown:Slope::SteepDown;
}
// Slope enum values are intentionally not ordered by terrain grade.
// Explicit indices keep the transition rules correct on both sides of flat.
inline int GradeIndex(Slope grade) {
    switch(grade){
    case Slope::SteepDown:return 0;
    case Slope::GentleDown:return 1;
    case Slope::Flat:return 2;
    case Slope::GentleUp:return 3;
    case Slope::SteepUp:return 4;
    default:return -1;
    }
}
inline float GradeBoundary(int lower_index) {
    constexpr float boundaries[]{-.20F,-.07F,.07F,.20F};
    return boundaries[lower_index];
}
inline constexpr float kGradeBoundaryTolerance=.02F;
inline constexpr float kGradeFloatTolerance=.000001F;
// Retain the previous established category only when the new category is
// adjacent and the measured grade is within two percentage points of the
// separating boundary. Multi-band changes take effect immediately.
inline Slope StableGrade(Slope previous,float grade) {
    const auto next=Grade(grade);
    const int before=GradeIndex(previous),after=GradeIndex(next);
    if(before<0 || after<0 || std::abs(before-after)!=1)return next;
    const float boundary=GradeBoundary(std::min(before,after));
    return std::abs(grade-boundary)<=kGradeBoundaryTolerance+kGradeFloatTolerance?previous:next;
}
// Preserve the existing strict same-band case. A differing half-window is
// accepted only when both halves lie inside one adjacent boundary's narrow
// tolerance zone and the selected grade is one of those two categories.
inline bool ConsistentHalfGrades(float first,float second,Slope selected) {
    const int a=GradeIndex(Grade(first)),b=GradeIndex(Grade(second)),chosen=GradeIndex(selected);
    if(a<0 || b<0 || chosen<0)return false;
    if(a==b && a==chosen)return true;
    for(int lower=0;lower<4;++lower){
        const float boundary=GradeBoundary(lower);
        if((a==lower || a==lower+1) && (b==lower || b==lower+1) &&
            (chosen==lower || chosen==lower+1) &&
            std::abs(first-boundary)<=kGradeBoundaryTolerance+kGradeFloatTolerance &&
            std::abs(second-boundary)<=kGradeBoundaryTolerance+kGradeFloatTolerance)return true;
    }
    return false;
}
struct Position {float x=0,y=0,z=0;};
class TravelContext {
    Position last_{},start_{};
    std::uint32_t resource_=0,material_=0;
    float seconds_=0,distance_=0,age_=0;
    bool seeded_=false;
    ContextKey key_{};
    Slope stable_slope_=Slope::Unknown;
    float grade_=0,coherence_=0;
    float first_distance_=0,first_rise_=0;
public:
    // Diagnostic subreasons for the existing Unsettled (16) decision. These
    // do not participate in classification or change the sampling guards.
    enum WindowDetail : unsigned {ShortTravel=1,Curvature=2,SpeedVariation=4,GradeVariation=8,InvalidHalves=16};
    struct Window {
        bool ready=false;float seconds=0,speed=0;bool settled=false;
        unsigned detail=0,first_grade=0,second_grade=0;
        float half_speed_delta=std::numeric_limits<float>::quiet_NaN();
        // Continuous rise/horizontal ratios, distinct from the enum buckets;
        // unavailable until both half-windows have valid distance/time.
        float first_grade_ratio=std::numeric_limits<float>::quiet_NaN();
        float second_grade_ratio=std::numeric_limits<float>::quiet_NaN();
    };
    Window completed{};
    void Reset(){*this={};}
    ContextKey Key() const{return key_;}
    std::uint32_t Resource() const{return resource_;}
    std::uint32_t Material() const{return material_;}
    float Rise() const{return grade_;}
    float Coherence() const{return coherence_;}
    // O(1), no terrain raycasts, route scans, allocation, or native calls.
    bool Update(Position p,std::uint32_t resource,float dt,bool grounded,std::uint32_t material=0) {
        completed={};
        if(!grounded || !resource || !std::isfinite(dt) || dt<=0 || dt>.25F ||
            !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {Reset();return false;}
        if(!seeded_ || resource!=resource_ || material!=material_) {
            Reset();last_=start_=p;resource_=resource;material_=material;seeded_=true;return false;
        }
        const float dx=p.x-last_.x,dy=p.y-last_.y,dz=p.z-last_.z;
        const float step=std::hypot(dx,dy);
        // Reject teleports, streaming relocations and a stationary vertical
        // jump. The caller additionally rejects airborne/jump/fall states.
        if(step>30.F*dt+.25F || std::abs(dz)>15.F*dt+.25F) {Reset();return false;}
        // Split a step at the same quarter-second boundary in both distance
        // and rise. Only completed windows with stable half-window speed and
        // consistent grades may teach; classification is separate.
        const float first_dt=std::clamp(.25F-seconds_,0.F,dt);
        first_distance_+=step*(first_dt/dt);first_rise_+=dz*(first_dt/dt);
        last_=p;seconds_+=dt;distance_+=step;age_+=dt;
        // Do not retain an unconfirmed prior grade indefinitely through
        // repeated unstable windows or insufficient travel.
        if(age_>1.F){key_={};coherence_=0;stable_slope_=Slope::Unknown;}
        if(seconds_<.5F)return true;
        completed={true,seconds_,distance_/seconds_};
        if(distance_<2.F){key_={};coherence_=0;completed.detail|=ShortTravel;}
        else {
            grade_=(p.z-start_.z)/distance_;
            coherence_=std::hypot(p.x-start_.x,p.y-start_.y)/distance_;
            const Slope candidate=StableGrade(stable_slope_,grade_);
            const float second_distance=distance_-first_distance_;
            const float second_rise=p.z-start_.z-first_rise_;
            if(coherence_<.985F)completed.detail|=Curvature;
            if(first_distance_>0 && second_distance>0 && seconds_>.25F) {
                completed.half_speed_delta=std::abs(first_distance_/.25F-second_distance/(seconds_-.25F));
                const float first_grade=first_rise_/first_distance_,second_grade=second_rise/second_distance;
                completed.first_grade_ratio=first_grade;completed.second_grade_ratio=second_grade;
                completed.first_grade=static_cast<unsigned>(Grade(first_grade));
                completed.second_grade=static_cast<unsigned>(Grade(second_grade));
                const bool grades_consistent=ConsistentHalfGrades(first_grade,second_grade,candidate);
                completed.settled=completed.half_speed_delta<=.35F && grades_consistent;
                if(!(completed.half_speed_delta<=.35F))completed.detail|=SpeedVariation;
                if(!grades_consistent)completed.detail|=GradeVariation;
            }else completed.detail|=InvalidHalves;
            if(completed.settled && coherence_>=.985F){
                // Only confirmed geometry may update the learning/pace key.
                key_={resource,candidate,material};stable_slope_=candidate;age_=0;
            }else if(candidate!=stable_slope_){
                // A potentially real transition outside the prior bucket's
                // tolerance must not reuse its pace until a stable window
                // establishes the new grade.
                key_={};stable_slope_=Slope::Unknown;
            }
        }
        start_=p;seconds_=distance_=first_distance_=first_rise_=0;
        return true;
    }
};

// A measured terminal WORLD pace already contains the native resource,
// slope/depth response and awake physical integration. It is a ceiling, not
// another factor to multiply into an already slowed native vector.
struct Compensation {
    float ceiling=0,gain=1;
    enum class Source {None=0,Measured=2,TerrainFlat=3,SpeedPreset=4,EstimatedContext=5,EstimatedTerrainFlat=6,NativeTerrainRatio=7} source=Source::None;
    float gain_limit=speed_limits::kBaseOutputGain;
};
inline Compensation Compose(float request,float flat_base,float native_normal,float native_current,
    float measured_pace,float terrain_flat_pace,float speed_preset,bool context_estimated=false,bool flat_estimated=false,
    float native_terrain_ratio=0) {
    Compensation out;
    if(!std::isfinite(request) || request<=0 || request>speed_limits::kRequestedMps || !std::isfinite(flat_base) || flat_base<3 || flat_base>25 ||
        !std::isfinite(native_normal) || native_normal<=0 || native_normal>25 ||
        !std::isfinite(native_current) || native_current<=0 || native_current>25)return out;
    const auto valid=[](float value){return std::isfinite(value) && value>=3 && value<=25;};
    float base=0;
    if(valid(measured_pace)){base=measured_pace;out.source=context_estimated?Compensation::Source::EstimatedContext:Compensation::Source::Measured;}
    else if(valid(terrain_flat_pace)){base=terrain_flat_pace;out.source=flat_estimated?Compensation::Source::EstimatedTerrainFlat:Compensation::Source::TerrainFlat;}
    else if(std::isfinite(native_terrain_ratio) && native_terrain_ratio>0 && native_terrain_ratio<=1 &&
        valid(flat_base*native_terrain_ratio)){
        base=flat_base*native_terrain_ratio;out.source=Compensation::Source::NativeTerrainRatio;
    }
    else if(valid(speed_preset)){base=speed_preset;out.source=Compensation::Source::SpeedPreset;}
    else return out;
    // Exact or cross-rank context -> terrain flat -> native resource ratio -> preset.
    // Native resource/braking responses are preserved once by Adjust; they
    // are never substituted for the capability fallback or multiplied twice.
    const float bonus=request/flat_base;
    out.ceiling=std::min(speed_limits::kRequestedMps,base*bonus);
    // Preset fallback must use the preset on the gain side too. Otherwise an
    // unrelated ordinary-flat observation can leave the output at native
    // speed despite telemetry claiming the higher preset ceiling. Measured
    // terrain still uses ordinary-flat ability gain, never another slope factor.
    const float ability=out.source==Compensation::Source::SpeedPreset?speed_preset:flat_base;
    float gain=ability*bonus/native_normal;
    // A known contextual terminal pace must be reachable at native full gait,
    // even when ordinary-flat history is lower. Only raise positive gain:
    // target/current below one would double-apply a learned terrain penalty.
    // Presets do not have this evidence, so keep their native terrain ratio.
    if(out.source!=Compensation::Source::SpeedPreset)gain=std::max(gain,out.ceiling/native_current);
    // Retain the old ability limit without a racing bonus. Extend that limit
    // by the requested bonus, otherwise a rank-10 +50% target is silently
    // clipped by the old 1.5 total multiplier before it reaches the engine.
    out.gain_limit=std::min(speed_limits::kMaxOutputGain,
        speed_limits::kBaseOutputGain*std::max(1.F,bonus));
    out.gain=std::clamp(gain,1.F,out.gain_limit);
    return out;
}
} // namespace horse_motion::speed_learning
