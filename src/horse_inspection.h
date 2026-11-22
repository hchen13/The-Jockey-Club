#pragma once
#include "game_natives.h"
#include "game_clock.h"
#include "horse_attributes.h"
#include "horse_story_support.h"
#include "race_horse_compat.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>

namespace horse_inspection {
// The original right-click horse information card, not the stable/pause menu.
// Source: 1436 short_update.c funcs 963, 250 and 251. Never modify that script's
// globals or call SET_SHOW_INFO_CARD repeatedly against its own input state.
inline constexpr Hash kHorseInfoEntry=static_cast<Hash>(-649639953);
inline constexpr Hash kHiddenHudComponent=static_cast<Hash>(-972988753);
inline Hash App() { return MISC::GET_HASH_KEY("SHOP_BROWSING"); }
inline bool Valid(Any entry) {
    return entry && entry<=std::numeric_limits<Hash>::max() &&
        DATABINDING::DATABINDING_IS_ENTRY_VALID(static_cast<Hash>(entry));
}

class Card {
    using Clock=horse_time::GameClock;
    Any root_=0, owner_tag_=0;
    race_horses::Identity horse_{};
    const story_horses::Labels* labels_=nullptr;
    bool app_owned_=false, closing_=false, close_requested_=false, hud_hidden_=false;
    Hash activity_=0;
    int token_=0;
    inline static unsigned serial_=0;
    Clock::time_point opened_{}, next_refresh_{};
    std::string last_open_reason_,fill_failure_;

