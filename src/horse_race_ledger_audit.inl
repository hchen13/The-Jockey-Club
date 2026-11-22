// Economic receipts are retry/audit evidence, never a payment replay queue.
// Successful gameplay history belongs to the full slot snapshot. Keep only 32
// recent whole audit events of each type/profile, with fixed 4 MiB admission.
namespace {
constexpr std::size_t kAuditEvents=32,kAuditBytes=4*1024*1024;
struct AuditEntry {std::string event;unsigned stage{},final_stage{};bool retired{};Bytes payload;};
Bytes AuditOut(const std::string& profile,const std::string& kind,const std::vector<AuditEntry>& entries,
    const std::string& previous_hash,std::uint64_t generation){
    Need(entries.size()<=kAuditEvents,"transaction audit event capacity reached");
    Writer w;w.S(profile);w.S(kind);w.U(entries.size(),4);
    for(const auto& e:entries){w.S(e.event);w.U(e.stage,4);w.U(e.final_stage,4);w.U(e.retired,1);BlobOut(w,e.payload);}
    w.S(previous_hash);w.U(generation);
    auto bytes=Wrap("TJCAUD01",w.b);Need(bytes.size()<=kAuditBytes,"transaction audit byte capacity reached");return bytes;
}
std::vector<AuditEntry> AuditIn(const Bytes& bytes,const std::string& profile,const std::string& kind,
    std::string& previous_hash,std::uint64_t& generation){
    Need(bytes.size()<=kAuditBytes,"transaction audit byte capacity exceeded");
    auto body=Unwrap("TJCAUD01",bytes);Reader r{body};
    Need(r.S()==profile&&r.S()==kind,"foreign transaction audit container");
    const auto count=r.U(4);Need(count<=kAuditEvents,"transaction audit count exceeded");
    std::vector<AuditEntry> entries;
    for(std::uint64_t i=0;i<count;++i){
        AuditEntry e;e.event=r.S();e.stage=static_cast<unsigned>(r.U(4));e.final_stage=static_cast<unsigned>(r.U(4));
        e.retired=BoolIn(r);e.payload=BlobIn(r);
        const unsigned final=kind=="win"?2:kind=="sale"?5:kind=="membership"?3:0;
        Need(final&&e.final_stage==final&&e.stage>=1&&e.stage<=final&&!e.event.empty(),"invalid audit stage");
        const char* magic=kind=="win"?"TJCJRN01":kind=="membership"?"TJCMEM01":MagicIs(e.payload,"TJCSAL01")?"TJCSAL01":"TJCSAL02";
        auto payload=Unwrap(magic,e.payload);Reader p{payload};const auto base=RevisionIn(p);
        Need(base.Valid()&&base.profile==profile,"invalid audit base");
        std::string event;
        if(kind=="win"){
            const auto wins=WinsIn(p.b.subspan(p.at));Need(wins.size()==1&&Validate(wins[0]),"invalid win audit");event=wins[0].event_id;
        }else if(kind=="sale"){
            const auto sale=SaleIn(p,MagicIs(e.payload,"TJCSAL02"));p.End();Need(Validate(sale),"invalid sale audit");event=sale.event_id;
        }else {const auto member=MembershipIn(p);p.End();Need(Validate(member),"invalid membership audit");event=member.event_id;}
        Need(event==e.event&&std::none_of(entries.begin(),entries.end(),[&](const auto& x){return x.event==e.event;}),"duplicate or foreign audit event");
        entries.push_back(std::move(e));
    }
    previous_hash=r.S();generation=r.U();
    Need(generation>0&&(generation==1?previous_hash.empty():Hex(previous_hash)),"invalid audit generation");
    r.End();return entries;
}
}
WriteResult Store::WriteAudit(const Revision& base,const std::string& event_id,const char* type,
    const Bytes& payload,unsigned stage,unsigned final_stage,std::string& error){
    std::lock_guard lock(slot_publish_mutex);
    try{
        const std::string kind=type;
        const auto profile_hash=Sha256({reinterpret_cast<const std::uint8_t*>(base.profile.data()),base.profile.size()});
        const auto path=root_/"audit"/(profile_hash+"-"+kind+".bin");
        for(unsigned i=0;i<8;++i){auto pending=path;pending+=L".pending"+std::to_wstring(i);
            const auto attributes=GetFileAttributesW(pending.c_str());
            Need(attributes==INVALID_FILE_ATTRIBUTES,"interrupted transaction audit candidate retained; recovery required");
            const auto code=GetLastError();Need(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND,"cannot inspect audit staging");
        }
        std::vector<AuditEntry> entries;Bytes current_bytes;std::string previous_hash;std::uint64_t generation=0;
        try{current_bytes=Read(path,kAuditBytes);entries=AuditIn(current_bytes,base.profile,kind,previous_hash,generation);}
        catch(const ReadIoError& e){if(e.code!=ERROR_FILE_NOT_FOUND&&e.code!=ERROR_PATH_NOT_FOUND)throw;}
        auto ready_path=path;ready_path+=L".ready";
        std::optional<Bytes> ready_bytes;
        try{ready_bytes=Read(ready_path,kAuditBytes);}catch(const ReadIoError& e){if(e.code!=ERROR_FILE_NOT_FOUND&&e.code!=ERROR_PATH_NOT_FOUND)throw;}
        if(ready_bytes){
            std::string prior;std::uint64_t next=0;
            auto recovered=AuditIn(*ready_bytes,base.profile,kind,prior,next);
            Need(generation<UINT64_MAX&&next==generation+1&&prior==(current_bytes.empty()?std::string{}:Sha256(current_bytes)),
                "transaction commit decision does not extend current audit");
            FinishArchiveCommit(ready_path,path,*ready_bytes);
            current_bytes=*ready_bytes;entries=std::move(recovered);generation=next;
        }
        Need(generation<UINT64_MAX,"transaction audit generation exhausted");
        const auto pinned=[&](const AuditEntry& entry){
            return std::find(journal_pins_.begin(),journal_pins_.end(),JournalIdentity{base.profile,entry.event})!=journal_pins_.end();
        };
        for(auto& entry:entries)if(retire_unpinned_journals_&&!pinned(entry))entry.retired=true;
        auto existing=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return e.event==event_id;});
        unsigned highest=0;
        if(existing!=entries.end()){
            if(existing->payload!=payload){error="transaction audit payload/base conflict";return WriteResult::Conflict;}
            highest=existing->stage;
        }else{
            // Read an old same-event receipt group during a non-migrated update;
            // it is never rewritten, replayed, or silently treated as absent.
            Writer key;key.S(base.profile);key.S(event_id);const auto name=Sha256(key.b);
            const std::vector<const char*> suffixes=kind=="win"?std::vector<const char*>{".prepared",".paid"}:
                kind=="sale"?std::vector<const char*>{".prepared",".removal-entered",".removed",".payout-entered",".paid"}:
                std::vector<const char*>{".prepared",".payment-entered",".paid"};
            const auto directory=root_/(kind=="win"?"receipts":kind=="sale"?"sale-receipts":"membership-receipts");
            for(std::size_t i=0;i<suffixes.size();++i){
                try{
                    const auto old=Read(directory/(name+suffixes[i]));
                    if(old!=payload){error="legacy transaction payload conflict";return WriteResult::Conflict;}
                    if(i!=highest){error="legacy transaction stage gap";return WriteResult::Conflict;}highest=static_cast<unsigned>(i)+1;
                }catch(const ReadIoError& e){if(e.code!=ERROR_FILE_NOT_FOUND&&e.code!=ERROR_PATH_NOT_FOUND)throw;}
            }
        }
        if(stage<=highest)return WriteResult::AlreadyPresent;
        // Win Paid historically accepts a one-call Prepared+Paid boundary. Sales
        // and memberships still require each explicit predecessor.
        if(stage!=highest+1&&!(kind=="win"&&highest==0&&stage==2)){
            error="transaction audit stage requires predecessor";return WriteResult::Conflict;
        }
        if(existing!=entries.end()){existing->stage=stage;existing->retired=false;}
        else{
            if(entries.size()==kAuditEvents){
                const auto victim=std::find_if(entries.begin(),entries.end(),[&](const auto& e){return !pinned(e)&&(e.retired||e.stage==e.final_stage);});
                Need(victim!=entries.end(),"transaction audit capacity occupied by unresolved live operations");entries.erase(victim);
            }
            entries.push_back({event_id,stage,final_stage,false,payload});
        }
        Bytes bytes;
        for(;;){
            try{bytes=AuditOut(base.profile,kind,entries,current_bytes.empty()?std::string{}:Sha256(current_bytes),generation+1);break;}
            catch(const std::exception&){
                const auto victim=std::find_if(entries.begin(),entries.end(),[&](const auto& e){
                    return e.event!=event_id&&!pinned(e)&&(e.retired||e.stage==e.final_stage);
                });
                if(victim==entries.end())throw;entries.erase(victim);
            }
        }
        return ReplaceArchive(path,bytes,error,{},true);
    }catch(const std::exception& e){if(error.empty())error=e.what();return WriteResult::Error;}
}
WriteResult Store::Journal(const Revision& base,const Win& win,bool paid,std::string& error){
    std::lock_guard lock(slot_publish_mutex);error.clear();
    try{
        Need(base.Valid()&&Validate(win,&error),"invalid journal payload");
        Need(Load(base,error).has_value(),"unknown or corrupt journal base revision");
        Writer w;RevisionOut(w,base);const auto wins=WinsOut({win});w.b.insert(w.b.end(),wins.begin(),wins.end());
        return WriteAudit(base,win.event_id,"win",Wrap("TJCJRN01",w.b),paid?2:1,2,error);
    }catch(const std::exception& e){if(error.empty())error=e.what();return WriteResult::Error;}
}
WriteResult Store::SaleJournal(const Revision& base,const Sale& sale,SaleStage stage,std::string& error){
    std::lock_guard lock(slot_publish_mutex);error.clear();
    try{
        Need(base.Valid()&&Validate(sale,&error),"invalid sale journal payload");
        Need(Load(base,error).has_value(),"unknown or corrupt sale journal base revision");
        const auto target=static_cast<unsigned>(stage);Need(target>=1&&target<=5,"invalid sale journal stage");
        const bool attributed=sale.seller!=SellerEra::Unknown;Writer w;RevisionOut(w,base);SaleOut(w,sale,attributed);
        return WriteAudit(base,sale.event_id,"sale",Wrap(attributed?"TJCSAL02":"TJCSAL01",w.b),target,5,error);
    }catch(const std::exception& e){if(error.empty())error=e.what();return WriteResult::Error;}
}
WriteResult Store::MembershipJournal(const Revision& base,const Membership& member,MembershipStage stage,std::string& error){
    std::lock_guard lock(slot_publish_mutex);error.clear();
    try{
        Need(base.Valid()&&Validate(member,&error),"invalid membership journal payload");
        Need(Load(base,error).has_value(),"unknown or corrupt membership journal base");
        const auto target=static_cast<unsigned>(stage);Need(target>=1&&target<=3,"invalid membership journal stage");
        Writer w;RevisionOut(w,base);MembershipOut(w,member);
        return WriteAudit(base,member.event_id,"membership",Wrap("TJCMEM01",w.b),target,3,error);
    }catch(const std::exception& e){if(error.empty())error=e.what();return WriteResult::Error;}
}
