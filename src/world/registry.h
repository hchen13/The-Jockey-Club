#pragma once
#include "identity.h"
#include "leases.h"
#include "../kernel/limits.h"
#include <array>
#include <cstdint>

// Everything the mod has made and is therefore responsible for.
//
// Today ownership is implicit: an entity is "ours" because some global variable happens to hold its
// handle, and it stops being ours when whichever piece of code remembers to let go does so. The
// failures that come of it are the ones the user has reported by name - a host still standing at his
// post after the race that borrowed him ended, a second Arthur in the camp, a race steering a horse
// that had become somebody else's between one frame and the next.
//
// Three things fix that, and they only work together:
//
//   * One place knows what is ours. `IsOurs` is how the course sweep tells a bystander from a racer,
//     instead of comparing against a handful of globals that each piece of code knows a different
//     subset of.
//   * A reference is not a handle. It is a slot and a generation, so a reference to something that
//     has been retired - or to a whole world that has been unloaded - reads back as nothing instead
//     of as whatever is in that slot now.
//   * There is one way out. `Retire` is the only exit, and it takes the same Release modes as a
//     lease: after a save is loaded the entities belong to a world that no longer exists, and the
//     only safe thing is to forget them.
namespace horse_races::world {

enum class Kind : std::uint8_t { Rider, Horse, Prop, Resident, Starter };

// A slot and the generation that slot was in when the reference was taken. The generation carries
// the session in its high byte, so a save load invalidates every reference in one stroke.
struct Ref {
    std::uint16_t slot = 0;
    std::uint16_t gen = 0;
    bool Known() const { return gen != 0; }
    bool operator==(const Ref&) const = default;
};

class Registry {
public:
    // A save was loaded, or the world went away. Everything from before belongs to that world.
    void NewSession() {
        session_ = static_cast<std::uint8_t>(session_ + 1);
        if (session_ == 0) session_ = 1;
        for (auto& slot : slots_) slot = {};
    }

    // Takes ownership of an entity the mod has just made. The identity is read now, so a handle that
    // is recycled later stops answering for this one.
    Ref Adopt(Kind kind, int entity) {
        const Identity id = Identify(entity);
        if (!id.Known()) return {};
        for (std::uint16_t i = 0; i < slots_.size(); ++i) {
            Slot& slot = slots_[i];
            if (slot.live) continue;
            slot = {id, kind, Generation(), true};
            return {i, slot.gen};
        }
        return {};   // Full. The caller makes room and tries again; it never silently loses one.
    }

    // The raw handle, or 0. Zero is what a caller must get for something gone, because a native
    // given a stale handle acts on whatever is in that slot now.
    int Handle(Ref ref) const {
        const Slot* slot = Live(ref);
        return slot ? Handle(slot->id) : 0;
    }

    bool Has(Ref ref) const { return Handle(ref) != 0; }

    // What the course sweep asks about everything it finds: is this one of ours, or a bystander?
    bool IsOurs(int entity) const {
        if (!entity) return false;
        for (const auto& slot : slots_)
            if (slot.live && slot.id.entity == entity && Still(slot.id)) return true;
        return false;
    }

    // The station's host becomes the race's host: the same entity, a different owner. Nothing is
    // created and nothing is destroyed, which is the point - the host the player rode up to is the
    // host on the starting line.
    bool Transfer(Ref ref, Kind kind) {
        Slot* slot = Live(ref);
        if (!slot) return false;
        slot->kind = kind;
        return true;
    }

    Kind KindOf(Ref ref) const {
        const Slot* slot = Live(ref);
        return slot ? slot->kind : Kind::Prop;
    }

    // The one way out. Normal deletes the entity; Abandon forgets it, because after a save load the
    // handle names something in a world that is gone.
    void Retire(Ref ref, Release mode, const std::function<void(int)>& remove) {
        Slot* slot = Live(ref);
        if (!slot) return;
        const int entity = Handle(slot->id);
        slot->live = false;
        slot->id = {};
        if (mode == Release::Normal && entity && remove) remove(entity);
    }

    // Slots whose entity has gone by itself - killed, streamed out, deleted by another script - stop
    // being ours. Without this the mod counts as owning things it does not.
    void Sweep() {
        for (auto& slot : slots_)
            if (slot.live && !Still(slot.id)) { slot.live = false; slot.id = {}; }
    }

    std::size_t Count() const {
        std::size_t live = 0;
        for (const auto& slot : slots_) live += slot.live ? 1 : 0;
        return live;
    }
    std::size_t Count(Kind kind) const {
        std::size_t live = 0;
        for (const auto& slot : slots_) live += slot.live && slot.kind == kind ? 1 : 0;
        return live;
    }

private:
    struct Slot {
        Identity id;
        Kind kind = Kind::Prop;
        std::uint16_t gen = 0;
        bool live = false;
    };

    // The session in the high byte, a counter in the low one: a reference from before a save load
    // cannot match a slot that has been reused since.
    std::uint16_t Generation() {
        ++counter_;
        if (counter_ == 0) counter_ = 1;
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(session_) << 8) | counter_);
    }
    const Slot* Live(Ref ref) const {
        if (!ref.Known() || ref.slot >= slots_.size()) return nullptr;
        const Slot& slot = slots_[ref.slot];
        return slot.live && slot.gen == ref.gen ? &slot : nullptr;
    }
    Slot* Live(Ref ref) { return const_cast<Slot*>(std::as_const(*this).Live(ref)); }
    using Identity = world::Identity;
    static int Handle(const Identity& id) { return world::Handle(id); }

    std::array<Slot, limits::kMaxOwnedActors> slots_{};
    std::uint8_t session_ = 1;
    std::uint8_t counter_ = 0;
};

}  // namespace horse_races::world
