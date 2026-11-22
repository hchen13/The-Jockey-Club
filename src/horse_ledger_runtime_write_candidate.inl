// Save-publication locals belong only to a proven write-candidate dispatch.
// Keep this separate stack frame out of cold-start idle and load handling.
__declspec(noinline) void HandleWriteCandidate(const binding::Action& action) {
    auto& s=S();
    struct EvidenceRelease {std::uint64_t sequence;~EvidenceRelease(){save_observer::ReleaseCompletedWrite(sequence);}} release{action.sequence};
    const auto found=s.packs.find(action.pack_counter);
    if(found==s.packs.end()){Log("write unbound: no matching pack candidate slot="+std::to_string(action.slot));return;}
    auto frozen=found->second.frozen;
    if(found->second.tick>action.request_tick_ms || !s.ledger.Active() ||
        frozen.epoch!=s.ledger.Epoch()){
        s.packs.erase(found);Log("write rejected: stale pack candidate");return;
    }
    // The same serialized buffer may be written to several slots. Rebase only
    // within this loaded-world epoch; AcceptSaved still checks the exact prefix.
    frozen.parent=*s.ledger.Active();
    if(!s.ledger.CanAcceptSaved(frozen)){
        s.packs.erase(found);Log("write rejected: capture belongs to another archive session");return;
    }
    const auto consistent=[&](const save_observer::Observation& observation,bool repackFollowup=false){
        const auto& r=observation.raw;
        const auto end=std::find(r.filename.begin(),r.filename.end(),'\0');
        return observation.healthy && (r.operation==0 || (repackFollowup && r.operation==2)) &&
            r.pending==0 && r.load_state==0 &&
            r.disk_counter==action.disk_counter && r.disk_state==0 && r.save_state==0 &&
            r.save_slot==action.slot && std::string(r.filename.begin(),end)==SlotName(action.slot);
    };
    // Packing and disk writes are separate engine queues. A later pack does
    // not replace this completed disk request or its original frozen ledger.
    // Check the entire queued interval too: a completed intervening load can
    // leave the current idle fields unchanged. Never consume/drop that load.
    const auto unchangedDisk=[&](){
        const auto current=save_observer::ReadNow();
        PullObservations();
        if(!consistent(current) || current.sequence<action.sequence){
            Log("write fence current mismatch expected_seq="+std::to_string(action.sequence)+
                " expected_disk_request="+std::to_string(action.disk_counter)+" "+Describe(current));return false;
        }
        auto sequence=action.sequence;
        auto tick=action.tick_ms;
        auto lastPack=action.observed_pack_counter;
        bool repackPending=static_cast<std::uint32_t>(lastPack-action.pack_counter)==1;
        auto repackTick=action.tick_ms;
        bool inFollowup=false;
        for(const auto& pending:s.observations){
            const auto delta=static_cast<std::uint32_t>(pending.raw.pack_counter-lastPack);
            if(delta==1 && !inFollowup){repackPending=true;repackTick=pending.tick_ms;}
            // Retained manual trace152..155: unit pack advance, op2, idle;
            // per-edge double hashes of the saved file are identical. Only
            // accept that bounded observed follow-up, not arbitrary op2.
            // General op2 semantics remain unproven; snapshots stay candidates.
            const bool followupTimely=pending.tick_ms>=repackTick && pending.tick_ms-repackTick<=2000;
            if(pending.sequence!=sequence+1 || pending.tick_ms<tick ||
                !consistent(pending,repackPending && followupTimely) || delta>1 ||
                (inFollowup && (!followupTimely || delta!=0))){
                Log("write fence queued mismatch expected_seq="+std::to_string(sequence+1)+" "+Describe(pending));return false;
            }
            if(inFollowup && pending.raw.operation==0)repackPending=false;
            inFollowup=pending.raw.operation==2;
            sequence=pending.sequence;tick=pending.tick_ms;lastPack=pending.raw.pack_counter;
        }
        if(sequence<current.sequence){Log("write fence missing intervening observations");return false;}
        return true;
    };
    const auto evidence=save_observer::CompletedWrite(action.sequence);
    const bool completed=evidence&&evidence->profile_directory==s.profile_dir.native()&&
        evidence->slot==action.slot&&evidence->pack_counter==action.pack_counter&&
        evidence->disk_counter==action.disk_counter&&evidence->request_tick_ms==action.request_tick_ms&&
        evidence->terminal_tick_ms==action.tick_ms;
    const auto exactWrite=[&](){
        if(!completed)return unchangedDisk();
        const bool valid=save_observer::ValidateCompletedWrite(*evidence);
        PullObservations();return valid;
    };
    if(!exactWrite()){Log("write rejected: superseded disk observation");return;}
    // A completed capability contains the file hash taken at that native
    // completion, before later loads. Never hash a different current file to
    // reconstruct the old queued operation. The old Frozen is still consumed
    // in observation order, before the later LoadBegin suspends this world.
    const auto disk=completed?std::optional<StableFileHash>{StableFileHash{evidence->sha256,evidence->size,
        evidence->write_filetime,evidence->read_start_tick_ms,evidence->read_end_tick_ms}}:ReadSlot(action.slot);
    if(!disk||disk->read_start_tick_ms<action.tick_ms||!exactWrite()){
        Log("write rejected: file/transaction changed during hash");return;
    }
    const auto prior=s.store->LookupSlot(s.profile,action.slot);
    if(prior.status!=LookupStatus::Loaded && prior.status!=LookupStatus::Missing){
        WarnArchiveSave();
        Log("save archive publish rejected; working state retained: "+prior.error);return;
    }
    // Persisted race/payment bases can be older than the slot's fixed
    // recovery generation. Pin every live owner before replacing the slot.
    if(!RefreshArchivePins("save publication"))return;
    Snapshot snapshot{NewArchiveRevision(),frozen.parent,Evidence::ObservedCandidate,
        frozen.wins,frozen.sales,frozen.memberships,frozen.progression};
    SlotBinding pairing{snapshot.revision,action.slot,disk->sha256,prior.binding?prior.binding->generation+1:1};
    std::string error;
    const auto fence=[&](){return exactWrite() && s.ledger.CanAcceptSaved(frozen);};
    // A background publication of this exact capture must not consume the
    // second recovery generation when the script catches up. Adopt only
    // identical payload AND native bytes under the same capability fence.
    const auto adopt=[&](const auto& existing){
        if(existing.status!=LookupStatus::Loaded||!existing.binding||!existing.snapshot||
           existing.binding->game_sha256!=disk->sha256||
           existing.snapshot->wins!=frozen.wins||existing.snapshot->sales!=frozen.sales||
           existing.snapshot->memberships!=frozen.memberships||existing.snapshot->progression!=frozen.progression||
           !fence())return false;
        // Pin the immutable snapshot before adoption: another publication can evict
        // this physical generation, but never our retained logical base.
        if(!s.store->HoldSnapshot(*existing.snapshot,error)||!fence())return false;
        snapshot=*existing.snapshot;pairing=*existing.binding;return true;
    };
    auto result=adopt(prior)?WriteResult::AlreadyPresent:
        s.store->PublishSlot(snapshot,pairing,prior.binding,error,fence);
    if(result==WriteResult::Conflict&&adopt(s.store->LookupSlot(s.profile,action.slot)))
        result=WriteResult::AlreadyPresent;
    if(result==WriteResult::Error||result==WriteResult::Conflict){
        WarnArchiveSave();
        Log("save archive publish rejected; working state retained: "+error);return;
    }
    if(!s.ledger.CanAcceptSaved(frozen)||(s.ledger.Active()!=snapshot.revision&&
        !s.ledger.AcceptSaved(frozen,snapshot.revision,snapshot.evidence))){
        Log("saved archive published; current working state retained after stale activation");return;
    }
    s.active_slot=pairing;
    s.cache[action.slot]=disk;
    s.archive_issue.clear();
    RefreshArchivePins("save publication complete");
    Log("snapshot candidate saved slot="+std::to_string(action.slot)+" sha="+disk->sha256+
        " completion_evidence="+(completed?std::string("background"):std::string("synchronous"))+
        " wins="+std::to_string(snapshot.wins.size())+" pack="+std::to_string(action.pack_counter)+
        " disk_request="+std::to_string(action.disk_counter)+" hash_begin="+std::to_string(disk->read_start_tick_ms)+
        " hash_end="+std::to_string(disk->read_end_tick_ms)+" sales="+std::to_string(snapshot.sales.size())+
        " memberships="+std::to_string(snapshot.memberships.size()));
    return;
}