    bool OwnsRoot() const {
        return Valid(root_) && DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("InfoBox")==root_ &&
            Valid(owner_tag_) && DATABINDING::DATABINDING_READ_INT(owner_tag_)==token_;
    }
    bool AppActivityChanged() const {
        if (!app_owned_ || !activity_) return false;
        const Hash current=UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App());
        return current && current!=activity_;
    }
    static Any Child(Any parent,const char* name) {
        return DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(parent),name);
    }
    static float Unit(float value) { return std::isfinite(value)?std::clamp(value,0.0F,1.0F):0.0F; }
    static const char* Handling(int rank) {
        if(rank<0 || rank>9)return "";
        return rank<=1?"HORSE_HANDLING_HEAVY":rank<=3?"HORSE_HANDLING_STANDARD":
            rank<=5?"HORSE_HANDLING_RACE":"HORSE_HANDLING_ELITE";
    }
    bool Fill() {
        using namespace DATABINDING;
        fill_failure_.clear();
        const auto data=horse_attributes::Read(horse_.horse);
        if (data.model!=horse_.model || !data.current.Valid() || !labels_) {
            fill_failure_="horse identity/current rank invalid model="+std::to_string(data.model)+
                " current="+horse_attributes::Pair(data.current);return false;
        }
        // A freshly spawned horse may expose its current ranks before the
        // instance base ranks are populated. The model default is still the
        // authoritative unmodified breed value; never reject the whole card.
        const auto base=data.entity_base.Valid()?data.entity_base:data.model_default;
        if(!base.Valid()){
            fill_failure_="base and model ranks unavailable base="+horse_attributes::Pair(data.entity_base)+
                " model="+horse_attributes::Pair(data.model_default);return false;
        }
        // Native text results may share a scratch buffer. Copy each result
        // before the next lookup, otherwise the coat can overwrite the breed.
        const char* compound_text=HUD::_GET_LABEL_TEXT_2(labels_->breed);
        const std::string compound=compound_text?compound_text:"";
        if (compound.empty()) {fill_failure_="breed text empty";return false;}
        const char* coat_text=HUD::_GET_LABEL_TEXT_2(labels_->coat);
        const std::string coat=coat_text?coat_text:"";
        const std::string breed=story_horses::BreedOnlyText(compound,coat);
        if(breed.empty()){fill_failure_="breed-only text empty";return false;}
        bool ok=true;
        const auto added=[&](const std::string& name,Any entry){
            if(!Valid(entry)){if(fill_failure_.empty())fill_failure_="binding "+name+" invalid";ok=false;}
        };
        added("isVisible",_DATABINDING_ADD_DATA_BOOL(root_,"isVisible",true));
        added("showHorseStats",_DATABINDING_ADD_DATA_BOOL(root_,"showHorseStats",true));
        added("itemLabel",_DATABINDING_ADD_DATA_STRING(root_,"itemLabel",breed.c_str()));
        added("itemDescription",_DATABINDING_ADD_DATA_HASH(root_,"itemDescription",
            COMPENDIUM::COMPENDIUM_GET_SHORT_DESCRIPTION_FROM_PED(horse_.horse)));
        added("horseName",_DATABINDING_ADD_DATA_STRING(root_,"horseName",breed.c_str()));
        added("HorseCoat",_DATABINDING_ADD_DATA_STRING(root_,"HorseCoat",labels_->coat));
        added("HorseHandling",_DATABINDING_ADD_DATA_STRING(root_,"HorseHandling",
            Handling(ATTRIBUTE::GET_ATTRIBUTE_RANK(horse_.horse,4))));
        added("itemTipText",_DATABINDING_ADD_DATA_HASH(root_,"itemTipText",0)); // Not the player's main/temporary horse.
        const auto bar=[&](const std::string& name,int base,int current,int potential) {
            const auto bind=[&](const char* suffix,int value){const auto field=name+suffix;
                added(field,_DATABINDING_ADD_DATA_INT(root_,field.c_str(),value));};
            bind("Value",base);bind("MinValue",0);bind("MaxValue",10);
            bind("EquipmentValue",current);bind("EquipmentMinValue",0);bind("EquipmentMaxValue",10);
            bind("CapacityValue",std::max(current,std::min(10,base+potential)));
            bind("CapacityMinValue",0);bind("CapacityMaxValue",10);
        };
        bar("HorseSpeed",horse_attributes::DisplayLevel(base.speed),
            horse_attributes::DisplayLevel(data.current.speed),3);
        bar("HorseAcc",horse_attributes::DisplayLevel(base.acceleration),
            horse_attributes::DisplayLevel(data.current.acceleration),2);

        const Any rpg=Child(root_,"horseRpgStats");
        if (!Valid(rpg)) {fill_failure_="horseRpgStats container invalid";return false;}
        added("horseRpgStats.visible",_DATABINDING_ADD_DATA_BOOL(rpg,"visible",true));
        added("horseRpgStats.showHorseStats",_DATABINDING_ADD_DATA_BOOL(rpg,"showHorseStats",true));
        for (int core:{0,1}) {
            const Any ring=Child(rpg,core==0?"health":"stamina");
            if (!Valid(ring)) {fill_failure_=core==0?"health container invalid":"stamina container invalid";return false;}
            const int max_health=core==0?ENTITY::GET_ENTITY_MAX_HEALTH(horse_.horse,false):1;
            const float fraction=core==0?(max_health>0?static_cast<float>(ENTITY::GET_ENTITY_HEALTH(horse_.horse))/
                static_cast<float>(max_health):0.0F):PED::_GET_PED_STAMINA_NORMALIZED(horse_.horse);
            added(core==0?"health.unlockedMaxAsState":"stamina.unlockedMaxAsState",_DATABINDING_ADD_DATA_INT(ring,"unlockedMaxAsState",
                std::clamp(ATTRIBUTE::GET_ATTRIBUTE_RANK(horse_.horse,core),0,10)));
            added(core==0?"health.normalizedValue":"stamina.normalizedValue",_DATABINDING_ADD_DATA_FLOAT(ring,"normalizedValue",Unit(fraction)));
            added(core==0?"health.normalizedUnlockedMax":"stamina.normalizedUnlockedMax",_DATABINDING_ADD_DATA_FLOAT(ring,"normalizedUnlockedMax",Unit(fraction)));
            const Any inner=Child(ring,core==0?"healthCore":"staminaCore");
            if (!Valid(inner)) {fill_failure_=core==0?"healthCore container invalid":"staminaCore container invalid";return false;}
            const Any status=Child(inner,"status");
            if (!Valid(status)) {fill_failure_="core status container invalid";return false;}
            const int value=std::clamp(ATTRIBUTE::_GET_ATTRIBUTE_CORE_VALUE(horse_.horse,core),0,100);
            added(core==0?"healthCore.state":"staminaCore.state",_DATABINDING_ADD_DATA_INT(status,"state",static_cast<int>(std::lround(value*0.15F))));
        }
        const Any bonding=Child(rpg,"bonding");
        if (!Valid(bonding)) {fill_failure_="bonding container invalid";return false;}
        const int rank=std::clamp(ATTRIBUTE::GET_ATTRIBUTE_RANK(horse_.horse,7),0,4);
        added("bonding.bondingRankTexture",_DATABINDING_ADD_DATA_HASH(bonding,"bondingRankTexture",
            MISC::GET_HASH_KEY(("BLIP_HORSE_TEMP_BONDING_"+std::to_string(rank)).c_str())));
        return ok;
    }
    void Drop(bool remove_root) {
        if (remove_root && Valid(root_)) DATABINDING::_DATABINDING_REMOVE_DATA_ENTRY(static_cast<Hash>(root_));
        if (hud_hidden_ && !UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App())) {
            HUD::_DISABLE_HUD_CONTEXT(kHiddenHudComponent);
            hud_hidden_=false;
        }
        root_=owner_tag_=0;horse_={};labels_=nullptr;
        activity_=0;
        app_owned_=closing_=close_requested_=false;
    }
