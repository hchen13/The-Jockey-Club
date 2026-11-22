#pragma once
#include "horse_inspection.h"
#include "race_localization.h"
#include <iterator>
#include <utility>
#include <vector>

namespace horse_races::inspection {
struct Subject {
    race_horses::Identity rider{},horse{};
    const story_horses::Labels* labels=nullptr;
    std::string name;
    Prompt primary=0;
    bool ready=true;
};
class Interactions {
    struct Item {
        Subject subject; Prompt prompt=0;
        bool registered=false,configured=false,group_bound=false;
        int group=0;
        std::string label,name;
    };
    std::vector<Item> items_;
    race_horses::Identity rider_{},horse_{};
    bool owns_=false,was_pressed_=false;
    unsigned open_attempts_=0;
    bool last_opened_=false;
    std::string last_close_reason_;
    static bool Same(const Subject& a,const Subject& b) { return a.rider==b.rider && a.horse==b.horse; }
    static void Label(Item& item,bool hide) {
        if(!item.prompt || !HUD::_UI_PROMPT_IS_VALID(item.prompt))return;
        const std::string text=hide?i18n::Text("Hide Horse Info","收起马匹属性"):
            i18n::Text("Horse Info","查看马匹属性");
        if(item.label==text)return;
        HUD::_UI_PROMPT_SET_TEXT(item.prompt,i18n::Literal(text));item.label=text;
    }
    static void Bind(Item& item,Entity target) {
        const int group=HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(target);
        if(item.group_bound && item.group==group)return;
        if(item.group_bound)HUD::_UI_PROMPT_REMOVE_GROUP(item.prompt,item.group);
        HUD::_UI_PROMPT_SET_GROUP(item.prompt,group,0);item.group=group;item.group_bound=true;
    }
    template<class Identify> static void Remove(Item& item,Identify identify) {
        if(item.registered && !item.subject.primary && identify(item.subject.rider.horse)==item.subject.rider)
            MISC::UNREGISTER_INTERACTION_LOCKON_PROMPT(item.subject.rider.horse);
        if(item.prompt && HUD::_UI_PROMPT_IS_VALID(item.prompt)) {
            if(item.group_bound)HUD::_UI_PROMPT_REMOVE_GROUP(item.prompt,item.group);
            HUD::_UI_PROMPT_DELETE(item.prompt);
        }
        item.group_bound=false;
    }
    static Prompt Create() {
        const Prompt p=HUD::_UI_PROMPT_REGISTER_BEGIN();
        if(!p)return 0;
        HUD::_UI_PROMPT_SET_CONTROL_ACTION(p,MISC::GET_HASH_KEY("INPUT_INTERACT_LOCKON_TARGET_INFO"));
        HUD::_UI_PROMPT_SET_TEXT(p,i18n::Literal(i18n::Text("Horse Info","查看马匹属性")));
        HUD::_UI_PROMPT_SET_PRIORITY(p,0);HUD::_UI_PROMPT_SET_TRANSPORT_MODE(p,0);
        HUD::_UI_PROMPT_SET_ATTRIBUTE(p,18,true);HUD::_UI_PROMPT_SET_STANDARD_MODE(p,false);
        HUD::_UI_PROMPT_REGISTER_END(p);
        if(!HUD::_UI_PROMPT_IS_VALID(p))return 0;
        HUD::_UI_PROMPT_SET_VISIBLE(p,false);HUD::_UI_PROMPT_SET_ENABLED(p,false);return p;
    }
    static void Register(Item& item) {
        const Ped rider=item.subject.rider.horse;
        PED::SET_PED_CAN_BE_TARGETTED(rider,true);
        if(!item.configured) {
            for(const auto [flag,value]:{std::pair{130,true},{178,false},{297,true},{301,false},
                    {315,true},{317,false},{331,false},{493,false}})
                PED::SET_PED_CONFIG_FLAG(rider,flag,value);
            item.configured=true;
        }
        item.registered=MISC::REGISTER_INTERACTION_LOCKON_PROMPT(rider,"",15.F,15.F,32776,
            0.F,0.F,item.prompt,false,-1);
    }
    template<class Identify> static bool Valid(const Subject& s,Identify identify) {
        return s.rider.Valid() && s.horse.Valid() && identify(s.rider.horse)==s.rider &&
            identify(s.horse.horse)==s.horse && !ENTITY::IS_ENTITY_DEAD(s.rider.horse) &&
            !ENTITY::IS_ENTITY_DEAD(s.horse.horse);
    }
    template<class Identify> static bool Ready(const Subject& s,Ped player,Identify identify) {
        if(!s.ready || !s.labels || !Valid(s,identify))return false;
        const auto p=ENTITY::GET_ENTITY_COORDS(player,true,false);
        const auto h=ENTITY::GET_ENTITY_COORDS(s.horse.horse,true,false);
        const float x=p.x-h.x,y=p.y-h.y,z=p.z-h.z;
        const float distance=x*x+y*y+z*z;
        return std::isfinite(distance) && distance<=225.F;
    }
public:
    template<class Identify> void Sync(const std::vector<Subject>& subjects,Identify identify) {
        for(auto it=items_.begin();it!=items_.end();) {
            const auto found=std::find_if(subjects.begin(),subjects.end(),[&](const Subject& s){return Same(s,it->subject) && Valid(s,identify);});
            if(found==subjects.end()){Remove(*it,identify);it=items_.erase(it);}else ++it;
        }
        for(const auto& s:subjects) {
            if(!Valid(s,identify))continue;
            auto it=std::find_if(items_.begin(),items_.end(),[&](const Item& i){return Same(s,i.subject);});
            if(it==items_.end()) {
                const Prompt prompt=Create();if(!prompt)continue;
                items_.push_back({s,prompt,false});it=std::prev(items_.end());
            } else if(!HUD::_UI_PROMPT_IS_VALID(it->prompt)) {
                // The engine can invalidate a prompt independently of its actor.
                Remove(*it,identify);it->prompt=Create();it->registered=false;it->label.clear();
                if(!it->prompt)continue;
            }
            it->subject=s;
            if(!it->group_bound)Bind(*it,s.rider.horse);
            if(s.primary)it->registered=false; // The external primary now owns interaction registration.
            else if(!it->registered)Register(*it);
            if(it->name!=s.name) {
                PED::_SET_PED_PROMPT_NAME(s.rider.horse,i18n::Literal(s.name));it->name=s.name;
            }
        }
    }
    template<class Identify> void Tick(horse_inspection::Card& card,Ped player,bool allowed,Identify identify) {
        const bool aim=PAD::IS_CONTROL_PRESSED(0,MISC::GET_HASH_KEY("INPUT_AIM")) ||
            PAD::IS_DISABLED_CONTROL_PRESSED(0,MISC::GET_HASH_KEY("INPUT_AIM"));
        bool pressed=PAD::IS_CONTROL_PRESSED(0,MISC::GET_HASH_KEY("INPUT_INTERACT_LOCKON_TARGET_INFO")) ||
            PAD::IS_DISABLED_CONTROL_PRESSED(0,MISC::GET_HASH_KEY("INPUT_INTERACT_LOCKON_TARGET_INFO"));
        Entity target=0;
        const bool targeted=PLAYER::GET_PLAYER_TARGET_ENTITY(PLAYER::PLAYER_ID(),&target) && target;
        const bool player_ready=allowed && player && ENTITY::DOES_ENTITY_EXIST(player) &&
            !ENTITY::IS_ENTITY_DEAD(player) && !HUD::IS_PAUSE_MENU_ACTIVE() && !NETWORK::NETWORK_IS_SESSION_ACTIVE();
        Item* selected=nullptr;Item* owner=nullptr;bool owner_ready=false,selected_ready=false;
        for(auto& item:items_) {
            const auto& s=item.subject;
            if(!item.prompt || !HUD::_UI_PROMPT_IS_VALID(item.prompt))continue;
            const bool matching=targeted && (target==s.rider.horse || target==s.horse.horse);
            if(matching)selected=&item;
            if(owns_ && rider_==s.rider && horse_==s.horse)owner=&item;
            const bool is_owner=owns_ && rider_==s.rider && horse_==s.horse;
            const bool ready=player_ready && (matching || is_owner) && Ready(s,player,identify);
            if(is_owner)owner_ready=ready;
            if(matching)selected_ready=ready;
            // PRF_DisableAutoGreets (184), also used by original
            // beat_horse_race func_394. Suppress chatter during explicit focus.
            if(ready && aim && matching)PED::SET_PED_RESET_FLAG(s.rider.horse,184,true);
            HUD::_UI_PROMPT_SET_VISIBLE(item.prompt,player_ready);
            HUD::_UI_PROMPT_SET_ENABLED(item.prompt,ready && aim && matching);
            Bind(item,matching?target:s.rider.horse);
            pressed=pressed || HUD::_UI_PROMPT_IS_PRESSED(item.prompt);
        }
        const bool edge=pressed && !was_pressed_;was_pressed_=pressed;
        if(owns_) {
            const bool keep=player_ready && aim && owner && owner_ready &&
                (!targeted || selected==owner);
            if(card.Active() && !keep)last_close_reason_=!player_ready?"inspection unavailable":!aim?
                "aim released":!owner?"subject removed":targeted && selected!=owner?
                "target changed":"subject invalid or out of range";
            if(keep && edge){last_close_reason_="info action";card.Close();}
            card.Tick(keep,identify);
            if(!card.Busy()) {
                if(last_close_reason_.empty())last_close_reason_=card.LastCloseReason();
                owns_=false;rider_={};horse_={};
            }
        } else if(edge && aim && player_ready && selected && selected_ready && !card.Busy()) {
            ++open_attempts_;last_opened_=card.Open(selected->subject.horse,selected->subject.labels);
            if(last_opened_){owns_=true;rider_=selected->subject.rider;horse_=selected->subject.horse;last_close_reason_.clear();}
        }
        for(auto& item:items_)Label(item,owns_ && card.Active() && rider_==item.subject.rider && horse_==item.subject.horse);
    }
    Prompt PromptFor(Ped rider) const { for(const auto& i:items_)if(i.subject.rider.horse==rider)return i.prompt;return 0; }
    bool OwnsCard() const { return owns_; }
    unsigned OpenAttempts() const { return open_attempts_; }
    bool LastOpened() const { return last_opened_; }
    race_horses::Identity OwnerHorse() const { return horse_; }
    const char* LastCloseReason() const { return last_close_reason_.c_str(); }
    void Close(horse_inspection::Card& card) { if(owns_){last_close_reason_="controller closed";card.Close();} }
};
}
