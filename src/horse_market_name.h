#pragma once
#include "race_breeds.h"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace horse_races::horse_market {
namespace name_detail {
inline constexpr char Lower(char c){return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;}
inline constexpr std::uint32_t ModelHash(std::string_view model){
    std::uint32_t hash=0;
    for(char c:model){hash+=static_cast<unsigned char>(Lower(c));hash+=hash<<10;hash^=hash>>6;}
    hash+=hash<<3;hash^=hash>>11;hash+=hash<<15;return hash;
}
inline bool SameBreed(std::string_view a,std::string_view b){
    const auto upper=[](std::string_view text){std::string result(text);for(char& c:result)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');return result;};
    const auto first=upper(a),second=upper(b);
    a=horse_identity::CanonicalBreed(first);b=horse_identity::CanonicalBreed(second);
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](char x,char y){return Lower(x)==Lower(y);});
}
inline std::string BreedText(std::string_view model,bool chinese){
    const auto breed=horse_identity::Breed(model);
    for(const auto& known:field_race::kBreeds)
        if(SameBreed(breed,known.id))return std::string(chinese?known.name_zh:known.name);
    // Existing market catalog's ninth supported breed; use the full breed name.
    if(SameBreed(breed,"MISSOURIFOXTROTTER"))return chinese?"密苏里狐步马":"Missouri Fox Trotter";
    // All remaining stock/provider breed keys in fixed_horse_pool. Localized
    // game labels take precedence through DisplayHorseName's optional callback.
    constexpr std::pair<std::string_view,std::string_view> names[]{
        {"ANDALUSIAN","Andalusian"},{"ARDENNES","Ardennes"},{"BELGIAN","Belgian Draft"},
        {"BRETON","Breton"},{"CRIOLLO","Criollo"},{"DUTCHWARMBLOOD","Dutch Warmblood"},
        {"GYPSYCOB","Gypsy Cob"},{"HUNGARIANHALFBRED","Hungarian Halfbred"},
        {"KLADRUBER","Kladruber"},{"MORGAN","Morgan"},{"NOKOTA","Nokota"},
        {"NORFOLKROADSTER","Norfolk Roadster"},{"SHIRE","Shire"},
        {"SUFFOLKPUNCH","Suffolk Punch"},{"TURKOMAN","Turkoman"}};
    for(const auto& [key,name]:names)if(SameBreed(breed,key)){
        if(chinese&&key=="MORGAN")return "摩根马";
        if(chinese&&key=="NOKOTA")return "诺科塔马";
        return std::string(name);
    }
    // A provider may introduce another genuine breed. Keep its identity readable
    // instead of replacing a recognized model with generic Horse.
    std::string readable(breed);
    for(std::size_t i=1;i<readable.size();++i)readable[i]=Lower(readable[i]);
    std::replace(readable.begin(),readable.end(),'_',' ');
    return readable;
}
}
// Presentation only. A nonblank saved custom name is always literal, including
// a player deliberately naming their horse "Horse". Never rewrite identity data.
using BreedLocalizer=std::function<std::string(std::string_view model)>;
inline std::string DisplayHorseName(std::string_view custom_name,std::uint32_t model,bool chinese,
    const BreedLocalizer& localized_breed={}){
    if(custom_name.find_first_not_of(" \t\r\n\f\v")!=std::string_view::npos)return std::string(custom_name);
    const auto match=[&](std::string_view name)->std::string{
        if(name_detail::ModelHash(name)!=model)return {};
        if(localized_breed){auto name_text=localized_breed(name);if(!name_text.empty()&&name_text!="NULL")return name_text;}
        return name_detail::BreedText(name,chinese);
    };
    // Effective identities take precedence over stock aliases, as in the race roster.
    for(const auto& [name,identity]:horse_identity::effective){
        (void)identity;if(auto result=match(name);!result.empty())return result;
    }
    for(const auto& breed:field_race::kBreeds){
        for(auto name:breed.allowed)if(auto result=match(name);!result.empty())return result;
        for(auto name:breed.host_horses)if(auto result=match(name);!result.empty())return result;
    }
    for(const auto& labels:story_horses::kApproved)if(auto result=match(labels.model);!result.empty())return result;
    return chinese?"马匹":"Horse";
}
}
