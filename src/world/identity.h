#pragma once
#include "../game_natives.h"
#include <main.h>
#include <cstdint>

// Whether the thing you are holding is still the thing you were given.
//
// A game handle is a slot number that the engine reuses. Hold one across a few seconds and it may
// have become somebody else's horse - or, after a save is loaded, somebody else entirely. The mod
// learned this the hard way and answered it nine separate times, in nine files, with three different
// names (`Same()`, `Valid()`, `Identify()`) and two different meanings: one re-asks the game, the
// other only checks the numbers are not zero. Code that wanted the first and called the second is
// how a race ends up steering a stranger's horse.
//
// One answer: a handle is only yours if the game still says it exists, it is still the same model,
// and its script-handle address is unchanged. The address is what catches reuse - a new entity in the
// same slot has a different one.
namespace horse_races::world {

struct Identity {
    int entity = 0;
    std::uint32_t model = 0;
    std::uintptr_t address = 0;

    bool operator==(const Identity&) const = default;
    // Was this ever filled in? Says nothing about whether the entity is still there.
    bool Known() const { return entity != 0 && address != 0; }
};

// Reads the three parts. An entity that does not exist reads as nothing, so an unchecked read cannot
// be mistaken for a live one.
inline Identity Identify(int entity) {
    if (!entity || !ENTITY::DOES_ENTITY_EXIST(entity)) return {};
    return {entity, ENTITY::GET_ENTITY_MODEL(entity),
            reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(entity))};
}

// The question every caller actually wants: is what I was given still what I was given?
inline bool Still(const Identity& id) { return id.Known() && Identify(id.entity) == id; }

// The handle, or 0 if the thing it named is gone. Callers pass this straight to a native, so the
// zero matters: a native given a stale handle acts on whatever is in that slot now.
inline int Handle(const Identity& id) { return Still(id) ? id.entity : 0; }

}  // namespace horse_races::world
