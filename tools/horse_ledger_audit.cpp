// Offline, read-only ledger/receipt inspection. No repair, seeding or wallet calls.
#include "../src/horse_race_ledger_store.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace horse_races::horse_ledger;
namespace {
using Bytes=std::vector<std::uint8_t>;
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::string Q(const std::string& text){std::string out="\"";for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32){char b[7]{};std::snprintf(b,sizeof(b),"\\u%04x",c);out+=b;}else out+=static_cast<char>(c);}return out+'"';}
bool Hex(const std::string& s,std::size_t size){return s.size()==size && std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
struct Reader {
    std::span<const std::uint8_t> b;std::size_t at{};
    std::uint64_t U(int width=8){Need(at+width<=b.size(),"truncated receipt");std::uint64_t value=0;for(int i=0;i<width;++i)value|=std::uint64_t(b[at++])<<(i*8);return value;}
    std::string S(){const auto n=U(4);Need(n<=4096 && n<=b.size()-at,"receipt field length rejected");std::string s(reinterpret_cast<const char*>(b.data()+at),static_cast<std::size_t>(n));at+=static_cast<std::size_t>(n);return s;}
    bool B(){auto n=U(1);Need(n<=1,"invalid receipt boolean");return n!=0;}
};
Bytes Read(const std::filesystem::path& path,DWORD maximum=262144){
    const auto h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);Need(h!=INVALID_HANDLE_VALUE,"cannot read receipt");
    try {BY_HANDLE_FILE_INFORMATION before{},after{};Need(GetFileInformationByHandle(h,&before)!=0,"receipt metadata unavailable");Need(!(before.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) && before.nFileSizeHigh==0 && before.nFileSizeLow>=80 && before.nFileSizeLow<=maximum,"receipt type/size rejected");Bytes bytes(before.nFileSizeLow);DWORD got=0;Need(ReadFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)!=0 && got==bytes.size(),"receipt short read");Need(GetFileInformationByHandle(h,&after)!=0 && before.nFileSizeLow==after.nFileSizeLow && before.nFileSizeHigh==after.nFileSizeHigh && CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)==0,"receipt changed during read");CloseHandle(h);return bytes;}catch(...){CloseHandle(h);throw;}
}
struct Receipt {Revision base;Win win;Bytes bytes;};
Receipt Decode(const std::filesystem::path& path){
    auto bytes=Read(path);Need(std::equal(bytes.begin(),bytes.begin()+8,"TJCJRN01"),"receipt schema rejected");Reader envelope{bytes};envelope.at=8;Need(envelope.U(4)==1,"receipt version rejected");Need(envelope.U(4)==bytes.size()-80,"receipt envelope length rejected");const auto digest=Sha256(std::span(bytes).first(bytes.size()-64));Need(std::equal(digest.begin(),digest.end(),bytes.end()-64),"receipt checksum mismatch");Reader r{std::span(bytes).subspan(16,bytes.size()-80)};Receipt result;result.base={r.S(),r.S()};Need(result.base.Valid(),"invalid receipt base");Need(r.U(4)==1,"receipt must contain exactly one win");auto& w=result.win;w.event_id=r.S();for(auto& word:w.horse.words)word=static_cast<std::uint32_t>(r.U(4));w.race_id=r.S();w.race_name=r.S();w.protagonist=r.S();w.horse_name=r.S();w.horse_model=static_cast<std::uint32_t>(r.U(4));w.kind=static_cast<RaceKind>(r.U(4));const auto n=r.U(4);Need(n<=32,"receipt opponent count rejected");
    for(std::uint64_t i=0;i<n;++i){Opponent o;o.character=r.S();o.model=static_cast<std::uint32_t>(r.U(4));auto speed=r.U(4),acceleration=r.U(4);Need(speed<=10 && acceleration<=10,"receipt opponent stats rejected");o.speed=static_cast<int>(speed);o.acceleration=static_cast<int>(acceleration);o.host=r.B();w.opponents.push_back(o);}
    auto cents=r.U();Need(cents<=INT64_MAX,"receipt amount rejected");w.net_cents=static_cast<Cents>(cents);w.unix_ms=r.U();w.practice=r.B();w.opponent_accident=r.B();Need(r.at==r.b.size(),"receipt trailing data rejected");std::string reason;if(!Validate(w,&reason))throw std::runtime_error(reason);
    Bytes key;auto add=[&](const std::string& text){for(int i=0;i<4;++i)key.push_back(static_cast<std::uint8_t>(text.size()>>(8*i)));key.insert(key.end(),text.begin(),text.end());};add(result.base.profile);add(w.event_id);Need(path.stem().string()==Sha256(key),"receipt filename key mismatch");result.bytes=std::move(bytes);return result;
}
void WinJson(const Win& w){
    std::cout<<"{\"event_id\":"<<Q(w.event_id)<<",\"guid\":"<<Q(w.horse.Key())<<",\"horse_name\":"<<Q(w.horse_name)<<",\"horse_model\":"<<w.horse_model<<",\"race_id\":"<<Q(w.race_id)<<",\"race_name\":"<<Q(w.race_name)<<",\"kind\":"<<Q(w.kind==RaceKind::Duel?"duel":"breed_limited")<<",\"protagonist\":"<<Q(w.protagonist)<<",\"net_cents\":"<<w.net_cents<<",\"unix_ms\":"<<w.unix_ms<<",\"practice\":"<<(w.practice?"true":"false")<<",\"opponent_accident\":"<<(w.opponent_accident?"true":"false")<<",\"opponents\":[";
    bool first=true;for(const auto& o:w.opponents){if(!first)std::cout<<',';first=false;std::cout<<"{\"character\":"<<Q(o.character)<<",\"model\":"<<o.model<<",\"speed\":"<<o.speed<<",\"acceleration\":"<<o.acceleration<<",\"host\":"<<(o.host?"true":"false")<<'}';}std::cout<<"]}";
}
void SaleJson(const Sale& sale){
    std::cout<<"{\"event_id\":"<<Q(sale.event_id)<<",\"guid\":"<<Q(sale.horse.Key())
        <<",\"base_cents\":"<<sale.base_cents<<",\"premium_cents\":"<<sale.premium_cents
        <<",\"total_cents\":"<<sale.total_cents<<",\"win_event_ids\":[";
    bool first=true;for(const auto& id:sale.win_event_ids){if(!first)std::cout<<',';first=false;std::cout<<Q(id);}
    std::cout<<"],\"seller\":"<<static_cast<int>(sale.seller)<<",\"unix_ms\":"<<sale.unix_ms<<'}';
}
struct SaleReceipt {Revision base;Sale sale;Bytes bytes;};
void MembershipJson(const Membership& m){
    std::cout<<"{\"event_id\":"<<Q(m.event_id)<<",\"seller\":"<<static_cast<int>(m.seller)
        <<",\"fee_cents\":"<<m.fee_cents<<",\"qualifying_sale_id\":"<<Q(m.qualifying_sale_id)<<",\"unix_ms\":"<<m.unix_ms<<'}';
}
struct MembershipReceipt {Revision base;Membership member;Bytes bytes;};
MembershipReceipt DecodeMembership(const std::filesystem::path& path){
    auto bytes=Read(path);Need(std::equal(bytes.begin(),bytes.begin()+8,"TJCMEM01"),"membership receipt schema rejected");
    Reader envelope{bytes};envelope.at=8;Need(envelope.U(4)==1,"membership receipt version rejected");
    Need(envelope.U(4)==bytes.size()-80,"membership receipt length rejected");
    const auto digest=Sha256(std::span(bytes).first(bytes.size()-64));Need(std::equal(digest.begin(),digest.end(),bytes.end()-64),"membership receipt checksum mismatch");
    Reader r{std::span(bytes).subspan(16,bytes.size()-80)};MembershipReceipt result;result.base={r.S(),r.S()};Need(result.base.Valid(),"invalid membership receipt base");
    auto& m=result.member;m.event_id=r.S();m.seller=static_cast<SellerEra>(r.U(4));const auto fee=r.U();Need(fee<=INT64_MAX,"membership receipt fee rejected");
    m.fee_cents=static_cast<Cents>(fee);m.qualifying_sale_id=r.S();m.unix_ms=r.U();Need(r.at==r.b.size(),"membership receipt trailing data rejected");
    std::string reason;if(!Validate(m,&reason))throw std::runtime_error(reason);
    Bytes key;auto add=[&](const std::string& text){for(int i=0;i<4;++i)key.push_back(static_cast<std::uint8_t>(text.size()>>(8*i)));key.insert(key.end(),text.begin(),text.end());};
    add(result.base.profile);add(m.event_id);Need(path.stem().string()==Sha256(key),"membership receipt filename key mismatch");result.bytes=std::move(bytes);return result;
}
SaleReceipt DecodeSale(const std::filesystem::path& path){
    auto bytes=Read(path,32*1024*1024);
    const bool attributed=std::equal(bytes.begin(),bytes.begin()+8,"TJCSAL02");
    Need(attributed || std::equal(bytes.begin(),bytes.begin()+8,"TJCSAL01"),"sale receipt schema rejected");
    Reader envelope{bytes};envelope.at=8;
    Need(envelope.U(4)==1,"sale receipt version rejected");
    Need(envelope.U(4)==bytes.size()-80,"sale receipt envelope length rejected");
    const auto digest=Sha256(std::span(bytes).first(bytes.size()-64));
    Need(std::equal(digest.begin(),digest.end(),bytes.end()-64),"sale receipt checksum mismatch");
    Reader r{std::span(bytes).subspan(16,bytes.size()-80)};SaleReceipt result;
    result.base={r.S(),r.S()};Need(result.base.Valid(),"invalid sale receipt base");
    auto& sale=result.sale;sale.event_id=r.S();for(auto& word:sale.horse.words)word=static_cast<std::uint32_t>(r.U(4));
    const auto money=[&](){const auto value=r.U();Need(value<=INT64_MAX,"sale receipt amount rejected");return static_cast<Cents>(value);};
    sale.base_cents=money();sale.premium_cents=money();sale.total_cents=money();
    const auto count=r.U(4);Need(count<=100000,"sale receipt win count rejected");
    for(std::uint64_t i=0;i<count;++i)sale.win_event_ids.push_back(r.S());sale.unix_ms=r.U();if(attributed)sale.seller=static_cast<SellerEra>(r.U(4));
    Need(r.at==r.b.size(),"sale receipt trailing data rejected");
    std::string reason;if(!Validate(sale,&reason))throw std::runtime_error(reason);
    Bytes key;auto add=[&](const std::string& text){for(int i=0;i<4;++i)key.push_back(static_cast<std::uint8_t>(text.size()>>(8*i)));key.insert(key.end(),text.begin(),text.end());};
    add(result.base.profile);add(sale.event_id);
    Need(path.stem().string()==Sha256(key),"sale receipt filename key mismatch");
    result.bytes=std::move(bytes);return result;
}
void SaleReceiptsJson(const std::filesystem::path& root,const Store& store,unsigned& errors){
    static constexpr std::array<const char*,5> phases{{".prepared",".removal-entered",".removed",".payout-entered",".paid"}};
    std::map<std::string,std::map<std::string,Bytes>> groups;
    std::cout<<",\"sale_receipts\":[";bool first=true;
    const auto directory=root/"sale-receipts";
    if(std::filesystem::exists(directory))for(const auto& entry:std::filesystem::directory_iterator(directory)){
        const auto extension=entry.path().extension().string();
        if(std::find(phases.begin(),phases.end(),extension)==phases.end())continue;
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"file\":"<<Q(entry.path().filename().string());
        try{
            Need(Hex(entry.path().stem().string(),64),"invalid sale receipt filename");
            auto receipt=DecodeSale(entry.path());groups[entry.path().stem().string()][extension]=receipt.bytes;
            std::string error;const auto base=store.Load(receipt.base,error);
            std::cout<<",\"valid\":true,\"phase\":"<<Q(extension.substr(1))
                <<",\"base_profile\":"<<Q(receipt.base.profile)<<",\"base_sha256\":"<<Q(receipt.base.sha256)
                <<",\"base_known\":"<<(base?"true":"false")<<",\"sale\":";
            SaleJson(receipt.sale);
        }catch(const std::exception&e){++errors;std::cout<<",\"valid\":false,\"error\":"<<Q(e.what());}
        std::cout<<'}';
    }
    std::cout<<"],\"sale_pairs\":[";first=true;
    for(const auto& [key,present]:groups){
        if(!first)std::cout<<',';first=false;
        bool matching=true,sequential=true,missing=false;std::string highest;
        const auto& bytes=present.begin()->second;
        std::cout<<"{\"key\":"<<Q(key)<<",\"phases\":[";bool phase_first=true;
        for(const auto* phase:phases){const auto found=present.find(phase);
            if(found==present.end()){missing=true;continue;}
            if(missing)sequential=false;if(found->second!=bytes)matching=false;
            if(!phase_first)std::cout<<',';phase_first=false;highest=std::string(phase).substr(1);std::cout<<Q(highest);
        }
        const bool complete=matching&&sequential&&present.size()==phases.size();
        std::cout<<"],\"highest_phase\":"<<Q(highest)<<",\"sequential\":"<<(sequential?"true":"false")
            <<",\"matching_metadata\":"<<(matching?"true":"false")<<",\"complete\":"<<(complete?"true":"false")
            <<",\"replay_permitted\":false";
        if(!matching){++errors;std::cout<<",\"error\":\"sale receipt phase metadata mismatch\"";}
        if(!sequential)++errors;
        std::cout<<'}';
    }
    std::cout<<']';
}
void MembershipReceiptsJson(const std::filesystem::path& root,const Store& store,unsigned& errors){
    static constexpr std::array<const char*,3> phases{{".prepared",".payment-entered",".paid"}};
    std::map<std::string,std::map<std::string,Bytes>> groups;
    std::cout<<",\"membership_receipts\":[";bool first=true;
    const auto directory=root/"membership-receipts";
    if(std::filesystem::exists(directory))for(const auto& entry:std::filesystem::directory_iterator(directory)){
        const auto extension=entry.path().extension().string();
        if(std::find(phases.begin(),phases.end(),extension)==phases.end())continue;
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"file\":"<<Q(entry.path().filename().string());
        try{
            Need(Hex(entry.path().stem().string(),64),"invalid membership receipt filename");
            auto receipt=DecodeMembership(entry.path());groups[entry.path().stem().string()][extension]=receipt.bytes;
            std::string error;const auto base=store.Load(receipt.base,error);
            std::cout<<",\"valid\":true,\"phase\":"<<Q(extension.substr(1))
                <<",\"base_profile\":"<<Q(receipt.base.profile)<<",\"base_sha256\":"<<Q(receipt.base.sha256)
                <<",\"base_known\":"<<(base?"true":"false")<<",\"membership\":";
            MembershipJson(receipt.member);
        }catch(const std::exception&e){++errors;std::cout<<",\"valid\":false,\"error\":"<<Q(e.what());}
        std::cout<<'}';
    }
    std::cout<<"],\"membership_pairs\":[";first=true;
    for(const auto& [key,present]:groups){
        if(!first)std::cout<<',';first=false;
        bool matching=true,sequential=true,missing=false;std::string highest;
        const auto& bytes=present.begin()->second;
        std::cout<<"{\"key\":"<<Q(key)<<",\"phases\":[";bool phase_first=true;
        for(const auto* phase:phases){const auto found=present.find(phase);
            if(found==present.end()){missing=true;continue;}
            if(missing)sequential=false;if(found->second!=bytes)matching=false;
            if(!phase_first)std::cout<<',';phase_first=false;highest=std::string(phase).substr(1);std::cout<<Q(highest);
        }
        const bool complete=matching&&sequential&&present.size()==phases.size();
        std::cout<<"],\"highest_phase\":"<<Q(highest)<<",\"sequential\":"<<(sequential?"true":"false")
            <<",\"matching_metadata\":"<<(matching?"true":"false")<<",\"complete\":"<<(complete?"true":"false")
            <<",\"replay_permitted\":false";
        if(!matching){++errors;std::cout<<",\"error\":\"membership receipt phase metadata mismatch\"";}
        if(!sequential)++errors;
        std::cout<<'}';
    }
    std::cout<<']';
}
}
int wmain(int argc,wchar_t** argv){
    try {
        std::filesystem::path root,profile;int schema=0;
        for(int i=1;i<argc;++i){
            const std::wstring arg=argv[i];
            Need((arg==L"--store"||arg==L"--profile"||arg==L"--schema")&&i+1<argc,
                "usage: horse_ledger_audit --store PATH --profile PROFILE_DIRECTORY [--schema 1|2]");
            const std::wstring value=argv[++i];
            if(arg==L"--schema"){Need(!schema&&(value==L"1"||value==L"2"),"schema must be 1 or 2, specified once");schema=value==L"2"?2:1;}
            else if(arg==L"--store"){Need(root.empty(),"duplicate store argument");root=value;}
            else{Need(profile.empty(),"duplicate profile argument");profile=value;}
        }
        Need(!root.empty()&&!profile.empty(),"both store and profile arguments required");
        root=root.lexically_normal();if(root.filename().empty())root=root.parent_path();
        if(!schema)schema=(root.filename()==L"ModArchives-v2"||std::filesystem::is_directory(root/"slots"))?2:1;
        profile=profile.lexically_normal();if(profile.filename().empty())profile=profile.parent_path();
        const auto name=profile.filename().string();
        Need(name.size()==8&&std::all_of(name.begin(),name.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}),"invalid profile directory name");
        Store store(root);unsigned errors=0;
        std::cout<<"{\"read_only\":true,\"schema\":"<<schema<<",\"profile\":"<<Q(name)<<",\"slots\":[";bool first=true;
        for(unsigned slot=0;slot<16;++slot){
            char file[16]{};std::snprintf(file,sizeof(file),"SRDR3%04u",slot);const auto path=profile/file;
            if(!std::filesystem::exists(path))continue;
            if(!first)std::cout<<',';first=false;
            std::cout<<"{\"slot\":"<<slot;std::string error;const auto hash=HashStableFile(path,error);
            if(!hash){std::cout<<",\"ledger_bound\":false,\"state\":\"game_file_error\",\"error\":"<<Q(error)<<'}';++errors;continue;}
            std::cout<<",\"sha256\":"<<Q(hash->sha256)<<",\"size\":"<<hash->size<<",\"write_filetime\":"<<hash->write_filetime;
            std::optional<Snapshot> snapshot;
            if(schema==2){
                const auto lookup=store.LookupSlot(name,slot);
                const char* state="corrupt";
                if(lookup.status==LookupStatus::Missing)state="first_use";
                else if(lookup.status==LookupStatus::IoError)state="io_error";
                else if(lookup.status==LookupStatus::Loaded){
                    if(!lookup.binding||!lookup.snapshot){state="corrupt";++errors;}
                    else{
                        const auto& binding=*lookup.binding;
                        std::cout<<",\"archive_id\":"<<Q(binding.archive.sha256)<<",\"generation\":"<<binding.generation
                            <<",\"binding_game_sha256\":"<<Q(binding.game_sha256);
                        if(binding.game_sha256==hash->sha256){state="bound";snapshot=lookup.snapshot;}
                        else{
                            state="game_hash_mismatch";
                            // Distinguish preserved history from current-file binding.
                            // This is diagnostic only and never adopts stale history.
                            const auto& stored=*lookup.snapshot;
                            std::cout<<",\"stored_archive\":{\"campaign_encoded\":"
                                <<(stored.progression?Q(*stored.progression):"null")
                                <<",\"wins\":"<<stored.wins.size()<<",\"sales\":"<<stored.sales.size()
                                <<",\"memberships\":"<<stored.memberships.size()<<'}';
                        }
                    }
                }
                if(lookup.status==LookupStatus::Corrupt||lookup.status==LookupStatus::IoError)++errors;
                std::cout<<",\"state\":"<<Q(state);
                if(!lookup.error.empty())std::cout<<",\"lookup_error\":"<<Q(lookup.error);
            }else{
                const auto lookup=store.Lookup({name,hash->sha256});snapshot=lookup.snapshot;
                std::cout<<",\"state\":"<<Q(lookup.status==LookupStatus::Loaded?"bound":lookup.status==LookupStatus::Missing?"missing":lookup.status==LookupStatus::IoError?"io_error":"corrupt");
                if(lookup.status==LookupStatus::Corrupt||lookup.status==LookupStatus::IoError)++errors;
                if(!lookup.error.empty())std::cout<<",\"lookup_error\":"<<Q(lookup.error);
            }
            std::cout<<",\"ledger_bound\":"<<(snapshot?"true":"false");
            if(snapshot){
                std::cout<<",\"evidence\":"<<static_cast<int>(snapshot->evidence)<<",\"campaign_encoded\":"
                    <<(snapshot->progression?Q(*snapshot->progression):"null")<<",\"wins\":[";
                bool item_first=true;for(const auto& w:snapshot->wins){if(!item_first)std::cout<<',';item_first=false;WinJson(w);}
                std::cout<<"],\"sales\":[";item_first=true;for(const auto& sale:snapshot->sales){if(!item_first)std::cout<<',';item_first=false;SaleJson(sale);}
                std::cout<<"],\"memberships\":[";item_first=true;for(const auto& member:snapshot->memberships){if(!item_first)std::cout<<',';item_first=false;MembershipJson(member);}
                std::cout<<']';
            }
            std::cout<<'}';
        }
        std::cout<<"],\"receipts\":[";first=true;std::map<std::string,std::map<std::string,Bytes>> pairs;const auto receipts=root/"receipts";
        if(std::filesystem::exists(receipts))for(const auto& entry:std::filesystem::directory_iterator(receipts)){const auto extension=entry.path().extension().string();if(extension!=".paid"&&extension!=".prepared")continue;if(!first)std::cout<<',';first=false;std::cout<<"{\"file\":"<<Q(entry.path().filename().string());try{Need(Hex(entry.path().stem().string(),64),"invalid receipt filename");auto receipt=Decode(entry.path());pairs[entry.path().stem().string()][extension]=receipt.bytes;std::string error;const auto base=store.Load(receipt.base,error);std::cout<<",\"valid\":true,\"phase\":"<<Q(extension.substr(1))<<",\"base_profile\":"<<Q(receipt.base.profile)<<",\"base_sha256\":"<<Q(receipt.base.sha256)<<",\"base_known\":"<<(base?"true":"false")<<",\"base_win_ids\":[";bool idFirst=true;if(base)for(const auto& existing:base->wins){if(!idFirst)std::cout<<',';idFirst=false;std::cout<<Q(existing.event_id);}std::cout<<"],\"win\":";WinJson(receipt.win);}catch(const std::exception& e){++errors;std::cout<<",\"valid\":false,\"error\":"<<Q(e.what());}std::cout<<'}';}
        std::cout<<"],\"pairs\":[";first=true;for(const auto& [key,phases]:pairs){if(!first)std::cout<<',';first=false;const auto p=phases.find(".prepared"),d=phases.find(".paid");bool match=p!=phases.end()&&d!=phases.end()&&p->second==d->second;std::cout<<"{\"key\":"<<Q(key)<<",\"matching_prepared_paid\":"<<(match?"true":"false")<<'}';if(p!=phases.end()&&d!=phases.end()&&!match)++errors;}
        std::cout<<']';SaleReceiptsJson(root,store,errors);MembershipReceiptsJson(root,store,errors);std::cout<<",\"errors\":"<<errors<<"}\n";return errors?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
