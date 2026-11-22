#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// The player's answer to a host's earned bonus: a horse stimulant used during
// a race puts the player's mount, for a few seconds, on speed level 11 - one
// past the top of the curve - and acceleration level 10, whichever of the two
// is higher than the mount's own. The game sells three - regular, potent,
// special - and the burst lasts 8, 10 or 20 seconds at bonding level 1,
// scaled by the mount's bonding (x1, x1.5, x2, x2.5, x3 at levels 1..5 -
// the game's bonding ranks 0..4; until 2026-09-19 ranks 3 and 4 were one
// level), so a bonded horse answers a stimulant for longer. The game's own horse
// attributes are never written; only the racing curve the mod drives sees the
// levels, so nothing can fail to restore.
//
// Pure: a completed horse interaction and HORSE_STIMULANT_GIVEN's increment
// confirm a dose, even if an infinite-items trainer keeps the stack unchanged.
// Inventory changes and a gold stamina core do not confirm consumption.
namespace horse_races::stimulant {
enum class Tier { None=0, Regular=1, Potent=2, Special=3 };
inline const char* TierName(Tier t) {
    switch(t) {
    case Tier::Regular:return "regular";
    case Tier::Potent:return "potent";
    case Tier::Special:return "special";
    default:return "none";
    }
}
// The curve knows displayed levels 1..10; level 11 is the force one step
// past the top, so the burst may exceed a rank-10 horse.
inline constexpr int kMaxBurstSpeedLevel=11;
struct Config {
    double regular_seconds=8,potent_seconds=10,special_seconds=20; // The burst at bonding level 1.
    std::array<float,5> bonding_factor{1.0F,1.5F,2.0F,2.5F,3.0F}; // Bonding levels 1..5: the game's ranks 0..4.
    int speed_level=11,acceleration_level=10;                    // The levels the mount runs on while the burst runs.
    // Retained for compatibility with old INIs. Confirmed interactions now
    // supply their actual tier; the old gold-core fallback is not used.
    Tier uncounted_tier=Tier::Potent;
    bool Valid() const {
        if(!(regular_seconds>=0 && potent_seconds>=0 && special_seconds>=0))return false;
        if(speed_level<1 || speed_level>kMaxBurstSpeedLevel || acceleration_level<1 || acceleration_level>10)return false;
        for(float f:bonding_factor)if(!std::isfinite(f) || f<=0 || f>10)return false;
        return true;
    }
    double Seconds(Tier t,int bonding_level) const {
        const double base=t==Tier::Regular?regular_seconds:t==Tier::Potent?potent_seconds:t==Tier::Special?special_seconds:0;
        return base*bonding_factor[static_cast<std::size_t>(std::clamp(bonding_level,1,5)-1)];
    }
};
struct Counts {int regular=0,potent=0,special=0;};
struct Scope {
    std::uint64_t race=0;
    int player=0,mount=0;
    bool operator==(const Scope&) const=default;
    bool Eligible() const {return race && player>0 && mount>0;}
};
struct Interaction {
    bool known=false;
    int state=0,mount=0;
    std::uint32_t item=0,started_ms=0;
    bool consumed=false;
};
// The names, not guessed native/event hashes. These are the game's item keys.
constexpr std::uint32_t ItemHash(const char* name) {
    std::uint32_t hash=0;
    for(;*name;++name){hash+=static_cast<unsigned char>(*name);hash+=hash<<10;hash^=hash>>6;}
    hash+=hash<<3;hash^=hash>>11;return hash+(hash<<15);
}
inline Tier ItemTier(std::uint32_t item) {
    if(item==ItemHash("consumable_horse_stimulant") || item==ItemHash("consumable_horse_stimulant_used"))return Tier::Regular;
    if(item==ItemHash("consumable_potent_horse_stimulant"))return Tier::Potent;
    if(item==ItemHash("consumable_special_horse_stimulant") || item==ItemHash("consumable_special_horse_stimulant_crafted"))return Tier::Special;
    return Tier::None;
}
class DoseDetector {
    struct Action {
        std::uint32_t item=0,started_ms=0;
        int mount=0,before=0;
        double completed_at=0;
        bool complete=false,handled=false;
        bool Same(const Interaction& i) const {return item==i.item && mount==i.mount && started_ms==i.started_ms;}
    } action_,completed_;
    Scope scope_;
    int last_total_=-1;
    double last_now_=0,last_increment_=-1;
    bool primed_=false;
    // The stat and the script snapshot can straddle the effects frame. A
    // completed action may wait this long for its one matching increment;
    // an increment never gets carried into a later action's item selection.
    static constexpr double kConfirmSeconds=1.0;
public:
    void Reset(){*this={};}
    Tier Update(double now,const Scope& scope,int total,const Interaction& interaction) {
        if(!scope.Eligible() || !std::isfinite(now) || total<0){Reset();return Tier::None;}
        if(!primed_ || scope_!=scope || now<last_now_ || total<last_total_ ||
            static_cast<std::int64_t>(total)-last_total_>1) {
            Reset();scope_=scope;last_total_=total;last_now_=now;primed_=true;
            // A first read, save rollback, or stat jump is a new baseline,
            // including a consumption marker already set before this read.
            if(interaction.known && interaction.consumed)
                action_={interaction.item,interaction.started_ms,interaction.mount,total,now,true,true};
            return Tier::None;
        }
        const int before=last_total_;
        const bool increment=total>before;
        if(increment)last_increment_=now;
        last_total_=total;last_now_=now;
        // A different completed action on another target or of another item
        // class cannot supply a delayed confirmation for an older stimulant.
        if(interaction.known && interaction.consumed && interaction.state>=6 &&
            (interaction.mount!=scope.mount || ItemTier(interaction.item)==Tier::None)) {
            action_={};completed_={};return Tier::None;
        }
        const auto confirm=[&]() {
            if(!completed_.complete || completed_.handled)return Tier::None;
            if(now-completed_.completed_at>kConfirmSeconds){completed_.handled=true;return Tier::None;}
            if(static_cast<std::int64_t>(total)!=static_cast<std::int64_t>(completed_.before)+1 || last_increment_<0 ||
                now-last_increment_>kConfirmSeconds || completed_.mount!=scope.mount)return Tier::None;
            completed_.handled=true;
            // An older completed dose owns this increment, even if the next
            // item is already being interacted with. That next dose needs its
            // own increment, not this one's delayed stat read.
            if(!action_.Same({true,6,completed_.mount,completed_.item,completed_.started_ms,true}))
                action_.before=total;
            return ItemTier(completed_.item);
        };
        const Tier prior=confirm();
        const bool active=interaction.known && interaction.mount==scope.mount &&
            (interaction.state==5 || ((interaction.state==6 || interaction.state==7) && interaction.consumed));
        const bool complete=active && interaction.consumed && interaction.state>=6;
        if(active && ItemTier(interaction.item)!=Tier::None) {
            if(!action_.Same(interaction)) {
                // Only a completion observed alongside this increment can
                // take the previous baseline. A newly starting action takes
                // the current total, so an older credit cannot fund it.
                action_={interaction.item,interaction.started_ms,interaction.mount,
                    complete && increment && prior==Tier::None?before:total,now,complete,false};
            } else if(complete && !action_.complete) {
                action_.complete=true;action_.completed_at=now;
            }
        } else if(!action_.complete)action_={}; // Cancelled before INTERACT.
        if(action_.complete && !action_.handled && (!completed_.complete || completed_.handled)) {
            completed_=action_;action_.handled=true;
        }
        return prior!=Tier::None?prior:confirm();
    }
};
class Tracker {
    Scope owner_;
    double until_=0,seconds_=0;
    Tier tier_=Tier::None;
    int bonding_=1;
    unsigned doses_=0;
public:
    void Reset(){owner_={};until_=0;seconds_=0;tier_=Tier::None;bonding_=1;}
    // A dose belongs to this race/player/horse. A short dismount does not
    // extend or cancel its timer; mounting a different horse cannot inherit it.
    bool Bind(const Scope& scope) {
        const bool changed=!scope.race || scope.player<=0 || owner_.race!=scope.race ||
            owner_.player!=scope.player || (scope.mount>0 && owner_.mount!=scope.mount);
        if(changed){Reset();owner_=scope;}
        return changed;
    }
    unsigned Doses() const {return doses_;}
    Tier Current() const {return tier_;}
    double Seconds() const {return seconds_;}
    int Bonding() const {return bonding_;}
    bool Active(double now) const {return tier_!=Tier::None && std::isfinite(now) && now<until_;}
    double Remaining(double now) const {return Active(now)?until_-now:0;}
    // Only the detector's confirmed dose starts (or restarts) the same burst.
    Tier Update(double now,Tier taken,int bonding_level,const Config& config) {
        if(!std::isfinite(now) || !config.Valid() || taken<Tier::Regular || taken>Tier::Special)return Tier::None;
        bonding_=std::clamp(bonding_level,1,5);
        seconds_=config.Seconds(taken,bonding_);
        tier_=taken;until_=now+seconds_;++doses_;
        return taken;
    }
    // The burst is over: back to the rank's own force.
    void Expire(){tier_=Tier::None;until_=0;}
};
// What the mount runs on while the burst runs. A stimulant never slows a
// horse: each level is the higher of the mount's own and the burst's. The
// curve takes levels 1..10, so `speed` above 10 is driven as level 10 with
// that slot's force raised to the level's (BurstForce). A mount whose levels
// the game did not report (0) is left alone.
struct Levels {
    int speed=0,acceleration=0; // The burst's levels; speed may be 11.
    int curve_speed=0,curve_acceleration=0; // What the curve is told.
    bool operator==(const Levels&) const=default;
};
inline Levels Boosted(const Config& config,int mount_speed,int mount_acceleration) {
    if(mount_speed<1 || mount_speed>10 || mount_acceleration<1 || mount_acceleration>10)
        return {mount_speed,mount_acceleration,mount_speed,mount_acceleration};
    Levels l;
    l.speed=std::max(mount_speed,config.speed_level);l.acceleration=std::max(mount_acceleration,config.acceleration_level);
    l.curve_speed=std::min(l.speed,10);l.curve_acceleration=std::min(l.acceleration,10);
    return l;
}
// The force of displayed `level` on the curve: the slot's own for 6..10,
// none below 6 (those ride on gait ceilings), and past 10 the last step of
// the curve continued, the same continuation a host's bonus uses at the top.
inline float BurstForce(const std::array<float,5>& forces,int level) {
    if(level<6)return 0;
    if(level<=10)return forces[static_cast<std::size_t>(level-6)];
    return forces[4]+(forces[4]-forces[3])*static_cast<float>(level-10);
}
} // namespace horse_races::stimulant
