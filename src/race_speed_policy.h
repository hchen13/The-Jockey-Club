#pragma once
#include <atomic>

namespace horse_races::race_speed_policy {
// The script thread publishes the player's switch before either race runtime
// ticks. The native output callback may run on an engine worker.
inline std::atomic_bool enabled{false};
inline bool Enabled(){return enabled.load(std::memory_order_acquire);}
inline void SetEnabled(bool value){enabled.store(value,std::memory_order_release);}
}
