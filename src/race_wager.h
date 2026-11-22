#pragma once

#include "race_breeds.h"
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace horse_races::wager {
enum class Outcome { Win, Loss, Void };
inline constexpr int PrizeForSpeed(int displayed_speed) {
    // One global schedule, never relative to a host's difficulty lower bound.
    // Levels 1..4 contribute zero; they never erase another rider's prize.
    return displayed_speed>=5 && displayed_speed<=10 ? (displayed_speed-4)*1000:0;
}
// Everyone the player races against is priced: the host plus the largest field
// a route may declare. Both bounds are DERIVED, because the previous hardcoded
// five silently rejected every New Austin quote the moment fields grew past
// four rivals - TotalPrize returned nothing and the race cancelled itself.
inline constexpr std::size_t kMaximumPaidOpponents=
    1+static_cast<std::size_t>(field_race::kMaxFieldRivals);
inline constexpr int kMaximumCents=
    PrizeForSpeed(10)*static_cast<int>(kMaximumPaidOpponents)*100; // Distance factor at most 100.
inline int ScaleHalfUnits(double multiplier) {
    if(!std::isfinite(multiplier) || multiplier<1 || multiplier>100)return 0;
    const double twice=multiplier*2;
    return std::trunc(twice)==twice?static_cast<int>(twice):0;
}
inline constexpr int kCashStepCents=100; // Final quote rounds up to whole dollars.
inline bool ValidCash(int cents) {
    return cents>=kCashStepCents && cents<=kMaximumCents && cents%kCashStepCents==0;
}
struct Quote {
    int cents=0;
    bool valid=false; // A valid zero purse is distinct from malformed input.
};
struct RosterBase {
    std::int64_t weighted=0,divisor=1;
    bool valid=false;
};
inline RosterBase PriceRoster(std::span<const int> displayed_speeds)
{
    if(displayed_speeds.empty() || displayed_speeds.size()>kMaximumPaidOpponents)return {};
    // N is the accepted companion roster, excluding host and player. Keep
    // zero-value companions in N. A duel has no field and uses divisor one.
    const auto companions=displayed_speeds.size()-1;
    const std::int64_t divisor=companions?static_cast<std::int64_t>(companions):1;
    std::int64_t weighted_base=0;
    for(std::size_t i=0;i<displayed_speeds.size();++i) {
        const int speed=displayed_speeds[i];
        if(speed<1 || speed>10)return {};
        const int prize=PrizeForSpeed(speed);
        // Full host prize plus the arithmetic mean of companion base prizes.
        weighted_base+=i==0?prize*divisor:prize;
    }
    return {weighted_base,divisor,true};
}
inline Quote QuoteRoster(std::span<const int> displayed_speeds,double multiplier)
{
    const int half_units=ScaleHalfUnits(multiplier);
    const auto base=PriceRoster(displayed_speeds);
    if(!half_units || !base.valid)return {};
    // Apply the route multiplier and round UP to a whole dollar only once. Keep the
    // rational numerator intact: per-rider or per-cent truncation loses money
    // when the mean has thirds, sixths or sevenths of a cent.
    const std::int64_t numerator=base.weighted*half_units;
    const std::int64_t step=base.divisor*2*kCashStepCents;
    const std::int64_t cents=(numerator+step-1)/step*kCashStepCents;
    if(cents>kMaximumCents)return {};
    return {static_cast<int>(cents),true};
}
inline Quote QuoteRosterProportional(std::span<const int> displayed_speeds,double factor) {
    if(!std::isfinite(factor) || factor<=0 || factor>100)return {};
    const auto base=PriceRoster(displayed_speeds);
    if(!base.valid)return {};
    if(!base.weighted)return {0,true};
    const long double dollars=static_cast<long double>(base.weighted)*factor/(base.divisor*kCashStepCents);
    // Suppress binary arithmetic noise at an exact whole-dollar boundary only.
    // This tolerance is many orders below one cent, not a monetary rounding step.
    const long double noise=8*std::numeric_limits<long double>::epsilon()*std::max(1.0L,dollars);
    const long double cents=std::max(1.0L,std::ceil(dollars-noise))*kCashStepCents;
    if(cents>kMaximumCents)return {};
    return {static_cast<int>(cents),true};
}
inline int TotalPrize(std::span<const int> displayed_speeds,double multiplier)
{
    return QuoteRoster(displayed_speeds,multiplier).cents;
}
inline int ScaledPrize(int displayed_speed,double multiplier) {
    return TotalPrize(std::span<const int>(&displayed_speed,1),multiplier);
}

// No cash is debited at acceptance. Reserve the agreed terms, then settle the
// signed NET change once. Actor cleanup must not erase a failed credit/debit.
struct Ledger {
    int stake = 0;
    int net_reward = 0;
    int pending_delta = 0;
    bool open = false;
    bool practice = false;
    bool CanBegin() const { return !open && pending_delta == 0; }
    bool Begin(int stake_cents, bool free_practice, int reward_cents = -1)
    {
        if (reward_cents==-1) reward_cents=stake_cents;
        if (!CanBegin() || (free_practice ? stake_cents!=0 || reward_cents!=0 :
            !ValidCash(stake_cents) || !ValidCash(reward_cents))) return false;
        stake = stake_cents; net_reward=reward_cents; practice = free_practice; open = true;
        return true;
    }
    void Settle(Outcome outcome)
    {
        if (!open) return;
        pending_delta = practice || outcome==Outcome::Void ? 0 : (outcome==Outcome::Win?net_reward:-stake);
        stake = net_reward = 0; open = false;
    }
    bool QueueRewardOnly(int cents) {
        if(!CanBegin() || cents<=0 || cents>kMaximumCents)return false;
        pending_delta=cents;return true;
    }
    void PaymentSucceeded() { pending_delta = 0; }
};
} // namespace horse_races::wager
