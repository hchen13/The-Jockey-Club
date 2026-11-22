#pragma once
#include "race_character_policy.h"
#include "rider_outfits.h"
#include <array>
#include <span>
#include <string_view>

namespace horse_races::hosts {
// The horses a host owns: their own, recognisable mount, and its epilogue coat where there is one.
// Nobody else rides these - they are the horse the player knows that character by.
inline constexpr std::array<std::string_view,2> kSadieMounts{"A_C_HORSE_GANG_SADIE","A_C_HORSE_GANG_SADIE_ENDLESSSUMMER"};
inline constexpr std::array<std::string_view,2> kCharlesMounts{"A_C_HORSE_GANG_CHARLES","A_C_HORSE_GANG_CHARLES_ENDLESSSUMMER"};
inline constexpr std::array<std::string_view,2> kUncleMounts{"A_C_HORSE_GANG_UNCLE","A_C_HORSE_GANG_UNCLE_ENDLESSSUMMER"};
inline constexpr std::array<std::string_view,1> kKarenMounts{"A_C_HORSE_GANG_KAREN"};
// Gwydion. The model name is the game's own spelling.
inline constexpr std::array<std::string_view,1> kTrelawnyMounts{"A_C_HORSE_GANG_TRELAWNEY"};
inline constexpr std::array<std::string_view,1> kArthurMounts{"A_C_HORSE_HUNGARIANHALFBRED_LIVERCHESTNUT"};
inline constexpr std::array<std::string_view,1> kJohnMounts{"A_C_HORSE_JOHN_ENDLESSSUMMER"};
// Boaz: the native gang-specific Grey Overo American Paint (pause_menu.c).
inline constexpr std::array<std::string_view,1> kJavierMounts{"A_C_HORSE_GANG_JAVIER"};
// Brown Jack: native gang-specific Brown Roan Ardennes (pause_menu.c).
inline constexpr std::array<std::string_view,1> kBillMounts{"A_C_HORSE_GANG_BILL"};

struct Profile {
    characters::Id id;const char* key;const char* name;const char* chinese;
    const char* model;const char* voice;bool female;int minimum,maximum;
    // Displayed 1..10 absolute acceleration band. 0 selects the drawn
    // horse's natural value plus the relative addition declared below.
    int accel_minimum=0,accel_maximum=0;
    // Their own horses. Mary-Beth, Pearson and Rains Fall have none: they ride a coat of the breed.
    std::span<const std::string_view> own_mounts{};
    // Which outfits they may be seen in. The ini key is the truth - the user edits it after looking
    // through the showcase - and the fallback is what ships.
    const char* outfits_key=nullptr;
    const char* outfits_fallback=nullptr;
    // Inclusive level additions to the selected horse's natural acceleration.
    // Duels/WFA/jockeys draw independently; a breed-challenge host takes max.
    // An explicit F11 absolute band never stacks either addition on top.
    int natural_acceleration_bonus_min=0,natural_acceleration_bonus_max=0;
};
inline constexpr std::array<Profile,13> kProfiles{{
    {characters::Id::MaryBeth,"MaryBeth","Mary-Beth","玛丽贝思","CS_MARYBETH","MARYBETH",true,5,9,0,0,
     {},"MaryBethAllowedOutfits",rider_outfits::kMaryBethApproved},
    {characters::Id::Sadie,"Sadie","Sadie Adler","莎迪·阿德勒","CS_MRSADLER","SADIE_ADLER",true,8,10,0,0,
     kSadieMounts,"SadieAllowedOutfits","2,3,4,5,6,8,9,10,11,12,17,18,19,20",0,4},
    {characters::Id::Charles,"Charles","Charles Smith","查尔斯·史密斯","CS_CHARLESSMITH","CHARLES_SMITH",false,7,10,0,0,
     kCharlesMounts,"CharlesAllowedOutfits",
     "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,24,25,26,27,28,29,31,32,33",0,2},
    {characters::Id::Uncle,"Uncle","Uncle","大叔","CS_UNCLE","UNCLE",false,5,9,0,0,
     kUncleMounts,"UncleAllowedOutfits","0,1,2,5,6,8",0,2},
    {characters::Id::Karen,"Karen","Karen Jones","凯伦·琼斯","CS_KAREN","KAREN",true,7,10,0,0,
     kKarenMounts,"KarenAllowedOutfits","1,2,3,4,12,13"},
    {characters::Id::Pearson,"Pearson","Simon Pearson","西蒙·皮尔逊","CS_MRPEARSON","PEARSON",false,5,9,0,0,
     {},"PearsonAllowedOutfits","0,1,2,6,7",0,1},
    {characters::Id::Arthur,"Arthur","Arthur Morgan","亚瑟·摩根","PLAYER_ZERO","ARTHUR",false,10,10,10,10,
     kArthurMounts},
    {characters::Id::John,"John","John Marston","约翰·马斯顿","PLAYER_THREE","JOHN",false,6,10,0,0,
     kJohnMounts,nullptr,nullptr,0,3},
    // All ten looked at in the showcase (the user, 2026-09-21): number five is out.
    {characters::Id::Trelawny,"Trelawny","Josiah Trelawny","乔赛亚·特里劳尼","CS_JOSIAHTRELAWNY","TRELAWNY",false,7,9,0,0,
     kTrelawnyMounts,"TrelawnyAllowedOutfits","1,2,3,6,7,8,9",0,3},
    // His first outfit only until the rest have been looked at.
    {characters::Id::RainsFall,"RainsFall","Rains Fall","落雨","CS_RAINSFALL","RAINS_FALL",false,7,10,0,0,
     {},"RainsFallAllowedOutfits","0",0,1},
    // F9 exposes every stock outfit; the user will curate the approved pool.
    // An empty INI value means all runtime wardrobe slots in this review build.
    {characters::Id::Tilly,"Tilly","Tilly Jackson","蒂莉·杰克逊","CS_TILLY","TILLY",true,5,9,0,0,
     {},"TillyAllowedOutfits","",0,4},
    // Native 1436 homerobjavier_invite.c func_516: CS_JAVIERESCUELLA.
    // JAVIER is the voice ID, not an abbreviated character model name.
    {characters::Id::Javier,"Javier","Javier Escuella","哈维尔·埃斯奎拉","CS_JAVIERESCUELLA","JAVIER",false,9,10,0,0,
     kJavierMounts,"JavierAllowedOutfits","0",0,5},
    {characters::Id::Bill,"Bill","Bill Williamson","比尔·威廉姆森","CS_BILLWILLIAMSON","BILL",false,8,9,0,0,
     kBillMounts,"BillAllowedOutfits","0",0,5},
}};
inline const Profile* Find(std::string_view key) {
    for(const auto& p:kProfiles)if(key==p.key)return &p;return nullptr;
}
inline const Profile* Find(characters::Id id) {
    for(const auto& p:kProfiles)if(id==p.id)return &p;return nullptr;
}
inline bool BurstDisplayEligible(characters::Id id) {
    return id==characters::Id::Sadie || id==characters::Id::Arthur;
}
inline bool AllowedPlayer(const Profile& profile,unsigned player_model,bool explicit_dialogue_test=false) {
    if(explicit_dialogue_test)return true; // F11 host + listener simulation only; never story unlocks.
    // Never show a second copy of the current protagonist. Arthur is a private
    // epilogue surprise/test host, not a resurrected persistent story companion.
    if(profile.id==characters::Id::Arthur)return player_model==0x00B69710;
    if(profile.id==characters::Id::John)return player_model==0x0D7114C9;
    return true; // Non-protagonist hosts also support a player's custom SP model.
}
}
