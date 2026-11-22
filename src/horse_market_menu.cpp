#include "horse_market_menu.h"
#include "race_bookmaker_title.h"
#include "horse_market_subtitle.h"

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

// Isolated adaptation of race_bookmaker_native_shop.h for horse-sale details.
// We own only our GenericShop datastore and only close the activity we opened.
namespace horse_races::horse_sale_shop {
using Clock=std::chrono::steady_clock;
inline Clock::time_point (*ClockNow)()=&Clock::now;
struct Row {
    std::string label;
    std::string detail;
    int speed=0,acceleration=0;
    Hash handling=0;
};
enum class Action {Select,Back};
struct Event {Action action;std::size_t index=0;};
class Catalog {
    Any root_=0,list_=0,entries_=0,scene_=0,owner_=0;
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
    int sale_total_=0;
    std::string horse_subtitle_;
    std::string tooltip_text_;
    bool launched_=false,transitioned_=false,scene_filled_=false,scene_ready_event_=false,closing_=false;
    bool startup_failed_=false,binding_failed_=false;
    bool root_created_=false,owner_claimed_=false;
    bool stopped_confirmed_=false;
    int events_seen_=0,requests_seen_=0,items_filled_=0;
    Hash last_event_=0,last_param_=0;
    const char* last_error_="";
    std::vector<std::string> diagnostics_;
    std::string diagnostic_state_,diagnostic_line_;
    std::chrono::steady_clock::time_point opened_{},close_requested_{},last_close_attempt_{};
    inline static unsigned serial_=0;
    static Hash App(){return MISC::GET_HASH_KEY("shop_menu");}
    static Hash Entry(){return MISC::GET_HASH_KEY("generic_shop");}
    // Native generic_shop_main (screen archive 1A4C81AC), not the launch entry.
    static Hash ShellScreen(){return HashOf("shop_menu_main");}
    Hash SceneActivity() const{return HashOf("ITEM_LIST_DESCRIPTION");}
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
    // launched_ is an exclusive launch lease, acquired only after Open observed
    // both app and root absent. A current-screen hash never proves takeover.
    bool ForeignRoot() const {
        const Any live=DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("GenericShop");
        if(!Valid(live))return false;
        if(!root_created_ || live!=root_)return true;
        return owner_claimed_ && Valid(owner_) && DATABINDING::DATABINDING_READ_INT(owner_)!=token_;
    }
    bool CreatedRootStillOurs() const {
        return root_created_ && Valid(root_) &&
            DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("GenericShop")==root_ &&
            (!Valid(owner_) || !owner_claimed_ || DATABINDING::DATABINDING_READ_INT(owner_)==token_);
    }
    void Trace(std::string_view reason,bool force=false){
        const Hash current=UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App());
        const std::string phase=closing_?"closing":scene_filled_?"presented":scene_ready_event_?"target collection":
            transitioned_?"transition requested":root_?"owned shell":launched_?"launching":"idle";
        const std::string state="phase="+phase+" current="+std::to_string(current)+
            " expected="+std::to_string(SceneActivity())+" shell="+std::to_string(ShellScreen())+
            " owner="+(Owns()?"ours":root_?"lost":"none")+" root="+std::to_string(root_)+
            " lease="+std::to_string(launched_)+" root_created="+std::to_string(root_created_)+
            " owner_claimed="+std::to_string(owner_claimed_)+
            " active="+std::to_string(UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()))+
            " transitioning="+std::to_string(UIAPPS::IS_UIAPP_TRANSITIONING_BY_HASH(App()))+
            " collection="+std::to_string(collection_);
        const std::string line="reason="+std::string(reason)+" "+state;
        if((force || state!=diagnostic_state_) && line!=diagnostic_line_){
            if(diagnostics_.size()>=128)diagnostics_.erase(diagnostics_.begin());
            diagnostics_.push_back(line);
            diagnostic_line_=line;
        }
        diagnostic_state_=state;
    }
    void Drop(bool remove,const std::string& reason="release"){
        stopped_confirmed_=remove;
        Trace(reason,true); // Capture the old owner/current/phase before clearing.
        if(!remove && Owns()){
            for(const char* field:{"TjcSaleVisible","TjcSaleSelectEnabled","TjcSaleSelectVisible"})
                DATABINDING::_DATABINDING_WRITE_DATA_BOOL_FROM_PARENT(root_,field,false);
        }
        if(remove && CreatedRootStillOurs())
            DATABINDING::_DATABINDING_REMOVE_DATA_ENTRY(static_cast<Hash>(root_));
        root_=list_=entries_=scene_=owner_=collection_=0;
        activity_=title_hash_=subtitle_hash_=0;rows_.clear();footer_text_.clear();tooltip_text_.clear();
        launched_=transitioned_=scene_filled_=scene_ready_event_=closing_=false;events_seen_=requests_seen_=items_filled_=0;
        root_created_=owner_claimed_=false;
        last_event_=last_param_=0;}
