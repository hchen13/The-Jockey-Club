#pragma once
#include "fixed_horse_pool.h"
#include <array>
#include <limits>
#include <cstdint>

namespace race_horses {
using horse_attributes::Ranks;
using fixed_horses::Choice;

// -1 acceleration means retain the chosen coat's natural acceleration.
inline std::vector<Choice> Nearest(const std::vector<Choice>& catalog, Ranks target)
{
    std::vector<Choice> result;
    if (target.speed<0 || target.speed>9 || target.acceleration < -1 || target.acceleration>9) return result;
    int best=std::numeric_limits<int>::max();
    for (const auto& c:catalog) {
        if (!c.raw.Valid()) continue;
        const int distance=std::abs(c.raw.speed-target.speed)+
            (target.acceleration<0 ? 0 : std::abs(c.raw.acceleration-target.acceleration));
        if (distance<best) { result.clear(); best=distance; }
        if (distance==best) result.push_back(c);
    }
    return result;
}

struct Coverage {
    std::array<int,10> speed{}, acceleration{};
    void Add(Ranks r) { if(r.Valid()){++speed[r.speed];++acceleration[r.acceleration];} }
};

// Lease retains its own identity even after the race state resets. A handle AND
// ScriptHook entity address AND model must match before any restoration write.
// Never save modified ranks to a model file or to a persistent mod database.
struct Identity {
    Ped horse=0; Hash model=0; std::uintptr_t address=0;
    bool operator==(const Identity&) const = default;
    bool Valid() const { return horse && model && address; }
};
struct Lease {
    Identity owner{};
    Ranks original{}, target{};
    bool pending=false;

    template<class Identify, class Read, class Write>
    bool Restore(Identify identify, Read read, Write write)
    {
        if (!pending) return true;
        if (identify(owner.horse)!=owner) { pending=false; return true; } // Gone/reused: never touch its replacement.
        for (const int index:{5,6}) {
            const int before=index==5?original.speed:original.acceleration;
            const int after=index==5?target.speed:target.acceleration;
            if (before==after) continue;
            const int current=read(owner.horse,index);
            // Don't overwrite a later independent change made by another mod.
            if (current==after) write(owner.horse,index,before);
        }
        pending=false;
        for (const int index:{5,6}) {
            const int before=index==5?original.speed:original.acceleration;
            const int after=index==5?target.speed:target.acceleration;
            if (before!=after && read(owner.horse,index)==after) pending=true;
        }
        return !pending;
    }

    template<class Identify, class Read, class Write>
    bool Apply(Identity id, Ranks baseline, Ranks desired, Identify identify, Read read, Write write)
    {
        if (pending || !id.Valid() || !baseline.Valid() || !desired.Valid() || identify(id.horse)!=id) return false;
        // A matching plan is not proof that the entity was initialised. Verify
        // both fields, including an unchanged acceleration or a no-write lease.
        if (read(id.horse,5)!=baseline.speed || read(id.horse,6)!=baseline.acceleration) return false;
        if (baseline==desired) return true;
        owner=id; original=baseline; target=desired; pending=true;
        for (const int index:{5,6}) {
            const int before=index==5?original.speed:original.acceleration;
            const int after=index==5?target.speed:target.acceleration;
            if (before!=after) write(owner.horse,index,after);
        }
        const bool ok=identify(owner.horse)==owner && read(owner.horse,5)==target.speed &&
            read(owner.horse,6)==target.acceleration;
        if (!ok) Restore(identify,read,write);
        return ok;
    }
};
} // namespace race_horses
