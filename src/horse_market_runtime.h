#pragma once
#include <cstdint>
#include <string>
namespace horse_races::horse_market_runtime {
struct Options {bool enabled=true;std::int64_t base_cents=1000;};
using LogFn=void (*)(const std::string&);
void Initialize(LogFn logger_fn,Options options={});
// available excludes races, missions and other custom menus.
void Tick(bool available,const char* unavailable_reason="other activity");
bool Active();
}
