#pragma once
#include <algorithm>
#include <cmath>

namespace horse_races::tracking_margin {
inline constexpr int kDefaultCentimetres=150,kMinCentimetres=0,kMaxCentimetres=500;
inline constexpr float kDefaultMetres=kDefaultCentimetres/100.0F,kMinMetres=0,kMaxMetres=5,kStepMetres=.1F;
inline bool Valid(float value) {return std::isfinite(value)&&value>=kMinMetres&&value<=kMaxMetres;}
inline float Step(float current,int steps) {
    const double ticks=std::round(static_cast<double>(Valid(current)?current:kDefaultMetres)*10)+steps;
    return static_cast<float>(std::clamp(ticks,0.0,50.0)/10);
}
}
