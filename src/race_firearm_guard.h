#pragma once
#include "race_character_policy.h"
#include <main.h>
#include "game_natives.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace horse_races::firearm_guard {
// Entity proof flags, not damage multipliers. Leave fire/explosion/collision/
// melee/projectile flags alone. Native proof references are recorded in docs.
inline constexpr int kBulletProof=1, kHeadshotProof=128;
inline constexpr int kGunProofs=kBulletProof|kHeadshotProof;
struct Identity {
    Ped ped{}; Hash model{}; std::uintptr_t address{};
    bool operator==(const Identity&) const = default;
    bool Valid() const { return ped && model && address; }
};
inline Identity Identify(Ped ped)
{
    if(!ped || !ENTITY::DOES_ENTITY_EXIST(ped))return {};
    return {ped,ENTITY::GET_ENTITY_MODEL(ped),reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(ped))};
}
struct Entry { Identity owner{}; int added_proofs{}; bool horse{},releasing{}; };
// Tracks ONLY explicitly registered mod-created entities, never a world scan.
// Retain protection after mission ownership is released: an ambient clone can
// still be encountered again before the engine actually removes it.
inline std::vector<Entry> entries;
inline std::chrono::steady_clock::time_point next_maintenance{};
inline constexpr std::size_t kEntryLimit=512;