#include "horse_market_menu_binding.inl"
public:
    void SetSaleTotal(int total){sale_total_=total;}
    void SetHorseSubtitle(std::string name){horse_subtitle_=std::move(name);}
    bool Active() const{return launched_ && !closing_;}
    bool Presented() const{return scene_filled_ && launched_ && !closing_ && Owns() &&
        UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) && !UIAPPS::IS_UIAPP_TRANSITIONING_BY_HASH(App()) &&
        UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App())==SceneActivity();}
    std::string Failure() const{return startup_failed_?last_error_:"";}
    std::vector<std::string> DrainDiagnostics(){auto result=std::move(diagnostics_);diagnostics_.clear();return result;}
    bool Closing() const{return closing_;}
    bool StoppedConfirmed() const{return stopped_confirmed_ && !Valid(DATABINDING::_DATABINDING_GET_DATA_CONTAINER_FROM_PATH("GenericShop")) && !UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()) && !UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App());}
    std::string Summary() const{
        return "root="+std::to_string(root_)+" list="+std::to_string(list_)+
            " collection="+std::to_string(collection_)+" activity="+std::to_string(activity_)+
            " title_hash="+std::to_string(title_hash_)+
            " current="+std::to_string(UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App()))+
            " app="+std::to_string(UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()))+
            "/"+std::to_string(UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()))+
            " transitioned="+std::to_string(transitioned_)+" events="+std::to_string(events_seen_)+
            " scene_filled="+std::to_string(scene_filled_)+
            " ready_event="+std::to_string(scene_ready_event_)+
            " requests="+std::to_string(requests_seen_)+" filled="+std::to_string(items_filled_)+
            " last="+std::to_string(last_event_)+"/"+std::to_string(last_param_)+
            " error="+last_error_;
    }
    bool ReplaceRows(std::vector<Row> rows){
        if(!Active() || !Owns() || rows.empty())return false;
        rows_=std::move(rows);
        last_error_="";
        DATABINDING::_DATABINDING_ADD_DATA_INT(root_,"ItemListEntryIndex",0);
        if(collection_ && DATABINDING::_VIRTUAL_COLLECTION_EXISTS(collection_)){
            DATABINDING::_VIRTUAL_COLLECTION_SET_SIZE(collection_,static_cast<int>(rows_.size()));
            DATABINDING::_VIRTUAL_COLLECTION_RESET(collection_);
            DATABINDING::_VIRTUAL_COLLECTION_SET_INTEREST_INDEX(collection_,0);
        }
        SetStats(0);Fill(0,static_cast<int>(rows_.size()),collection_);
        return true;
    }
    bool Open(std::vector<Row> rows,bool show_stats=true,bool results_mode=false,
        const char* result_back_label="Leave Race",Hash title_hash=0,
        Hash subtitle_hash=0,const char* footer_text="",const char* tooltip_text=nullptr){
        if(launched_ || root_ || UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()) ||
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
        result_back_label_=result_back_label;
        startup_failed_=binding_failed_=false;stopped_confirmed_=false;
        diagnostic_state_.clear();diagnostic_line_.clear();
        last_error_="";
        DrainStaleEvents();
        for(const char* block:{"shop","satch","ward"})HUD::TEXT_BLOCK_REQUEST(block);
        if(UIAPPS::LAUNCH_UIAPP_BY_HASH_WITH_ENTRY(App(),Entry())!=0){
            rows_.clear();last_error_="shop_menu launch failed";Trace(last_error_,true);return false;
        }
        launched_=true;opened_=horse_sale_shop::ClockNow();
        Trace("launch accepted",true);
        return true;
    }
    void Close(const std::string& reason="close"){
        if(!launched_ && !root_)return;
        if(ForeignRoot()){Drop(false,"foreign root/token handoff during close");return;}
        const auto now=ClockNow();
        if(!closing_){
            Trace(reason,true);
            closing_=true;scene_filled_=scene_ready_event_=false;
            close_requested_=now;last_close_attempt_=now;
            UIAPPS::_CLOSE_UIAPP_BY_HASH(App());
        }
        if(!UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()) && !UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App())){
            Drop(true,"leased native app stopped");return;
        }
        // A normal close may be queued behind animation. Never call completion
        // or discard our lease merely because its current screen changed.
        if(now-close_requested_>=std::chrono::seconds(3) &&
            now-last_close_attempt_>=std::chrono::seconds(1)){
            last_close_attempt_=now;
            Trace("native close recovery: immediate retry",true);
            UIAPPS::_CLOSE_UIAPP_BY_HASH_IMMEDIATE(App());
            if(!UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()) && !UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()))
                Drop(true,"leased native app stopped after recovery");
        }
    }
    std::optional<Event> Tick(){
        if(launched_)Trace("state changed");
        if(closing_){Close();return {};}
        if(!launched_)return {};
        if(!root_){
            if(UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App())){
                if(!BuildRoot()){startup_failed_=true;Close(last_error_);return Event{Action::Back,0};}
                Trace("root bound",true);
            }else if(horse_sale_shop::ClockNow()-opened_>std::chrono::seconds(2)){
                last_error_="shop_menu did not run";
                startup_failed_=true;
                Close(last_error_);return Event{Action::Back,0};
            }else return {};
        }
        if(ForeignRoot()){
            Drop(false,"foreign root/token handoff");return Event{Action::Back,0};
        }
        if(!Owns()){
            last_error_="owned shop root disappeared";startup_failed_=!scene_filled_;
            Close(last_error_);return Event{Action::Back,0};
        }
        if(scene_filled_ && (UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App())!=SceneActivity() ||
            UIAPPS::IS_UIAPP_TRANSITIONING_BY_HASH(App()) || !UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()))){
            Close("presented target changed; cancel leased app");return Event{Action::Back,0};
        }
        if(!UIAPPS::IS_UIAPP_RUNNING_BY_HASH(App()) &&
            horse_sale_shop::ClockNow()-opened_>std::chrono::seconds(2)){
            startup_failed_=!scene_filled_;
            last_error_="shop_menu stopped running";Close(last_error_);return Event{Action::Back,0};
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
            horse_sale_shop::ClockNow()-opened_>std::chrono::milliseconds(300)){
            scene_ready_event_=false;
            transitioned_=UIAPPS::REQUEST_UIAPP_TRANSITION_BY_HASH(App(),SceneActivity());
            Trace(transitioned_?"fallback transition accepted":"fallback transition rejected",true);
        }
        if(!UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) &&
            horse_sale_shop::ClockNow()-opened_>std::chrono::seconds(3)){
            startup_failed_=!scene_filled_;
            last_error_="shop_menu did not become active";Close(last_error_);return Event{Action::Back,0};
        }
        std::optional<Event> result;bool invalidate_presented=false;
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
                    if(scene_filled_){invalidate_presented=true;scene_filled_=false;}
                    scene_ready_event_=false;
                    if(!invalidate_presented)transitioned_=UIAPPS::REQUEST_UIAPP_TRANSITION_BY_HASH(App(),SceneActivity());
                    Trace(transitioned_?"native page transition accepted":"native page transition rejected",true);
                }else if(type==HashOf("PAGED_COLLECTION_INITIALIZED") ||
                    type==HashOf("PAGED_COLLECTION_RESET")){
                    const bool previous=scene_ready_event_;const Any previous_collection=collection_;
                    if(scene_filled_ && (type==HashOf("PAGED_COLLECTION_RESET") || collection_!=param)){
                        invalidate_presented=true;scene_filled_=false;
                    }
                    scene_ready_event_=false;
                    collection_=param;
                    if(DATABINDING::_VIRTUAL_COLLECTION_EXISTS(param)){
                        if(launched_ && !closing_ && transitioned_ && Owns())
                            scene_ready_event_=true;
                        DATABINDING::_VIRTUAL_COLLECTION_SET_SIZE(param,static_cast<int>(rows_.size()));
                        DATABINDING::_VIRTUAL_COLLECTION_SET_INTEREST_INDEX(param,0);
                    }
                    if(!previous || previous_collection!=collection_)Trace(scene_ready_event_?
                        "target collection captured":"collection ignored outside target",true);
                }else if(type==HashOf("PAGED_COLLECTION_REQUEST")){
                    ++requests_seen_;Fill(index,static_cast<int>(param),collection_);
                }
                else if(type==HashOf("ITEM_FOCUSED") && index>=0 &&
                    static_cast<std::size_t>(index)<rows_.size())SetStats(static_cast<std::size_t>(index));
                else if(type==HashOf("ITEM_SELECTED")){
                    if(param==HashOf("GENERIC_SHOP_UI_EXIT")){Trace("native Back",true);result=Event{Action::Back,0};}
                    else if(param==HashOf("TJC_HORSE_SALE_CONFIRM") && Presented() &&
                        (!result || result->action!=Action::Back))
                        // Sell is a whole-quote footer action, not a row purchase.
                        // Its native UI event need not carry a valid row index.
                        result=Event{Action::Select,index>=0?static_cast<std::size_t>(index):0};
                }
            }
            UIEVENTS::EVENTS_UI_POP_MESSAGE(Events());
        }
        if(invalidate_presented){
            Close("presented page/collection changed during event batch");return Event{Action::Back,0};
        }
        if(binding_failed_){
            last_error_="could not bind complete sale rows";startup_failed_=true;
            Close(last_error_);return Event{Action::Back,0};
        }
        // A successful request or active app only proves that the shell exists.
        // Require the target activity AND its native list initialization before
        // accepting any sale, including the direct keyboard/controller fallback.
        if(!scene_filled_ && transitioned_ && scene_ready_event_ && Owns() &&
            UIAPPS::IS_UIAPP_ACTIVE_BY_HASH(App()) &&
            !UIAPPS::IS_UIAPP_TRANSITIONING_BY_HASH(App()) &&
            UIAPPS::_GET_UIAPP_CURRENT_ACTIVITY_BY_HASH(App())==SceneActivity()){
            if(!DATABINDING::_VIRTUAL_COLLECTION_EXISTS(collection_) || !Fill(0,static_cast<int>(rows_.size()),collection_)){
                last_error_="could not bind complete target collection";startup_failed_=true;
                Close(last_error_);return Event{Action::Back,0};
            }
            activity_=SceneActivity();scene_filled_=true;
            Trace("target presented",true);
        }
        if(!scene_filled_ && ClockNow()-opened_>std::chrono::seconds(5)){
            last_error_="target shop scene did not become ready before deadline";
            startup_failed_=true;
            Close(last_error_);return Event{Action::Back,0};
        }
        return result;
    }
};
} // namespace horse_races::horse_sale_shop


