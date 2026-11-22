#pragma once
#include <cmath>

namespace horse_races::bookmaking {
inline float RoundIconWidth(float height,float surface_aspect) {
    return std::isfinite(height) && height>0 && std::isfinite(surface_aspect) && surface_aspect>=1 &&
        surface_aspect<=4?height/surface_aspect:height/(16.0F/9.0F);
}
} // namespace horse_races::bookmaking
