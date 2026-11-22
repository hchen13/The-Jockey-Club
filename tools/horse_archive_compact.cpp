#include "../src/horse_race_ledger_store.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace horse_races::horse_ledger;
namespace {
std::string Q(const std::string& value){
    std::string out="\"";for(unsigned char c:value){
        if(c=='\\'||c=='\"'){out+='\\';out+=static_cast<char>(c);}
        else if(c<32){static const char* h="0123456789abcdef";out+="\\u00";out+=h[c>>4];out+=h[c&15];}
        else out+=static_cast<char>(c);
    }return out+'\"';
}
void Need(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}
std::string Utf8(const std::filesystem::path& path){auto u=path.generic_u8string();return {reinterpret_cast<const char*>(u.data()),u.size()};}
void OrdinaryAncestors(std::filesystem::path path,bool may_be_missing=false){
    path=std::filesystem::absolute(path).lexically_normal();
    while(!path.empty()){
        const auto attributes=GetFileAttributesW(path.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){const auto code=GetLastError();
            Need(may_be_missing&&(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND),"cannot inspect archive path");
        }else Need(!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"archive path contains a reparse point");
        const auto parent=path.parent_path();if(parent==path)break;path=parent;
    }
}
std::vector<std::uint8_t> ReadOwned(const std::filesystem::path& path){
    OrdinaryAncestors(path);
    struct File {HANDLE h;~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}} file{
        CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    Need(file.h!=INVALID_HANDLE_VALUE,"cannot lock archive output for verification");BY_HANDLE_FILE_INFORMATION info{};
    Need(GetFileInformationByHandle(file.h,&info)&&info.nNumberOfLinks==1&&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),"unsafe archive output file");
    const auto size=(std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;Need(size<=32ull*1024*1024,"archive file too large");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));std::size_t at=0;
    while(at<bytes.size()){DWORD got=0;const auto want=static_cast<DWORD>(std::min<std::size_t>(65536,bytes.size()-at));
        Need(ReadFile(file.h,bytes.data()+at,want,&got,nullptr)&&got==want,"incomplete archive output read");at+=got;}
    return bytes;
}
void Report(const Store& store,const std::vector<SlotVersion>& slots){
    std::cout<<"{\"schema\":1,\"status\":\"ok\",\"slots\":[";bool first=true;
    for(const auto& v:slots){
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"profile\":"<<Q(v.binding.archive.profile)<<",\"slot\":"<<v.binding.slot
            <<",\"archive_id\":"<<Q(v.binding.archive.sha256)<<",\"game_sha256\":"<<Q(v.binding.game_sha256)
            <<",\"generation\":"<<v.binding.generation<<",\"snapshot_sha256\":"<<Q(Store::SnapshotDigest(v.snapshot))<<'}';
    }
    std::cout<<"],\"files\":[";first=true;
    if(std::filesystem::exists(store.Root()))for(const auto& entry:std::filesystem::recursive_directory_iterator(store.Root())){
        const auto attributes=GetFileAttributesW(entry.path().c_str());
        Need(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"reparse entry rejected");
        if(attributes&FILE_ATTRIBUTE_DIRECTORY)continue;
        const auto bytes=ReadOwned(entry.path());
        if(!first)std::cout<<',';first=false;
        std::cout<<"{\"path\":"<<Q(Utf8(entry.path().lexically_relative(store.Root())))
            <<",\"size\":"<<bytes.size()<<",\"sha256\":"<<Q(Sha256(bytes))<<'}';
    }
    std::cout<<"]}\n";
}
}
int wmain(int argc,wchar_t** argv){
    try{
        Need(argc>=4,"usage: horse_archive_compact convert --source-store PATH --output-store PATH | verify --store PATH");
        const std::wstring mode=argv[1];std::filesystem::path source,target;
        for(int i=2;i<argc;i+=2){Need(i+1<argc,"missing option value");const std::wstring arg=argv[i];
            if(arg==L"--source-store"||arg==L"--store")source=argv[i+1];
            else if(arg==L"--output-store")target=argv[i+1];else throw std::runtime_error("unknown option");
        }
        Need(!source.empty()&&std::filesystem::is_directory(source),"source store missing");
        OrdinaryAncestors(source);
        Store from(source);std::string error;const auto slots=from.ExportSlots(error);Need(slots.has_value(),error);
        if(mode==L"convert"){
            Need(!target.empty(),"output store missing");
            OrdinaryAncestors(target,true);
            Need(!std::filesystem::exists(target)||std::filesystem::is_empty(target),"output must be empty");
            Store to(target);
            for(const auto& slot:*slots){
                Need(to.ImportSlot(slot,error)==WriteResult::Written,error);
                const auto copied=to.LookupSlot(slot.binding.archive.profile,slot.binding.slot);
                Need(copied.status==LookupStatus::Loaded&&copied.binding==slot.binding&&copied.snapshot==slot.snapshot,
                    "converted slot differs from original");
                Need(Store::SnapshotDigest(*copied.snapshot)==Store::SnapshotDigest(slot.snapshot),"converted snapshot checksum differs");
            }
            std::filesystem::create_directories(target);Report(to,*slots);
        }else {Need(mode==L"verify"&&target.empty(),"invalid mode/options");Report(from,*slots);}
        return 0;
    }catch(const std::exception& e){std::cerr<<"{\"schema\":1,\"status\":\"error\",\"error\":"<<Q(e.what())<<"}\n";return 1;}
}
