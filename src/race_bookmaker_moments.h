#pragma once
#include "race_bookmaker_camera.h"
#include <map>
#include <utility>

namespace horse_races::bookmaking {
// Race data only: this detector never steers, changes placing or owns a camera.
// Pairwise 1.5 m hysteresis distinguishes an actual pass from rank jitter.
// A fall is supplied ONLY on a real dismount/death, not a navigation/boundary DQ.
class CameraMoments {
public:
    struct Sample {std::size_t rider=0;CameraPoint point;float metres=0;bool racing=false;};
    static constexpr double kLifetime=3.0;
    static constexpr float kPassLead=1.5F,kPassDistance=25.0F,kFallDistance=30.0F;
private:
    struct Moment {
        bool fall=false;
        std::size_t subject=0,other=0;
        CameraPoint point;
        double at=0;
    };
    std::map<std::pair<std::size_t,std::size_t>,std::size_t> leaders_;
    std::vector<Moment> moments_;
    double begun_at=-1;
    static float DistanceSquared(CameraPoint a,CameraPoint b){
        const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z;
    }
    void Expire(double now){
        moments_.erase(std::remove_if(moments_.begin(),moments_.end(),[now](const Moment& moment){
            return now<moment.at || now-moment.at>kLifetime;
        }),moments_.end());
    }
    void Add(Moment moment){
        if(moments_.size()>=16)moments_.erase(moments_.begin());
        moments_.push_back(moment);
    }
public:
    void Reset(){*this={};}
    void Fall(std::size_t rider,CameraPoint point,double now){
        if(!std::isfinite(now) || !std::isfinite(point.x) || !std::isfinite(point.y) ||
            !std::isfinite(point.z))return;
        Expire(now);Add({true,rider,rider,point,now});
    }
    void Observe(std::span<const Sample> samples,double now){
        if(!std::isfinite(now))return;
        if(begun_at<0)begun_at=now;
        Expire(now);
        for(std::size_t i=0;i<samples.size();++i)for(std::size_t j=i+1;j<samples.size();++j){
            const auto& a=samples[i];const auto& b=samples[j];
            const auto key=std::minmax(a.rider,b.rider);
            if(a.rider==b.rider || !a.racing || !b.racing || !std::isfinite(a.metres) ||
                !std::isfinite(b.metres)){leaders_.erase(key);continue;}
            const float gap=a.metres-b.metres;
            if(std::abs(gap)<kPassLead)continue;
            const auto leader=gap>0?a.rider:b.rider;
            const auto [entry,inserted]=leaders_.try_emplace(key,leader);
            if(inserted || entry->second==leader)continue;
            entry->second=leader;
            // Ignore the starting-grid shuffle and cars/paths on different
            // laps. The full-course metres, not the local lap, determine order.
            const float distance=DistanceSquared(a.point,b.point);
            if(now-begun_at<3 || std::abs(gap)>15 || !std::isfinite(distance) ||
                distance>kPassDistance*kPassDistance)continue;
            Add({false,leader,leader==a.rider?b.rider:a.rider,
                leader==a.rider?a.point:b.point,now});
        }
    }
    std::vector<std::size_t> Candidates(std::span<const std::size_t> eligible,
        std::span<const Sample> samples,double now,std::optional<std::size_t> current={}) const {
        std::vector<std::size_t> result;
        const auto add=[&](std::size_t rider){
            if(std::find(eligible.begin(),eligible.end(),rider)!=eligible.end() &&
                std::find(result.begin(),result.end(),rider)==result.end())result.push_back(rider);
        };
        // Newest falls first, then newest passes. Keep a fallen/invalid ped OUT
        // of the camera roster: film that incident through nearby live riders.
        for(const bool fall:{true,false})for(auto entry=moments_.rbegin();entry!=moments_.rend();++entry){
            const auto& moment=*entry;
            if(moment.fall!=fall || now<moment.at || now-moment.at>kLifetime)continue;
            if(!fall){
                if(current && (*current==moment.subject || *current==moment.other))continue;
                add(moment.subject);add(moment.other);continue;
            }
            std::vector<std::pair<float,std::size_t>> nearby;
            bool already_framed=false;
            for(const auto& sample:samples)if(sample.racing){
                const float distance=DistanceSquared(sample.point,moment.point);
                if(distance>kFallDistance*kFallDistance || !std::isfinite(distance))continue;
                if(current && sample.rider==*current)already_framed=true;
                nearby.emplace_back(distance,sample.rider);
            }
            if(already_framed)continue;
            std::sort(nearby.begin(),nearby.end());
            for(const auto [distance,rider]:nearby){(void)distance;add(rider);}
        }
        return result;
    }
};
} // namespace horse_races::bookmaking
