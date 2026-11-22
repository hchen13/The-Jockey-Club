#pragma once
#include "horse_motion_limits.h"
#include <main.h>
#include <cstdint>

// Read-only 1491.50 evidence: beat_horse_race.c func_78/258/259 awards
// Global_40.f_1095.f_3140.f_1; player_horse.c func_661 consumes that BOOL
// for the permanent +1 speed attribute. It is NOT an encounter count or bit.
namespace horse_races::native_reward {
inline constexpr int kFirstRewardGlobal=40+1095+3140; // Faster bonding when spurring.
inline constexpr int kSecondRewardGlobal=kFirstRewardGlobal+1;
enum class Status {UnsupportedBuild,Unreadable,Invalid,NotAwarded,Awarded};
inline const char* Name(Status status) {
    switch(status) {
    case Status::UnsupportedBuild:return "unsupported game build";
    case Status::Unreadable:return "reward global unreadable";
    case Status::Invalid:return "invalid reward flag";
    case Status::NotAwarded:return "native second speed reward not yet awarded";
    case Status::Awarded:return "native second speed reward awarded";
    }
    return "unknown reward state";
}
struct NativeApi {
    bool Supported() const {
        // Same verified binary-layout guard as the player stimulant reader.
        static const auto layout=horse_motion::engine_limits::ValidateLayout();
        return layout.valid;
    }
    bool ReadGlobal(int index,std::uint64_t& out) const {
        // A null, stale or unreadable script page fails closed, without a raw
        // pointer dereference. No game/save memory is ever written here.
        return horse_motion::engine_limits::Read(
            reinterpret_cast<std::uintptr_t>(getGlobalPtr(index)),out);
    }
};
template<class Api> inline Status ReadFlag(const Api& api,int global) {
    if(!api.Supported())return Status::UnsupportedBuild;
    std::uint64_t cell=0;
    if(!api.ReadGlobal(global,cell))return Status::Unreadable;
    // VM cells are 8 bytes, but script BOOL values occupy the low 32 bits.
    // The unused upper half is not part of the boolean's validity check.
    const auto flag=static_cast<std::uint32_t>(cell);
    if(flag>1)return Status::Invalid;
    return flag==1?Status::Awarded:Status::NotAwarded;
}
template<class Api> inline Status Read(const Api& api){return ReadFlag(api,kSecondRewardGlobal);}
inline Status ReadNativeReward(){return Read(NativeApi{});}
inline Status ReadFirstNativeReward(){return ReadFlag(NativeApi{},kFirstRewardGlobal);}
} // namespace horse_races::native_reward
