#pragma once

#include "game_natives.h"
#include "race_localization.h"
#include "race_bookmaker_venues.h"
#include "race_map_textures.h"

namespace horse_races::bookmaker_map {
inline constexpr Hash kStyle=0xD792CF71;          // BLIP_STYLE_MINIGAME
inline constexpr Hash kFallbackSprite=0xD99F0C2B; // BLIP_AMBIENT_HORSE
inline constexpr Hash kRaceSprite=564457427;     // BLIP_MP_GAME_RACE_HORSE
inline constexpr Hash kForSale=0xDAA61911;       // BLIP_MODIFIER_FOR_SALE

inline void Name(Blip marker){
    if(!marker)return;
    // Like the bounty/stakes markers, every venue uses the exact same legend
    // name and icon. The native map cycles these physical sites with left/right.
    // Venue titles belong in the betting/results UI, not the map legend.
    const auto label=i18n::Text("Race Betting","赛马投注");
    const auto text=MISC::VAR_STRING(10,"LITERAL_STRING",label);
    if(text)MAP::_SET_BLIP_NAME(marker,text);
}

inline Blip Create(const Vector3& point,bool fallback){
    const Blip marker=MAP::BLIP_ADD_FOR_COORDS(kStyle,point);
    if(marker){
        MAP::SET_BLIP_SPRITE(marker,fallback?kFallbackSprite:kRaceSprite,true);
        MAP::BLIP_ADD_MODIFIER(marker,kForSale);
        Name(marker);
    }
    return marker;
}

class Markers {
    std::array<Blip,bookmaking::kVenues.size()> markers_{};
    Hash sprite_=0;
    bool checked_mp_=false,has_mp_=false;
    double valid_since_=-1;
    int language_=-1;

    void Remove(){
        for(auto& marker:markers_){
            if(marker && MAP::DOES_BLIP_EXIST(marker))MAP::REMOVE_BLIP(&marker);
            marker=0;
        }
        sprite_=0;
    }
public:
    Markers()=default;
    Markers(const Markers&)=delete;
    Markers& operator=(const Markers&)=delete;
    const auto& Handles() const{return markers_;}
    Hash Sprite() const{return sprite_;}
    void Reset(){
        // Remove both the base glyph and dollar modifier before unpinning.
        Remove();
        map_textures::Release(map_textures::Dictionary::Multiplayer,map_textures::Owner::BettingMarkers);
        map_textures::Release(map_textures::Dictionary::Story,map_textures::Owner::BettingMarkers);
        checked_mp_=has_mp_=false;valid_since_=-1;language_=-1;
    }
    void Tick(double now){
        using map_textures::Dictionary;
        using map_textures::Owner;
        map_textures::Acquire(Dictionary::Story,Owner::BettingMarkers);
        if(!checked_mp_){
            has_mp_=TXD::DOES_STREAMED_TEXTURE_DICT_EXIST(map_textures::Name(Dictionary::Multiplayer));
            checked_mp_=true;
        }
        if(has_mp_)map_textures::Acquire(Dictionary::Multiplayer,Owner::BettingMarkers);
        const bool story_ready=map_textures::HorseIconReady(Dictionary::Story);
        const bool mp_ready=has_mp_ && map_textures::HorseIconReady(Dictionary::Multiplayer);
        if(!mp_ready)valid_since_=-1;
        else if(valid_since_<0 || now<valid_since_)valid_since_=now;
        // Give the MP glyph a render-frame boundary before selecting it. A
        // fallback must also have its actual glyph, not just a loaded TXD.
        const Hash desired=mp_ready && now-valid_since_>=0.1?kRaceSprite:
            (story_ready?kFallbackSprite:0);
        if(!desired){Remove();return;}
        for(std::size_t i=0;i<markers_.size();++i){
            auto& marker=markers_[i];
            if(marker && !MAP::DOES_BLIP_EXIST(marker))marker=0;
            if(!marker){
                const auto& pose=bookmaking::kVenues[i].clerk;
                marker=Create({pose.x,pose.y,pose.z},desired==kFallbackSprite);
            }else if(sprite_!=desired){
                MAP::SET_BLIP_SPRITE(marker,desired,true);
                Name(marker); // A sprite change can reset the native label.
            }else if(language_!=i18n::language)Name(marker);
        }
        sprite_=desired;language_=i18n::language;
    }
};
} // namespace horse_races::bookmaker_map
