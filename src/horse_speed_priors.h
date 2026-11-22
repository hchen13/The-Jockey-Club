#pragma once
#include <array>
namespace horse_motion::speed_learning {
// The same speed-rank planning priors used by race_pace::ForAttributes.
inline constexpr float PriorTopMps(int rank) {
    constexpr std::array<float,10> kmh{44,46.04F,48.31F,50.79F,53.5F,56.43F,59.58F,62.95F,66.54F,70.35F};
    return rank>=1 && rank<=10?kmh[rank-1]/3.6F:0.F;
}
}