public:
    bool Active() const { return root_ && app_owned_ && !closing_; }
    const char* LastCloseReason() const { return last_close_reason_; }
    const char* LastOpenReason() const { return last_open_reason_.c_str(); }
    const char* last_close_reason_="";
    bool Busy() const { return root_!=0 || hud_hidden_; }

    bool Open(race_horses::Identity horse,const story_horses::Labels* labels) {
        last_open_reason_="";
        if (Busy()) {last_open_reason_="previous horse card still closing";return false;}
        if (!horse.Valid() || !labels || MISC::GET_HASH_KEY(labels->model)!=horse.model) {
            last_open_reason_="horse identity or model label missing";return false;
        }
        if (!story_horses::TextReady(*labels,HUD::DOES_TEXT_LABEL_EXIST)) {
            last_open_reason_="breed or coat text label unavailable";return false;
        }
        // Do not take over the native player's card or any shop using this app.
        if (UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) || UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()) ||
            Valid(DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("InfoBox")) ||
            !UIAPPS::CAN_LAUNCH_UIAPP_BY_HASH_WITH_ENTRY(App(),kHorseInfoEntry)) {
            last_open_reason_="native info card busy";return false;
        }
        root_=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER_FROM_PATH("","InfoBox");
        if (!Valid(root_)) { root_=0;last_open_reason_="InfoBox root creation failed";return false; }
        token_=static_cast<int>(1U+(++serial_%0x7FFFFFFFU));
        owner_tag_=DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"hslRaceInspectOwner",token_);
        horse_=horse;labels_=labels;
        if (!Valid(owner_tag_)) {Drop(true);last_open_reason_="InfoBox owner binding invalid";return false;}
        if (!Fill()) {Drop(true);last_open_reason_="InfoBox data fill failed: "+fill_failure_;return false;}
        if (UIAPPS::LAUNCH_UIAPP_BY_HASH_WITH_ENTRY(App(),kHorseInfoEntry)!=0) {
            Drop(true);last_open_reason_="native info card launch failed";return false;
        }
        app_owned_=true;opened_=Clock::now();next_refresh_=opened_+std::chrono::milliseconds(250);
        if (UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App())) activity_=UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App());
        HUD::_ENABLE_HUD_CONTEXT(kHiddenHudComponent);hud_hidden_=true;
        AUDIO::PLAY_SOUND_FRONTEND("show_info","Study_Sounds",true,0);
        return true;
    }

    void Close() {
        if (!root_) return;
        if (!OwnsRoot() || AppActivityChanged()) { Drop(false);return; } // Never close a replacement shop/card.
        closing_=true;
        if (!close_requested_) {
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"isVisible",false);
            if (app_owned_) UIAPPS::_CLOSE_UIAPP_BY_HASH(App());
            close_requested_=true;
            if (hud_hidden_) AUDIO::PLAY_SOUND_FRONTEND("hide_info","Study_Sounds",true,0);
        }
        if (!UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) && !UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App())) Drop(true);
    }

    template<class Identify>
    void Tick(bool keep,Identify identify,Clock::time_point now=Clock::now()) {
        if (!root_) {
            if (hud_hidden_ && !UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App())) {
                HUD::_DISABLE_HUD_CONTEXT(kHiddenHudComponent);hud_hidden_=false;
            }
            return;
        }
        if (AppActivityChanged()) { last_close_reason_="the app's activity changed";Drop(false);return; }
        if (!activity_ && UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()))
            activity_=UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App());
        if (!keep || closing_ || identify(horse_.horse)!=horse_ ||
            ENTITY::IS_ENTITY_DEAD(horse_.horse)) {
            if(!closing_)last_close_reason_=!keep?"not kept":identify(horse_.horse)!=horse_?"the horse changed":"the horse died";
            Close();return;
        }
        if (!OwnsRoot()) { last_close_reason_="the root is somebody else's";Drop(false);return; }
        if (now-opened_>=std::chrono::seconds(2) && !UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App())) {
            last_close_reason_="the app never became active";Close();return;
        }
        if (now>=next_refresh_) {
            next_refresh_=now+std::chrono::milliseconds(250);
            if (!Fill()) { last_close_reason_="the card could not be filled";Close(); }
        }
    }
};
}
