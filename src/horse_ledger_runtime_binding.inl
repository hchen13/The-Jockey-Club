// Staged save archive activation, binding action handling and observer pumping.
// Included at the original definition site; declaration and initialization order are preserved.
bool StageArchive(std::uint32_t slot,const std::string& game_sha) {
    auto& s=S();const auto found=s.store->ResolveSlot(s.profile,slot,game_sha);
    if(found.status!=LookupStatus::Loaded && found.status!=LookupStatus::Missing){
        Quarantine("archive slot unreadable slot="+std::to_string(slot)+" reason="+found.error);
        s.archive_issue=i18n::Text("Racing records could not be read. Your records have been kept; load a matching save or repair the archive.",
            "赛马记录读取失败，原有记录已保留。请载入匹配的存档或修复档案。");return false;
    }
    if(found.status==LookupStatus::Missing&&found.current_binding){
        // Different bytes can mean an external replacement OR a successful
        // native save whose mod publication failed. Never guess an empty world.
        Quarantine("archive source does not match retained slot generations slot="+std::to_string(slot));
        s.archive_issue=i18n::Text("This save does not match its racing records. Your records have been kept; load a matching save or repair the archive.",
            "此存档与赛马记录不匹配，原有记录已保留。请载入匹配的存档或修复档案。");return false;
    }
    s.restore_previous=found.current_binding;
    s.create_restore=found.status==LookupStatus::Missing;
    if(s.create_restore){
        const auto revision=NewArchiveRevision();
        if(!revision.Valid()){Unbind("archive identity generation failed");return false;}
        s.restoring=Snapshot{revision,std::nullopt,Evidence::EmptyBaseline,{},{},{},progression::State{}.Encode()};
        s.restore_slot=SlotBinding{revision,slot,game_sha,found.current_binding?found.current_binding->generation+1:1};
        s.restore_origin="first use; initial archive";
    }else{
        s.restoring=found.snapshot;s.restore_slot=found.binding;
        s.restore_origin=found.binding==found.current_binding?"matching slot archive":"matching previous slot archive";
    }
    if(!s.restoring || !s.restoring->progression || !progression::State::Decode(*s.restoring->progression)){
        Unbind("archive campaign invalid slot="+std::to_string(slot));return false;
    }
    std::string error;
    if(s.create_restore&&!s.store->HoldSnapshot(*s.restoring,error)){
        Unbind("archive baseline retention failed: "+error);return false;
    }
    if(!RefreshArchivePins("archive staging")){Unbind("archive staging retention failed");return false;}
    s.ready_since=0;return true;
}
bool ActivateStagedArchive() {
    auto& s=S();if(!s.restoring||!s.restore_slot)return false;
    const auto snapshot=*s.restoring;const auto binding=*s.restore_slot;
    const auto origin=s.restore_origin;
    if(!RefreshArchivePins("archive activation"))return false;
    if(s.create_restore){
        std::string error;
        // Publish the empty baseline only for the proven loaded source. If an
        // early autosave has already replaced that file, retain its logical base
        // without pairing the old bytes to the new file; the save gets its own freeze.
        enum class SourceState { Matching, Changed, Unsafe };
        auto source_state=SourceState::Unsafe;
        const auto inspectSource=[&](){
            const auto observed=save_observer::ReadNow();PullObservations();
            if(!observed.healthy||observed.raw.pending||observed.raw.load_state||
                std::any_of(s.observations.begin(),s.observations.end(),[](const auto& item){
                    return !item.healthy||item.raw.pending||item.raw.load_state;
                }))return source_state=SourceState::Unsafe;
            const auto disk=ReadSlot(binding.slot);
            if(!disk)return source_state=SourceState::Unsafe;
            return source_state=disk->sha256==binding.game_sha256?SourceState::Matching:SourceState::Changed;
        };
        const auto sameSource=[&](){return inspectSource()==SourceState::Matching;};
        if(inspectSource()==SourceState::Unsafe)return false;
        auto written=source_state==SourceState::Matching?
            s.store->PublishSlot(snapshot,binding,s.restore_previous,error,sameSource):WriteResult::Conflict;
        if(written==WriteResult::Conflict && source_state==SourceState::Changed){
            // The loaded source can be overwritten by an early autosave while
            // this baseline is being published. Only that proven same-world
            // byte change permits an unpaired logical base; CAS/I/O/corruption
            // and a new load must not be converted into a successful restore.
            const auto current=s.store->LookupSlot(s.profile,binding.slot);
            if((current.status==LookupStatus::Loaded||current.status==LookupStatus::Missing)&&
                current.binding==s.restore_previous&&inspectSource()==SourceState::Changed)
                written=s.store->HoldSnapshot(snapshot,error)?WriteResult::AlreadyPresent:WriteResult::Error;
        }
        if(written!=WriteResult::Written && written!=WriteResult::AlreadyPresent){
            Unbind("archive initialization failed: "+error);WarnArchiveSave();return false;
        }
    }
    RevokeArchiveMirror();
    if(!s.ledger.Restore(snapshot)){Unbind("archive validation failed during restore");return false;}
    s.market_blocked_timeline.reset();s.market_blocked_event.clear();
    s.suspended.reset();s.active_slot=binding;s.restoring.reset();s.restore_slot.reset();s.restore_previous.reset();
    s.create_restore=false;s.restore_origin.clear();s.archive_issue.clear();s.waiting_world=!WorldReady();
    s.mutation_tick=GetTickCount64();s.last_horse.clear();
    s.store->RetireUnpinnedJournals();
    RefreshArchivePins("archive activation complete");
    Log("archive restored slot="+std::to_string(binding.slot)+" game_sha="+binding.game_sha256+
        " archive="+snapshot.revision.sha256+" wins="+std::to_string(snapshot.wins.size())+
        " sales="+std::to_string(snapshot.sales.size())+" memberships="+std::to_string(snapshot.memberships.size())+
        " timeline="+std::to_string(s.timeline)+" origin="+origin);
    return true;
}
#include "horse_ledger_runtime_write_candidate.inl"

