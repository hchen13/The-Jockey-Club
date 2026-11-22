#pragma once

#include "game_natives.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The game's shop_menu/generic_shop app, not a bitmap imitation of its panel.
// We own only our GenericShop datastore and only close the activity we opened.
namespace horse_races::bookmaker_shop {
using Clock=std::chrono::steady_clock;
inline Clock::time_point (*ClockNow)()=&Clock::now;
struct Row {
    std::string label;
    std::string detail;
    int speed=0,acceleration=0;
    Hash handling=0;
    std::string description;
};
enum class Action {Select,Back};
struct Event {Action action;std::size_t index=0;};
struct ResultPresentation {
    std::string odds,stake,payout,profit_label;
    int profit_cents=0;
};
class Catalog {
    Any root_=0,list_=0,entries_=0,scene_=0,owner_=0,tooltip_=0,market_rule_=0;
    Any collection_=0;
    int token_=0;
    Hash activity_=0;
    Hash title_hash_=0;
    Hash subtitle_hash_=0;
    std::vector<Row> rows_;
    bool stats_visible_=true;
    bool results_mode_=false;
    std::string result_back_label_="Leave Race";
    std::string footer_text_;
    std::string tooltip_text_;
    std::optional<ResultPresentation> result_presentation_;
    bool launched_=false,transitioned_=false,scene_filled_=false,scene_ready_event_=false,closing_=false;
    bool root_created_=false,owner_claimed_=false;
    int events_seen_=0,requests_seen_=0,items_filled_=0;
    Hash last_event_=0,last_param_=0;
    const char* last_error_="";
    Clock::time_point opened_{},close_requested_{},last_close_attempt_{};
    inline static unsigned serial_=0;
    static Hash App(){return MISC::GET_HASH_KEY("shop_menu");}
    static Hash Entry(){return MISC::GET_HASH_KEY("generic_shop");}
    Hash SceneTransition() const{return HashOf(results_mode_?"ITEM_LIST_DESCRIPTION":"MENU_LIST_HORSE_STATS");}
    // generic_shop_ui_flow (D6C97B0C) loads screen 2690A78C for the
    // MENU_LIST_HORSE_STATS transition. Its native Screen.ID is reused from
    // menu_list_weapon_stats; GET_UIAPP_CURRENT_ACTIVITY returns that ID,
    // not the transition key. The description screen's names happen to match.
    Hash SceneActivity() const{return HashOf(results_mode_?"item_list_description":"menu_list_weapon_stats");}
    static Hash Events(){return MISC::GET_HASH_KEY("generic_shop_ui_events");}
    static Hash HashOf(const char* text){return MISC::GET_HASH_KEY(text);}
    static void DrainStaleEvents(){
        for(int i=0;i<64 && UIEVENTS::EVENTS_UI_IS_PENDING(Events());++i)
            UIEVENTS::EVENTS_UI_POP_MESSAGE(Events());
    }
    static bool Valid(Any id){return id && id<=std::numeric_limits<Hash>::max() &&
        DATABINDING::DATABINDING_IS_ENTRY_VALID(static_cast<Hash>(id));}
    bool Owns() const {return Valid(root_) &&
        DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("GenericShop")==root_ &&
        Valid(owner_) && DATABINDING::DATABINDING_READ_INT(owner_)==token_;}
    // A screen change does not transfer the exclusive launch lease. Only a
    // foreign root or owner token proves another caller has taken over.
    bool ForeignRoot() const {
        const Any live=DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("GenericShop");
        if(!Valid(live))return false;
        return !root_created_ || live!=root_ ||
            (owner_claimed_ && Valid(owner_) && DATABINDING::DATABINDING_READ_INT(owner_)!=token_);
    }
    void Drop(bool remove){if(remove && root_created_ && Valid(root_) &&
        DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("GenericShop")==root_ &&
        (!Valid(owner_) || !owner_claimed_ || DATABINDING::DATABINDING_READ_INT(owner_)==token_))
        DATABINDING::_DATABINDING_REMOVE_DATA_ENTRY(static_cast<Hash>(root_));
        root_=list_=entries_=scene_=owner_=tooltip_=market_rule_=collection_=0;
        activity_=title_hash_=subtitle_hash_=0;rows_.clear();footer_text_.clear();tooltip_text_.clear();result_presentation_.reset();
        launched_=transitioned_=scene_filled_=scene_ready_event_=closing_=false;
        root_created_=owner_claimed_=false;events_seen_=requests_seen_=items_filled_=0;
        last_event_=last_param_=0;}
    void SetStats(std::size_t index){
        if(!stats_visible_ || !Valid(scene_) || index>=rows_.size())return;
        const auto& row=rows_[index];
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseSpeedValue",row.speed);
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseSpeedEquipmentValue",row.speed);
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseSpeedCapacityValue",row.speed);
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseAccValue",row.acceleration);
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseAccEquipmentValue",row.acceleration);
        DATABINDING::_DATABINDING_WRITE_DATA_INT_FROM_PARENT(scene_,"HorseAccCapacityValue",row.acceleration);
        DATABINDING::_DATABINDING_WRITE_DATA_HASH_STRING_FROM_PARENT(scene_,"HorseHandling",row.handling);
    }
    void SetFocus(std::size_t index){
        if(index>=rows_.size())return;
        SetStats(index);
        // The focused market rule occupies the native horse-stats area ABOVE
        // its splitter; the existing below-splitter tooltip remains empty.
        if(!results_mode_ && Valid(market_rule_))
            DATABINDING::_DATABINDING_WRITE_DATA_STRING(market_rule_,stats_visible_?"":rows_[index].description.c_str());
    }
    bool Fill(int start,int requested,Any collection){
        if(!Owns() || rows_.empty() || !Valid(list_))return false;
        DATABINDING::_DATABINDING_CLEAR_BINDING_ARRAY(static_cast<Hash>(list_));
        if(Valid(entries_))DATABINDING::_DATABINDING_REMOVE_DATA_ENTRY(static_cast<Hash>(entries_));
        entries_=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(root_),"ItemListEntries");
        if(!Valid(entries_))return false;
        const int count=static_cast<int>(rows_.size());
        start=std::clamp(start,0,count-1);requested=std::clamp(requested,0,count);
        for(int slot=0;slot<std::min(count,requested+1);++slot){
            const int i=(start+slot)%count;const auto& row=rows_[static_cast<std::size_t>(i)];
            const auto name="tjc_row_"+std::to_string(i);
            const Any item=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(entries_),name.c_str());
            if(!Valid(item))return false;
            DATABINDING::_DATABINDING_ADD_DATA_INT(item,"ItemIndex",i+1);
            DATABINDING::_DATABINDING_ADD_DATA_STRING(item,"uiItemID",name.c_str());
            DATABINDING::_DATABINDING_ADD_DATA_STRING(item,"uiItemType","TEXT");
            DATABINDING::_DATABINDING_ADD_DATA_HASH(item,"uiItemGsui",HashOf("GSUI_TEXT_LIST_ITEM"));
            DATABINDING::_DATABINDING_ADD_DATA_HASH(item,"uiItemLabel",0);
            DATABINDING::_DATABINDING_ADD_DATA_STRING(item,"uiItemRawText",row.label.c_str());
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(item,"itemEnabled",true);
            DATABINDING::_DATABINDING_ADD_DATA_HASH(item,"rightlabel",0);
            DATABINDING::_DATABINDING_ADD_DATA_STRING(item,"rightRawText",row.detail.c_str());
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(item,"rightLabelVisible",!row.detail.empty());
            DATABINDING::_DATABINDING_INSERT_UI_ITEM_TO_LIST_FROM_CONTEXT_HASH_ALIAS(list_,i,
                HashOf("GSUI_TEXT_LIST_ITEM"),item);
            if(collection && DATABINDING::_VIRTUAL_COLLECTION_EXISTS(collection))
                DATABINDING::_VIRTUAL_COLLECTION_ITEM_ADD(collection,i,HashOf("GSUI_TEXT_LIST_ITEM"),item);
            ++items_filled_;
        }
        return true;
    }
    bool BuildRoot(){
        if(root_ || !launched_ || !UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()))return false;
        if(Valid(DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("GenericShop"))){
            last_error_="GenericShop root already exists";return false;
        }
        root_=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER_FROM_PATH("","GenericShop");
        if(!Valid(root_)){last_error_="could not create GenericShop root";return false;}
        root_created_=true;
        token_=static_cast<int>(1U+(++serial_%0x7FFFFFFFU));
        owner_=DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"tjcBookmakerOwner",token_);
        owner_claimed_=Valid(owner_) && DATABINDING::DATABINDING_READ_INT(owner_)==token_;
        const char* title=results_mode_&&HUD::DOES_TEXT_LABEL_EXIST("RESULTS")?"RESULTS":
            HUD::DOES_TEXT_LABEL_EXIST("HORSE_RACE")?"HORSE_RACE":
            HUD::DOES_TEXT_LABEL_EXIST("RACE")?"RACE":"GUNSMITH";
        const Any title_entry=DATABINDING::_DATABINDING_ADD_DATA_HASH(root_,"Title",
            title_hash_?title_hash_:HashOf(title));
        DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"DefaultCategoryIndex",0);
        DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"CategoryCount",1);
        DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"ItemListEntryIndex",0);
        DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"PageFilterCurrentPageIndex",0);
        // The butcher's category subtitle (e.g. MEAT) is this native page
        // label hash, separate from the existing GenericShop.Title.
        DATABINDING::_DATABINDING_ADD_DATA_HASH(root_,"PageFilterCurrentPageLabel",subtitle_hash_);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"ShavingSplitterVisible",false);
        const bool result_price=results_mode_ && result_presentation_.has_value();
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"ItemPriceFooterVisible",result_price);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"uiPaletteVisible",false);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(root_,"InfoBoxVisible",false);
        list_=DATABINDING::_DATABINDING_ADD_UI_ITEM_LIST(root_,"ItemList");
        entries_=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(root_),"ItemListEntries");
        const Any prompts=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(root_),"Prompts");
        scene_=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(root_),"Scene");
        if(!Valid(title_entry) || !Valid(owner_) || !Valid(list_) || !Valid(entries_) || !Valid(scene_) || !Valid(prompts)){
            last_error_="could not create GenericShop fields";
            return false;
        }
        for(const auto& [name,label]:{std::pair{"Select","Select"},std::pair{"Back","Back"}}){
            const bool select=std::string_view(name)=="Select";
            DATABINDING::_DATABINDING_ADD_DATA_STRING(prompts,(std::string(name)+"Label").c_str(),
                results_mode_&&!select?result_back_label_.c_str():label);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(prompts,(std::string(name)+"Enabled").c_str(),
                !results_mode_ || !select);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(prompts,(std::string(name)+"Visible").c_str(),
                !results_mode_ || !select);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(prompts,(std::string(name)+"Held").c_str(),false);
        }
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(scene_,"AltHorseStats",false);
        const Any price=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(scene_),"price_details");
        if(!Valid(price)){
            last_error_="could not create GenericShop price binding";
            return false;
        }
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"visible",result_price);
        if(result_price){
            const auto& result=*result_presentation_;
            const auto details=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(scene_),"TjcBettingResults");
            if(!Valid(details)){last_error_="could not create betting result fields";return false;}
            const std::int64_t net=result.profit_cents;
            const std::int64_t magnitude=net<0?-net:net;
            const auto dollars=std::to_string(magnitude/100);
            const auto cents=(magnitude%100<10?"0":"")+std::to_string(magnitude%100);
            const std::array<Any,8> fields{
                DATABINDING::_DATABINDING_ADD_DATA_BOOL(details,"Visible",true),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(details,"Odds",result.odds.c_str()),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(details,"Stake",result.stake.c_str()),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(details,"Payout",result.payout.c_str()),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(details,"ProfitPrefix",net<0?"-$":"+$"),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(details,"ProfitDollars",dollars.c_str()),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(details,"ProfitCents",cents.c_str()),
                DATABINDING::_DATABINDING_ADD_DATA_STRING(price,"purchaseLabel",result.profit_label.c_str())};
            if(!std::all_of(fields.begin(),fields.end(),Valid)){
                last_error_="could not bind complete betting result fields";return false;
            }
            DATABINDING::_DATABINDING_ADD_DATA_INT(price,"purchasePrice",0);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"isGoldPrice",false);
            DATABINDING::_DATABINDING_ADD_DATA_INT(price,"tokenPrice",0);
            DATABINDING::_DATABINDING_ADD_DATA_STRING(price,"rightPriceRawText","");
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"rightPriceTextVisible",true);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"locked",false);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"itemPriceRankLocked",false);
            DATABINDING::_DATABINDING_ADD_DATA_INT(price,"itemPriceRank",0);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"isAffordable",true);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"modifiedPriceVisible",false);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(price,"modifiedPriceGold",false);
        }
        for(const char* name:{"HorseSpeed","HorseAcc"}){
            for(const char* suffix:{"Value","EquipmentValue","CapacityValue"})
                DATABINDING::_DATABINDING_ADD_DATA_INT(scene_,(std::string(name)+suffix).c_str(),0);
            for(const char* suffix:{"MinValue","EquipmentMinValue","CapacityMinValue"})
                DATABINDING::_DATABINDING_ADD_DATA_INT(scene_,(std::string(name)+suffix).c_str(),0);
            for(const char* suffix:{"MaxValue","EquipmentMaxValue","CapacityMaxValue"})
                DATABINDING::_DATABINDING_ADD_DATA_INT(scene_,(std::string(name)+suffix).c_str(),10);
        }
        DATABINDING::_DATABINDING_ADD_DATA_HASH(scene_,"HorseHandling",0);
        DATABINDING::_DATABINDING_ADD_DATA_BOOL(scene_,"StatsVisible",stats_visible_);
        tooltip_=DATABINDING::_DATABINDING_ADD_DATA_STRING(scene_,"ItemTooltip",tooltip_text_.c_str());
        if(!Valid(tooltip_)){last_error_="could not create GenericShop tooltip";return false;}
        if(!results_mode_){
            market_rule_=DATABINDING::_DATABINDING_ADD_DATA_STRING(scene_,"TjcMarketRuleText","");
            const auto rule_visible=DATABINDING::_DATABINDING_ADD_DATA_BOOL(scene_,"TjcMarketRuleVisible",!stats_visible_);
            const auto stats_visible=DATABINDING::_DATABINDING_ADD_DATA_BOOL(scene_,"FooterVisible",stats_visible_);
            if(!Valid(market_rule_) || !Valid(rule_visible) || !Valid(stats_visible)){
                last_error_="could not bind native market rule or horse stats visibility";return false;
            }
        }
        if(!footer_text_.empty()){
            // The native ITEM_LIST_DESCRIPTION activity renders ItemInfo1 in
            // its description area above the bottom divider. Horse-stats
            // activity ignores this binding, so results use that activity
            // separately from the horse-stat betting picker.
            const Any info=DATABINDING::_DATABINDING_ADD_DATA_CONTAINER(static_cast<Hash>(scene_),"ItemInfo1");
            if(!Valid(info)){
                last_error_="could not create GenericShop footer";
                return false;
            }
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(scene_,"FooterVisible",true);
            DATABINDING::_DATABINDING_ADD_DATA_BOOL(info,"Visible",true);
            DATABINDING::_DATABINDING_ADD_DATA_HASH(info,"Text",0);
            DATABINDING::_DATABINDING_ADD_DATA_STRING(info,"RawText",footer_text_.c_str());
            DATABINDING::_DATABINDING_ADD_DATA_HASH(info,"Style",static_cast<Hash>(432931142));
        }
        SetFocus(0);Fill(0,static_cast<int>(rows_.size()),0);
        if(!Owns())last_error_="GenericShop owner not retained";
        return Owns();
    }
