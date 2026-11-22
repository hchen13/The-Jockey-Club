#pragma once

namespace horse_motion::speed_limits {
// Capability and physical safety are different limits. Native resources and
// navigation brakes retain their previous range; a requested racing pace may
// also include the approved 50% catch-up. These are validation bounds, not a
// promise that physics will attain the requested speed.
inline constexpr float kNativeMps=25.F;
inline constexpr float kCatchupMultiplier=1.5F;
inline constexpr float kRequestedMps=kNativeMps*kCatchupMultiplier;
inline constexpr float kBaseOutputGain=1.5F;
inline constexpr float kMaxOutputGain=kBaseOutputGain*kCatchupMultiplier;
}
