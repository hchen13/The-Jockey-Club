#pragma once

#include "race_bookmaking.h"

namespace horse_races::bookmaking {
// These describe the same named markets used by Ticket::Settle: Win pays
// the quoted net odds on first place; Place pays 1-to-1 on any top-three
// finish. Both successful gross returns include the original stake.
inline constexpr const char* MarketRuleDescription(Market market,bool chinese) {
    if(market==Market::Win)
        return chinese?"所选骑手必须获得第一名。派彩包括本金及按所示赔率计算的奖金。":
            "Your pick must place first. Payout includes your stake and the quoted winnings.";
    return chinese?"所选骑手获得前三名即中奖。赔率为 1-to-1，派彩为赌注的两倍。":
        "Your pick must place in the top three. At 1-to-1, payout is twice your stake.";
}
} // namespace horse_races::bookmaking