inline bool Restore(const Entry& entry)
{
    if(entry.added_proofs && Identify(entry.owner.ped)==entry.owner) {
        const int current=ENTITY::_GET_ENTITY_PROOFS(entry.owner.ped);
        if(Identify(entry.owner.ped)!=entry.owner)return true;
        if(current&entry.added_proofs)
            ENTITY::SET_ENTITY_PROOFS(entry.owner.ped,current&~entry.added_proofs,false);
        return Identify(entry.owner.ped)!=entry.owner ||
            (ENTITY::_GET_ENTITY_PROOFS(entry.owner.ped)&entry.added_proofs)==0;
    }
    return true;
}
inline void Release(Ped ped)
{
    for(auto it=entries.begin();it!=entries.end();) {
        if(it->owner.ped!=ped) {++it;continue;}
        it->releasing=true;
        if(Restore(*it))it=entries.erase(it);
        else ++it; // Retry restoration; never reapply protection to a claimed horse.
    }
}
// A confirmed world replacement invalidates every old entity reference,
// including a numerically identical handle/model/address in the new world.
// Discard bookkeeping only: do not identify, read, or restore an entity.
inline void Discard(Ped ped)
{
    std::erase_if(entries,[ped](const Entry& entry){return entry.owner.ped==ped;});
}
// For a horse whose ownership is uncertain, relinquish this exact lease once.
// Remove all registration/retry state before the native attempt. An unconfirmed
// restore must never become a later write to a recycled entity identity.
inline bool ReleaseOnceAndDiscard(const Identity& expected)
{
    std::vector<Entry> restore;
    std::erase_if(entries,[&](const Entry& entry){
        if(entry.owner.ped!=expected.ped)return false;
        if(entry.owner==expected)restore.push_back(entry);
        return true;
    });
    bool confirmed=true;
    for(const auto& entry:restore)if(!Restore(entry))confirmed=false;
    return confirmed;
}
inline void Reset()
{
    for(auto& entry:entries)entry.releasing=true;
    std::erase_if(entries,[](const Entry& entry) {return Restore(entry);});
    next_maintenance={};
}
inline void ForgetGone()
{
    std::erase_if(entries,[](const Entry& entry) {return Identify(entry.owner.ped)!=entry.owner;});
}
inline bool Track(Ped ped,bool horse)
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    const auto id=Identify(ped);
    if(!id.Valid() || ped==player || (horse && player && PED::GET_MOUNT(player)==ped) ||
        NETWORK::NETWORK_IS_SESSION_ACTIVE())return false;
    ForgetGone();
    if(const auto it=std::find_if(entries.begin(),entries.end(),[&](const Entry& e) {return e.owner==id;});
        it!=entries.end())return it->horse==horse && !it->releasing;
    if(entries.size()>=kEntryLimit)return false;
    const int before=ENTITY::_GET_ENTITY_PROOFS(ped);
    Entry entry{id,kGunProofs&~before,horse};
    if(entry.added_proofs)ENTITY::SET_ENTITY_PROOFS(ped,before|kGunProofs,false);
    if(Identify(ped)!=id || (ENTITY::_GET_ENTITY_PROOFS(ped)&kGunProofs)!=kGunProofs) {
        if(!Restore(entry)) {entry.releasing=true;entries.push_back(entry);}
        return false;
    }
    entries.push_back(entry);
    return true;
}
inline bool ProtectPair(characters::Id character,Ped rider,Ped horse)
{
    if(!characters::ProtectFromFirearms(character))return true; // Do not strip anyone else's proofs.
    if(!rider || !horse || rider==horse)return false;
    if(!Track(rider,false))return false;
    if(!Track(horse,true)) {Release(rider);return false;}
    return true;
}
inline bool HoldsFirearm(Ped player)
{
    if(WEAPON::_IS_PED_CURRENT_WEAPON_HOLSTERED(player))return false;
    // Include both hand slots. Do not suppress unarmed dialogue or lasso use.
    for(int hand:{0,1}) {
        Hash weapon{};
        if(WEAPON::GET_CURRENT_PED_WEAPON(player,&weapon,false,hand,false) && WEAPON::IS_WEAPON_A_GUN(weapon))return true;
    }
    return false;
}
inline void Tick()
{
    if(entries.empty())return;
    if(NETWORK::NETWORK_IS_SESSION_ACTIVE())return; // No online input/proof changes.
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player))return;
    // A taken horse must not carry a mod-created bulletproof upgrade to the
    // player or stable. Rider protection remains independent of that handoff.
    const Ped player_mount=PED::GET_MOUNT(player);
    const auto now=std::chrono::steady_clock::now();
    for(auto it=entries.begin();it!=entries.end();) {
        if(it->owner.ped==player || (it->horse && it->owner.ped==player_mount)) {
            it->releasing=true;
        }
        if(it->releasing && now>=next_maintenance && Restore(*it))it=entries.erase(it);
        else ++it;
    }
    if(now>=next_maintenance) {
        next_maintenance=now+std::chrono::milliseconds(250);
        ForgetGone();
        for(const auto& entry:entries) {
            if(entry.releasing)continue;
            const int current=ENTITY::_GET_ENTITY_PROOFS(entry.owner.ped);
            if((current&kGunProofs)!=kGunProofs)
                ENTITY::SET_ENTITY_PROOFS(entry.owner.ped,current|kGunProofs,false);
        }
    }
    if(entries.empty() || HUD::IS_PAUSE_MENU_ACTIVE() || ENTITY::IS_ENTITY_DEAD(player) || !HoldsFirearm(player))return;
    const Player player_id=PLAYER::PLAYER_ID();
    Entity selected{};
    const bool selected_valid=PLAYER::GET_PLAYER_TARGET_ENTITY(player_id,&selected);
    for(const auto& entry:entries) {
        if(entry.releasing)continue;
        if(Identify(entry.owner.ped)!=entry.owner || ENTITY::IS_ENTITY_DEAD(entry.owner.ped))continue;
        if((selected_valid && selected==entry.owner.ped) ||
            PLAYER::IS_PLAYER_FREE_AIMING_AT_ENTITY(player_id,entry.owner.ped) ||
            PLAYER::IS_PLAYER_TARGETTING_ENTITY(player_id,entry.owner.ped,true)) {
            // Frame-local input suppression. Never change global weapon damage,
            // player relationship groups, targeting mode or dialogue lock-on.
            PLAYER::DISABLE_PLAYER_FIRING(player_id,true);
            return;
        }
    }
}
}
