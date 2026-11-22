#pragma once
#include "world/frame.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace horse_races::progression {
using Era=world::Protagonist;
enum class Race : std::size_t {
    MaryDuel,PearsonDuel,KarenDuel,CharlesDuel,BillDuel,SadieDuel,JavierDuel,
    Appaloosa,Kentucky,Tennessee,Standardbred,Paint,Arabian,Mustang,
    ArthurBreed,ArthurDuel,JohnBreed,Count
};
inline constexpr std::array<std::string_view,static_cast<std::size_t>(Race::Count)> kKeys{
    "emerald-valentine","rhodes-vanhorn","valentine-circuit","veteran-trail",
    "scarlett-meadows-bill-duel","adler-strawberry","new-austin-javier-duel",
    "appaloosa-stakes","blackwater","tennessee-stakes","standardbred-stakes",
    "paint-stakes","arabian-stakes","new-austin","arthur-camp","arthur-camp-duel","john-stakes"
};
inline constexpr std::array<Race,16> kJohnOrder{
    Race::MaryDuel,Race::PearsonDuel,Race::KarenDuel,Race::CharlesDuel,Race::BillDuel,
    Race::SadieDuel,Race::JavierDuel,Race::Appaloosa,Race::Kentucky,Race::Tennessee,
    Race::Standardbred,Race::Paint,Race::Arabian,Race::Mustang,Race::ArthurBreed,Race::ArthurDuel
};
inline constexpr std::array<Race,10> kArthurOrder{
    Race::MaryDuel,Race::PearsonDuel,Race::KarenDuel,Race::CharlesDuel,Race::BillDuel,
    Race::SadieDuel,Race::Appaloosa,Race::JohnBreed,Race::Standardbred,Race::Paint
};
inline constexpr std::size_t Index(Race race){return static_cast<std::size_t>(race);}
inline std::optional<Race> Find(std::string_view key){
    for(std::size_t i=0;i<kKeys.size();++i)if(kKeys[i]==key)return static_cast<Race>(i);
    return {};
}
inline std::span<const Race> Order(Era era){
    if(era==Era::John)return kJohnOrder;
    if(era==Era::Arthur)return kArthurOrder;
    return {};
}
inline bool Eligible(Era era,Race race){
    if(era==Era::None)return false;
    if(era==Era::John)return race!=Race::JohnBreed && race!=Race::Count;
    return race!=Race::ArthurBreed && race!=Race::ArthurDuel && race!=Race::Count;
}
enum class Action {None,Reset,UnlockNext,UnlockAll,LockPrevious};
enum class ActionResult {Ignored,Blocked,Unchanged,Applied};
struct Campaign {
    std::bitset<kKeys.size()> unlocked,won;
    bool honorary=false;
    bool operator==(const Campaign&) const=default;
};
struct State {
    Campaign john,arthur;
    bool operator==(const State&) const=default;
    Campaign& For(Era era){return era==Era::John?john:arthur;}
    const Campaign& For(Era era) const {return era==Era::John?john:arthur;}
    void Reset(Era era){
        if(era==Era::None)return;
        For(era)={};For(era).unlocked.set(Index(Race::MaryDuel));
    }
    State(){Reset(Era::John);Reset(Era::Arthur);}
    bool Open(Era era,Race race) const {
        return Eligible(era,race) && For(era).unlocked.test(Index(race));
    }
    // Eligibility is not enrollment: only the save-bound membership owns betting.
    bool Betting(Era) const {return false;}
    bool HonoraryMembershipEligible(Era era) const {
        return (era==Era::Arthur || era==Era::John) && For(era).honorary;
    }
    bool ClearHonoraryQualification(Era era){
        if(!HonoraryMembershipEligible(era))return false;
        For(era).honorary=false;return true;
    }
    bool AllEligibleWon(Era era) const {
        if(era!=Era::Arthur && era!=Era::John)return false;
        for(std::size_t i=0;i<kKeys.size();++i)
            if(Eligible(era,static_cast<Race>(i))&&!For(era).won.test(i))return false;
        return true;
    }
    bool Camp(Era era) const {return era==Era::John && Open(era,Race::ArthurBreed);}
    // Unlock-next changes availability only. Unlock-all explicitly completes
    // campaign flags; neither operation creates horse-ledger wins or enrollment.
    bool UnlockNext(Era era){
        if(era!=Era::Arthur && era!=Era::John)return false;
        for(auto race:Order(era))if(!Open(era,race)){For(era).unlocked.set(Index(race));return true;}
        if(!For(era).honorary){For(era).honorary=true;return true;}
        return false;
    }
    bool LockPrevious(Era era){
        if(era!=Era::John && era!=Era::Arthur)return false;
        auto& campaign=For(era);
        // The standalone administrative qualification is reversed first.
        if(campaign.honorary){campaign.honorary=false;return true;}
        const auto order=Order(era);
        std::size_t target=order.size();
        for(std::size_t i=order.size();i>1;--i)
            if(campaign.unlocked.test(Index(order[i-1]))){target=i-1;break;}
        if(target==order.size())return false; // Mary-Beth is the permanent floor.
        for(std::size_t i=target;i<order.size();++i){
            campaign.unlocked.reset(Index(order[i]));campaign.won.reset(Index(order[i]));
        }
        std::size_t predecessor=target;
        while(predecessor>0){--predecessor;if(campaign.unlocked.test(Index(order[predecessor])))break;}
        campaign.unlocked.set(Index(Race::MaryDuel));
        campaign.won.reset(Index(order[predecessor]));
        if(era==Era::Arthur){
            const auto clear_side=[&](Race owner,Race side){
                if(owner==order[target] || owner==order[predecessor]){
                    campaign.unlocked.reset(Index(side));campaign.won.reset(Index(side));
                }
            };
            clear_side(Race::SadieDuel,Race::JavierDuel);
            clear_side(Race::Appaloosa,Race::Kentucky);clear_side(Race::Appaloosa,Race::Tennessee);
            clear_side(Race::Paint,Race::Arabian);clear_side(Race::Paint,Race::Mustang);
        }
        return true;
    }
    void UnlockAll(Era era){
        if(era==Era::None)return;
        for(std::size_t i=0;i<kKeys.size();++i)
            if(Eligible(era,static_cast<Race>(i))){
                For(era).unlocked.set(i);For(era).won.set(i);
            }
        For(era).honorary=true;
    }
    bool Win(Era era,Race race,bool practice=false,bool substituted_host=false){
        if(practice || substituted_host || !Open(era,race))return false;
        auto& campaign=For(era);
        if(campaign.won.test(Index(race))){
            if(!campaign.honorary && AllEligibleWon(era)){campaign.honorary=true;return true;}
            return false;
        }
        campaign.won.set(Index(race));
        const auto order=Order(era);
        const auto at=std::find(order.begin(),order.end(),race);
        if(at!=order.end() && std::next(at)!=order.end())campaign.unlocked.set(Index(*std::next(at)));
        // Western branches do not block the accessible chain; they do count
        // toward completing every eligible duel and breed race for honorary status.
        if(era==Era::Arthur){
            if(race==Race::SadieDuel)campaign.unlocked.set(Index(Race::JavierDuel));
            if(race==Race::Appaloosa){campaign.unlocked.set(Index(Race::Kentucky));campaign.unlocked.set(Index(Race::Tennessee));}
            if(race==Race::Paint){campaign.unlocked.set(Index(Race::Arabian));campaign.unlocked.set(Index(Race::Mustang));}
        }
        if(AllEligibleWon(era))campaign.honorary=true;
        return true;
    }
    std::size_t Wins(Era era) const {
        const auto order=Order(era);
        return std::count_if(order.begin(),order.end(),[&](Race r){return For(era).won.test(Index(r));});
    }
    std::string Encode() const {
        std::ostringstream out;out<<"TheJockeyClubProgress=1\n";
        for(auto era:{Era::John,Era::Arthur}){
            out<<(era==Era::John?"[John]\n":"[Arthur]\n");
            const auto& c=For(era);
            for(std::size_t i=0;i<kKeys.size();++i)if(c.unlocked.test(i) || c.won.test(i))
                out<<kKeys[i]<<'='<<(c.won.test(i)?2:1)<<'\n';
            out<<"honorary="<<(HonoraryMembershipEligible(era)?1:0)<<'\n';
        }
        return out.str();
    }
    static std::optional<State> Decode(std::string_view bytes){
        if(bytes.size()>65536)return {};
        std::istringstream in{std::string(bytes)};std::string line;
        if(!std::getline(in,line))return {};
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line!="TheJockeyClubProgress=1")return {};
        State result;Era era=Era::None;bool john_seen=false,arthur_seen=false;
        std::array<bool,2> honorary_seen{},legacy_honorary{};
        while(std::getline(in,line)){
            if(!line.empty() && line.back()=='\r')line.pop_back();
            if(line.empty() || line.starts_with(';'))continue;
            if(line=="[John]"){if(john_seen)return {};era=Era::John;john_seen=true;continue;}
            if(line=="[Arthur]"){if(arthur_seen)return {};era=Era::Arthur;arthur_seen=true;continue;}
            if(era==Era::None)return {};
            const auto eq=line.find('=');if(eq==line.npos)return {};
            const auto key=std::string_view(line).substr(0,eq),value=std::string_view(line).substr(eq+1);
            if(key=="honorary" || key=="betting"){
                if(value!="0" && value!="1")return {};
                const std::size_t index=era==Era::John?0:1;
                if(key=="honorary"){
                    if(honorary_seen[index])return {};
                    honorary_seen[index]=true;result.For(era).honorary=value=="1";
                }else legacy_honorary[index]=era==Era::John && value=="1";
                continue;
            }
            const auto race=Find(key);if(!race)continue; // future/removed station keys are harmless
            if(value!="1" && value!="2")return {};
            if(!Eligible(era,*race))continue; // never trust a forged Arthur/boss flag
            result.For(era).unlocked.set(Index(*race));
            if(value=="2")result.For(era).won.set(Index(*race));
        }
        if(!john_seen || !arthur_seen)return {};
        for(auto candidate:{Era::John,Era::Arthur}){
            const std::size_t index=candidate==Era::John?0:1;
            if(!honorary_seen[index])result.For(candidate).honorary=legacy_honorary[index]||result.AllEligibleWon(candidate);
        }
        return result;
    }
};
} // namespace horse_races::progression
