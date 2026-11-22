#pragma once
#include "race_segmented_circuit.h"
#include "race_wager.h"

namespace horse_races::distance_pricing {
// INI spelling: Distance. Numeric coefficients remain explicit overrides.
inline constexpr double kAutomatic=0;

inline double ThreeLapMetres(const lap::Definition& definition) {
    if(definition.single_loop || definition.single_trip)return 0;
    auto three=definition;
    // The yardstick is three laps even when the offered instance draws another
    // count or its author narrows the random range to exclude three.
    three.extra_min=three.extra_max=1;
    lap::Plan plan;
    return plan.Build(three,1)?plan.course.path.Length():0;
}

inline double Multiplier(double configured,double metres,double reference_metres) {
    if(configured!=kAutomatic)return wager::ScaleHalfUnits(configured)?configured:0;
    if(!std::isfinite(metres) || !std::isfinite(reference_metres) || metres<=0 || reference_metres<=0)return 0;
    // Keep the complete ratio, including fractions below one. Only the final
    // roster purse is rounded to dollars; no per-lap/per-rider rounding.
    const double factor=metres/reference_metres;
    return std::isfinite(factor) && factor>0 && factor<=100?factor:0;
}
} // namespace horse_races::distance_pricing
