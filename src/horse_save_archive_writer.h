#pragma once
#include "horse_race_ledger_store.h"
#include "horse_save_observer.h"
#include <limits>
namespace horse_races::save_observer::archive_capture {
// Store I/O only. Caller supplies immutable authentic Freeze() data and a
// final live capability fence; never read or mutate the runtime Ledger here.
inline horse_ledger::WriteResult PublishCapturedArchive(const horse_ledger::Frozen& frozen,
    const CompletedWriteEvidence& evidence,horse_ledger::Store& store,
    const horse_ledger::Revision& revision,const std::function<bool()>& fence,std::string& error){
    using namespace horse_ledger;
    if(!frozen.capture||revision.profile!=frozen.parent.profile||
       frozen.parent.profile!=std::filesystem::path(evidence.profile_directory).filename().string()){
        error="background archive profile/capture mismatch";return WriteResult::Error;}
    if(!fence||!fence()){error="background archive capability expired";return WriteResult::Conflict;}
    Snapshot snapshot{revision,frozen.parent,Evidence::ObservedCandidate,
        frozen.wins,frozen.sales,frozen.memberships,frozen.progression};
    const auto prior=store.LookupSlot(frozen.parent.profile,evidence.slot);
    if(prior.status!=LookupStatus::Loaded&&prior.status!=LookupStatus::Missing){error=prior.error;return WriteResult::Error;}
    if(prior.binding&&prior.binding->game_sha256==evidence.sha256&&prior.snapshot&&
       prior.snapshot->wins==snapshot.wins&&prior.snapshot->sales==snapshot.sales&&
       prior.snapshot->memberships==snapshot.memberships&&prior.snapshot->progression==snapshot.progression)
        return WriteResult::AlreadyPresent;
    if(prior.binding&&prior.binding->generation==std::numeric_limits<std::uint64_t>::max()){
        error="background archive generation exhausted";return WriteResult::Error;}
    SlotBinding binding{snapshot.revision,evidence.slot,evidence.sha256,prior.binding?prior.binding->generation+1:1};
    return store.PublishSlot(snapshot,binding,prior.binding,error,fence);
}
}