public:
    bool Active() const{return launched_ && !closing_;}
    bool Presented() const{return scene_filled_ && Active() && Owns() &&
        DATABINDING::_VIRTUAL_COLLECTION_EXISTS(collection_) &&
        UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) && !UIAPPS::IS_UIAPP_TRANSITIONING_BY_HASH(App()) &&
        UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App())==SceneActivity();}
    bool Closing() const{return closing_;}
    const char* Failure() const{return last_error_;}
    std::string Summary() const{
        return "root="+std::to_string(root_)+" list="+std::to_string(list_)+
            " collection="+std::to_string(collection_)+" activity="+std::to_string(activity_)+
            " transition="+std::to_string(SceneTransition())+" expected="+std::to_string(SceneActivity())+
            " title_hash="+std::to_string(title_hash_)+
            " current="+std::to_string(UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App()))+
            " app="+std::to_string(UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()))+
            "/"+std::to_string(UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()))+
            " transitioned="+std::to_string(transitioned_)+" events="+std::to_string(events_seen_)+
            " scene_filled="+std::to_string(scene_filled_)+
            " requests="+std::to_string(requests_seen_)+" filled="+std::to_string(items_filled_)+
            " last="+std::to_string(last_event_)+"/"+std::to_string(last_param_)+
            " error="+last_error_;
    }
    bool ReplaceRows(std::vector<Row> rows,bool show_stats=true){
        if(!Active() || !Owns() || rows.empty())return false;
        rows_=std::move(rows);
        if(!results_mode_){
            stats_visible_=show_stats;
            DATABINDING::_DATABINDING_WRITE_DATA_BOOL_FROM_PARENT(scene_,"FooterVisible",stats_visible_);
            DATABINDING::_DATABINDING_WRITE_DATA_BOOL_FROM_PARENT(scene_,"StatsVisible",stats_visible_);
            DATABINDING::_DATABINDING_WRITE_DATA_BOOL_FROM_PARENT(scene_,"TjcMarketRuleVisible",!stats_visible_);
        }
        last_error_="";
        scene_filled_=scene_ready_event_=false;opened_=ClockNow();
        DrainStaleEvents();
        DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"ItemListEntryIndex",0);
        if(collection_ && DATABINDING::_VIRTUAL_COLLECTION_EXISTS(collection_)){
            DATABINDING::_VIRTUAL_COLLECTION_SET_SIZE(collection_,static_cast<int>(rows_.size()));
            DATABINDING::_VIRTUAL_COLLECTION_RESET(collection_);
            DATABINDING::_VIRTUAL_COLLECTION_SET_INTEREST_INDEX(collection_,0);
        }
        SetFocus(0);Fill(0,static_cast<int>(rows_.size()),collection_);
        return true;
    }
    bool Open(std::vector<Row> rows,bool show_stats=true,bool results_mode=false,
        const char* result_back_label="Leave Race",Hash title_hash=0,
        Hash subtitle_hash=0,const char* footer_text="",const char* tooltip_text=nullptr,
        const ResultPresentation* result_presentation=nullptr){
        if(launched_ || root_ || rows.empty() || UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()) ||
            UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) ||
            Valid(DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("GenericShop")) ||
            !UIAPPS::CAN_LAUNCH_UIAPP_BY_HASH_WITH_ENTRY(App(),Entry()))return false;
        rows_=std::move(rows);
        stats_visible_=show_stats;
        results_mode_=results_mode;
        title_hash_=title_hash;
        subtitle_hash_=subtitle_hash;
        footer_text_=footer_text?footer_text:"";
        tooltip_text_=tooltip_text?tooltip_text:footer_text_;
        result_presentation_=results_mode && result_presentation?std::optional<ResultPresentation>(*result_presentation):std::nullopt;
        result_back_label_=result_back_label;
        last_error_="";
        DrainStaleEvents();
        for(const char* block:{"shop","satch","ward"})HUD::TEXT_BLOCK_REQUEST(block);
        if(UIAPPS::LAUNCH_UIAPP_BY_HASH_WITH_ENTRY(App(),Entry())!=0){
            rows_.clear();last_error_="shop_menu launch failed";return false;
        }
        launched_=true;opened_=ClockNow();
        return true;
    }
    void Close(){
        if(!launched_ && !root_)return;
        if(ForeignRoot()){Drop(false);return;}
        const auto now=ClockNow();
        if(!closing_){
            closing_=true;scene_filled_=scene_ready_event_=false;
            close_requested_=last_close_attempt_=now;
            UIAPPS::_CLOSE_UIAPP_BY_HASH(App());
        }
        if(!UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()) && !UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App())){
            Drop(true);return;
        }
        // Retain the lease and owned data until the engine confirms shutdown,
        // even when a close animation changes its screen or an attempt fails.
        if(now-close_requested_>=std::chrono::seconds(3) &&
            now-last_close_attempt_>=std::chrono::seconds(1)){
            last_close_attempt_=now;
            UIAPPS::_CLOSE_UIAPP_BY_HASH_IMMEDIATE(App());
            if(!UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()) && !UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()))
                Drop(true);
        }
    }
    std::optional<Event> Tick(){
        if(closing_){Close();return {};}
        if(!launched_)return {};
        if(ForeignRoot()){
            last_error_="shop ownership changed";Drop(false);return Event{Action::Back,0};
        }
        // Back remains usable while the native shell is still loading and
        // cannot yet emit its own exit event. Never touch a foreign app.
        bool physical_back=false;
        for(const char* input:{"INPUT_FRONTEND_CANCEL","INPUT_FRONTEND_RRIGHT"}){
            const Hash control=HashOf(input);
            physical_back=physical_back || PAD::IS_CONTROL_JUST_PRESSED(0,control) ||
                PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0,control);
        }
        if(physical_back && !Presented()){Close();return Event{Action::Back,0};}
        if(!root_){
            if(UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App())){
                if(!BuildRoot()){Close();return Event{Action::Back,0};}
            }else if(ClockNow()-opened_>std::chrono::seconds(2)){
                last_error_="shop_menu did not run";
                Close();return Event{Action::Back,0};
            }else return {};
        }
        if(!Owns() || (scene_filled_ && !Presented())){
            last_error_="owned shop root or presented activity changed";Close();return Event{Action::Back,0};
        }
        if(!UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()) &&
            ClockNow()-opened_>std::chrono::seconds(2)){
            last_error_="shop_menu stopped running";Close();return Event{Action::Back,0};
        }
        if(!activity_ && transitioned_ && UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) &&
            !UIAPPS::IS_UIAPP_TRANSITIONING_BY_HASH(App())){
            const Hash current=UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App());
            if(current==SceneActivity())activity_=current;
        }
        // The first NEW_PAGE notification is not guaranteed to survive a
        // launch in the same script tick. Do not leave the player staring at
        // the app's empty shell if the notification was missed.
        if(!transitioned_ && Owns() && UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) &&
            !UIAPPS::IS_UIAPP_TRANSITIONING_BY_HASH(App()) &&
            ClockNow()-opened_>std::chrono::milliseconds(300))
            transitioned_=UIAPPS::REQUEST_UIAPP_TRANSITION_BY_HASH(App(),SceneTransition());
        if(!UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) &&
            ClockNow()-opened_>std::chrono::seconds(3)){
            last_error_="shop_menu did not become active";Close();return Event{Action::Back,0};
        }
        std::optional<Event> result;
        for(int n=0;n<32 && UIEVENTS::EVENTS_UI_IS_PENDING(Events());++n){
            std::array<Any,4> message{};
            if(UIEVENTS::EVENTS_UI_PEEK_MESSAGE(Events(),message.data())){
                ++events_seen_;
                const Hash type=static_cast<Hash>(message[0]);
                const int index=static_cast<int>(message[1]);
                const Hash param=static_cast<Hash>(message[2]);
                last_event_=type;last_param_=param;
                if(type==HashOf("NEW_PAGE") &&
                    (param==HashOf("GENERIC_SHOP_UI_NEXT_SCENE") ||
                     param==HashOf("GENERIC_SHOP_UI_PREV_SCENE") ||
                     param==HashOf("GENERIC_SHOP_UI_NEXT_PAGE")) &&
                    !UIAPPS::IS_UIAPP_TRANSITIONING_BY_HASH(App())){
                    if(scene_filled_){
                        last_error_="presented shop page changed";Close();return Event{Action::Back,0};
                    }
                    scene_ready_event_=false;
                    transitioned_=UIAPPS::REQUEST_UIAPP_TRANSITION_BY_HASH(App(),SceneTransition());
                }else if(type==HashOf("PAGED_COLLECTION_INITIALIZED") ||
                    type==HashOf("PAGED_COLLECTION_RESET")){
                    scene_filled_=scene_ready_event_=false;
                    collection_=param;
                    if(DATABINDING::_VIRTUAL_COLLECTION_EXISTS(param)){
                        scene_ready_event_=transitioned_ && Owns();
                        DATABINDING::_VIRTUAL_COLLECTION_SET_SIZE(param,static_cast<int>(rows_.size()));
                        DATABINDING::_VIRTUAL_COLLECTION_SET_INTEREST_INDEX(param,0);
                    }
                }else if(type==HashOf("PAGED_COLLECTION_REQUEST")){
                    ++requests_seen_;Fill(index,static_cast<int>(param),collection_);
                }
                else if(type==HashOf("ITEM_FOCUSED") && index>=0 &&
                    static_cast<std::size_t>(index)<rows_.size())SetFocus(static_cast<std::size_t>(index));
                else if(type==HashOf("ITEM_SELECTED")){
                    if(param==HashOf("GENERIC_SHOP_UI_EXIT"))result=Event{Action::Back,0};
                    else if(Presented() && (!result || result->action!=Action::Back) &&
                        index>=0 && static_cast<std::size_t>(index)<rows_.size() &&
                        (param==HashOf("GENERIC_SHOP_UI_SELECT") ||
                        param==HashOf("GENERIC_SHOP_UI_SECONDARY_SELECT")))
                        result=Event{Action::Select,static_cast<std::size_t>(index)};
                }
            }
            UIEVENTS::EVENTS_UI_POP_MESSAGE(Events());
        }
        if(!scene_filled_ && transitioned_ && scene_ready_event_ && Owns() &&
            UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) && !UIAPPS::IS_UIAPP_TRANSITIONING_BY_HASH(App()) &&
            UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App())==SceneActivity()){
            if(!DATABINDING::_VIRTUAL_COLLECTION_EXISTS(collection_) || !Fill(0,static_cast<int>(rows_.size()),collection_)){
                last_error_="could not bind target shop collection";Close();return Event{Action::Back,0};
            }
            activity_=SceneActivity();scene_filled_=true;
        }
        if(!scene_filled_ && ClockNow()-opened_>std::chrono::seconds(5)){
            last_error_="target shop scene did not become ready before deadline";Close();return Event{Action::Back,0};
        }
        if(physical_back)return Event{Action::Back,0};
        return result;
    }
};
} // namespace horse_races::bookmaker_shop
