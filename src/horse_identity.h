#pragma once
#include "horse_story_support.h"
#include <array>
#include <map>
#include <string>
#include <string_view>

// Identity, not performance. Effective provider metadata can override these
// stock identities before a roster is selected. Keys never use translated text.
namespace horse_identity {
struct Identity { std::string breed,coat; };
inline std::map<std::string,Identity,std::less<>> effective;
// The compendium uses Horse_KentuckySaddler_Common, while ordinary model
// names and Uncle's named-mount identity use KENTUCKYSADDLE. These are the
// same breed, not a provider reclassification. Keep model and route IDs intact.
inline constexpr std::string_view CanonicalBreed(std::string_view breed) {
    return breed=="KENTUCKYSADDLER"?std::string_view("KENTUCKYSADDLE"):breed;
}
inline constexpr std::pair<std::string_view,std::string_view> named[]{
    {"GANG_TRELAWNEY","APPALOOSA"},{"GANG_SEAN","AMERICANSTANDARDBRED"},
    {"GANG_CHARLES","APPALOOSA"},{"GANG_CHARLES_ENDLESSSUMMER","NOKOTA"},
    {"JOHN_ENDLESSSUMMER","THOROUGHBRED"},{"GANG_DUTCH","ARABIAN"},
    {"GANG_UNCLE","KENTUCKYSADDLE"},{"GANG_UNCLE_ENDLESSSUMMER","APPALOOSA"},
    {"GANG_SADIE","TURKOMAN"},{"GANG_SADIE_ENDLESSSUMMER","MUSTANG"},
    {"GANG_KAREN","NOKOTA"},{"BUELL_WARVETS","DUTCHWARMBLOOD"},
    {"GANG_HOSEA","TURKOMAN"},{"GANG_JOHN","HUNGARIANHALFBRED"},
    {"GANG_KIERAN","TENNESSEEWALKER"},{"GANG_LENNY","MUSTANG"},
    {"GANG_MICAH","MISSOURIFOXTROTTER"},
    {"WINTER02_01","THOROUGHBRED"},{"EAGLEFLIES","AMERICANPAINT"},
    {"GANG_JAVIER","AMERICANPAINT"},{"GANG_BILL","ARDENNES"}};
inline std::string_view Suffix(std::string_view model) {
    if(!model.starts_with("A_C_HORSE_"))return {};
    model.remove_prefix(10);const auto cut=model.find('_');
    if(cut==std::string_view::npos)return {};
    model.remove_prefix(cut+1);
    if(model.ends_with("_PC"))model.remove_suffix(3);
    return model;
}
inline std::string DefaultCoat(std::string_view model) {
    if(model=="A_C_HORSE_WINTER02_01")return "COAT_SILVERBAY";
    if(const auto* labels=story_horses::FindForRace(model))return labels->coat;
    const auto suffix=Suffix(model);
    // Several breeds share a coat but Rockstar abbreviates its label. Use the
    // same token for an ordinary coat and its character-specific alias.
    for(const auto& labels:story_horses::kApproved)
        if(!suffix.empty() && Suffix(labels.model)==suffix)return labels.coat;
    if(suffix=="SABLECHAMPAGNE")return "COAT_SABLECHAMP";
    if(suffix=="LIGHTBUCKSKIN")return "COAT_LIGHTBUCKSKIN";
    return suffix.empty()?std::string(model):"COAT_"+std::string(suffix);
}
inline std::string_view StockBreed(std::string_view model) {
    if(!model.starts_with("A_C_HORSE_"))return {};
    const auto suffix=model.substr(10);
    for(const auto& [name,breed]:named)if(suffix==name)return CanonicalBreed(breed);
    const auto cut=suffix.find('_');
    if(cut==std::string_view::npos || suffix.starts_with("GANG_") || suffix.starts_with("MP_") ||
        suffix.starts_with("MURFREEBROOD_"))return {};
    return CanonicalBreed(suffix.substr(0,cut));
}
inline std::string_view Breed(std::string_view model) {
    const auto found=effective.find(model);
    return found==effective.end()?StockBreed(model):CanonicalBreed(found->second.breed);
}
inline std::string Coat(std::string_view model) {
    const auto found=effective.find(model);
    return found==effective.end()?DefaultCoat(model):found->second.coat;
}
inline std::string Key(std::string_view model) {
    return std::string(Breed(model))+"/"+Coat(model);
}
inline bool SameCoat(std::string_view a,std::string_view b) {return a==b || Key(a)==Key(b);}
}
