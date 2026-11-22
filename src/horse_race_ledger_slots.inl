// Paired saves own complete snapshots. One atomically replaced file contains
// the current generation and one recovery generation; no ancestor traversal.
namespace {
thread_local std::vector<std::filesystem::path> active_archive_temporaries;
bool MagicIs(const Bytes& bytes,const char* magic){
    return bytes.size()>=8&&std::equal(magic,magic+8,bytes.begin());
}
void BlobOut(Writer& w,const Bytes& bytes){
    w.U(bytes.size(),4);w.b.insert(w.b.end(),bytes.begin(),bytes.end());
    Need(w.b.size()<=kMax,"archive container capacity reached");
}
Bytes BlobIn(Reader& r){
    const auto size=r.U(4);Need(size<=kMax&&size<=r.b.size()-r.at,"invalid archive blob");
    Bytes bytes(r.b.begin()+r.at,r.b.begin()+r.at+static_cast<std::size_t>(size));
    r.at+=static_cast<std::size_t>(size);return bytes;
}
std::pair<Bytes,Bytes> SnapshotParts(const Snapshot& s){
    std::string error;Need(Validate(s,&error),"invalid complete snapshot");
    Need(CanonicalCampaign(s.progression),"complete snapshot requires canonical campaign");
    auto history=Wrap("TJCHIS04",HistoryOut(s,true,true));
    Writer ref;RevisionOut(ref,s.revision);ref.U(s.parent.has_value(),1);
    if(s.parent)RevisionOut(ref,*s.parent);
    ref.U(static_cast<unsigned>(s.evidence),4);ref.S(Sha256(history));
    return {Wrap("TJCREF01",ref.b),std::move(history)};
}
Snapshot SnapshotFromParts(const Bytes& ref,const Bytes& history){
    auto payload=Unwrap("TJCREF01",ref);Reader r{payload};Snapshot s;
    s.revision=RevisionIn(r);if(BoolIn(r))s.parent=RevisionIn(r);
    s.evidence=static_cast<Evidence>(r.U(4));auto head=r.S();r.End();
    Need(Hex(head)&&Sha256(history)==head,"complete snapshot history mismatch");
    HistoryIn(Unwrap("TJCHIS04",history),s,true,true);
    std::string error;Need(Validate(s,&error)&&CanonicalCampaign(s.progression),"invalid complete snapshot semantics");
    return s;
}
Bytes VersionsOut(const std::vector<SlotVersion>& versions){
    Need(!versions.empty()&&versions.size()<=2,"invalid recovery generation count");
    Writer w;w.U(versions.size(),4);
    for(const auto& v:versions){
        Need(ValidBinding(v.binding)&&v.binding.archive==v.snapshot.revision,"complete slot identity mismatch");
        BlobOut(w,SlotOut(v.binding));auto [ref,history]=SnapshotParts(v.snapshot);
        BlobOut(w,ref);BlobOut(w,history);
    }
    return Wrap("TJCSLT03",w.b);
}
std::vector<SlotVersion> VersionsIn(const Bytes& bytes){
    auto payload=Unwrap("TJCSLT03",bytes);Reader r{payload};const auto count=r.U(4);
    Need(count>=1&&count<=2,"invalid recovery generation count");
    std::vector<SlotVersion> versions;
    for(std::uint64_t i=0;i<count;++i){
        auto binding=SlotIn(BlobIn(r));auto ref=BlobIn(r);auto history=BlobIn(r);
        auto snapshot=SnapshotFromParts(ref,history);
        Need(snapshot.revision==binding.archive,"slot snapshot address mismatch");
        if(!versions.empty())Need(binding.archive.profile==versions[0].binding.archive.profile&&
            binding.slot==versions[0].binding.slot&&binding.generation<versions[0].binding.generation,
            "foreign or unordered slot recovery generation");
        if(!versions.empty()&&snapshot.revision==versions[0].snapshot.revision)
            Need(snapshot==versions[0].snapshot,"one archive revision describes conflicting states");
        versions.push_back({std::move(binding),std::move(snapshot)});
    }
    r.End();return versions;
}
void OwnedDirectory(const std::filesystem::path& path){
    auto current=path;
    while(!current.empty()){
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){const auto code=GetLastError();
            Need(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND,"cannot inspect archive directory");
        }else Need(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&(attributes&FILE_ATTRIBUTE_DIRECTORY),
            "archive directory is not an ordinary directory");
        const auto parent=current.parent_path();if(parent==current)break;current=parent;
    }
    std::filesystem::create_directories(path);
}
// Fixed per-recursion scratch names also bound leftovers from process crashes.
// Never truncate an existing target or hard link in place.
WriteResult ReplaceArchive(const std::filesystem::path& target,const Bytes& bytes,
    std::string&,const std::function<bool()>& before_replace={},bool commit_ready=false){
    static thread_local unsigned depth=0;
    Need(depth<8,"too many nested archive publications");
    struct Depth {unsigned& value;explicit Depth(unsigned& n):value(n){++value;}~Depth(){--value;}} guard(depth);
    OwnedDirectory(target.parent_path());
    ArchiveAdmission(target.parent_path(),bytes.size());
    auto temporary=target;temporary+=L".pending"+std::to_wstring(depth-1);
    const auto attributes=GetFileAttributesW(temporary.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES){
        throw std::runtime_error("interrupted archive candidate retained; recovery required before replacement");
    }else Need(GetLastError()==ERROR_FILE_NOT_FOUND,"cannot inspect archive temporary path");
    Handle file{CreateFileW(temporary.c_str(),GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr)};
    Need(file.h!=INVALID_HANDLE_VALUE,"cannot create archive temporary file");
    struct ActiveTemporary {
        explicit ActiveTemporary(const std::filesystem::path& path){active_archive_temporaries.push_back(path);}
        ~ActiveTemporary(){active_archive_temporaries.pop_back();}
    } active(temporary);
    bool committed=false;
    try{
        std::size_t at=0;while(at<bytes.size()){
            DWORD done=0;const auto want=static_cast<DWORD>(std::min<std::size_t>(65536,bytes.size()-at));
            Need(WriteFile(file.h,bytes.data()+at,want,&done,nullptr)!=0&&done==want,"archive write failed");at+=done;
        }
        Need(FlushFileBuffers(file.h)!=0,"archive flush failed");
        if(before_replace&&!before_replace()){
            Need(RemoveOwnedFile(file.h),"cannot remove rejected owned archive temporary file");
            return WriteResult::Conflict;
        }
        if(commit_ready){
            auto ready=target;ready+=L".ready";
            // Only a candidate which passed both native fences and both CAS
            // checks may acquire this durable commit-decision name.
            Need(RenameOwnedFile(file.h,ready,false),"cannot publish archive commit decision");
            committed=true;Need(FlushFileBuffers(file.h)!=0,"cannot flush archive commit decision");
        }
        Need(RenameOwnedFile(file.h,target,true),"archive atomic replacement failed");
        committed=true;Need(FlushFileBuffers(file.h)!=0,"cannot flush archive replacement");return WriteResult::Written;
    }catch(...){
        if(!committed)RemoveOwnedFile(file.h);throw;
    }
}
}

