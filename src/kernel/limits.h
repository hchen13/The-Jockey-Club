#pragma once
#include <cstddef>

// Every fixed size in the mod derives from one number: how many jockeys a route may ask for.
//
// Today the same fact is written down in a dozen places - an array of 12 here, a check against 8
// there, a pool sized 24 somewhere else - and when the user asked for a bigger field one of them was
// missed. A race is bounded, so the bound is stated once and everything else is arithmetic.
// It lives in the kernel and not in src/pure, where 04-architecture.md §3 put it. The layering says
// a module may only call downwards, and pure sits ABOVE world - so world/registry, which needs to
// know how many entities it can hold, could not have read it there. Numbers with no dependencies
// belong at the bottom.
namespace horse_races::limits {

// What a route file's FieldMax may ask for (R-FIELD).
inline constexpr int kMaxFieldRivals = 7;
// ...plus the host, plus the player.
inline constexpr int kMaxRiders = kMaxFieldRivals + 2;
// Each rider sits on a horse, and a race may also make a starter to drop the flag.
inline constexpr int kMaxRaceEntities = kMaxRiders * 2 + 1;

// A recorded line. The engine's own waypoint recording holds far fewer (course::kNativeWaypointNodes)
// and that is checked where a course is registered; this is what a Path may hold in memory.
inline constexpr std::size_t kMaxLineNodes = 16384;

// How many entities the world registry can own at once: the race's, plus a camp's residents, props
// and horses, plus whatever a development tool has made.
inline constexpr std::size_t kMaxOwnedActors = 72;

static_assert(kMaxRiders == 9, "the budgets in docs/refactor/04-architecture.md are written for nine riders");
static_assert(kMaxRaceEntities == 19, "the budgets are written for nineteen entities");

}  // namespace horse_races::limits
