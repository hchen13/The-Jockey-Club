#include "horse_market_runtime.h"
#include "horse_market_help.h"
#include "bg_literal_text.h"
#include "horse_market_scene.h"
#include "horse_market_menu.h"
#include "horse_market_catalog.h"
#include "horse_market_name.h"
#include "horse_catalog.h"
#include "horse_market_sale.h"
#include "horse_market_target.h"
#include "horse_ledger_runtime.h"
#include "race_progression_runtime.h"
#include "race_progression_messages.h"
#include "race_progression_toast.h"
#include "race_localization.h"
#include "race_map_textures.h"
#include "race_transaction_feed.h"
#include "world/identity.h"
#include "world/camera_blackout.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <optional>

namespace horse_races::horse_market_runtime {
namespace {
namespace ledger=horse_ledger::runtime;
Options options;
void (*logger)(const std::string&){};
horse_market::SaleMenu menu;
std::optional<ledger::MarketQuote> quote;
world::Identity quoted_horse,clerk;
Prompt prompt=0,member_prompt=0;
Blip marker=0;
bool pressed=false,selling=false,lead_release_requested=false;
enum class SalePhase { None, Dismounting, FadingOut, FadingIn };
struct SalePresentation {
    SalePhase phase=SalePhase::None;
    world::Identity player,horse;
    world::CameraBlackout fade;
    std::uint64_t deadline=0,settled_since=0,next_delete=0;
    bool owns_fade=false;
} presentation;
std::vector<world::Identity> retained_releases;
std::uint64_t sale_wait_until=0;
bool sale_eligible=false,member_armed=false;
std::string eligibility_state,readiness_state,eligibility_reason;
horse_market_help::Presenter unavailable_help;
horse_market_target::Target eligibility_target;
horse_ledger::SellerEra member_era=horse_ledger::SellerEra::Unknown;
horse_ledger::Cents member_fee=-1;
std::uint64_t next_eligibility=0;
std::uint64_t next_check=0,next_greeting=0,dismount_until=0,next_prompt=0,world_timeline=0,notice_until=0,quote_serial=0;
std::string notice;
void Log(const std::string& line){if(logger)logger("horse market: "+line);}
void LogMenuDiagnostics(){for(const auto& line:menu.DrainDiagnostics())Log("menu: "+line);}
struct MenuDiagnosticFlush {~MenuDiagnosticFlush(){LogMenuDiagnostics();}};
void Notice(const std::string& line){notice=line;notice_until=GetTickCount64()+6000;Log(line);}
void SaleNotice(const std::string& reason){
    // Keep diagnostics in the log; player text never contains native identifiers.
    Log("sale notice detail: "+reason);
    Notice(horse_market_help::Message(reason));
}
void DrawNotice(){
    if(notice.empty()||GetTickCount64()>=notice_until||menu.Active()||HUD::IS_PAUSE_MENU_ACTIVE())return;
    UIDEBUG::_BG_SET_TEXT_SCALE(0,.35F);UIDEBUG::_BG_SET_TEXT_COLOR(245,239,225,255);
    bg_text::Display("~s~<FONT FACE='$body'>"+notice+"</FONT>",.30F,.80F);
}
float Distance(int a,int b){
    if(!a||!b||!ENTITY::DOES_ENTITY_EXIST(a)||!ENTITY::DOES_ENTITY_EXIST(b))return 100000;
    const auto x=ENTITY::GET_ENTITY_COORDS(a,true,false),y=ENTITY::GET_ENTITY_COORDS(b,true,false);
    return std::sqrt((x.x-y.x)*(x.x-y.x)+(x.y-y.y)*(x.y-y.y)+(x.z-y.z)*(x.z-y.z));
}
const char* WorldBlockedBy(bool own_fade=false){
    if(NETWORK::NETWORK_IS_GAME_IN_PROGRESS())return "online";
    if(HUD::IS_PAUSE_MENU_ACTIVE())return "pause menu";
    if(!own_fade&&!world::CameraBlackout{}.FullyVisible())return "camera not visible";
    const auto player=PLAYER::PLAYER_PED_ID();
    if(!player||!ENTITY::DOES_ENTITY_EXIST(player))return "player absent";
    if(ENTITY::IS_ENTITY_DEAD(player))return "player dead";
    if(PED::IS_PED_IN_COMBAT(player,0))return "player in combat";
    return nullptr;
}
void RemovePrompt(){
    unavailable_help.Clear(GetTickCount64());eligibility_reason.clear();
    if(world::Still(clerk))MISC::UNREGISTER_INTERACTION_LOCKON_PROMPT(clerk.entity);
    if(prompt&&HUD::_UI_PROMPT_IS_VALID(prompt))HUD::_UI_PROMPT_DELETE(prompt);
    if(member_prompt&&HUD::_UI_PROMPT_IS_VALID(member_prompt))HUD::_UI_PROMPT_DELETE(member_prompt);
    prompt=member_prompt=0;clerk={};pressed=member_armed=sale_eligible=false;
    eligibility_target={};eligibility_state.clear();next_eligibility=0;member_era=horse_ledger::SellerEra::Unknown;member_fee=-1;
}
void SetPrompt(Prompt handle,bool visible,bool enabled){
    if(handle&&HUD::_UI_PROMPT_IS_VALID(handle)){
        HUD::_UI_PROMPT_SET_VISIBLE(handle,visible);HUD::_UI_PROMPT_SET_ENABLED(handle,enabled);
    }
}
void ResetMembershipHold(){
    member_armed=false;
    if(member_prompt&&HUD::_UI_PROMPT_IS_VALID(member_prompt))HUD::_UI_PROMPT_RESTART_MODES(member_prompt);
}
bool HonoraryEligible(){
    const auto seller=ledger::CurrentSeller();
    const auto era=seller==horse_ledger::SellerEra::Arthur?progression::Era::Arthur:
        seller==horse_ledger::SellerEra::John?progression::Era::John:progression::Era::None;
    return progression::runtime::HonoraryMembershipEligible(era);
}
void HidePrompts(){unavailable_help.Clear(GetTickCount64());SetPrompt(prompt,false,false);SetPrompt(member_prompt,false,false);ResetMembershipHold();}
void EnsurePrompt(){
    const auto broker=horse_market_scene::Broker();
    if(world::Still(clerk)&&clerk.entity==broker&&prompt&&member_prompt&&
       HUD::_UI_PROMPT_IS_VALID(prompt)&&HUD::_UI_PROMPT_IS_VALID(member_prompt))return;
    RemovePrompt();if(!broker||GetTickCount64()<next_prompt)return;
    next_prompt=GetTickCount64()+2000;clerk=world::Identify(broker);
    prompt=HUD::_UI_PROMPT_REGISTER_BEGIN();if(!prompt)return;
    HUD::_UI_PROMPT_SET_CONTROL_ACTION(prompt,MISC::GET_HASH_KEY("INPUT_INTERACT_LOCKON_POS"));
    HUD::_UI_PROMPT_SET_TEXT(prompt,i18n::Literal(i18n::Text("Sell Horse","出售马匹")));
    HUD::_UI_PROMPT_SET_PRIORITY(prompt,0);HUD::_UI_PROMPT_SET_TRANSPORT_MODE(prompt,0);
    HUD::_UI_PROMPT_SET_ATTRIBUTE(prompt,18,true);HUD::_UI_PROMPT_SET_STANDARD_MODE(prompt,false);
    HUD::_UI_PROMPT_REGISTER_END(prompt);
    HUD::_UI_PROMPT_SET_GROUP(prompt,HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(broker),0);
    member_prompt=HUD::_UI_PROMPT_REGISTER_BEGIN();
    if(!member_prompt){RemovePrompt();return;}
    HUD::_UI_PROMPT_SET_CONTROL_ACTION(member_prompt,MISC::GET_HASH_KEY("INPUT_INTERACT_LOCKON_NEG"));
    HUD::_UI_PROMPT_SET_TEXT(member_prompt,i18n::Literal(i18n::Text("Club Membership ($1000)","赛马会会员（$1000）")));
    HUD::_UI_PROMPT_SET_PRIORITY(member_prompt,0);HUD::_UI_PROMPT_SET_TRANSPORT_MODE(member_prompt,0);
    HUD::_UI_PROMPT_SET_ATTRIBUTE(member_prompt,18,true);HUD::_UI_PROMPT_SET_HOLD_MODE(member_prompt,1200);
    HUD::_UI_PROMPT_REGISTER_END(member_prompt);
    HUD::_UI_PROMPT_SET_GROUP(member_prompt,HUD::_UI_PROMPT_GET_GROUP_ID_FOR_TARGET_ENTITY(broker),0);
    PED::_SET_PED_PROMPT_NAME(broker,i18n::Literal(i18n::Text("Club Agent","赛马会经纪人")));
    for(const int flag:{130,297,315})PED::SET_PED_CONFIG_FLAG(broker,flag,true);
    for(const int flag:{178,301,317,331,493})PED::SET_PED_CONFIG_FLAG(broker,flag,false);
    if(!MISC::REGISTER_INTERACTION_LOCKON_PROMPT(broker,"",15,15,32776,0,0,prompt,false,-1)){
        RemovePrompt();Log("broker interaction registration deferred");return;
    }
    HidePrompts();
}
void RemoveMarker(){
    if(marker&&MAP::DOES_BLIP_EXIST(marker))MAP::REMOVE_BLIP(&marker);
    marker=0;map_textures::Release(map_textures::Dictionary::Story,map_textures::Owner::HorseMarket);
}
void TickMarker(){
    using namespace map_textures;
    Acquire(Dictionary::Story,Owner::HorseMarket);
    if(!HorseIconReady(Dictionary::Story)){if(marker&&MAP::DOES_BLIP_EXIST(marker))MAP::REMOVE_BLIP(&marker);marker=0;return;}
    if(marker&&MAP::DOES_BLIP_EXIST(marker))return;
    const auto p=horse_market_scene::Location();
    marker=MAP::BLIP_ADD_FOR_COORDS(0xD792CF71,{p.x,p.y,p.z});
    if(marker){MAP::SET_BLIP_SPRITE(marker,HorseSprite(Dictionary::Story),true);
        // Same verified dollar badge as bookmaker_map::Create; retain the
        // pinned/glyph-checked story TXD for the complete base+modifier lifetime.
        MAP::BLIP_ADD_MODIFIER(marker,0xDAA61911); // BLIP_MODIFIER_FOR_SALE
        MAP::_SET_BLIP_NAME(marker,i18n::Literal(i18n::Text("Racehorse Market","赛马市场")));}
}
void ClearQuote(){quote.reset();quoted_horse={};selling=lead_release_requested=false;next_eligibility=0;}
void ReleaseWhenUnused(world::Identity horse){
    if(!world::Still(horse))return;
    if(std::find(retained_releases.begin(),retained_releases.end(),horse)==retained_releases.end())
        retained_releases.push_back(horse);
}
void TickRetainedReleases(){
    std::erase_if(retained_releases,[](const auto& identity){
        const int horse=world::Handle(identity);if(!horse)return true;
        const int player=PLAYER::PLAYER_PED_ID();
        if(PED::_GET_RIDER_OF_MOUNT(horse,true)!=0||PED::GET_MOUNT(player)==horse||
           (TASK::_IS_PED_LEADING_HORSE(player)&&TASK::_GET_LED_HORSE_FROM_PED(player)==horse))return false;
        int released=horse;ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&released);
        Log("retained sold horse released after interrupted presentation");return true;
    });
}
void EndPresentation(bool same_world,const char* reason){
    if(presentation.phase==SalePhase::None)return;
    Log(std::string("sale presentation ended: ")+reason);
    // A load or online handoff transfers the camera and handles. Never lift
    // that world's fade or touch a recycled entity on behalf of an old sale.
    if(same_world){
        if(presentation.owns_fade&&!presentation.fade.FullyVisible()){
            if(presentation.phase==SalePhase::FadingIn)presentation.fade.RecoverIn(350);
            else presentation.fade.Restore();
        }
        ReleaseWhenUnused(presentation.horse);
    }
    presentation={};
}
bool Standing(int player){
    return PED::IS_PED_ON_FOOT(player)&&!PED::IS_PED_RAGDOLL(player)&&
        !PED::IS_PED_FALLING(player)&&!TASK::IS_PED_GETTING_UP(player)&&
        !TASK::_IS_PED_LEADING_HORSE(player);
}
void BeginPresentation(world::Identity player,world::Identity horse,int cents){
    presentation={};presentation.player=player;presentation.horse=horse;
    presentation.phase=SalePhase::Dismounting;presentation.deadline=GetTickCount64()+12000;
    // Ownership, wallet and the sold ledger event are already committed. This
    // feed is the very same native cash receipt used by race settlement.
    const int feed=transaction_feed::Cash(cents);
    Log("sale cash receipt cents="+std::to_string(cents)+" right_feed="+std::to_string(feed));
    if(PED::GET_MOUNT(player.entity)==horse.entity||PED::_GET_RIDER_OF_MOUNT(horse.entity,true)==player.entity){
        TASK::TASK_DISMOUNT_ANIMAL(player.entity,0,0,0,0,0);
        Log("sale dismount started");
    }
    // Per-frame input suppression has no persistent control lock to leak on
    // pause/load/failure, and does not cancel the scripted dismount task.
    PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
}
void TickPresentation(){
    if(presentation.phase==SalePhase::None)return;
    const auto now=GetTickCount64();const int player=world::Handle(presentation.player);
    if(!player||player!=PLAYER::PLAYER_PED_ID()){
        EndPresentation(true,"player instance changed within current timeline");return;
    }
    PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
    if(now>=presentation.deadline){
        // Payment is final. Stop suppressing input so a failed scripted dismount
        // can be completed manually; retain only the original horse until unused.
        const int retained=world::Handle(presentation.horse);
        if(retained&&(PED::GET_MOUNT(player)==retained||
           (TASK::_IS_PED_LEADING_HORSE(player)&&TASK::_GET_LED_HORSE_FROM_PED(player)==retained)))
            Notice(i18n::Text("Horse sold. Dismount or let go of the reins.","马匹已售出，请下马或松开缰绳。"));
        EndPresentation(true,"presentation timed out; payment is not replayed");return;
    }
    if(presentation.phase==SalePhase::Dismounting){
        if(!Standing(player)){presentation.settled_since=0;return;}
        if(!presentation.settled_since)presentation.settled_since=now;
        if(now-presentation.settled_since<250||!presentation.fade.FullyVisible())return;
        presentation.fade.StartOut(450);presentation.owns_fade=true;
        presentation.phase=SalePhase::FadingOut;presentation.deadline=now+5000;presentation.settled_since=0;
        Log("sale fade out started");return;
    }
    if(presentation.phase==SalePhase::FadingOut){
        if(!presentation.owns_fade||!presentation.fade.Black()||!Standing(player))return;
        const auto& retained=presentation.horse;
        if(ENTITY::DOES_ENTITY_EXIST(retained.entity)&&!world::Still(retained)){
            EndPresentation(true,"sold horse handle replaced; replacement untouched");return;
        }
        if(world::Still(retained)){
            if(PED::_GET_RIDER_OF_MOUNT(retained.entity,true)!=0||now<presentation.next_delete)return;
            presentation.next_delete=now+250;
            int removed=retained.entity;PED::DELETE_PED(&removed);
            if(ENTITY::DOES_ENTITY_EXIST(retained.entity))return;
            Log("sold horse entity removed under owned blackout");
        }
        if(!presentation.settled_since){presentation.settled_since=now;return;}
        if(now-presentation.settled_since<250)return;
        presentation.fade.StartIn(450);presentation.phase=SalePhase::FadingIn;
        presentation.deadline=now+5000;Log("sale fade in started");return;
    }
    if(presentation.fade.FullyVisible())EndPresentation(true,"complete");
}
int OfferedHorse(){return horse_market_target::CurrentTarget().horse;}
void OpenQuote(){
    unavailable_help.Clear(GetTickCount64());
    std::string readiness;
    if(ledger::TradingReadiness(readiness)!=ledger::MarketTransactionState::Ready)return;
    const int horse=OfferedHorse();std::string reason;
    auto captured=ledger::QuoteHorse(horse,options.base_cents,reason);
    if(!captured){SaleNotice(reason);return;}
    const auto check=horse_market_sale::Inspect(horse,captured->identity);
    if(!check.eligible){SaleNotice(check.reason);return;}
    horse_market::SaleQuote display;
    display.horse_identity=captured->identity.guid.Key();display.quote_version=++quote_serial;
    display.title=horse_market::DisplayHorseName(captured->identity.name,captured->identity.model,i18n::Chinese(),
        [](std::string_view model)->std::string{
            const auto* labels=horse_catalog::current.Labels(model);if(!labels)return {};
            const auto local=[](const char* key)->std::string{
                const char* text=HUD::_GET_LABEL_TEXT_2(key);
                return text&&*text&&std::string_view(text)!="NULL"&&std::string_view(text)!=key?text:"";
            };
            const auto breed=local(labels->breed);if(breed.empty())return {};
            const auto short_name=story_horses::BreedOnlyText(breed,local(labels->coat));
            return short_name.empty()?breed:short_name;
        });
    display.base_cents=captured->sale.base_cents;display.total_cents=captured->sale.total_cents;
    display.chinese=i18n::Chinese();display.wins=horse_market::MarketRows(captured->wins,display.chinese);
    if(!menu.Open(std::move(display))){Notice(i18n::Text("The offer could not be shown. Try again shortly.","暂时无法查看报价，请稍后重试。"));Log(menu.Summary());return;}
    quote=std::move(captured);quoted_horse=world::Identify(horse);
    next_check=GetTickCount64()+500;selling=lead_release_requested=false;
    Log("quote opened guid="+quote->identity.guid.Key()+" total_cents="+std::to_string(quote->sale.total_cents));
}
void TickSale(){
    if(!selling||!quote||menu.Active())return;
    const auto horse=world::Handle(quoted_horse),player=PLAYER::PLAYER_PED_ID();
    const auto target=horse_market_target::CurrentTarget(player);
    std::string reason;
    if(!horse||!world::Still(clerk)||Distance(player,clerk.entity)>18||
       Distance(player,horse)>15||(target.horse&&target.horse!=horse)||
       (!lead_release_requested&&target.horse!=horse)||
       !ledger::CheckQuote(*quote,horse,reason)){
        Log(reason.empty()?"sale cancelled: current horse changed":"sale cancelled: "+reason);ClearQuote();return;
    }
    const auto ready=ledger::ValidateSaleForCommit(*quote,horse,reason);
    if(ready==ledger::MarketTransactionState::Saving){
        if(GetTickCount64()>=sale_wait_until){Log("sale confirmation expired while saving; no transaction started");ClearQuote();}
        return;
    }
    if(ready!=ledger::MarketTransactionState::Ready){Log("sale confirmation invalid: "+reason);ClearQuote();return;}
    if(target.leading){
        if(!lead_release_requested){
            const auto released=horse_market_target::ReleaseLeadIfOwned(player,horse);
            if(released!=horse_market_target::LeadRelease::Requested){
                Notice(i18n::Text("You are leading a different horse. Ask for another offer.","你牵着的马已改变，请重新询价。"));ClearQuote();return;
            }
            lead_release_requested=true;dismount_until=GetTickCount64()+12000;
        }else if(GetTickCount64()>=dismount_until){
            Notice(i18n::Text("The horse could not be released. Lead it and try again.","未能松开缰绳，请重新牵马后重试。"));ClearQuote();
        }
        return;
    }
    const auto checked=horse_market_sale::Inspect(horse,quote->identity);
    if(!checked.eligible){SaleNotice(checked.reason);ClearQuote();return;}
    // Commit ownership and money together before starting the presentation.
    // The native backend retains the physical mount (including its saddle
    // visuals) until the dismount and our own blackout are complete.
    const auto approved=quote->identity;
    const auto player_identity=world::Identify(player),horse_identity=quoted_horse;
    const int cents=static_cast<int>(quote->sale.total_cents);
    bool retained_entity=false;
    const auto result=ledger::CommitSale(*quote,horse,[&](std::string& message){
        const auto removed=horse_market_sale::TransferOwnership(horse,approved);
        retained_entity=removed.retained_entity;message=removed.reason;
        switch(removed.state){
        case horse_market_sale::State::RemovedConfirmed:return ledger::RemovalOutcome::Removed;
        case horse_market_sale::State::Unchanged:return ledger::RemovalOutcome::Unchanged;
        case horse_market_sale::State::Uncertain:return ledger::RemovalOutcome::Uncertain;
        default:return ledger::RemovalOutcome::NotStarted;
        }
    });
    if(result.sold){notice.clear();BeginPresentation(player_identity,horse_identity,cents);}
    else{
        if(result.reload_required&&retained_entity&&ledger::ObservedTimeline()==quote->timeline)
            ReleaseWhenUnused(horse_identity);
        std::string readiness;
        if(!result.reload_required&&ledger::TradingReadiness(readiness)!=ledger::MarketTransactionState::Ready)
            Log("sale deferred before ownership transfer: "+result.message);
        else SaleNotice(result.message);
    }
    ClearQuote();
}
}
void Initialize(LogFn logger_fn,Options value){
    const auto timeline=ledger::Timeline();
    logger=logger_fn;options=value;menu.Close("runtime initialize");LogMenuDiagnostics();RemovePrompt();RemoveMarker();ClearQuote();
    EndPresentation(!NETWORK::NETWORK_IS_GAME_IN_PROGRESS()&&world_timeline&&world_timeline==timeline,"runtime initialize");
    if(NETWORK::NETWORK_IS_GAME_IN_PROGRESS()||(world_timeline&&world_timeline!=timeline))retained_releases.clear();
    notice.clear();readiness_state.clear();notice_until=0;
    world_timeline=timeline;horse_market_scene::Initialize(logger_fn);
}
bool Active(){return menu.Active()||selling||presentation.phase!=SalePhase::None;}
void Tick(bool available,const char* unavailable_reason){
    const MenuDiagnosticFlush diagnostics;
    // This fixed world location outlives nearby actors and trading eligibility.
    // In particular opening the pause-map must not delete its own destination.
    if(options.enabled&&!NETWORK::NETWORK_IS_GAME_IN_PROGRESS())TickMarker();
    else RemoveMarker();
    const auto now=GetTickCount64();
    // Ledger Tick has already drained observations in the app loop. A fresh
    // Timeline also drains a queued load before touching any cached actor.
    const auto timeline=ledger::Timeline();
    if(world_timeline&&world_timeline!=timeline){menu.Close("save timeline changed");EndPresentation(false,"save timeline changed");retained_releases.clear();RemovePrompt();horse_market_scene::Cleanup();ClearQuote();}
    world_timeline=timeline;
    const bool presenting=presentation.phase!=SalePhase::None;
    const char* blocked_by=options.enabled?WorldBlockedBy(presenting):"market disabled";
    if(blocked_by||!available){
        if(menu.NeedsTick()){
            menu.Close(std::string("runtime blocked: ")+(blocked_by?blocked_by:(unavailable_reason?unavailable_reason:"other activity")));
            menu.Tick();
        }
        const bool same_world=!NETWORK::NETWORK_IS_GAME_IN_PROGRESS();
        RemovePrompt();ClearQuote();EndPresentation(same_world,"world unavailable");
        if(!same_world)retained_releases.clear();
        horse_market_scene::Tick(false);DrawNotice();return;
    }
    horse_market_scene::Tick(true);EnsurePrompt();
    TickRetainedReleases();
    if(presenting){HidePrompts();TickPresentation();return;}
    if(menu.NeedsTick()){
        HidePrompts();
        if(const auto event=menu.Tick()){
            if(event->action==horse_market::SaleAction::SellRequested&&quote&&
               event->quote.quote_version==quote_serial&&event->quote.horse_identity==quote->identity.guid.Key()&&
               event->quote.total_cents==quote->sale.total_cents&&world::Still(quoted_horse)&&
               OfferedHorse()==quoted_horse.entity){
                selling=true;sale_wait_until=now+15000;Log("sale requested guid="+quote->identity.guid.Key()+" total_cents="+std::to_string(quote->sale.total_cents));
            }
            else {
                if(event->action==horse_market::SaleAction::SellRequested)
                    Log("sale confirmation rejected: frozen quote or current horse changed");
                if(event->action==horse_market::SaleAction::BackClosed&&!menu.Failure().empty()){
                    Notice(i18n::Text("The offer could not be shown. Try again shortly.","暂时无法查看报价，请稍后重试。"));
                    Log("menu unavailable: "+menu.Failure()+"; "+menu.Summary());
                }
                ClearQuote();
            }
        }
        if(quote&&!selling&&now>=next_check){
            next_check=now+500;std::string reason;
            if(!world::Still(quoted_horse)||OfferedHorse()!=quoted_horse.entity||
               !ledger::CheckQuote(*quote,quoted_horse.entity,reason)){
                menu.Close(reason.empty()?"quoted horse changed":"quote invalid: "+reason);
                ClearQuote();
            }
        }
        if(menu.Active())return;
        if(!selling)ClearQuote();
    }
    if(selling){HidePrompts();TickSale();DrawNotice();return;}
    const auto player=PLAYER::PLAYER_PED_ID();Entity target=0;
    const bool nearby=world::Still(clerk)&&Distance(player,clerk.entity)<=15;
    const bool focused=nearby&&PLAYER::GET_PLAYER_TARGET_ENTITY(PLAYER::PLAYER_ID(),&target)&&target==clerk.entity;
    std::string readiness;
    const auto ready=ledger::TradingReadiness(readiness);
    if(nearby){
        const auto state=ready==ledger::MarketTransactionState::Ready?std::string("ready"):
            "hidden: "+readiness;
        if(state!=readiness_state){Log("interaction readiness="+state);readiness_state=state;}
    }
    if(ready!=ledger::MarketTransactionState::Ready){
        SetPrompt(prompt,false,false);SetPrompt(member_prompt,false,false);ResetMembershipHold();
        unavailable_help.Update(focused,readiness,now);pressed=true;next_eligibility=0;return;
    }
    const auto offered=horse_market_target::CurrentTarget(player);
    if(nearby&&(offered!=eligibility_target||now>=next_eligibility)){
        eligibility_target=offered;next_eligibility=now+250;sale_eligible=false;
        eligibility_reason="no current mounted or led horse";
        std::string state="sale eligibility target="+std::to_string(offered.horse)+" leading="+std::to_string(offered.leading)+
            " on_mount="+std::to_string(PED::IS_PED_ON_MOUNT(player))+" mount="+std::to_string(PED::GET_MOUNT(player));
        if(offered.horse){
            std::string reason;const auto value=ledger::QuoteHorse(offered.horse,options.base_cents,reason);
            if(!value){eligibility_reason=reason;state+=" stage=ledger_quote enabled=0 reason="+reason;}
            else{
                const auto check=horse_market_sale::Inspect(offered.horse,value->identity);
                sale_eligible=check.eligible;eligibility_reason=check.reason;
                state+=" guid="+value->identity.guid.Key()+" slot="+std::to_string(value->identity.slot)+
                    " wins="+std::to_string(value->wins.size())+" premium_cents="+std::to_string(value->sale.premium_cents)+
                    " stage="+(check.eligible?std::string("ready enabled=1"):std::string("native_preflight enabled=0 reason=")+check.reason);
            }
        }else state+=" stage=current_target enabled=0 reason=no current mounted or led horse";
        if(state!=eligibility_state){Log(state);eligibility_state=std::move(state);}
    }
    unavailable_help.Update(focused&&!sale_eligible,eligibility_reason,now);
    SetPrompt(prompt,nearby,nearby&&sale_eligible);
    const bool unlocked=progression::runtime::Betting();
    const auto era=ledger::CurrentSeller();
    const bool honorary=HonoraryEligible();
    const horse_ledger::Cents fee=honorary?0:horse_ledger::kMembershipFeeCents;
    if(era!=member_era||fee!=member_fee){
        ResetMembershipHold();member_era=era;member_fee=fee;
        if(member_prompt&&HUD::_UI_PROMPT_IS_VALID(member_prompt))
            HUD::_UI_PROMPT_SET_TEXT(member_prompt,i18n::Literal(honorary?
                i18n::Text("Honorary Membership ($0)","荣誉会员（$0）"):
                i18n::Text("Club Membership ($1000)","赛马会会员（$1000）")));
    }
    std::string member_reason;
    const bool member_visible=nearby&&!unlocked;
    const bool member_enabled=member_visible&&ledger::CanPurchaseMembership(member_reason,HonoraryEligible);
    SetPrompt(member_prompt,member_visible,member_enabled);
    if(!focused||!member_enabled)ResetMembershipHold();
    else{
        const auto control=MISC::GET_HASH_KEY("INPUT_INTERACT_LOCKON_NEG");
        const bool held=PAD::IS_CONTROL_PRESSED(0,control)||PAD::IS_DISABLED_CONTROL_PRESSED(0,control);
        if(!held)member_armed=true;
        if(held&&member_armed&&HUD::_UI_PROMPT_HAS_HOLD_MODE_COMPLETED(member_prompt)){
            ResetMembershipHold();
            const auto result=ledger::PurchaseMembership(member_era,[]{return progression::runtime::Betting();},HonoraryEligible,member_fee);
            if(result.purchased){
                auto text=progression::MembershipRegistered(member_fee==0,i18n::Chinese());
                progression_toast::Show(std::move(text.title),std::move(text.body),text.duration_ms);
                notice.clear();Log("Club membership registered.");
            }else{
                std::string readiness;
                if(!result.reload_required&&ledger::TradingReadiness(readiness)!=ledger::MarketTransactionState::Ready)
                    Log("membership deferred: "+result.message);
                else Notice(result.message);
            }
            if(result.purchased)SetPrompt(member_prompt,false,false);
            next_eligibility=0;
        }
    }
    if(focused&&now>=next_greeting){
        next_greeting=now+1500;
        horse_market_scene::Greet(true,sale_eligible);
    }else if(!focused)horse_market_scene::Greet(false,false);
    const bool down=focused&&sale_eligible&&prompt&&HUD::_UI_PROMPT_IS_VALID(prompt)&&HUD::_UI_PROMPT_IS_PRESSED(prompt);
    if(down&&!pressed)OpenQuote();pressed=down;DrawNotice();
}
}