std::vector<SlotVersion> Store::ReadSlotVersions(const std::filesystem::path& path)const{
    for(unsigned i=0;i<8;++i){
        auto pending=path;pending+=L".pending"+std::to_wstring(i);
        if(std::find(active_archive_temporaries.begin(),active_archive_temporaries.end(),pending)!=active_archive_temporaries.end())continue;
        const auto attributes=GetFileAttributesW(pending.c_str());
        Need(attributes==INVALID_FILE_ATTRIBUTES,"unvalidated interrupted slot candidate retained; refusing archive initialization");
        const auto code=GetLastError();
        if(code!=ERROR_FILE_NOT_FOUND&&code!=ERROR_PATH_NOT_FOUND)throw ReadIoError("cannot inspect slot staging",code);
    }
    const auto decode=[&](const Bytes& bytes){
        if(MagicIs(bytes,"TJCSLT03"))return VersionsIn(bytes);
        const auto binding=SlotIn(bytes);const auto found=LookupLegacy(binding.archive);
        if(found.status==LookupStatus::IoError)throw ReadIoError("legacy bound archive unavailable",ERROR_READ_FAULT);
        Need(found.status==LookupStatus::Loaded&&found.snapshot.has_value(),"legacy bound archive missing or corrupt");
        Need(CanonicalCampaign(found.snapshot->progression),"slot archive campaign is invalid or noncanonical");
        return std::vector<SlotVersion>{{binding,*found.snapshot}};
    };
    std::vector<SlotVersion> current;std::optional<ReadIoError> absent;
    try{current=decode(Read(path));}catch(const ReadIoError& e){
        if(e.code!=ERROR_FILE_NOT_FOUND&&e.code!=ERROR_PATH_NOT_FOUND)throw;absent=e;
    }
    auto ready_path=path;ready_path+=L".ready";
    std::optional<Bytes> ready_bytes;
    try{ready_bytes=Read(ready_path);}catch(const ReadIoError& e){
        if(e.code!=ERROR_FILE_NOT_FOUND&&e.code!=ERROR_PATH_NOT_FOUND)throw;
    }
    if(ready_bytes){
        const auto ready=VersionsIn(*ready_bytes);const auto& next=ready.front();
        Need(path.filename()==SlotPath(root_,next.binding.archive.profile,next.binding.slot).filename(),"foreign slot commit decision");
        if(current.empty())Need(ready.size()==1&&next.binding.generation==1,"unrooted slot commit decision");
        else Need(ready.size()==2&&ready[1].binding==current[0].binding&&ready[1].snapshot==current[0].snapshot&&
            current[0].binding.generation<UINT64_MAX&&next.binding.generation==current[0].binding.generation+1,
            "slot commit decision does not extend authoritative generation");
        // Reading a committed .ready is read-only. A later save completes its
        // rename before preparing another generation; generation is not reused.
        return ready;
    }
    if(absent)throw *absent;return current;
}
LookupResult Store::Lookup(const Revision& revision)const{
    std::lock_guard lock(slot_publish_mutex);
    try{
        Need(revision.Valid(),"invalid revision");
        for(const auto& s:retained_)if(s.revision==revision)return {LookupStatus::Loaded,s,{}};
        // Only this profile's bounded physical addresses are consulted. Neither
        // parent links nor another slot's game hash select a loaded world.
        for(std::uint32_t slot=0;slot<16;++slot){
            const auto path=SlotPath(root_,revision.profile,slot);
            const auto attributes=GetFileAttributesW(path.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES){
                const auto code=GetLastError();
                if(code!=ERROR_FILE_NOT_FOUND&&code!=ERROR_PATH_NOT_FOUND)throw ReadIoError("cannot inspect paired archive",code);
                auto ready=path;ready+=L".ready";
                const auto ra=GetFileAttributesW(ready.c_str());
                if(ra==INVALID_FILE_ATTRIBUTES){const auto rc=GetLastError();
                    if(rc==ERROR_FILE_NOT_FOUND||rc==ERROR_PATH_NOT_FOUND)continue;
                    throw ReadIoError("cannot inspect committed archive",rc);
                }
            }
            for(const auto& v:ReadSlotVersions(path))if(v.snapshot.revision==revision)
                return {LookupStatus::Loaded,v.snapshot,{}};
        }
        return LookupLegacy(revision);
    }catch(const ReadIoError& e){return {LookupStatus::IoError,{},e.what()};}
    catch(const std::exception& e){return {LookupStatus::Corrupt,{},e.what()};}
}
bool Store::HoldSnapshot(const Snapshot& snapshot,std::string& error){
    std::lock_guard lock(slot_publish_mutex);error.clear();
    try{
        Need(Validate(snapshot,&error),"invalid retained snapshot");
        for(const auto& held:retained_)if(held.revision==snapshot.revision){
            Need(held==snapshot,"retained revision conflict");return true;
        }
        Need(retained_.size()<32,"session archive pin capacity reached");
        retained_.push_back(snapshot);return true;
    }catch(const std::exception& e){if(error.empty())error=e.what();return false;}
}
bool Store::RetainRevisions(const std::vector<Revision>& roots,std::string& error){
    std::lock_guard lock(slot_publish_mutex);error.clear();
    try{
        Need(roots.size()<=32,"session archive root capacity reached");
        if(std::all_of(roots.begin(),roots.end(),[&](const auto& r){return std::any_of(retained_.begin(),retained_.end(),[&](const auto& s){return s.revision==r;});})&&
            std::all_of(retained_.begin(),retained_.end(),[&](const auto& s){return std::find(roots.begin(),roots.end(),s.revision)!=roots.end();}))return true;
        std::vector<Snapshot> next;
        for(const auto& revision:roots){
            if(std::any_of(next.begin(),next.end(),[&](const auto& s){return s.revision==revision;}))continue;
            const auto found=Lookup(revision);
            Need(found.status==LookupStatus::Loaded&&found.snapshot.has_value(),"session archive root unavailable");
            next.push_back(*found.snapshot);
        }
        retained_=std::move(next);return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void Store::SetJournalPins(const std::vector<JournalIdentity>& pins){
    std::lock_guard lock(slot_publish_mutex);Need(pins.size()<=16,"live transaction pin capacity reached");
    for(const auto& old:journal_pins_)if(std::find(pins.begin(),pins.end(),old)==pins.end())retire_unpinned_journals_=true;
    journal_pins_=pins;
}
void Store::RetireUnpinnedJournals(){
    std::lock_guard lock(slot_publish_mutex);retire_unpinned_journals_=true;
}
SlotLookupResult Store::LookupSlot(const std::string& profile,std::uint32_t slot)const{
    std::lock_guard lock(slot_publish_mutex);
    try{
        Need(Revision{profile,std::string(64,'0')}.Valid()&&slot<=15,"invalid slot address");
        auto versions=ReadSlotVersions(SlotPath(root_,profile,slot));
        const auto& current=versions.front();
        Need(current.binding.archive.profile==profile&&current.binding.slot==slot,"foreign slot binding");
        return {LookupStatus::Loaded,current.binding,current.snapshot,{}};
    }catch(const ReadIoError& e){
        const bool absent=e.code==ERROR_FILE_NOT_FOUND||e.code==ERROR_PATH_NOT_FOUND;
        return {absent?LookupStatus::Missing:LookupStatus::IoError,{},{},e.what()};
    }catch(const std::exception& e){return {LookupStatus::Corrupt,{},{},e.what()};}
}
SlotResolveResult Store::ResolveSlot(const std::string& profile,std::uint32_t slot,const std::string& game_sha)const{
    std::lock_guard lock(slot_publish_mutex);
    try{
        Need(Revision{profile,std::string(64,'0')}.Valid()&&slot<=15&&Hex(game_sha),"invalid save resolution address");
        const auto versions=ReadSlotVersions(SlotPath(root_,profile,slot));
        const auto current=versions.front().binding;
        Need(current.archive.profile==profile&&current.slot==slot,"foreign slot binding");
        for(const auto& v:versions)if(v.binding.game_sha256==game_sha)
            return {LookupStatus::Loaded,v.binding,v.snapshot,current,{}};
        return {LookupStatus::Missing,{},{},current,"no exact retained generation for this slot"};
    }catch(const ReadIoError& e){
        const bool absent=e.code==ERROR_FILE_NOT_FOUND||e.code==ERROR_PATH_NOT_FOUND;
        return {absent?LookupStatus::Missing:LookupStatus::IoError,{},{},{},e.what()};
    }catch(const std::exception& e){return {LookupStatus::Corrupt,{},{},{},e.what()};}
}
WriteResult Store::PublishSlot(const Snapshot& snapshot,const SlotBinding& binding,
    const std::optional<SlotBinding>& expected,std::string& error,const std::function<bool()>& fence){
    std::lock_guard lock(slot_publish_mutex);error.clear();
    try{
        Need(ValidBinding(binding)&&snapshot.revision==binding.archive,"invalid paired snapshot");
        Need(Validate(snapshot,&error)&&CanonicalCampaign(snapshot.progression),"invalid paired archive");
        if(expected)Need(ValidBinding(*expected)&&expected->archive.profile==binding.archive.profile&&
            expected->slot==binding.slot&&expected->generation<UINT64_MAX&&binding.generation==expected->generation+1,
            "invalid slot generation");
        else Need(binding.generation==1,"first slot generation must be one");
        const auto matches=[&](){
            const auto current=LookupSlot(binding.archive.profile,binding.slot);
            if(current.status==LookupStatus::Corrupt||current.status==LookupStatus::IoError){error=current.error;return false;}
            const bool same=expected?(current.status==LookupStatus::Loaded&&current.binding==expected):current.status==LookupStatus::Missing;
            if(!same)error="slot compare-and-swap conflict";return same;
        };
        if(!matches())return WriteResult::Conflict;
        const auto known=Lookup(snapshot.revision);
        if(known.status==LookupStatus::Corrupt||known.status==LookupStatus::IoError){error=known.error;return WriteResult::Error;}
        if(known.snapshot&&*known.snapshot!=snapshot){error="immutable revision identity conflicts with retained snapshot";return WriteResult::Conflict;}
        const auto target=SlotPath(root_,binding.archive.profile,binding.slot);
        auto ready=target;ready+=L".ready";
        if(GetFileAttributesW(ready.c_str())!=INVALID_FILE_ATTRIBUTES){
            // LookupSlot above has validated the exact durable commit decision.
            FinishArchiveCommit(ready,target,VersionsOut(ReadSlotVersions(target)));
        }
        std::vector<SlotVersion> versions{{binding,snapshot}};
        if(expected){const auto old=LookupSlot(binding.archive.profile,binding.slot);versions.push_back({*old.binding,*old.snapshot});}
        const auto bytes=VersionsOut(versions);
        const auto publishable=[&](){
            if(fence&&!fence()){error="native save fence rejected slot publication";return false;}
            if(!matches())return false;
            if(fence&&!fence()){error="native save fence changed before slot replacement";return false;}
            return matches();
        };
        return ReplaceArchive(target,bytes,error,publishable,true);
    }catch(const std::exception& e){if(error.empty())error=e.what();return WriteResult::Error;}
}
std::string Store::SnapshotDigest(const Snapshot& snapshot){
    auto [ref,history]=SnapshotParts(snapshot);ref.insert(ref.end(),history.begin(),history.end());return Sha256(ref);
}
std::optional<std::vector<SlotVersion>> Store::ExportSlots(std::string& error)const{
    std::lock_guard lock(slot_publish_mutex);error.clear();
    try{
        std::vector<SlotVersion> out;const auto directory=root_/"slots";
        if(!std::filesystem::exists(directory))return out;
        const auto attributes=GetFileAttributesW(directory.c_str());
        Need(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"unsafe slot directory");
        for(const auto& entry:std::filesystem::directory_iterator(directory)){
            const auto a=GetFileAttributesW(entry.path().c_str());
            Need(a!=INVALID_FILE_ATTRIBUTES&&!(a&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)),"unsafe slot entry");
            auto path=entry.path();const auto filename=path.filename().string();
            Need(filename.find(".pending")==std::string::npos,"unvalidated interrupted slot candidate retained; migration requires recovery review");
            if(path.extension()==L".ready")path.replace_extension();
            const auto versions=ReadSlotVersions(path);const auto& current=versions.front();
            Need(path.filename()==SlotPath(root_,current.binding.archive.profile,current.binding.slot).filename(),"unexpected slot filename");
            if(std::any_of(out.begin(),out.end(),[&](const auto& v){return v.binding.archive.profile==current.binding.archive.profile&&v.binding.slot==current.binding.slot;}))continue;
            out.push_back(current);
        }
        return out;
    }catch(const std::exception& e){error=e.what();return {};}
}
WriteResult Store::ImportSlot(const SlotVersion& version,std::string& error){
    std::lock_guard lock(slot_publish_mutex);error.clear();
    try{
        const auto existing=LookupSlot(version.binding.archive.profile,version.binding.slot);
        if(existing.status!=LookupStatus::Missing){
            if(existing.status==LookupStatus::Loaded&&existing.binding==version.binding&&existing.snapshot==version.snapshot)
                return WriteResult::AlreadyPresent;
            error="offline slot import conflicts with existing data";return WriteResult::Conflict;
        }
        return ReplaceArchive(SlotPath(root_,version.binding.archive.profile,version.binding.slot),VersionsOut({version}),error);
    }catch(const std::exception& e){error=e.what();return WriteResult::Error;}
}
