#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace horse_races::bgm {
// Eight seconds matches the existing Arthur-duel STOP_MUSIC_8S ending.
// Ramp amplitude, converting to the engine's decibel gain at each update.
struct Fade {
    static constexpr std::int64_t duration_ms=8000;
    bool active=false;
    std::int64_t began=0,length_ms=duration_ms;
    float initial_db=0;
    static constexpr std::int64_t TailDuration(std::int64_t track_ms) {
        return std::min(duration_ms,std::max<std::int64_t>(1,track_ms/2));
    }
    bool Begin(std::int64_t now,float db,std::int64_t length=duration_ms) {
        if(!std::isfinite(db) || db < -100 || db > 24 || length<=0)return false;
        active=true;began=now;initial_db=db;length_ms=length;return true;
    }
    float Gain(std::int64_t now) const {
        const auto elapsed=std::clamp<std::int64_t>(now-began,0,length_ms);
        if(elapsed==length_ms)return -100.0F;
        const double amplitude=1.0-static_cast<double>(elapsed)/length_ms;
        return std::max(-100.0F,initial_db+static_cast<float>(20.0*std::log10(amplitude)));
    }
    bool Complete(std::int64_t now) const {return active && now-began>=length_ms;}
};
} // namespace horse_races::bgm
