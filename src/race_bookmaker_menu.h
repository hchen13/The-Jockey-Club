#pragma once

#include "race_bookmaking.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace horse_races::bookmaking {
// Selection is a transaction proposal, not a cash write. The runtime debits
// only after Confirm, and never on Back or a failed scene preparation.
struct BetChoice {characters::Id rider;Market market;int cents;};
enum class MenuInput {Up,Down,Left,Right,Accept,Back};
class Menu {
public:
    enum class Page {Closed,Riders,Market,Stake};
private:
    Page page_=Page::Closed;
    std::vector<characters::Id> roster_;
    std::size_t rider_=0;
    Market market_=Market::Win;
    int cents_=kStakeStepCents,balance_=0;
public:
    Page Current() const {return page_;}
    std::size_t RiderIndex() const {return rider_;}
    characters::Id Rider() const {return roster_.at(rider_);}
    Market SelectedMarket() const {return market_;}
    int Stake() const {return cents_;}
    int Balance() const {return balance_;}
    void Close(){page_=Page::Closed;roster_.clear();rider_=0;market_=Market::Win;cents_=kStakeStepCents;balance_=0;}
    void AdjustAmount(int delta_cents){
        // Both the maximum and every delta are aligned. A cash balance such as
        // $27.53 must cap at $27, never create an invalid partial-denomination bet.
        const long long delta=static_cast<long long>(delta_cents/kStakeStepCents)*kStakeStepCents;
        if(page_==Page::Stake)cents_=static_cast<int>(std::clamp(static_cast<long long>(cents_)+delta,
            static_cast<long long>(kStakeStepCents),static_cast<long long>(balance_)));
    }
    bool Open(std::vector<characters::Id> roster,int balance) {
        Close();
        if(roster.empty() || balance<kStakeStepCents)return false;
        roster_=std::move(roster);balance_=std::min(balance,kMaxStakeCents)/kStakeStepCents*kStakeStepCents;
        page_=Page::Riders;return true;
    }
    std::optional<BetChoice> Press(MenuInput input) {
        if(page_==Page::Closed)return {};
        if(input==MenuInput::Back) {
            switch(page_) {
            case Page::Riders:Close();break;
            case Page::Market:page_=Page::Riders;break;
            case Page::Stake:page_=Page::Market;break;
            default:break;
            }
            return {};
        }
        if(page_==Page::Riders) {
            if(input==MenuInput::Up)rider_=(rider_+roster_.size()-1)%roster_.size();
            else if(input==MenuInput::Down)rider_=(rider_+1)%roster_.size();
            else if(input==MenuInput::Accept)page_=Page::Market;
        } else if(page_==Page::Market) {
            if(input==MenuInput::Left || input==MenuInput::Right || input==MenuInput::Up || input==MenuInput::Down)
                market_=market_==Market::Win?Market::TopThree:Market::Win;
            else if(input==MenuInput::Accept)page_=Page::Stake;
        } else if(page_==Page::Stake) {
            if(input==MenuInput::Up)AdjustAmount(kStakeStepCents);
            else if(input==MenuInput::Down)AdjustAmount(-kStakeStepCents);
            else if(input==MenuInput::Accept)return BetChoice{Rider(),market_,cents_};
        }
        return {};
    }
};
// Poker-style stake adjustment: one dollar on a tap, then increasingly fast
// repeats on a sustained hold. Wall-clock milliseconds keep this responsive
// while the stock reduced menu time scale is active.
class AmountRepeat {
    int direction_=0;
    std::uint64_t started_=0,next_=0;
public:
    void Reset(){direction_=0;started_=next_=0;}
    int Update(int direction,std::uint64_t now) {
        if(direction==0){Reset();return 0;}
        direction=direction>0?1:-1;
        if(direction_!=direction){direction_=direction;started_=now;next_=now+350;return direction*kStakeStepCents;}
        if(now<next_)return 0;
        const auto held=now-started_;
        const int cents=kStakeStepCents*(held>=5000?25:held>=3000?10:held>=1500?5:1);
        const std::uint64_t interval=held>=5000?40:held>=3000?60:held>=1500?85:110;
        next_=now+interval;
        return direction*cents;
    }
};
} // namespace horse_races::bookmaking
