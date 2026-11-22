#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace horse_motion::speed_learning {
// One bounded ring per horse. A qualified half-second gait sample is stored
// once, then filtered by native resource/material and optional slope when
// finding a recent P90. No per-frame allocation or per-material reservation.
class GaitPercentile {
public:
    static constexpr unsigned kCapacity=64, kPercentileSamples=20, kMinimumSamples=4;
    struct Result {
        float resource=0,slope=0;
        unsigned resource_samples=0,slope_samples=0;
    };
private:
    struct Sample {
        double at=0;
        std::uint32_t resource=0,material=0;
        unsigned char slope=0,bin=0;
    };
    std::array<Sample,kCapacity> ring_{};
    unsigned first_=0,size_=0;
    static float Percentile(std::array<unsigned char,kPercentileSamples>& bins,unsigned count){
        if(count<kMinimumSamples)return 0;
        const unsigned rank=(count*9+9)/10;
        std::nth_element(bins.begin(),bins.begin()+rank-1,bins.begin()+count);
        return Value(bins[rank-1]);
    }
public:
    static unsigned Bin(float gait){return static_cast<unsigned>(std::clamp(gait,0.F,4.F)*50.F);}
    static float Value(unsigned bin){return static_cast<float>(bin)/50.F;}
    void Clear(){first_=size_=0;}
    unsigned Size() const{return size_;}
    void Expire(double now){
        if(size_ && now<ring_[(first_+size_-1)%kCapacity].at){Clear();return;}
        while(size_ && (now<ring_[first_].at || now-ring_[first_].at>20.)) {
            first_=(first_+1)%kCapacity;--size_;
        }
    }
    Result Query(std::uint32_t resource,std::uint32_t material,unsigned slope,double now){
        Expire(now);
        std::array<unsigned char,kPercentileSamples> resources{},slopes{};
        Result result;
        for(unsigned i=0;i<size_;++i){
            // The resource-wide and slope-specific windows independently
            // use their latest 20 observations, just like separate rings.
            const auto& sample=ring_[(first_+size_-1-i)%kCapacity];
            if(sample.resource!=resource || sample.material!=material)continue;
            if(result.resource_samples<kPercentileSamples)resources[result.resource_samples++]=sample.bin;
            if(sample.slope==slope && result.slope_samples<kPercentileSamples)slopes[result.slope_samples++]=sample.bin;
            if(result.resource_samples==kPercentileSamples && result.slope_samples==kPercentileSamples)break;
        }
        result.resource=Percentile(resources,result.resource_samples);
        result.slope=Percentile(slopes,result.slope_samples);
        return result;
    }
    // Add only after the preceding query so a sample cannot lower the P90
    // threshold that decides whether that same sample is eligible.
    void Add(std::uint32_t resource,std::uint32_t material,unsigned slope,unsigned bin,double now){
        if(size_==kCapacity){first_=(first_+1)%kCapacity;--size_;}
        ring_[(first_+size_)%kCapacity]={now,resource,material,static_cast<unsigned char>(slope),static_cast<unsigned char>(bin)};
        ++size_;
    }
};
} // namespace horse_motion::speed_learning
