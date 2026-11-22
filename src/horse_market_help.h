#pragma once
#include "race_help.h"
#include "race_localization.h"
#include <string_view>

namespace horse_races::horse_market_help {
inline std::string Message(std::string_view reason) {
    struct Translation {std::string_view reason;const char* english;const char* chinese;};
    static constexpr Translation messages[]{
        {"remove stored pelts before selling this horse","Unload the pelts before selling this horse.","先卸下马上的皮毛，再出售这匹马。"},
        {"remove stored animal cargo before selling this horse","Unload the pelts or carcasses before selling this horse.","先卸下马上的皮毛或猎物，再出售这匹马。"},
        {"remove stored carcasses before selling this horse","Unload the pelts or carcasses before selling this horse.","先卸下马上的皮毛或猎物，再出售这匹马。"},
        {"remove carried cargo before selling this horse","Unload the horse before selling it.","先卸下马上的货物，再出售这匹马。"},
        {"remove carried cargo before sale","Unload the horse before selling it.","先卸下马上的货物，再出售这匹马。"},
        {"remove pelts before sale","Unload the pelts before selling this horse.","先卸下马上的皮毛，再出售这匹马。"},
        {"remove carcass before sale","Unload the pelts or carcasses before selling this horse.","先卸下马上的皮毛或猎物，再出售这匹马。"},
        {"no current mounted or led horse","Mount or lead the horse you wish to sell.","骑上或牵着要出售的马。"},
        {"The club only buys horses with an official race victory.","The club only buys horses that have won an official race.","赛马会只收购赢过正式比赛的马匹。"},
        {"The game is saving. Please wait before selling.","Saving. Please wait.","正在保存，请稍候。"},
        {"The game is saving.","Saving. Please wait.","正在保存，请稍候。"},
        {"A save or load operation is still active.","Saving or loading. Please wait.","正在保存或载入，请稍候。"},
        {"The current save archive is not ready.","Your records are not ready yet. Please wait.","记录尚未读取完毕，请稍候。"},
        {"The current save is still loading.","Loading your save. Please wait.","正在载入存档，请稍候。"},
        {"horse must be healthy and out of combat","The horse must be healthy and out of combat before it can be sold.","马匹恢复健康并脱离战斗后，才能出售。"},
        {"another rider is using this horse","Wait for the other rider to dismount.","等其他骑手下马后再出售。"},
        {"This horse has already been sold.","This horse has already been sold.","这匹马已经出售。"},
        {"Horse sales are unavailable.","Horse sales are disabled.","售马功能已关闭。"},
        {"Finish or cancel the current race before trading.","Finish or cancel your race first.","先完成或取消当前比赛。"},
        {"A race payment is still pending.","Wait for the race payment to be settled.","等比赛款项结清后再交易。"},
        {"A market transaction is already in progress.","Finish the current transaction first.","先完成当前交易。"},
        {"The game world is not ready for trading.","The game world is not ready for trading.","游戏世界尚未就绪，暂时无法交易。"},
        {"Horse sales require Arthur or John.","Only Arthur or John can sell horses.","只有亚瑟或约翰可以出售马匹。"}
    };
    for(const auto& entry:messages)if(reason==entry.reason)return i18n::Text(entry.english,entry.chinese);
    // Transaction outcomes carry safety information, not ordinary eligibility.
    // Match them before technical categories; never turn an uncertain payment
    // into an invitation to retry the sale.
    if(reason=="Sale could not be confirmed. Reload your pre-sale save before continuing." ||
       reason=="The sale could not be confirmed. Load your save from before the sale." ||
       reason=="交易结果无法确认，请载入出售前的存档。")
        return i18n::Text("The sale could not be confirmed. Load your save from before the sale.",
            "交易结果无法确认，请载入出售前的存档。");
    for(const std::string_view outcome:{
        "Racing records could not be read. Your records have been kept; load a matching save or repair the archive.",
        "赛马记录读取失败，原有记录已保留。请载入匹配的存档或修复档案。",
        "This save does not match its racing records. Your records have been kept; load a matching save or repair the archive.",
        "此存档与赛马记录不匹配，原有记录已保留。请载入匹配的存档或修复档案。",
        "Racing records could not be saved. Your records have been kept. Check storage and save again.",
        "赛马记录保存失败，原有记录已保留。请检查存储空间后重新保存。",
        "The sale could not be confirmed. No money was paid. Reload a save before trading again; race records remain available.",
        "交易未能确认，未支付钱款。请重新载入存档后再交易，赛马记录仍然保留。",
        "The sale could not be completed. Your horse and money are unchanged.",
        "交易未完成。马匹仍归你所有，钱款未变。",
        "An earlier horse sale needs review. Reload a save before trading again.",
        "上一笔卖马交易尚未确认，请重新载入存档后再交易。",
        "This horse's price exceeds the game's cash limit.",
        "The sale would exceed the game's cash limit.",
        "Could not prepare the sale receipt.",
        "The quoted price changed. Open Sell Horse again.",
        "The save or race history changed. Open Sell Horse again.",
        "dismount before sale",
        "finish dismounting before sale",
        "finish releasing the lead before sale",
        "Could not write the sale receipt. No sale was attempted.",
        "Could not prepare ownership transfer. No sale was attempted.",
        "The save changed. No sale was attempted.",
        "Could not prepare the sale. No sale was attempted.",
        "Horse sold."})if(reason==outcome)return std::string(reason);
    // The producer's ownership stage takes precedence over words in its detail.
    if(reason.starts_with("Owned horse identity unavailable:"))
        return i18n::Text("This horse's ownership could not be verified. Try again shortly.",
            "暂时无法确认这匹马的归属，请稍后重试。");
    const auto has=[&](std::string_view part){return reason.find(part)!=std::string_view::npos;};
    // A changed snapshot requires another offer, not repeated use of the old one.
    if(reason.starts_with("identity changed:") ||
       reason=="saddle assignment changed" || reason=="owned horse changed during confirmation" ||
       reason=="mount or lead relationship changed before sale" || reason=="horse inventory slot changed before removal" ||
       reason=="The horse changed. Open Sell Horse again.")
        return i18n::Text("The horse or saddle has changed. Ask for another offer.",
            "马匹或马鞍状态已改变，请重新询价。");
    if(reason=="horse cargo layout unavailable")
        return i18n::Text("The horse's cargo could not be checked. Try again shortly.",
            "暂时无法检查马上的货物，请稍后重试。");
    if(has("tack") || has("saddle") || reason.starts_with("generic inventory"))
        return i18n::Text("The horse's equipment could not be checked. Try again shortly.",
            "暂时无法检查这匹马的马具，请稍后重试。");
    if(reason.starts_with("horse inventory") ||
       reason.starts_with("horse entity") || reason.starts_with("horse identity") ||
       reason.starts_with("owned horse slot") || reason.starts_with("individual horse identity") ||
       reason.starts_with("horse/player") || reason.starts_with("duplicate horse inventory") ||
       reason=="horse is not mapped to a player-owned slot" || reason=="entity is not an existing horse" ||
       reason.starts_with("special horse slots") || reason=="stable record layout changed")
        return i18n::Text("This horse's ownership could not be verified. Try again shortly.",
            "暂时无法确认这匹马的归属，请稍后重试。");
    // Raw native details belong only in diagnostics, never in player help.
    return i18n::Text("The sale could not be prepared. Try again shortly.",
        "暂时无法办理这笔交易，请稍后重试。");
}
// A separate owner from race_help's global presenter. One short message per
// focus/reason episode; never refresh every frame or clear another feed owner.
class Presenter {
    race_help::Native api;
    std::string shown;
    int handle=0;
    std::uint64_t posted=0,retry=0;
    bool delivered=false;
public:
    void Clear(std::uint64_t now) {
        if(handle && now>=posted && now-posted<race_help::kVisibleWaitMs+race_help::kDurationMs){
            const int state=api.State(handle);
            if((state>=0 && state<3) || (state>=3 && state<6 && api.Current()==handle))api.Clear(handle);
        }
        handle=0;shown.clear();delivered=false;retry=0;
    }
    void Update(bool active,const std::string& reason,std::uint64_t now) {
        const auto text=active&&!reason.empty()?Message(reason):std::string{};
        if(text!=shown){Clear(now);shown=text;}
        if(shown.empty()||delivered||now<retry||api.Paused())return;
        handle=api.Post(shown);posted=now;
        if(handle)delivered=true;else retry=now+race_help::kRetryMs;
    }
};
}
