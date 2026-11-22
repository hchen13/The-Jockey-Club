#pragma once
#include "horse_individual_identity.h"
#include <string>
namespace horse_races::horse_market_sale {
enum class State { NotStarted, Unchanged, RemovedConfirmed, Uncertain, Unsupported };
struct Result { State state{State::NotStarted}; std::string reason; bool retained_entity=false; };
struct Check { bool eligible{}, needs_dismount{}; std::string reason; };
// Read-only. Player may still be riding while inspecting the offer.
Check Inspect(int horse, const individual_identity::Identity& approved);
// Caller must durably record mutation-entered BEFORE this call. Never call it
// again for an uncertain receipt. This adapter NEVER pays money.
// Owned slots 0-4. Dismount first; unload cargo. Saddle inventory is preserved.
// Unchanged means a synchronous request was made but exact ownership, tack and
// world relationships were proven unchanged; no removal/payout may be replayed.
Result Remove(int horse, const individual_identity::Identity& approved);
// Same durable-entry/no-retry contract, but RemovedConfirmed means ownership
// removed while the identical mission-owned entity survives with all visuals.
// Player rider is allowed; finish releasing a lead first. Never pays or yields.
// Caller retains world::Identity and deletes only under its own black-screen
// presentation, after dismount. No further ownership mutation is necessary.
Result TransferOwnership(int horse, const individual_identity::Identity& approved);
}
