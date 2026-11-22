#pragma once
#include "horse_market_menu.h"
#include "horse_race_ledger.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string_view>
namespace horse_races::horse_market {
namespace catalog_detail {
struct Name {std::string_view key,en,zh;};
inline constexpr std::array<Name,13> hosts{{
    {"MaryBeth","Mary-Beth","玛丽贝思"},{"Sadie","Sadie","莎迪"},{"Charles","Charles","查尔斯"},
    {"Uncle","Uncle","大叔"},{"Karen","Karen","凯伦"},{"Pearson","Pearson","皮尔逊"},
    {"Arthur","Arthur","亚瑟"},{"John","John","约翰"},{"Trelawny","Trelawny","特里劳尼"},
    {"RainsFall","Rains Fall","落雨"},{"Tilly","Tilly","蒂莉"},{"Javier","Javier","哈维尔"},{"Bill","Bill","比尔"}
}};
inline constexpr std::array<Name,9> breeds{{
    {"KentuckySaddler","Kentucky Saddler","肯塔基骑乘马"},{"Mustang","Mustang","美洲野马"},
    {"Thoroughbred","Thoroughbred","纯血马"},{"Arabian","Arabian","阿拉伯马"},
    {"AmericanStandardbred","Standardbred","标准种马"},{"AmericanPaint","Paint","花马"},
    {"TennesseeWalker","Tennessee Walker","田纳西走马"},{"Appaloosa","Appaloosa","阿帕卢萨马"},
    {"MissouriFoxTrotter","foxtrotter","狐步马"}
}};
inline constexpr std::array<std::pair<std::string_view,std::string_view>,10> stations{{
    {"blackwater","KentuckySaddler"},{"new-austin","Mustang"},{"arthur-camp","Thoroughbred"},
    {"john-stakes","Thoroughbred"},{"arabian-stakes","Arabian"},{"appaloosa-stakes","Appaloosa"},
    {"standardbred-stakes","AmericanStandardbred"},{"paint-stakes","AmericanPaint"},
    {"tennessee-stakes","TennesseeWalker"},{"foxtrotter-stakes","MissouriFoxTrotter"}
}};
inline std::string Host(const horse_ledger::Win& win,bool chinese){
    for(const auto& opponent:win.opponents)if(opponent.host){
        for(const auto& name:hosts)if(name.key==opponent.character)return std::string(chinese?name.zh:name.en);
        const auto end=opponent.character.find_first_of(" ·");
        return opponent.character.substr(0,end);
    }
    return chinese?"骑手":"Rider";
}
inline std::string Breed(const horse_ledger::Win& win,bool chinese){
    std::string_view key;
    // Explicit breed tokens take precedence over legacy station inference.
    for(const auto& breed:breeds)if(win.race_id.find(breed.key)!=std::string::npos || win.race_name.find(breed.key)!=std::string::npos){key=breed.key;break;}
    if(key.empty()){
        const std::string_view station=std::string_view(win.race_id).substr(0,win.race_id.find(':'));
        for(const auto& [site,breed]:stations)if(station==site){key=breed;break;}
    }
    for(const auto& breed:breeds)if(breed.key==key)return std::string(chinese?breed.zh:breed.en);
    return chinese?"品种":"Breed";
}
}
// Presentation only: raw receipt IDs and individual prizes remain untouched.
// Call with a single horse's receipts. Overflow fails closed rather than clipping money.
inline std::vector<WinningRow> MarketRows(const std::vector<horse_ledger::Win>& wins,bool chinese){
    std::vector<WinningRow> rows;
    for(const auto& win:wins){
        if(win.practice)continue;
        const std::string name=win.kind==horse_ledger::RaceKind::BreedLimited?
            catalog_detail::Breed(win,chinese)+(chinese?"限定赛":" Challenge"):
            catalog_detail::Host(win,chinese)+(chinese?"对决赛":"'s Duel");
        auto row=std::find_if(rows.begin(),rows.end(),[&](const WinningRow& prior){return prior.race_name==name;});
        if(row==rows.end()){rows.push_back({name,0});row=rows.end()-1;}
        if((win.net_cents>0 && row->net_bonus_cents>std::numeric_limits<std::int64_t>::max()-win.net_cents) ||
            (win.net_cents<0 && row->net_bonus_cents<std::numeric_limits<std::int64_t>::min()-win.net_cents))
            throw std::overflow_error("market race winnings overflow");
        row->net_bonus_cents+=win.net_cents;
    }
    std::erase_if(rows,[](const WinningRow& row){return row.net_bonus_cents<=0;});
    return rows;
}
}
