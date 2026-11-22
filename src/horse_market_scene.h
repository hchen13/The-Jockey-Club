#pragma once
#include <string>

namespace horse_races::horse_market_scene {
using LogFn=void (*)(const std::string&);
struct Pose {float x,y,z,heading;};
Pose Location();
void Initialize(LogFn log);
// Call false across save/load, player switch or other unsafe world transitions.
void Tick(bool world_ready);
void Cleanup();
int Broker();
bool Ready();
// Root supplies the lock-on state and the same eligibility used by Sell Horse.
void Greet(bool focused,bool eligible);
}