void Handle(const binding::Action& action) {
    auto& s=S();using binding::Kind;
    CloseStartup("manual/save protocol activity kind="+std::to_string(static_cast<int>(action.kind)));
    switch(action.kind){
    case Kind::LoadBegin: {
        ++s.timeline;
        const auto before=action.slot<16?s.cache[action.slot]:std::nullopt;
        Quarantine("load begin slot="+std::to_string(action.slot));
        s.loading=Loading{action.slot,action.request_tick_ms,before};
        break;
    }
    case Kind::LoadCandidate: {
        const auto loading=s.loading;s.loading.reset();
        const auto after=ReadSlot(action.slot);
        if(action.slot<16)s.cache[action.slot]=after;
        if(!loading||loading->slot!=action.slot||!loading->before||!after||
            loading->before->read_end_tick_ms>loading->request||after->read_start_tick_ms<action.tick_ms||
            loading->before->sha256!=after->sha256){Unbind("load file changed or pre-load evidence missing");break;}
        if(!StageArchive(action.slot,after->sha256))break;
        Log("restore staged slot="+std::to_string(action.slot)+" sha="+after->sha256+
            " wins="+std::to_string(s.restoring->wins.size()));
        break;
    }
    case Kind::LoadFailed:
        s.loading.reset();Quarantine("explicit load failure");break;
    case Kind::Uncertain:
        ++s.timeline;s.loading.reset();Quarantine("save observation uncertain: "+action.reason);break;
    case Kind::SaveCaptureUncertain:
        s.packs.clear();RefreshArchivePins("save capture rejection");
        Log("save captures discarded; working archive retained: "+action.reason);break;
    case Kind::PackRequested: {
        if(s.sale_in_progress||s.cash_in_progress){s.packs.erase(action.pack_counter);Log("pack rejected: economic mutation in progress");break;}
        // The proven loaded archive can be captured before the fade-in finishes.
        // Its data is already known; trading and race starts still wait for the world.
        const bool restored=s.restoring && ActivateStagedArchive();
        const auto frozen=s.ledger.Freeze();
        if(!frozen)break;
        if(!restored && s.mutation_tick>=action.tick_ms){s.packs.erase(action.pack_counter);Log("pack rejected: ledger mutation overlaps request");break;}
        if(s.packs.size()>=8)s.packs.clear();
        s.packs.insert_or_assign(action.pack_counter,Pack{*frozen,action.tick_ms});
        if(!RefreshArchivePins("serializer capture")){s.packs.erase(action.pack_counter);break;}
        Log("pack candidate counter="+std::to_string(action.pack_counter)+" wins="+std::to_string(frozen->wins.size())+
            " epoch="+std::to_string(frozen->epoch)+" mutation="+std::to_string(frozen->mutation)+
            " request_tick="+std::to_string(action.tick_ms));
        break;
    }
    case Kind::WriteBegin:
        Log("write begin slot="+std::to_string(action.slot)+" pack="+std::to_string(action.pack_counter));break;
    case Kind::WriteRejected:
        // An unproven/failed save does not change the loaded world. Preserve its
        // working wins and cash timeline; only this disk association is refused.
        s.packs.erase(action.pack_counter);
        RefreshArchivePins("save rejection");
        Log("save candidate rejected; working wins retained: "+action.reason);break;
    case Kind::WriteCandidate: HandleWriteCandidate(action);break;
    }
}
void Pump(bool synchronous=false) {
    auto& s=S();if(!s.enabled||s.pumping)return;
    s.pumping=true;
    try{
        if(synchronous)save_observer::ReadNow();
        PullObservations();
        while(!s.observations.empty()){
            const auto observation=std::move(s.observations.front());s.observations.pop_front();
            Log("observer "+Describe(observation));
            const char* const stage=horse_diagnostics::trace.stage;
            horse_diagnostics::MarkFaultStage("ledger pump: decode observation");
            for(const auto& action:s.detector.Consume(observation)){
                horse_diagnostics::MarkFaultStage("ledger pump: dispatch observation");
                if(action.kind==binding::Kind::WriteRejected || action.kind==binding::Kind::Uncertain)
                    Log("observation rejected reason="+action.reason+" "+Describe(observation));
                Handle(action);
            }
            s.handled_sequence=observation.sequence;
            horse_diagnostics::MarkFaultStage(stage);
        }
        if(s.restoring && WorldReady()&&!Busy(save_observer::Latest()))ActivateStagedArchive();
        if(s.waiting_world && WorldReady()&&!Busy(save_observer::Latest()))s.waiting_world=false;
    }catch(const std::exception& e){Disable(std::string("runtime exception: ")+e.what());}
    s.pumping=false;
    try{OfferArchiveMirror();}catch(...){RevokeArchiveMirror();}
}
