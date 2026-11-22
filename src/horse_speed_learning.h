#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace horse_motion::speed_learning {
// Gameplay catch-up, NOT the small engine-output/world-speed calibration.
inline constexpr float kCatchupLimit=.50F;
inline constexpr float kHalfBonusMetres=100.F;
inline float CatchupFraction(float gap) {
    if(!std::isfinite(gap) || gap<=0)return 0;
    // Equivalent to d*d/(D*D+d*d), without overflowing for a large d.
    const double d=gap/kHalfBonusMetres;
    return static_cast<float>(kCatchupLimit*(1.0-1.0/(1.0+d*d)));
}
inline std::optional<float> Gap(float reference,float entrant,bool qualified) {
    if(!qualified || !std::isfinite(reference) || !std::isfinite(entrant) || reference<0 || entrant<0)return {};
    return reference-entrant;
}
struct Key {
    std::uintptr_t address=0;
    int handle=0;std::uint32_t model=0;
    int speed=0,acceleration=0;
    bool operator==(const Key&) const=default;
};
struct Observation {
    float prior=0,base=0,window_mean=0,gap=0,bonus=0,target=0;
    unsigned windows=0,accepted=0,rejected=0;
    bool learned=false,gap_valid=false;
    std::uint64_t history_samples=0;
    bool inherited=false;
};
// Counters are per live entity. Context/reason describe the last completed
// position window; actual_gait describes the current observation.
struct SamplingObservation {
    std::uint32_t resource=0;
    unsigned slope=0,windows=0,qualified=0,reject_mask=0;
    float actual_gait=std::numeric_limits<float>::quiet_NaN(),mean_gait=0;
    float minimum_gait=std::numeric_limits<float>::quiet_NaN(); // calibrated recent gait P90
    // Retained with the last completed window: 0 warmup/rejected,
    // 1 resource-wide fallback, 2 same-resource same-slope P90.
    unsigned p90_source=0,p90_resource_samples=0,p90_slope_samples=0;
};
// Read-only accounting for the existing sampling decisions. Counters record
// actual candidate interruptions, not every frame an unsafe state persists.
enum class CandidateBreakReason : unsigned {Other,Travel,Context,Ineligible,RejectedWindow,Span,SpeedRange,Count};
struct LearningDiagnostics {
    std::uint64_t completed_windows=0;
    std::uint64_t p90_warmup=0,p90_below=0,p90_qualified=0;
    std::uint64_t pair_completed=0,pair_promoted=0;
    unsigned last_pair_state=0,last_pair_pending=0;
    float last_pair_pace=std::numeric_limits<float>::quiet_NaN();
    double last_pair_clock=std::numeric_limits<double>::quiet_NaN();
    std::array<std::uint64_t,static_cast<unsigned>(CandidateBreakReason::Count)> breaks{};
    unsigned last_detail=0,last_first_grade=0,last_second_grade=0,last_p90_samples=0;
    float last_first_grade_ratio=std::numeric_limits<float>::quiet_NaN();
    float last_second_grade_ratio=std::numeric_limits<float>::quiet_NaN();
    unsigned last_p90_source=0,last_p90_resource_samples=0,last_p90_slope_samples=0;
    float last_half_speed_delta=std::numeric_limits<float>::quiet_NaN();
    float last_coherence=std::numeric_limits<float>::quiet_NaN();
    float last_p90=std::numeric_limits<float>::quiet_NaN();
    double last_window_clock=std::numeric_limits<double>::quiet_NaN();
    float candidate_seconds=0;
};
// One sampler per currently bound environment bucket. Repeated steady window
// means refine its terminal world pace ceiling; the prior is not a floor. Context
// transitions discard partial windows in PersistentReference::Bind.
class Reference {
    Key key_{};
    Observation view_{};
    float seconds_=0,wall_=0,sum_=0,previous_mean_=0;
    float burst_tail_=0;
    bool previous_valid_=false;
    // Separate from the legacy 1.5 s accumulator: context learning supplies
    // already-qualified, non-overlapping 0.5 s P90 movement windows.
    float slice_speed_=0,slice_seconds_=0,slice_wall_=0;
    bool slice_valid_=false;
public:
    struct Sample {
        float dt=0,wall_dt=0,speed=0;
        bool eligible=false,burst=false;
        bool context_valid=true;
    };
    void Reset(){*this={};}
    void Break(){seconds_=wall_=sum_=0;previous_valid_=false;
        slice_speed_=slice_seconds_=slice_wall_=0;slice_valid_=false;}
    bool Bind(Key key,float prior) {
        const bool changed=key!=key_;
        if(changed){Reset();key_=key;}
        if(std::isfinite(prior) && prior>=3 && prior<=25) {
            view_.prior=prior;if(!view_.learned)view_.base=prior;
        }
        return changed;
    }
    bool Restore(float base,std::uint64_t samples) {
        if(!samples || !std::isfinite(base) || base<3 || base>25 || view_.prior<=0)return false;
        view_.base=base;view_.learned=true;view_.inherited=true;view_.history_samples=samples;
        return true;
    }
    void History(std::uint64_t samples){view_.history_samples=samples;}
    void Update(const Sample& s) {
        // The adapter supplies completed ~0.5-second position windows. Each
        // frame in that window has already passed the <=0.25-second guard.
        if(!std::isfinite(s.dt) || !std::isfinite(s.wall_dt) || s.dt<=0 || s.dt>.75F ||
            s.wall_dt<=0 || s.wall_dt>1.F){Break();return;}
        if(s.burst){burst_tail_=1.F;Break();return;}
        if(burst_tail_>0){burst_tail_=std::max(0.F,burst_tail_-s.dt);Break();return;}
        if(!s.context_valid || !s.eligible || !std::isfinite(s.speed) || s.speed<3 || s.speed>25 || view_.prior<=0) {
            Break();return;
        }
        seconds_+=s.dt;wall_+=s.wall_dt;sum_+=s.speed*s.dt;
        if(seconds_<1.5F)return;
        const float mean=sum_/seconds_,duration=seconds_,ratio=seconds_/wall_;
        seconds_=wall_=sum_=0;++view_.windows;view_.window_mean=mean;
        if(ratio<.75F || ratio>1.25F) {++view_.rejected;previous_valid_=false;return;}
        // Two consecutive 1.5-second windows must agree within ~1.3 km/h.
        // Three measured seconds plus the burst tail fit the existing five-
        // second normal-gallop interval, so learning is not starved by bursts.
        // Acceleration and a single stride/impact spike cannot set a record.
        if(previous_valid_ && std::abs(mean-previous_mean_)<=.35F) {
            const float candidate=std::min(mean,previous_mean_);
            ++view_.accepted;
            if(!view_.learned){view_.base=candidate;view_.learned=true;}
            // Keep the established repeated-window upper envelope WITHIN
            // this bucket. A traffic/fatigue plateau cannot erase previously
            // observed capability; it also cannot contaminate another slope.
            else view_.base+=(1.F-std::exp(-duration/8.F))*std::max(0.F,candidate-view_.base);
        }else if(previous_valid_)++view_.rejected;
        previous_mean_=mean;previous_valid_=true;
    }
    // Pair two eligible non-overlapping slices, but do not treat one pair as
    // a learned terminal speed: it can still be part of an acceleration.
    std::optional<float> QualifiedPair(const Sample& s) {
        if(!std::isfinite(s.dt) || !std::isfinite(s.wall_dt) || s.dt<=0 || s.dt>.75F ||
            s.wall_dt<=0 || s.wall_dt>1.F || !s.context_valid || !s.eligible || s.burst ||
            !std::isfinite(s.speed) || s.speed<3 || s.speed>25 || view_.prior<=0 ||
            s.dt/s.wall_dt<.75F || s.dt/s.wall_dt>1.25F) {Break();return {};}
        if(!slice_valid_) {
            slice_speed_=s.speed;slice_seconds_=s.dt;slice_wall_=s.wall_dt;slice_valid_=true;return {};
        }
        if(std::abs(s.speed-slice_speed_)>.35F) {
            slice_speed_=s.speed;slice_seconds_=s.dt;slice_wall_=s.wall_dt;return {};
        }
        const float total=slice_seconds_+s.dt,wall=slice_wall_+s.wall_dt;
        if(total/wall<.75F || total/wall>1.25F){Break();return {};}
        const float candidate=std::min(slice_speed_,s.speed);
        slice_valid_=false;slice_speed_=slice_seconds_=slice_wall_=0;
        return candidate;
    }
    // A promoted observation was independently corroborated by Store. Existing
    // learned values can only rise gradually, not be replaced by one pair.
    void AcceptCorroborated(float candidate) {
        if(!std::isfinite(candidate) || candidate<3 || candidate>25 || view_.prior<=0)return;
        ++view_.windows;++view_.accepted;view_.window_mean=candidate;
        if(!view_.learned){view_.base=candidate;view_.learned=true;}
        else view_.base+=(1.F-std::exp(-1.F/8.F))*std::max(0.F,candidate-view_.base);
    }
    float Base() const{return view_.base;}
    // Diagnostic only: elapsed selected samples in the current 1.5 s group.
    float PendingSeconds() const{return seconds_;}
    bool HasPreviousWindow() const{return previous_valid_;}
    float PendingQualifiedSeconds() const{return slice_valid_?slice_seconds_:0.F;}
    void DiscardQualifiedSlice(){slice_speed_=slice_seconds_=slice_wall_=0;slice_valid_=false;}
    void DiscardStrictWindow(){seconds_=wall_=sum_=0;previous_valid_=false;}
    void SetTarget(std::optional<float> gap,float target,float fraction=0) {
        view_.gap_valid=gap.has_value() && std::isfinite(*gap);
        view_.gap=view_.gap_valid?*gap:0;
        view_.bonus=view_.gap_valid&&std::isfinite(fraction)?std::clamp(fraction,0.F,kCatchupLimit):0;
        view_.target=target;
    }
    const Observation& Observe() const{return view_;}
};
} // namespace horse_motion::speed_learning
