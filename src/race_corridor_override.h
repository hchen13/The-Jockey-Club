#pragma once
#include "kernel/config.h"
#include "kernel/config.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

// A per-station override of the judged corridor half-width, persisted in
// TheJockeyClub.ini under [CorridorHalfWidth] with the station key as the key
// (blackwater=12.0). The route file keeps its own TrackHalfWidth as the default;
// an absent, malformed or out-of-range key means "route default". The width is
// an instance parameter of the frozen lap::Plan (like X laps), never a rule:
// rules::RaceRules stays derived from the route alone, and nothing here is
// ever written back into a station's route.
namespace horse_races::corridor_override {
// lap::Plan::Build's hard bounds (race_segmented_circuit.h): a width outside
// them is refused at Freeze, so the editor must never produce one.
inline constexpr float kMin=5,kMax=20,kStep=.5F;
inline constexpr const char* kSection="CorridorHalfWidth";
inline bool Valid(float width) {return std::isfinite(width) && width>=kMin && width<=kMax;}
// Strict: the whole text must be one finite number inside the bounds.
inline std::optional<float> Parse(const char* text) {
    if(!text || !*text)return {};
    try {
        std::size_t used=0;const float width=std::stof(text,&used);
        if(used==std::string(text).size() && Valid(width))return width;
    } catch(...) {}
    return {};
}
inline std::string Encode(float width) {
    char text[32]{};std::snprintf(text,sizeof(text),"%.1f",static_cast<double>(width));return text;
}
inline std::optional<float> Read(const std::string& ini,const char* station_key) {
    if(ini.empty() || !station_key || !*station_key)return {};
    char value[64]{};
    GetPrivateProfileStringA(kSection,station_key,"",value,sizeof(value),ini.c_str());
    return Parse(value);
}
// Persist and confirm by reading back; false leaves the caller's state as it was.
inline bool Save(const std::string& ini,const char* station_key,float width) {
    if(ini.empty() || !station_key || !*station_key || !Valid(width))return false;
    const auto text=Encode(width);
    return config::SetRaw(ini.c_str(),kSection,station_key,text.c_str()) && Read(ini,station_key)==Parse(text.c_str());
}
// Removing the key is the reset: the route default is not copied into the file.
inline bool Clear(const std::string& ini,const char* station_key) {
    if(ini.empty() || !station_key || !*station_key)return false;
    return config::Clear(ini.c_str(),kSection,station_key) && !Read(ini,station_key);
}
inline float Effective(std::optional<float> override_width,float route_default) {
    const float width=override_width.value_or(route_default);
    return std::clamp(std::isfinite(width)?width:route_default,kMin,kMax);
}
// One editor step, kept on the half-metre grid and inside the bounds.
inline float Step(float current,int steps) {
    const float next=std::round((current+kStep*static_cast<float>(steps))/kStep)*kStep;
    return std::clamp(std::isfinite(next)?next:current,kMin,kMax);
}
} // namespace horse_races::corridor_override
