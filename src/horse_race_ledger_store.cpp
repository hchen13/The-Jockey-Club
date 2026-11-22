#include "horse_race_ledger_store.h"
#include "race_progression.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace horse_races::horse_ledger {
namespace {
using Bytes=std::vector<std::uint8_t>;
constexpr std::size_t kMax=32*1024*1024, kSaveMax=8*1024*1024;
struct Handle { HANDLE h{INVALID_HANDLE_VALUE}; ~Handle(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);} };
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Writer {
    Bytes b;
    void U(std::uint64_t n,int width=8){for(int i=0;i<width;++i)b.push_back(static_cast<std::uint8_t>(n>>(i*8)));Need(b.size()<=kMax,"snapshot too large");}
    void S(const std::string& s,std::size_t maximum=4096){Need(s.size()<=maximum,"field too long");U(s.size(),4);b.insert(b.end(),s.begin(),s.end());}
};
struct Reader {
    std::span<const std::uint8_t> b; std::size_t at{};
    std::uint64_t U(int width=8){Need(at+width<=b.size(),"truncated snapshot");std::uint64_t v=0;for(int i=0;i<width;++i)v|=std::uint64_t(b[at++])<<(i*8);return v;}
    std::string S(std::size_t maximum=4096){const auto n=U(4);Need(n<=maximum && n<=b.size()-at,"invalid field length");std::string s(reinterpret_cast<const char*>(b.data()+at),static_cast<std::size_t>(n));at+=static_cast<std::size_t>(n);return s;}
    void End(){Need(at==b.size(),"trailing snapshot data");}
};
void RevisionOut(Writer& w,const Revision& r){w.S(r.profile);w.S(r.sha256);}
Revision RevisionIn(Reader& r){return {r.S(),r.S()};}
Bytes WinsOut(const std::vector<Win>& wins){
    Writer w;w.U(wins.size(),4);
    for(const auto& x:wins){w.S(x.event_id);for(auto v:x.horse.words)w.U(v,4);w.S(x.race_id);w.S(x.race_name);w.S(x.protagonist);w.S(x.horse_name);w.U(x.horse_model,4);w.U(static_cast<unsigned>(x.kind),4);w.U(x.opponents.size(),4);
        for(const auto& o:x.opponents){w.S(o.character);w.U(o.model,4);w.U(static_cast<unsigned>(o.speed),4);w.U(static_cast<unsigned>(o.acceleration),4);w.U(o.host,1);}
        w.U(static_cast<std::uint64_t>(x.net_cents));w.U(x.unix_ms);w.U(x.practice,1);w.U(x.opponent_accident,1);
    }return w.b;
}
bool BoolIn(Reader& r){auto v=r.U(1);Need(v<=1,"invalid boolean");return v!=0;}
std::vector<Win> WinsIn(std::span<const std::uint8_t> bytes){
    Reader r{bytes};auto n=r.U(4);Need(n<=100000,"too many wins");std::vector<Win> out;out.reserve(static_cast<std::size_t>(n));
    for(std::uint64_t i=0;i<n;++i){Win x;x.event_id=r.S();for(auto& v:x.horse.words)v=static_cast<std::uint32_t>(r.U(4));x.race_id=r.S();x.race_name=r.S();x.protagonist=r.S();x.horse_name=r.S();x.horse_model=static_cast<std::uint32_t>(r.U(4));x.kind=static_cast<RaceKind>(r.U(4));auto count=r.U(4);Need(count<=32,"too many opponents");
        for(std::uint64_t j=0;j<count;++j){Opponent o;o.character=r.S();o.model=static_cast<std::uint32_t>(r.U(4));auto speed=r.U(4),acc=r.U(4);Need(speed<=10 && acc<=10,"invalid opponent stats");o.speed=static_cast<int>(speed);o.acceleration=static_cast<int>(acc);o.host=BoolIn(r);x.opponents.push_back(o);}
        auto cents=r.U();Need(cents<=static_cast<std::uint64_t>(INT64_MAX),"invalid winnings");x.net_cents=static_cast<Cents>(cents);x.unix_ms=r.U();x.practice=BoolIn(r);x.opponent_accident=BoolIn(r);out.push_back(std::move(x));
    }r.End();return out;
}
void SaleOut(Writer& w,const Sale& s,bool attributed=false){
    w.S(s.event_id);for(auto word:s.horse.words)w.U(word,4);
    w.U(static_cast<std::uint64_t>(s.base_cents));w.U(static_cast<std::uint64_t>(s.premium_cents));w.U(static_cast<std::uint64_t>(s.total_cents));
    w.U(s.win_event_ids.size(),4);for(const auto& id:s.win_event_ids)w.S(id);w.U(s.unix_ms);
    if(attributed)w.U(static_cast<unsigned>(s.seller),4);
}
Sale SaleIn(Reader& r,bool attributed=false){
    Sale s;s.event_id=r.S();for(auto& word:s.horse.words)word=static_cast<std::uint32_t>(r.U(4));
    const auto money=[&](){auto value=r.U();Need(value<=static_cast<std::uint64_t>(INT64_MAX),"invalid sale cents");return static_cast<Cents>(value);};
    s.base_cents=money();s.premium_cents=money();s.total_cents=money();auto count=r.U(4);Need(count<=100000,"too many sale win IDs");
    for(std::uint64_t i=0;i<count;++i)s.win_event_ids.push_back(r.S());s.unix_ms=r.U();if(attributed)s.seller=static_cast<SellerEra>(r.U(4));return s;
}
void MembershipOut(Writer& w,const Membership& m){w.S(m.event_id);w.U(static_cast<unsigned>(m.seller),4);w.U(static_cast<std::uint64_t>(m.fee_cents));w.S(m.qualifying_sale_id);w.U(m.unix_ms);}
Membership MembershipIn(Reader& r){Membership m;m.event_id=r.S();m.seller=static_cast<SellerEra>(r.U(4));auto fee=r.U();Need(fee<=INT64_MAX,"invalid membership fee");m.fee_cents=static_cast<Cents>(fee);m.qualifying_sale_id=r.S();m.unix_ms=r.U();return m;}
Bytes HistoryOut(const Snapshot& s,bool attributed=false,bool progression=false){
    Writer w;auto wins=WinsOut(s.wins);w.U(wins.size(),4);w.b.insert(w.b.end(),wins.begin(),wins.end());
    w.U(s.sales.size(),4);for(const auto& sale:s.sales)SaleOut(w,sale,attributed);
    if(attributed){w.U(s.memberships.size(),4);for(const auto& member:s.memberships)MembershipOut(w,member);}
    if(progression){Need(s.progression.has_value(),"missing progression");w.S(*s.progression,4096);}return w.b;
}
void HistoryIn(std::span<const std::uint8_t> bytes,Snapshot& s,bool attributed=false,bool progression=false){
    Reader r{bytes};auto size=r.U(4);Need(size<=r.b.size()-r.at,"invalid wins section length");
    s.wins=WinsIn(r.b.subspan(r.at,static_cast<std::size_t>(size)));r.at+=static_cast<std::size_t>(size);
    auto count=r.U(4);Need(count<=100000,"too many sales");s.sales.reserve(static_cast<std::size_t>(count));
    for(std::uint64_t i=0;i<count;++i)s.sales.push_back(SaleIn(r,attributed));
    if(attributed){auto members=r.U(4);Need(members<=2,"too many membership eras");for(std::uint64_t i=0;i<members;++i)s.memberships.push_back(MembershipIn(r));}
    if(progression)s.progression=r.S(4096);r.End();
}
Bytes Wrap(const char* magic,const Bytes& payload){Writer w;w.b.insert(w.b.end(),magic,magic+8);w.U(1,4);w.U(payload.size(),4);w.b.insert(w.b.end(),payload.begin(),payload.end());auto digest=Sha256(w.b);w.b.insert(w.b.end(),digest.begin(),digest.end());Need(w.b.size()<=kMax,"snapshot too large");return w.b;}
Bytes Unwrap(const char* magic,const Bytes& b){Need(b.size()>=80 && std::equal(magic,magic+8,b.begin()),"invalid snapshot magic");Reader r{b};r.at=8;Need(r.U(4)==1,"unsupported snapshot version");auto n=r.U(4);Need(n==b.size()-80,"invalid snapshot length");auto digest=Sha256(std::span(b).first(b.size()-64));Need(std::equal(digest.begin(),digest.end(),b.end()-64),"snapshot integrity failure");return Bytes(b.begin()+16,b.end()-64);}
bool Hex(const std::string& s){return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
std::string RefKey(const Revision& r){Writer w;RevisionOut(w,r);return Sha256(w.b);}
std::filesystem::path SlotPath(const std::filesystem::path& root,const std::string& profile,std::uint32_t slot){
    const auto hash=Sha256({reinterpret_cast<const std::uint8_t*>(profile.data()),profile.size()});
    return root/"slots"/(hash+"-"+std::to_string(slot)+".bin");
}
bool CanonicalCampaign(const std::optional<std::string>& encoded){
    if(!encoded)return false;
    const auto decoded=progression::State::Decode(*encoded);
    return decoded&&decoded->Encode()==*encoded;
}
bool ValidBinding(const SlotBinding& b){return b.archive.Valid()&&b.slot<=15&&Hex(b.game_sha256)&&b.generation!=0;}
Bytes SlotOut(const SlotBinding& b){Writer w;RevisionOut(w,b.archive);w.U(b.slot,4);w.S(b.game_sha256);w.U(b.generation);return Wrap("TJCSLT02",w.b);}
SlotBinding SlotIn(const Bytes& bytes){auto payload=Unwrap("TJCSLT02",bytes);Reader r{payload};SlotBinding b;
    b.archive=RevisionIn(r);b.slot=static_cast<std::uint32_t>(r.U(4));b.game_sha256=r.S();b.generation=r.U();r.End();Need(ValidBinding(b),"invalid slot binding");return b;}
// Serialize cooperating writers across Store objects. Recursive so a fence may
// observe/publish a newer save; the outer writer then fails its second CAS.
std::recursive_mutex slot_publish_mutex;
void OwnedDirectory(const std::filesystem::path&);
struct ReadIoError:std::runtime_error {DWORD code;ReadIoError(const char* message,DWORD value):std::runtime_error(message),code(value){}};
Bytes Read(const std::filesystem::path& p,std::size_t maximum=kMax){
    Handle f{CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};if(f.h==INVALID_HANDLE_VALUE)throw ReadIoError("cannot open snapshot",GetLastError());BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(f.h,&info))throw ReadIoError("cannot inspect snapshot",GetLastError());Need(!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&info.nNumberOfLinks==1,"invalid or hard-linked snapshot file");auto n=(std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;Need(n<=maximum,"file too large");Bytes b(static_cast<std::size_t>(n));std::size_t at=0;while(at<b.size()){DWORD got=0;auto want=static_cast<DWORD>(std::min<std::size_t>(65536,b.size()-at));if(!ReadFile(f.h,b.data()+at,want,&got,nullptr))throw ReadIoError("snapshot read failed",GetLastError());if(got!=want)throw ReadIoError("incomplete file read",ERROR_HANDLE_EOF);at+=got;}return b;
}
}
#include "horse_race_ledger_io.inl"
namespace {
WriteResult Immutable(const std::filesystem::path& target,const Bytes& bytes){
    OwnedDirectory(target.parent_path());auto tmp=target;tmp+=L".tmp";
    Handle f{CreateFileW(tmp.c_str(),GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr)};
    Need(f.h!=INVALID_HANDLE_VALUE,"cannot create snapshot temporary file");bool published=false;
    try {
        std::size_t at=0;while(at<bytes.size()){DWORD done=0;auto want=static_cast<DWORD>(std::min<std::size_t>(65536,bytes.size()-at));Need(WriteFile(f.h,bytes.data()+at,want,&done,nullptr)!=0 && done==want,"snapshot write failed");at+=done;}
        Need(FlushFileBuffers(f.h)!=0,"snapshot flush failed");
        if(RenameOwnedFile(f.h,target,false)){published=true;Need(FlushFileBuffers(f.h)!=0,"snapshot publication flush failed");return WriteResult::Written;}
        const auto code=GetLastError();Need(RemoveOwnedFile(f.h),"cannot discard owned snapshot candidate");
        if(code==ERROR_ALREADY_EXISTS||code==ERROR_FILE_EXISTS)return Read(target)==bytes?WriteResult::AlreadyPresent:WriteResult::Conflict;
        throw std::runtime_error("snapshot publish failed");
    }catch(...){if(!published)RemoveOwnedFile(f.h);throw;}
}
void LegacyAdmission(const std::filesystem::path& root,std::size_t reserve_bytes){
    constexpr std::uint64_t maximum=256ull*1024*1024;std::uint64_t total=0;
    for(const auto* category:{"refs","heads"}){
        const auto folder=root/category;std::size_t count=0;
        if(!std::filesystem::exists(folder))continue;
        Need((GetFileAttributesW(folder.c_str())&FILE_ATTRIBUTE_REPARSE_POINT)==0,"unsafe legacy store directory");
        for(const auto& entry:std::filesystem::directory_iterator(folder)){
            const auto attributes=GetFileAttributesW(entry.path().c_str());
            Need(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)),"unknown legacy store entry");
            const auto name=entry.path().filename().string();
            Need(name.size()==68&&name.ends_with(".bin")&&Hex(name.substr(0,64)),"unknown or interrupted legacy store entry retained");
            const auto size=entry.file_size();Need(++count<=510&&size<=maximum-total,"legacy compatibility store capacity reached");total+=size;
        }
    }
    Need(reserve_bytes<=maximum-total,"legacy compatibility store byte capacity reached");
}
}
std::string Sha256(std::span<const std::uint8_t> bytes){
    BCRYPT_ALG_HANDLE alg{};Need(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider unavailable");BCRYPT_HASH_HANDLE hash{};std::string result;
    try {DWORD size=0,got=0;Need(BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&got,0)>=0,"SHA256 properties failed");Bytes object(size),digest(32);Need(BCryptCreateHash(alg,&hash,object.data(),size,nullptr,0,0)>=0,"SHA256 create failed");std::size_t at=0;while(at<bytes.size()){auto n=static_cast<ULONG>(std::min<std::size_t>(65536,bytes.size()-at));Need(BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()+at),n,0)>=0,"SHA256 update failed");at+=n;}Need(BCryptFinishHash(hash,digest.data(),32,0)>=0,"SHA256 finish failed");static constexpr char hex[]="0123456789abcdef";for(auto b:digest){result+=hex[b>>4];result+=hex[b&15];}BCryptDestroyHash(hash);hash=nullptr;BCryptCloseAlgorithmProvider(alg,0);return result;}catch(...){if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);throw;}
}
std::optional<StableFileHash> HashStableFile(const std::filesystem::path& path,std::string& error){
    error.clear();try {auto start=GetTickCount64();std::optional<BY_HANDLE_FILE_INFORMATION> prior;std::string digest;std::uint64_t size=0,stamp=0;
        for(int pass=0;pass<2;++pass){Handle h{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};Need(h.h!=INVALID_HANDLE_VALUE,"cannot open vanilla save");BY_HANDLE_FILE_INFORMATION before{},after{};Need(GetFileInformationByHandle(h.h,&before)!=0 && !(before.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)),"reparse/directory save rejected");size=(std::uint64_t(before.nFileSizeHigh)<<32)|before.nFileSizeLow;Need(size>0 && size<=kSaveMax,"invalid vanilla save size");Bytes b(static_cast<std::size_t>(size));std::size_t at=0;while(at<b.size()){DWORD got=0;auto n=static_cast<DWORD>(std::min<std::size_t>(65536,b.size()-at));Need(ReadFile(h.h,b.data()+at,n,&got,nullptr)!=0 && got==n,"unstable vanilla save read");at+=got;}Need(GetFileInformationByHandle(h.h,&after)!=0,"save metadata unavailable");auto same=[](const auto& a,const auto& z){return a.dwVolumeSerialNumber==z.dwVolumeSerialNumber && a.nFileIndexHigh==z.nFileIndexHigh && a.nFileIndexLow==z.nFileIndexLow && a.nFileSizeHigh==z.nFileSizeHigh && a.nFileSizeLow==z.nFileSizeLow && CompareFileTime(&a.ftLastWriteTime,&z.ftLastWriteTime)==0 && CompareFileTime(&a.ftCreationTime,&z.ftCreationTime)==0;};Need(same(before,after) && (!prior || same(*prior,before)),"vanilla save changed during hash");auto value=Sha256(b);Need(pass==0 || digest==value,"vanilla save content changed");digest=value;prior=before;stamp=(std::uint64_t(before.ftLastWriteTime.dwHighDateTime)<<32)|before.ftLastWriteTime.dwLowDateTime;}
        return StableFileHash{digest,size,stamp,start,GetTickCount64()};
    }catch(const std::exception& e){error=e.what();return std::nullopt;}
}
LookupResult Store::LookupLegacy(const Revision& revision)const{
    bool ref_read=false;
    try{
        Need(revision.Valid(),"invalid revision");
        auto ref=Read(root_/"refs"/(RefKey(revision)+".bin"));ref_read=true;
        auto payload=Unwrap("TJCREF01",ref);Reader r{payload};Snapshot s;
        s.revision=RevisionIn(r);Need(s.revision==revision,"revision mismatch");
        if(BoolIn(r))s.parent=RevisionIn(r);s.evidence=static_cast<Evidence>(r.U(4));
        const auto head=r.S();r.End();Need(Hex(head),"invalid history digest");
        const auto bytes=Read(root_/"heads"/(head+".bin"));Need(Sha256(bytes)==head,"history digest mismatch");
        const auto is=[&](const char* magic){return bytes.size()>=8&&std::equal(magic,magic+8,bytes.begin());};
        if(is("TJCHIS04"))HistoryIn(Unwrap("TJCHIS04",bytes),s,true,true);
        else if(is("TJCHIS03"))HistoryIn(Unwrap("TJCHIS03",bytes),s,true);
        else if(is("TJCHIS02"))HistoryIn(Unwrap("TJCHIS02",bytes),s);
        else s.wins=WinsIn(Unwrap("TJCHIS01",bytes));
        std::string error;if(!Validate(s,&error))throw std::runtime_error(error.empty()?"invalid snapshot semantics":error);
        return {LookupStatus::Loaded,std::move(s),{}};
    }catch(const ReadIoError& e){
        const bool absent=e.code==ERROR_FILE_NOT_FOUND||e.code==ERROR_PATH_NOT_FOUND;
        return {!ref_read&&absent?LookupStatus::Missing:absent?LookupStatus::Corrupt:LookupStatus::IoError,{},
            std::string(e.what())+" (win32="+std::to_string(e.code)+")"};
    }catch(const std::exception& e){return {LookupStatus::Corrupt,{},e.what()};}
}
std::optional<Snapshot> Store::Load(const Revision& revision,std::string& error)const{
    auto result=Lookup(revision);error=std::move(result.error);return std::move(result.snapshot);
}
WriteResult Store::Publish(const Snapshot& snapshot,std::string& error){
    std::lock_guard lock(slot_publish_mutex);
    error.clear();try {Need(Validate(snapshot,&error),"invalid snapshot semantics");auto target=root_/"refs"/(RefKey(snapshot.revision)+".bin");
        if(std::filesystem::exists(target)){auto existing=Load(snapshot.revision,error);if(!existing)return WriteResult::Conflict;if(existing->parent==snapshot.parent && existing->evidence==snapshot.evidence && existing->wins==snapshot.wins && existing->sales==snapshot.sales && existing->memberships==snapshot.memberships && existing->progression==snapshot.progression)return WriteResult::AlreadyPresent;error="immutable revision conflict";return WriteResult::Conflict;}
        const bool attributed=!snapshot.memberships.empty() || std::any_of(snapshot.sales.begin(),snapshot.sales.end(),[](const Sale& sale){return sale.seller!=SellerEra::Unknown;});
        auto history=snapshot.progression?Wrap("TJCHIS04",HistoryOut(snapshot,true,true)):attributed?Wrap("TJCHIS03",HistoryOut(snapshot,true)):snapshot.sales.empty()?Wrap("TJCHIS01",WinsOut(snapshot.wins)):Wrap("TJCHIS02",HistoryOut(snapshot));
        LegacyAdmission(root_,2*(history.size()+4096));
        auto head=Sha256(history);auto hr=Immutable(root_/"heads"/(head+".bin"),history);if(hr==WriteResult::Conflict){error="immutable history conflict";return hr;}
        Writer w;RevisionOut(w,snapshot.revision);w.U(snapshot.parent.has_value(),1);if(snapshot.parent)RevisionOut(w,*snapshot.parent);w.U(static_cast<unsigned>(snapshot.evidence),4);w.S(head);auto result=Immutable(target,Wrap("TJCREF01",w.b));
        if(result==WriteResult::Conflict){auto existing=Load(snapshot.revision,error);if(existing && existing->parent==snapshot.parent && existing->evidence==snapshot.evidence && existing->wins==snapshot.wins && existing->sales==snapshot.sales && existing->memberships==snapshot.memberships && existing->progression==snapshot.progression){error.clear();return WriteResult::AlreadyPresent;}if(error.empty())error="immutable revision conflict";}return result;
    }catch(const std::exception& e){if(error.empty())error=e.what();return WriteResult::Error;}
}
#include "horse_race_ledger_slots.inl"
#include "horse_race_ledger_audit.inl"
}
