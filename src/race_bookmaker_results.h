#pragma once
#include <string>

namespace horse_races::bookmaking {
// Result data is presentation only; the ticket remains the source of truth.
struct ResultNotes {
    std::string description,odds,stake,payout,profit_label;
    bool wagered=false;
};
// Presentation only. Amounts come from the already settled ticket, never from
// a second odds calculation or a HUD-triggered wallet mutation.
inline ResultNotes ResultsNotes(bool wagered,bool chinese,const std::string& choice,
    bool win,const std::string& stake,const std::string& odds,
    const std::string& returned){
    if(!wagered)return {};
    const std::string market=chinese?(win?"独赢":"位置"):(win?"Win":"Place");
    return {
        (chinese?"下注对象：":"PICK: ")+choice+"\n"+
            (chinese?"玩法：":"RULE: ")+market,
        (chinese?"赔率\n":"ODDS\n")+odds,
        (chinese?"赌注\n":"STAKE\n")+stake,
        (chinese?"派彩\n":"PAYOUT\n")+returned,
        chinese?"盈亏":"PROFIT",true
    };
}
} // namespace horse_races::bookmaking
