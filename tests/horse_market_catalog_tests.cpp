#include "../src/horse_market_catalog.h"
#include "../src/horse_market_name.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace horse_races;
namespace {
void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
horse_ledger::Win Win(std::string id,std::string host,std::int64_t cents){
    horse_ledger::Win win;win.event_id=id;win.horse.words={1,2,3,4};win.race_id="scarlett-meadows-bill-duel:duel:28";
    win.race_name="Rider Duel";win.net_cents=cents;win.opponents.push_back({host,0,8,8,true});return win;
}
void TestCatalog(){
    std::vector<horse_ledger::Win> wins{Win("one","Bill",100),Win("two","Bill",200),Win("three","Javier",1234),Win("zero","John",0)};
    wins[1].race_id="different-route:duel:44";wins[1].protagonist="John";
    auto practice=Win("practice","Bill",9999);practice.practice=true;wins.push_back(practice);
    auto rows=horse_market::MarketRows(wins,false);
    Check(rows.size()==2 && rows[0].race_name=="Bill's Duel" && rows[0].net_bonus_cents==300 && rows[1].race_name=="Javier's Duel" && rows[1].net_bonus_cents==1234,"duel aggregation or actual prizes incorrect");
    auto zh=horse_market::MarketRows(wins,true);
    Check(zh.size()==2 && zh[0].race_name=="比尔对决赛" && zh[1].race_name=="哈维尔对决赛","Chinese first names incorrect");
    const std::vector<horse_ledger::Win> named{Win("pearson","Pearson",100),Win("trelawny","Trelawny",200),Win("rains","RainsFall",300),Win("charles","Charles",400)};
    const auto named_before=named;rows=horse_market::MarketRows(named,false);zh=horse_market::MarketRows(named,true);
    Check(rows[0].race_name=="Pearson's Duel"&&rows[1].race_name=="Trelawny's Duel"&&rows[2].race_name=="Rains Fall's Duel"&&rows[3].race_name=="Charles's Duel","recognized host names or possessive Duel format incorrect");
    Check(zh[0].race_name=="皮尔逊对决赛"&&zh[1].race_name=="特里劳尼对决赛"&&named==named_before,"host presentation changed receipts or Chinese duel format");
    auto limited=Win("limited","Arthur",500);limited.kind=horse_ledger::RaceKind::BreedLimited;limited.race_id="arthur-camp:breed-limited:19";
    wins={limited};limited.event_id="john";limited.race_id="john-stakes:breed-limited:21";limited.opponents[0].character="John";wins.push_back(limited);
    rows=horse_market::MarketRows(wins,false);zh=horse_market::MarketRows(wins,true);
    Check(rows.size()==1 && rows[0].race_name=="Thoroughbred Challenge" && rows[0].net_bonus_cents==1000 && zh[0].race_name=="纯血马限定赛","same breed split by host or protagonist");
    limited.race_id="custom:breed-limited:MissouriFoxTrotter";wins={limited};
    Check(horse_market::MarketRows(wins,false)[0].race_name=="foxtrotter Challenge" && horse_market::MarketRows(wins,true)[0].race_name=="狐步马限定赛","foxtrotter short name incorrect");
    limited.race_id="standardbred-stakes:breed-limited:24";wins={limited};
    Check(horse_market::MarketRows(wins,false)[0].race_name=="Standardbred Challenge" && horse_market::MarketRows(wins,true)[0].race_name=="标准种马限定赛","Standardbred short name incorrect");
    wins={Win("positive","Bill",100),Win("negative","Bill",-100)};
    Check(horse_market::MarketRows(wins,false).empty(),"nonpositive aggregate not removed");
    wins={Win("max","Bill",std::numeric_limits<std::int64_t>::max()),Win("extra","Bill",1)};
    bool rejected=false;try{horse_market::MarketRows(wins,false);}catch(const std::overflow_error&){rejected=true;}
    Check(rejected,"aggregation overflow silently clipped or wrapped");
    horse_market::SaleQuote q{"horse",1,"Rachel",1000,{{"Bill's Duel",1},{"Bill's Duel",1}},1002};
    Check(!horse_market::ValidSaleQuote(q),"duplicate presentation row accepted");q.wins={{"Bill's Duel",0}};q.total_cents=1000;
    Check(!horse_market::ValidSaleQuote(q),"zero presentation row accepted");q.wins.clear();
    Check(horse_market::ValidSaleQuote(q),"base-only quote rejected");
}
void TestHorseDisplayName(){
    using horse_market::DisplayHorseName;
    constexpr std::uint32_t silver_bay=0xF31B7859; // Actual WINTER02_01 joaat, not an invented model.
    Check(horse_market::name_detail::ModelHash("A_C_HORSE_WINTER02_01")==silver_bay,"Silver Bay model hash mismatch");
    Check(DisplayHorseName("",silver_bay,false)=="Thoroughbred"&&DisplayHorseName(" \t\r\n",silver_bay,true)=="纯血马","unnamed Silver Bay did not use the localized full breed");
    const std::string custom="Silver <Belle> & 马";
    Check(DisplayHorseName(custom,silver_bay,false)==custom&&DisplayHorseName(custom,silver_bay,true)==custom,"custom name was localized or modified");
    Check(DisplayHorseName("Horse",silver_bay,true)=="Horse","intentional custom name Horse was mistaken for missing name");
    const auto bay=horse_market::name_detail::ModelHash("A_C_HORSE_THOROUGHBRED_BLOODBAY");
    Check(DisplayHorseName("",bay,false)=="Thoroughbred","ordinary Thoroughbred coat did not share breed display");
    const auto saddler=horse_market::name_detail::ModelHash("A_C_HORSE_KENTUCKYSADDLE_SILVERBAY");
    Check(DisplayHorseName("",saddler,true)=="肯塔基骑乘马","Silver Bay coat alone incorrectly classified Kentucky Saddler as Thoroughbred");
    Check(DisplayHorseName("",0,false)=="Horse"&&DisplayHorseName("",0,true)=="马匹","unknown model invented a breed");
    const auto previous=horse_identity::effective;
    horse_identity::effective["A_C_HORSE_WINTER02_01"]={"ARABIAN","COAT_SILVERBAY"};
    Check(DisplayHorseName("",silver_bay,false)=="Arabian","display ignored existing effective breed metadata");
    horse_identity::effective=previous;
    // Every supported ordinary stock model has a readable breed without a race whitelist.
    for(const auto& labels:story_horses::kApproved)
        Check(DisplayHorseName("",horse_market::name_detail::ModelHash(labels.model),false)!="Horse","known stock breed fell back to Horse");
    constexpr std::pair<std::string_view,std::string_view> other_breeds[]{
        {"A_C_HORSE_ANDALUSIAN_DARKBAY","Andalusian"},{"A_C_HORSE_ARDENNES_BAYROAN","Ardennes"},
        {"A_C_HORSE_BELGIAN_BLONDCHESTNUT","Belgian Draft"},{"A_C_HORSE_BRETON_GRULLODUN","Breton"},
        {"A_C_HORSE_CRIOLLO_BAYBRINDLE","Criollo"},{"A_C_HORSE_DUTCHWARMBLOOD_SEALBROWN","Dutch Warmblood"},
        {"A_C_HORSE_GYPSYCOB_PIEBALD","Gypsy Cob"},{"A_C_HORSE_HUNGARIANHALFBRED_DARKDAPPLEGREY","Hungarian Halfbred"},
        {"A_C_HORSE_KLADRUBER_BLACK","Kladruber"},{"A_C_HORSE_MORGAN_BAY","Morgan"},
        {"A_C_HORSE_NOKOTA_WHITEROAN","Nokota"},{"A_C_HORSE_NORFOLKROADSTER_BLACK","Norfolk Roadster"},
        {"A_C_HORSE_SHIRE_DARKBAY","Shire"},{"A_C_HORSE_SUFFOLKPUNCH_SORREL","Suffolk Punch"},
        {"A_C_HORSE_TURKOMAN_GOLD","Turkoman"}};
    for(const auto& [model,expected]:other_breeds){
        horse_identity::effective[std::string(model)]={std::string(horse_identity::StockBreed(model)),horse_identity::DefaultCoat(model)};
        Check(DisplayHorseName("",horse_market::name_detail::ModelHash(model),false)==expected,"supported breed readable name incorrect");
    }
    const auto morgan=horse_market::name_detail::ModelHash("A_C_HORSE_MORGAN_BAY");unsigned localized_calls=0;
    const horse_market::BreedLocalizer localized=[&](std::string_view model){++localized_calls;return model=="A_C_HORSE_MORGAN_BAY"?std::string("游戏本地化摩根马"):std::string{};};
    Check(DisplayHorseName("",morgan,true,localized)=="游戏本地化摩根马"&&localized_calls==1,"game breed localization was ignored");
    Check(DisplayHorseName("Horse",morgan,true,localized)=="Horse"&&localized_calls==1,"custom name unexpectedly consulted or changed game breed label");
    horse_identity::effective=previous;
}
}
int main(){try{TestCatalog();TestHorseDisplayName();std::cout<<"Horse market race aggregation and naming passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
