#pragma once
#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace horse_races::arthur_greeting {
enum class Time {Any, Morning, Day, Evening, Night};
struct Clip {const char* id;const char* context;int variation;Time time;};
inline constexpr const char* kVoice="ARTHUR"; // Registered voice, not an AWC split-bank name.
inline constexpr std::size_t kMaxAttempts=3;
inline constexpr bool Allowed(Time time,int hour) {
    if(time==Time::Any)return true;
    if(hour<0 || hour>=24)return false;
    switch(time) {
    case Time::Morning:return hour>=6 && hour<12;
    case Time::Day:return hour>=6 && hour<18;
    case Time::Evening:return hour>=18;
    case Time::Night:return hour>=18 || hour<6;
    default:return false;
    }
}
inline constexpr const char* Label(Time time) {
    switch(time) {
    case Time::Morning:return "morning";
    case Time::Day:return "day";
    case Time::Evening:return "evening";
    case Time::Night:return "night";
    default:return "any";
    }
}
struct Candidates {std::array<const Clip*,kMaxAttempts> clips{};std::size_t size=0;};
// Sample only approved, time-eligible exact variants. Three distinct attempts
// bound the native work if a context is unavailable; no random-variant fallback.
template<class Random> Candidates Choose(std::span<const Clip> clips,int hour,std::string_view last,Random random) {
    std::vector<const Clip*> eligible;
    for(const auto& clip:clips)if(Allowed(clip.time,hour))eligible.push_back(&clip);
    if(eligible.size()>1)std::erase_if(eligible,[&](const auto* clip){return clip->id==last;});
    Candidates result;
    while(!eligible.empty() && result.size<kMaxAttempts) {
        const auto index=static_cast<std::size_t>(std::clamp(random(0,static_cast<int>(eligible.size())),0,static_cast<int>(eligible.size()-1)));
        result.clips[result.size++]=eligible[index];
        eligible[index]=eligible.back();eligible.pop_back();
    }
    return result;
}
} // namespace horse_races::arthur_greeting
