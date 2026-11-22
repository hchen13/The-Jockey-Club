#pragma once
#include "horse_race_ledger.h"
#include <optional>
#include <string>
namespace horse_races::individual_identity {
struct Identity {
    horse_ledger::Guid guid;
    std::string name;
    std::uint32_t model{};
    int slot{-1};
};
enum class Ownership { Unknown, Owned, NotOwned };
enum class OwnershipIssue {
    None, WrongThread, UnsupportedBuild, Multiplayer, PlayerUnavailable,
    InvalidHorse, SnapshotUnavailable, AmbiguousSlot, SpecialSlot,
    ModelMismatch, InventoryUnavailable, InventoryUnstable, IdentityChanged, Exception
};
struct OwnershipProbe {
    Ownership state{Ownership::Unknown};
    OwnershipIssue issue{OwnershipIssue::None};
    std::optional<Identity> identity;
    std::string detail;
};
// Read-only, with the same calling/build restrictions as Capture. NotOwned is
// emitted only after stable player/entity checks and three identical complete
// player-horse table snapshots explicitly exclude this exact horse entity.
// Unavailable/ambiguous metadata is Unknown, never evidence of nonownership.
OwnershipProbe ProbeOwnership(int horse);
// Call only on the ScriptHook script fiber, never the save-observer worker.
// Exact1491.50 SP only. Slots5/6 and ambiguous/transient mappings fail closed.
// Name is display metadata; the validated inventory GUID is the identity key.
std::optional<Identity> Capture(int horse,std::string& reason);
}
