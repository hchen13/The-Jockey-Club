#pragma once
#include "kernel/config.h"
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace horse_races::bgm::volume {
enum class Group {Venom,Credits,MountainHymn,CampSongs,Humming,Instruments,Count};
inline constexpr std::size_t kCount=static_cast<std::size_t>(Group::Count);
struct Spec {const char* key;const char* english;const char* chinese;int default_percent;int catalogue_percent=100;};
inline constexpr std::array<Spec,kCount> specs{{
    {"AmericanVenom","American Venom","American Venom",40,70},
    {"CruelCruelWorld","Cruel, Cruel World","Cruel, Cruel World",100},
    {"MountainHymn","Mountain Hymn","Mountain Hymn",100},
    {"CampSongs","Camp singing","营地演唱",100},
    {"Humming","Humming","日常哼唱",100},
    {"Instruments","Instruments","乐器伴奏",100},
}};
using Values=std::array<int,kCount>;
inline constexpr const char* section="RaceBgmVolume";
inline bool Valid(Group group){return static_cast<std::size_t>(group)<kCount;}
inline int Default(Group group){return Valid(group)?specs[static_cast<std::size_t>(group)].default_percent:100;}
inline std::optional<Group> For(std::string_view id) {
    if(id=="native:american-venom/stem-09")return Group::Venom;
    if(id=="native:cruel-cruel-world/ending")return Group::Credits;
    if(id=="native:mountain-hymn/ending")return Group::MountainHymn;
    if(id.starts_with("camp:01-Camp-Songs/"))return Group::CampSongs;
    if(id.starts_with("camp:02-Humming/"))return Group::Humming;
    if(id.starts_with("camp:03-Instruments/"))return Group::Instruments;
    return {};
}
inline int ReadPercent(const std::string& path,Group group) {
    if(!Valid(group))return 100;
    const auto& spec=specs[static_cast<std::size_t>(group)];
    return config::ReadInt(path,section,spec.key,spec.default_percent,0,100);
}
inline Values Read(const std::string& path) {
    Values result{};
    for(std::size_t i=0;i<kCount;++i)result[i]=ReadPercent(path,static_cast<Group>(i));
    return result;
}
inline bool Save(const std::string& path,Group group,int percent) {
    return Valid(group) && percent>=0 && percent<=100 &&
        config::SetRaw(path,section,specs[static_cast<std::size_t>(group)].key,percent);
}
// Percentages use the level before 0.28.87 as their reference: Venom's
// Venom's catalogue gain encodes 70%, independently of the user-facing default.
// Keep the reference fixed so changing the default cannot rescale saved values.
// Snapshot once at Play: menu changes cannot interrupt an active song/fade.
inline float Gain(float catalogue_db,Group group,int percent) {
    if(!Valid(group) || percent<0 || percent>100)percent=Default(group);
    if(percent==0)return -100.0F; // Native mixer treats levels below -99 dB as zero.
    const auto reference=Valid(group)?specs[static_cast<std::size_t>(group)].catalogue_percent:100;
    return catalogue_db+20.0F*std::log10(static_cast<float>(percent)/static_cast<float>(reference));
}
} // namespace horse_races::bgm::volume
