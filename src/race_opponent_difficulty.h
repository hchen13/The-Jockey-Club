#pragma once
#include "kernel/config.h"
#include "race_host_profiles.h"
#include "horse_attributes.h"
#include <windows.h>
#include <charconv>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <algorithm>
#include <cmath>

namespace horse_races::opponent_difficulty {
inline int OwnHorsePercent(const std::string& ini,int fallback) {
    return std::clamp(static_cast<int>(GetPrivateProfileIntA("RacePrototype","GangOwnHorseChancePercent",
        fallback,ini.c_str())),20,100);
}
inline float SharedCornering(const std::string& ini) {
    char value[32]{};
    GetPrivateProfileStringA("RaceAI","CorneringEnvelopeMps2","14",value,sizeof(value),ini.c_str());
    try {
        std::size_t used=0;const float amount=std::stof(value,&used);
        if(used==std::string(value).size() && std::isfinite(amount) && amount>=5 && amount<=30)return amount;
    }catch(...){}
    return 14;
}
inline constexpr std::size_t kCount=hosts::kProfiles.size()+3;
struct Opponent {const char* key;const char* name;const char* chinese;const hosts::Profile* host=nullptr;};
inline Opponent At(std::size_t index) {
    if(index<hosts::kProfiles.size()) {
        const auto& h=hosts::kProfiles[index];return {h.key,h.name,h.chinese,&h};
    }
    constexpr std::array<Opponent,3> encounters{{
        {"EncounterRhodes","Rhodes woman","罗兹黑人女"},
        {"EncounterStrawberry","Strawberry elder","草莓镇老头"},
        {"EncounterMountain","Mountain rider","山地男"}}};
    return encounters.at(index-hosts::kProfiles.size());
}
inline std::optional<std::size_t> Find(std::string_view key) {
    for(std::size_t i=0;i<kCount;++i)if(key==At(i).key)return i;return {};
}
struct Range {
    bool enabled=false;int minimum=1,maximum=10; // Player-visible, inclusive 1..10, never raw ranks.
    bool Valid() const {return minimum>=1 && maximum<=10 && minimum<=maximum;}
    bool operator==(const Range&) const=default;
    void Edit(bool lower,int step) {
        enabled=true;
        if(lower){minimum=std::clamp(minimum+step,1,10);maximum=std::max(maximum,minimum);}
        else {maximum=std::clamp(maximum+step,1,10);minimum=std::min(minimum,maximum);}
    }
};
struct Rule {
    Range speed,acceleration;
    // How hard this opponent's riders corner: the lateral acceleration a bend
    // is taken at, m/s^2, 5..30. 0 leaves it to the shared [RaceAI] envelope.
    int cornering=0;
    bool Valid() const {return speed.Valid() && acceleration.Valid() && (cornering==0 || (cornering>=5 && cornering<=30));}
    bool operator==(const Rule&) const=default;
};
inline Rule Defaults(std::size_t index) {
    Rule rule;
    if(const auto* host=At(index).host) {
        rule.speed={false,host->minimum,host->maximum};
        if(host->accel_minimum>0)rule.acceleration={false,host->accel_minimum,host->accel_maximum};
    }
    return rule;
}
inline std::string Encode(const Rule& rule) {
    return std::to_string(rule.speed.enabled)+","+std::to_string(rule.speed.minimum)+","+
        std::to_string(rule.speed.maximum)+","+std::to_string(rule.acceleration.enabled)+","+
        std::to_string(rule.acceleration.minimum)+","+std::to_string(rule.acceleration.maximum)+","+
        std::to_string(rule.cornering);
}
// Six values as always written before 2026-09-17, or seven with the cornering
// envelope: a value saved by an earlier build still reads.
inline std::optional<Rule> Parse(std::string_view text) {
    std::array<int,7> values{};
    std::size_t count=0;
    for(std::size_t i=0;i<values.size();++i) {
        const auto comma=text.find(',');const auto token=text.substr(0,comma);
        if(token.empty() || token.size()>2 || token.find_first_not_of("0123456789")!=std::string_view::npos)return {};
        const auto parsed=std::from_chars(token.data(),token.data()+token.size(),values[i]);
        if(parsed.ec!=std::errc{} || parsed.ptr!=token.data()+token.size())return {};
        ++count;
        if(comma==std::string_view::npos)break;
        if(i+1==values.size())return {};   // An eighth value is nobody's.
        text.remove_prefix(comma+1);
    }
    if(count!=6 && count!=7)return {};
    if(values[0]>1 || values[3]>1)return {};
    Rule rule{{values[0]!=0,values[1],values[2]},{values[3]!=0,values[4],values[5]}};
    rule.cornering=count==7?values[6]:0;
    return rule.Valid()?std::optional<Rule>{rule}:std::nullopt;
}
inline Rule Read(const std::string& ini,std::size_t index) {
    if(ini.empty())return Defaults(index);
    char value[96]{};
    GetPrivateProfileStringA("OpponentDifficulty",At(index).key,"",value,sizeof(value),ini.c_str());
    return Parse(value).value_or(Defaults(index));
}
inline bool Save(const std::string& ini,std::size_t index,const Rule& rule) {
    if(ini.empty() || !rule.Valid())return false;
    // One key per character: runtime can never see min/max from different edits.
    const auto value=Encode(rule);
    return config::SetRaw(ini.c_str(),"OpponentDifficulty",At(index).key,value.c_str()) && Read(ini,index)==rule;
}
template<class Random> int Draw(const Range& range,Random random) {
    if(!range.Valid())return -1;
    const int value=range.minimum==range.maximum?range.minimum-1:random(range.minimum-1,range.maximum);
    return value>=range.minimum-1 && value<range.maximum?value:-1;
}
template<class Random> horse_attributes::Ranks Target(const Rule& rule,const hosts::Profile* host,
    horse_attributes::Ranks encounter_default,Random random,bool upper_bound=false) {
    if(!rule.Valid())return {};
    const auto pick=[&](const Range& range){return upper_bound?range.maximum-1:Draw(range,random);};
    const int speed=rule.speed.enabled?pick(rule.speed):
        host?pick(Range{false,host->minimum,host->maximum}):encounter_default.speed;
    const int acceleration=rule.acceleration.enabled?pick(rule.acceleration):
        host?(host->accel_minimum>0?pick(Range{false,host->accel_minimum,host->accel_maximum}):-1):
        encounter_default.acceleration;
    if(speed<0 || ((rule.acceleration.enabled || (host && host->accel_minimum>0)) && acceleration<0))return {};
    return {speed,acceleration}; // -1 acceleration means use the selected horse's natural value.
}
template<class Random> horse_attributes::Ranks ResolveNaturalAcceleration(horse_attributes::Ranks target,
    const hosts::Profile* host,horse_attributes::Ranks natural,Random random,bool upper_bound=false) {
    // Resolve exactly once from immutable model defaults, never from a leased
    // entity rank. Absolute F11/default acceleration bands pass through without
    // drawing or stacking a bonus. Zero is a valid inclusive lower addition.
    if(target.acceleration!=-1 || !natural.Valid())return target;
    const int low=host?host->natural_acceleration_bonus_min:0;
    const int high=host?host->natural_acceleration_bonus_max:0;
    if(low<0 || high>9 || low>high)return {};
    const int bonus=upper_bound || low==high?high:random(low,high+1);
    if(bonus<low || bonus>high)return {};
    target.acceleration=std::clamp(natural.acceleration+bonus,0,9);
    return target;
}
// Bookmaker WFA fields draw each character's normal band and relative bonus,
// not a common rank or the breed-challenge host ceiling. A displayed-five
// draw remains five; the unchanged burst gate simply makes it ineligible.
// Never edit the character's INI or normal duel/challenge difficulty here.
template<class Random> horse_attributes::Ranks BookmakerTarget(const Rule& rule,
    const hosts::Profile& host,horse_attributes::Ranks natural,Random random) {
    if(!rule.Valid() || !natural.Valid())return {};
    auto result=Target(rule,&host,natural,random);
    result=ResolveNaturalAcceleration(result,&host,natural,random);
    return result.Valid()?result:horse_attributes::Ranks{};
}
} // namespace horse_races::opponent_difficulty
