#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace horse_races::horse_market_tack {
using Cell=std::uint64_t;
// Generic inventory item data, not the horse-specific full 29-cell layout.
using Item=std::array<Cell,14>;
struct Equipment {
    std::uint32_t slot=0;
    std::vector<Item> items;
    std::array<Cell,4> equipped{};
    bool has_equipped=false;
    bool operator==(const Equipment&)const=default;
};
struct Plan {
    int horse=0,slot=-1,inventory=0;
    bool saddled=false,has_container=false;
    std::array<Cell,4> root{},container{};
    Item container_item{};
    std::vector<Equipment> equipment;
    std::array<Cell,80> common{};
    std::array<Cell,74> cargo{};
    Cell saddle_object=0;
    std::uintptr_t saddle_object_address=0;
    std::uint32_t saddle_object_model=0;
    std::uintptr_t address=0;
    std::uint32_t model=0;
};
// Requires successful version-gated individual_identity::Capture first.
// Rejects carried pelts/carcasses, which vanilla merges with loss at capacity.
bool Prepare(int horse,int slot,Plan& plan,std::string& reason,bool allow_player_rider=false);
// Mutation; caller must durably persist mutation-entered first. Never retry on false.
bool Relocate(const Plan& plan,std::string& reason);
// Ownership transfer: allow the player rider and retain every visible component.
bool DetachBindingPreservingVisuals(const Plan& plan,std::string& reason);
// Read-only proof that an unsuccessful ownership request left saddle/cargo intact.
bool Unchanged(const Plan& plan,std::string& reason,bool allow_player_rider=false);
// Call after horse GUID removal, before entity deletion/payout.
bool Preserved(const Plan& plan,std::string& reason);
}
