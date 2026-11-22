#pragma once
#include "player_stimulant.h"
#include "game_natives.h"
#include "horse_motion_limits.h"
#include <main.h>

// Read only. Evidence for 1491.50: player_horse.c funcs 127, 415..430,
// 741 and 791. The script's active item/target are not the unrelated
// TASK_ITEM_INTERACTION's item. See docs/player-stimulant-consumption.md.
namespace horse_races::stimulant {
inline constexpr int kInteractionGlobal=1900372;
enum class SampleStatus { Unsupported, MissingGlobals, WrongPlayer, InvalidFields, Ready };
inline const char* SampleStatusName(SampleStatus status) {
    switch(status) {
    case SampleStatus::MissingGlobals:return "missing globals";
    case SampleStatus::WrongPlayer:return "script player mismatch";
    case SampleStatus::InvalidFields:return "invalid scalar fields";
    case SampleStatus::Ready:return "ready";
    default:return "unsupported layout";
    }
}
struct NativeSample {
    bool supported=false;
    int total=-1;
    Interaction interaction;
    SampleStatus status=SampleStatus::Unsupported;
    std::array<std::uint64_t,4> raw_fields{}; // state and flags 5..7, for rejected-snapshot diagnostics.
};
struct NativeApi {
    bool Supported() const {
        // Reuse the repo's guarded 1491.50 build check; no extra scanner,
        // new process access, patches, or global-layout guesses.
        static const auto layout=horse_motion::engine_limits::ValidateLayout();
        return layout.valid;
    }
    const UINT64* Global(int index) const {return getGlobalPtr(index);}
    bool Total(int& out) const {
        // The game's func_791 puts this hash in PermutationId, BaseId=0.
        Any stat[2]{0,ItemHash("horse_stimulant_given")};
        return STATS::STAT_ID_GET_INT(stat,&out)!=0;
    }
};
template<class Api> inline NativeSample ReadConsumption(const Api& api,int player) {
    NativeSample sample;sample.supported=api.Supported();
    if(!sample.supported)return sample; // Never dereference fixed globals on another build.
    if(!api.Total(sample.total) || sample.total<0)sample.total=-1;
    const UINT64* protagonist=api.Global(35);
    const UINT64* base=api.Global(kInteractionGlobal);
    sample.status=SampleStatus::MissingGlobals;
    if(!protagonist || !base)return sample;
    sample.status=SampleStatus::WrongPlayer;
    if(player<=0 || static_cast<int>(static_cast<std::uint32_t>(*protagonist))!=player)return sample;
    sample.raw_fields={base[0],base[5],base[6],base[7]};
    // Globals have an 8-byte slot stride, but script int/BOOL values use
    // 32 bits. Never validate the unused upper half as part of the scalar.
    const auto scalar=[&](int field){return static_cast<std::uint32_t>(base[field]);};
    sample.status=SampleStatus::InvalidFields;
    if(scalar(0)>7 || scalar(5)>1 || scalar(6)>1 || scalar(7)>1)return sample;
    const int state=static_cast<int>(scalar(0)),consumed=static_cast<int>(scalar(7));
    sample.status=SampleStatus::Ready;
    sample.interaction={true,state,static_cast<int>(scalar(2)),scalar(1),scalar(4),consumed!=0};
    return sample;
}
} // namespace horse_races::stimulant