namespace horse_races::horse_market {
struct SaleMenu::Impl {
    horse_sale_shop::Catalog catalog;
    SaleQuote quote;
    bool locked=false,delivered=false,armed=false,input_tail=false,pending_sell=false;
    std::chrono::steady_clock::time_point opened{},released{},tail_until{};
    void Lock(){
        if(!locked){PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(),false,0,false);locked=true;}
    }
    void SuppressWorld(){
        for(const char* input:{"INPUT_ATTACK","INPUT_ATTACK2","INPUT_AIM","INPUT_MELEE_ATTACK",
            "INPUT_MELEE_GRAPPLE","INPUT_MELEE_BLOCK","INPUT_INTERACT_LOCKON",
            "INPUT_INTERACT_LOCKON_NEG","INPUT_SELECT_WEAPON","INPUT_WEAPON_WHEEL_NEXT",
            "INPUT_WEAPON_WHEEL_PREV","INPUT_ENTER","INPUT_JUMP"})
            PAD::DISABLE_CONTROL_ACTION(0,MISC::GET_HASH_KEY(input),true);
    }
    bool AcceptHeld() const {
        for(const char* input:{"INPUT_FRONTEND_ACCEPT","INPUT_FRONTEND_RDOWN"}){
            const Hash control=MISC::GET_HASH_KEY(input);
            if(PAD::IS_CONTROL_PRESSED(0,control) || PAD::IS_DISABLED_CONTROL_PRESSED(0,control))return true;
        }
        return false;
    }
    bool WorldHeld() const {
        for(const char* input:{"INPUT_ATTACK","INPUT_ATTACK2","INPUT_AIM","INPUT_MELEE_ATTACK"}){
            const Hash control=MISC::GET_HASH_KEY(input);
            if(PAD::IS_CONTROL_PRESSED(0,control) || PAD::IS_DISABLED_CONTROL_PRESSED(0,control))return true;
        }
        return false;
    }
    void SuppressTail(){
        for(const char* input:{"INPUT_ATTACK","INPUT_ATTACK2","INPUT_AIM","INPUT_MELEE_ATTACK"})
            PAD::DISABLE_CONTROL_ACTION(0,MISC::GET_HASH_KEY(input),true);
    }
    bool ExplicitSellPressed() const {
        // Keyboard/controller acceptance is sufficient even if the native
        // text list does not emit a row selection in this frame.
        for(const char* input:{"INPUT_FRONTEND_ACCEPT","INPUT_FRONTEND_RDOWN"}){
            const Hash control=MISC::GET_HASH_KEY(input);
            if(PAD::IS_CONTROL_JUST_PRESSED(0,control) || PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0,control))return true;
        }
        return false;
    }
    bool BackPressed() const {
        for(const char* input:{"INPUT_FRONTEND_CANCEL","INPUT_FRONTEND_RRIGHT"}){
            const Hash control=MISC::GET_HASH_KEY(input);
            if(PAD::IS_CONTROL_JUST_PRESSED(0,control) || PAD::IS_DISABLED_CONTROL_JUST_PRESSED(0,control))return true;
        }
        return false;
    }
    void Unlock(){
        if(locked){PLAYER::SET_PLAYER_CONTROL(PLAYER::PLAYER_ID(),true,0,false);locked=false;}
    }
};
SaleMenu::SaleMenu():impl_(std::make_unique<Impl>()){}
SaleMenu::~SaleMenu(){impl_->catalog.Close("menu destroyed");impl_->Unlock();}
bool SaleMenu::Open(SaleQuote quote){
    if(impl_->locked || impl_->catalog.Active() || impl_->catalog.Closing() || !ValidSaleQuote(quote))return false;
    // Hash titles require actual registered text. Never silently show a different shop name.
    const char* title=quote.chinese?"TJC_TITLE_HORSE_SALE_ZH":"TJC_TITLE_HORSE_SALE_EN";
    if(!bookmaker_title::Available(title))return false;
    std::vector<horse_sale_shop::Row> rows;
    rows.reserve(quote.wins.size());
    for(const auto& win:quote.wins)rows.push_back({win.race_name,SaleMoney(win.net_bonus_cents)});
    impl_->catalog.SetSaleTotal(static_cast<int>(quote.total_cents));
    impl_->catalog.SetHorseSubtitle(quote.title);
    const std::string description=std::string(quote.chinese?"基础价值：":"Base Value: ")+SaleMoney(quote.base_cents);
    if(!impl_->catalog.Open(std::move(rows),false,false,"Back",MISC::GET_HASH_KEY(title),0,description.c_str(),""))return false;
    impl_->quote=std::move(quote);impl_->pending_sell=false;impl_->delivered=false;impl_->armed=false;impl_->input_tail=false;
    impl_->opened=horse_sale_shop::ClockNow();impl_->released={};impl_->Lock();
    return true;
}
void SaleMenu::Close(const std::string& reason){impl_->pending_sell=false;impl_->delivered=true;impl_->catalog.Close(reason);}
bool SaleMenu::Active() const{return impl_->locked;}
bool SaleMenu::NeedsTick() const{return impl_->locked || impl_->input_tail;}
bool SaleMenu::Closing() const{return impl_->catalog.Closing() || (impl_->locked && !impl_->catalog.Active());}
std::string SaleMenu::Summary() const{return impl_->catalog.Summary();}
std::string SaleMenu::Failure() const{return impl_->catalog.Failure();}
std::vector<std::string> SaleMenu::DrainDiagnostics(){return impl_->catalog.DrainDiagnostics();}
std::optional<SaleEvent> SaleMenu::Tick(){
    if(!impl_->locked){
        if(impl_->input_tail){
            if(!impl_->WorldHeld() || horse_sale_shop::ClockNow()>=impl_->tail_until)impl_->input_tail=false;
            else impl_->SuppressTail();
        }
        return {};
    }
    impl_->SuppressWorld();
    const bool presented_before=impl_->catalog.Presented();
    const auto event=impl_->catalog.Tick();
    const auto now=horse_sale_shop::ClockNow();
    // Back must work even if no streamed scene exists to emit a native event.
    // Cancellation wins over every confirmation route in this frame.
    if((!impl_->delivered || impl_->pending_sell) && impl_->BackPressed()){
        impl_->pending_sell=false;
        impl_->delivered=true;impl_->catalog.Close("physical Back");
        return SaleEvent{SaleAction::BackClosed,impl_->quote};
    }
    // The context selection's press must be released before a distinct Sell press.
    const bool previously_armed=impl_->armed;
    if(!impl_->armed && now-impl_->opened>std::chrono::milliseconds(250) && !impl_->AcceptHeld())impl_->armed=true;
    const bool explicit_sell=previously_armed && presented_before && impl_->catalog.Presented() && impl_->ExplicitSellPressed();
    std::optional<SaleEvent> result;
    if(event && !impl_->delivered){
        if(event->action==horse_sale_shop::Action::Back){
            impl_->pending_sell=false;impl_->delivered=true;result=SaleEvent{SaleAction::BackClosed,impl_->quote};impl_->catalog.Close("native Back");
        }else if(previously_armed && impl_->catalog.Presented()){
            impl_->delivered=true;impl_->pending_sell=true;impl_->catalog.Close("native Sell confirmed");
        }
    }
    // Accept the displayed keyboard/controller Sell independently of a list
    // event, for both populated and empty informational win lists.
    if(!impl_->delivered && explicit_sell){
            impl_->delivered=true;impl_->pending_sell=true;impl_->catalog.Close("keyboard/controller Sell confirmed");
    }
    if(!impl_->catalog.Active() && !impl_->catalog.Closing()){
        if(impl_->released==std::chrono::steady_clock::time_point{})impl_->released=now;
        const bool held=impl_->WorldHeld();
        if((!held && now-impl_->released>std::chrono::milliseconds(250)) ||
            now-impl_->released>=std::chrono::milliseconds(750)){
            impl_->Unlock();impl_->input_tail=held;impl_->tail_until=now+std::chrono::milliseconds(1500);
            if(held)impl_->SuppressTail();
        }
    }
    if(impl_->pending_sell && !impl_->catalog.Active() && !impl_->catalog.Closing()){
        if(!impl_->catalog.StoppedConfirmed()){
            impl_->pending_sell=false;result=SaleEvent{SaleAction::BackClosed,impl_->quote};
        }else if(!impl_->locked){
            impl_->pending_sell=false;result=SaleEvent{SaleAction::SellRequested,impl_->quote};
        }
    }
    return result;
}
}
