#pragma once
#include "horse_speed_limits.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace horse_motion::native_vector {
// Last completed measurement window; retained across ordinary detach so a
// 10 Hz recording explains rejection without another per-frame disk log.
struct WorldSpeedWindow {
    unsigned windows=0,accepted=0,reject_mask=0;
    float clock_ratio=0,eligible_fraction=0,native_fraction=0,speed_fraction=0;
    float before_ratio=0,after_ratio=0,mean_z=0,peak_z=0,coherence=0,heading_dot=0,speed_delta=0;
    float position_mean_z=0;
    bool position_valid=false;
};
// A final movement vector is a REQUEST, not the resulting world displacement.
// Calibrate only its small steady-state loss, not horse ability or catch-up.
// No distance/rank/player speed enters this controller. Same world target as
// the physical pace model, with the existing native ramp and turns retained.
class WorldSpeedFeedback {
    float gain_=1,filtered_=0,settled_=0,previous_target_=0;
    float seconds_=0,wall_seconds_=0,eligible_=0,speed_sum_=0,dir_x_=0,dir_y_=0;
    float previous_x_=0,previous_y_=0,previous_speed_=0;
    float sample_seconds_=0,sample_wall_=0,sample_x_=0,sample_y_=0,sample_z_=0;
    float sample_peak_z_=0,vertical_sum_=0,peak_z_=0,native_seconds_=0,speed_seconds_=0;
    float before_sum_=0,after_sum_=0;
    float native_interval_=0;
    std::optional<float> previous_height_;
    float sample_height_delta_=0,sample_height_seconds_=0,height_delta_=0,height_seconds_=0;
    bool previous_window_=false;
    WorldSpeedWindow window_{};
    void ClearWindow(){seconds_=wall_seconds_=eligible_=speed_sum_=dir_x_=dir_y_=0;
        vertical_sum_=peak_z_=native_seconds_=speed_seconds_=before_sum_=after_sum_=0;
        height_delta_=height_seconds_=0;}
    void ClearSample(){sample_seconds_=sample_wall_=sample_x_=sample_y_=sample_z_=sample_peak_z_=0;
        sample_height_delta_=sample_height_seconds_=0;}
public:
    enum Rejection : unsigned {Clock=1,Eligibility=2,Direction=4,Slope=8,
        VerticalSpike=16,HeadingChange=32,Acceleration=64,NativeSlow=128,PositionUnavailable=256};
    struct Sample {
        float dt=0,time_ratio=1,target=0,native_top=0,before=0,after=0;
        float x=0,y=0,z=0;
        bool fresh=false;
        bool native_restricted=false;
        // Production supplies actual world height. The engine's sleeping
        // velocity can point upward while the horse descends several m/s.
        std::optional<float> height;
    };
    static constexpr float kMaxGain=1.06F;
    void Reset(){*this={};}
    void Break(){ClearWindow();ClearSample();native_interval_=0;
        previous_height_.reset();previous_window_=false;settled_=filtered_=0;previous_target_=0;}
    float Gain() const{return gain_;}
    float Filtered() const{return filtered_;}
    float Settled() const{return settled_;}
    float NativeInterval() const{return native_interval_;}
    float SampleAge() const{return sample_seconds_;}
    const WorldSpeedWindow& Window() const{return window_;}
    float Update(const Sample& s) {
        // Snow/other native resource restrictions are not output-to-world loss.
        // Keep the flat calibration for later, but never learn a way around an
        // active native restriction, even if the sleeping output exceeds it.
        if(s.native_restricted){Break();return gain_;}
        const bool finite=std::isfinite(s.dt) && std::isfinite(s.time_ratio) && std::isfinite(s.target) &&
            std::isfinite(s.native_top) && std::isfinite(s.before) && std::isfinite(s.after) &&
            std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.z) &&
            (!s.height || std::isfinite(*s.height));
        if(!finite || s.dt<=0 || s.dt>.25F || s.time_ratio<=0 ||
            s.target<10 || s.target>speed_limits::kRequestedMps || s.native_top<10 || s.native_top>25) {
            Break();return gain_;
        }
        if(std::abs(s.target-previous_target_)>=.01F*s.target)Break();
        previous_target_=s.target;
        if(s.height) {
            if(previous_height_) {
                sample_height_delta_+=*s.height-*previous_height_;
                sample_height_seconds_+=s.dt;
            }
            previous_height_=s.height;
        } else if(previous_height_) {Break();return gain_;}
        // Sleeping movement output is decimated by the engine (the 0.28.41
        // recordings show about one callback per eight display frames).
        // Freshness marks a completed measurement interval, NOT whether each
        // intervening display frame qualifies for the one-second window.
        // Average actual world velocity over that interval; never fabricate a
        // fresh engine output on the intervening frames. Discard long gaps.
        sample_seconds_+=s.dt;sample_wall_+=s.dt/s.time_ratio;
        sample_x_+=s.x*s.dt;sample_y_+=s.y*s.dt;
        sample_z_+=s.z*s.dt;
        sample_peak_z_=std::max(sample_peak_z_,std::abs(s.z));
        if(sample_seconds_>.5F || sample_wall_>.5F){Break();return gain_;}
        if(!s.fresh)return gain_;
        const float dt=sample_seconds_,wall=sample_wall_;
        const float vx=sample_x_/dt,vy=sample_y_/dt;
        vertical_sum_+=sample_z_;peak_z_=std::max(peak_z_,sample_peak_z_);
        height_delta_+=sample_height_delta_;height_seconds_+=sample_height_seconds_;
        native_interval_=dt;ClearSample();
        const float speed=std::hypot(vx,vy);
        // Native braking/turning, stalls, steep slopes and entry acceleration
        // must NEVER teach the controller to add more speed. Slow item wheels
        // also fail the window time-ratio gate, rather than accumulating a boost.
        // Keep the hook's hard slow boundary per interval; full-speed .97
        // eligibility is judged over the window, not an arbitrary leg phase.
        const bool native_fast=s.before>=.9F*s.native_top && s.after>=.9F*s.target;
        const bool world_fast=speed>=.93F*s.target && speed<=1.08F*s.target;
        const bool eligible=native_fast && world_fast;
        seconds_+=dt;wall_seconds_+=wall;
        before_sum_+=s.before/s.native_top*dt;after_sum_+=s.after/s.target*dt;
        if(native_fast)native_seconds_+=dt;
        if(world_fast)speed_seconds_+=dt;
        if(eligible) {
            eligible_+=dt;speed_sum_+=speed*dt;
            dir_x_+=vx/speed*dt;dir_y_+=vy/speed*dt;
        }
        if(seconds_<1.F)return gain_;
        // Actual movement is noisy within a gait cycle. Judge full one-second
        // windows, not a derivative between individual leg-animation frames.
        const float direction=std::hypot(dir_x_,dir_y_);
        const float clock_ratio=seconds_/wall_seconds_;
        // Vertical velocity also oscillates with the legs: ordinary gallop in
        // the .42 trace peaks at 2.1--2.3 m/s with almost no net height change.
        // A slope is sustained vertical motion, not the largest stride sample.
        // Keep a separate hard-peak veto, including BETWEEN native callbacks,
        // so a jump/fall cannot be averaged away. The caller's unsafe gate and
        // original native-output Z guard are unchanged.
        unsigned rejected=0;
        if(clock_ratio<.75F || clock_ratio>1.25F)rejected|=Clock;
        if(eligible_<.8F*seconds_)rejected|=Eligibility;
        if(direction<.995F*eligible_)rejected|=Direction;
        if(std::abs(vertical_sum_/seconds_)>=2.F)rejected|=Slope;
        // Measure net world height over the same complete window, not a
        // per-frame derivative: sleeping positions may advance in batches.
        // This is a calibration veto, not an invented slope speed limit.
        const bool position_valid=height_seconds_>=.8F*seconds_;
        const float position_mean_z=height_seconds_>0?height_delta_/height_seconds_:0;
        if(s.height && !position_valid)rejected|=PositionUnavailable;
        if(position_valid && std::abs(position_mean_z)>=2.F)rejected|=Slope;
        if(peak_z_>=4.F)rejected|=VerticalSpike;
        if(before_sum_<.97F*seconds_ || after_sum_<.97F*seconds_)rejected|=NativeSlow;
        const float mean=eligible_>0?speed_sum_/eligible_:0;
        const float x=direction>0?dir_x_/direction:0,y=direction>0?dir_y_/direction:0;
        const float heading_dot=x*previous_x_+y*previous_y_,speed_delta=std::abs(mean-previous_speed_);
        if(previous_window_) {
            if(heading_dot<std::cos(.25F*seconds_))rejected|=HeadingChange;
            if(speed_delta>.75F*seconds_)rejected|=Acceleration;
        }
        const bool stable=rejected==0;
        ++window_.windows;if(stable)++window_.accepted;
        window_.reject_mask=rejected;window_.clock_ratio=clock_ratio;
        window_.eligible_fraction=eligible_/seconds_;window_.native_fraction=native_seconds_/seconds_;
        window_.speed_fraction=speed_seconds_/seconds_;window_.mean_z=vertical_sum_/seconds_;
        window_.before_ratio=before_sum_/seconds_;window_.after_ratio=after_sum_/seconds_;
        window_.peak_z=peak_z_;window_.coherence=eligible_>0?direction/eligible_:0;
        window_.heading_dot=previous_window_?heading_dot:1;
        window_.speed_delta=previous_window_?speed_delta:0;
        window_.position_valid=position_valid;window_.position_mean_z=position_mean_z;
        if(stable) {
            settled_+=seconds_;
            if(!previous_window_)filtered_=mean;
            else filtered_+=(mean-filtered_)*(1-std::exp(-seconds_/1.5F));
            if(previous_window_ && settled_>=2.F) {
                const float error=(s.target-filtered_)/s.target;
                // Integral correction removes sustained under-speed, bounded
                // to 6% and 0.4 percentage points per game second. A deadband
                // prevents hunting around the intended world speed.
                if(std::abs(error)>.002F)
                    gain_=std::clamp(gain_+std::clamp(.25F*error,-.004F,.004F)*seconds_,1.F,kMaxGain);
            }
        } else settled_=filtered_=0;
        previous_window_=stable;previous_x_=x;previous_y_=y;previous_speed_=mean;
        ClearWindow();
        return gain_;
    }
    float Command(float world_target,bool native_restricted=false) const {
        return std::min(speed_limits::kRequestedMps,world_target*(native_restricted?1.F:gain_));
    }
};
} // namespace horse_motion::native_vector
