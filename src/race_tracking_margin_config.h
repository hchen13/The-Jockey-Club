#pragma once
#include "race_tracking_margin.h"
#include "kernel/config.h"

// Event-triggered reads only: plan creation and settings-page entry.
namespace horse_races::tracking_margin {
inline constexpr const char* kSection="RaceAI";
inline constexpr const char* kKey="TrackingMarginCentimetres";
inline float Read(const std::string& ini) {
    return config::ReadInt(ini,kSection,kKey,kDefaultCentimetres,kMinCentimetres,kMaxCentimetres)/100.0F;
}
inline bool Save(const std::string& ini,float value) {
    if(ini.empty()||!Valid(value))return false;
    const int centimetres=static_cast<int>(std::lround(static_cast<double>(value)*100));
    return config::SetRaw(ini.c_str(),kSection,kKey,centimetres)&&
        config::ReadInt(ini,kSection,kKey,-1,kMinCentimetres,kMaxCentimetres)==centimetres;
}
}
