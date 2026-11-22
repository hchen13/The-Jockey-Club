#pragma once

#include "race_character_policy.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace horse_races::bookmaking {
// Each venue draws from all admitted hosts except the current protagonist.
// Its field size is fixed independently of the growing candidate pool.
// Coat and handling type are deliberately not part of this draw:
// each rider keeps their own horse, while this one race shares two base ranks.
inline constexpr std::array<characters::Id,13> kHosts{
    characters::Id::MaryBeth,characters::Id::Sadie,characters::Id::Charles,
    characters::Id::Uncle,characters::Id::Karen,characters::Id::Pearson,
    characters::Id::Arthur,characters::Id::John,characters::Id::Trelawny,
    characters::Id::RainsFall,characters::Id::Tilly,characters::Id::Javier,characters::Id::Bill};
inline constexpr std::size_t kExhibitionRacers=10; // Capacity/Valentine count, not candidate-pool size.
inline std::vector<characters::Id> Roster(characters::Id protagonist) {
    if(protagonist!=characters::Id::Arthur && protagonist!=characters::Id::John)return {};
    std::vector<characters::Id> result;
    for(auto host:kHosts)if(host!=protagonist)result.push_back(host);
    return result;
}
template<class Random> std::vector<characters::Id> DrawRoster(
    characters::Id protagonist,std::size_t count,Random random) {
    auto available=Roster(protagonist);
    if(count==0 || count>available.size())return {};
    // Fisher-Yates without replacement; there is no permanently favoured
    // host or fixed grid order in a smaller WFA field.
    for(std::size_t i=available.size();i>1;--i){
        const int drawn=random(0,static_cast<int>(i));
        if(drawn<0 || drawn>=static_cast<int>(i))return {};
        std::swap(available[i-1],available[static_cast<std::size_t>(drawn)]);
    }
    available.resize(count);
    return available;
}
inline bool KnownHost(characters::Id id) {
    for(auto host:kHosts)if(host==id)return true;
    return false;
}
struct SharedRanks {
    int speed=-1,acceleration=-1; // engine's 0..9 ranks; UI displays +1.
    bool Valid() const {return speed>=5 && speed<=9 && acceleration>=1 && acceleration<=9;}
};
template<class Random> SharedRanks DrawSharedRanks(Random random) {
    // Random(min,exclusive_max) matches GET_RANDOM_INT_IN_RANGE. The two
    // draws happen once when an exhibition is accepted, never per horse.
    SharedRanks result{random(6,11)-1,random(2,11)-1};
    return result.Valid()?result:SharedRanks{};
}
inline constexpr float kBurstLevels=.5F;
inline constexpr double kBurstChargeSeconds=3.0,kBurstDurationSeconds=5.0;
inline constexpr int kMaxStakeCents=125000;
inline constexpr int kStakeStepCents=100; // $1 minimum/step; returns remain exact cents.

enum class Market {Win,TopThree};
enum class Finish {Placed,Lost,AccidentalVoid,PlayerSabotage};
inline constexpr int ProfitMultiplier(Market market) {return market==Market::Win?3:1;}
inline bool Wins(Market market,int place) {return place>0 && (market==Market::Win?place==1:place<=3);}
// A confirmed ticket debits the stake immediately. The only later cash
// operation is the gross return: (locked Win odds + 1)x for Win, 2x for Place,
// 1x for a void, zero for a loss or player sabotage. Win odds are locked at
// confirmation, so later race changes cannot alter an accepted ticket.
class Ticket {
public:
    enum class State {Empty,DebitPending,Active,CreditPending,Closed};
private:
    State state_=State::Empty;
    characters::Id selection_=characters::Id::OtherRival;
    Market market_=Market::Win;
    int stake_=0,pending_credit_=0,net_odds_hundredths_=300;
public:
    State Phase() const {return state_;}
    int Stake() const {return stake_;}
    int PendingDebit() const {return state_==State::DebitPending?stake_:0;}
    int PendingCredit() const {return state_==State::CreditPending?pending_credit_:0;}
    characters::Id Selection() const {return selection_;}
    Market BetMarket() const {return market_;}
    int LockedNetOdds() const {return net_odds_hundredths_;}
    bool Begin(characters::Id selection,Market market,int cents,int balance_cents,
        int win_multiplier=3) {
        return win_multiplier>=1 && win_multiplier<=20 &&
            BeginQuoted(selection,market,cents,balance_cents,win_multiplier*100);
    }
    bool BeginQuoted(characters::Id selection,Market market,int cents,int balance_cents,
        int win_net_odds_hundredths) {
        const int odds=market==Market::Win?win_net_odds_hundredths:100;
        if(state_!=State::Empty || cents<kStakeStepCents || cents%kStakeStepCents ||
            cents>balance_cents || cents>kMaxStakeCents ||
            odds<10 || odds>9900 || odds%10 ||
            static_cast<std::int64_t>(cents)*(100+odds)/100>std::numeric_limits<int>::max() ||
            !KnownHost(selection))return false;
        selection_=selection;market_=market;stake_=cents;
        net_odds_hundredths_=odds;state_=State::DebitPending;
        return true;
    }
    bool DebitSucceeded() {
        if(state_!=State::DebitPending)return false;
        state_=State::Active;return true;
    }
    bool Settle(Finish finish,int place=0) {
        if(state_!=State::Active)return false;
        if(finish==Finish::AccidentalVoid)pending_credit_=stake_;
        else if(finish==Finish::PlayerSabotage || finish==Finish::Lost)pending_credit_=0;
        else pending_credit_=Wins(market_,place)?static_cast<int>(
            static_cast<std::int64_t>(stake_)*(100+net_odds_hundredths_)/100):0;
        state_=pending_credit_>0?State::CreditPending:State::Closed;
        return true;
    }
    bool CreditSucceeded() {
        if(state_!=State::CreditPending)return false;
        pending_credit_=0;state_=State::Closed;return true;
    }
};
} // namespace horse_races::bookmaking
