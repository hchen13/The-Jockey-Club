// Explicit offline migration only. Never launched by the ASI or at runtime.
#include "../src/horse_race_ledger_store.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <stdexcept>

using namespace horse_races::horse_ledger;
int wmain(int argc,wchar_t** argv){
    try {
        std::filesystem::path root,profile;
        for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if((arg==L"--store" || arg==L"--profile") && i+1<argc){auto value=std::filesystem::path(argv[++i]);if(arg==L"--store"){if(!root.empty())throw std::runtime_error("duplicate --store");root=value;}else {if(!profile.empty())throw std::runtime_error("duplicate --profile");profile=value;}}else throw std::runtime_error("usage: horse_ledger_seed --store PATH --profile PROFILE_DIRECTORY");}
        if(root.empty() || profile.empty())throw std::runtime_error("usage: horse_ledger_seed --store PATH --profile PROFILE_DIRECTORY");
        profile=profile.lexically_normal();if(profile.filename().empty())profile=profile.parent_path();const auto name=profile.filename().string();
        if(name.size()!=8 || !std::all_of(name.begin(),name.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}))throw std::runtime_error("profile directory must have an eight-hex-digit name");
        const auto attributes=GetFileAttributesW(profile.c_str());if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("profile must be an existing non-reparse directory");
        Store store(root);unsigned seeded=0,skipped=0,failed=0,found=0;std::cout<<"profile="<<name<<"\n";
        for(int slot=0;slot<16;++slot){char filename[16]{};std::snprintf(filename,sizeof(filename),"SRDR3%04d",slot);const auto path=profile/filename;if(!std::filesystem::exists(path))continue;++found;std::string error;auto hash=HashStableFile(path,error);if(!hash){std::cerr<<filename<<": "<<error<<"\n";++failed;continue;}
            Revision revision{name,hash->sha256};if(store.Load(revision,error)){++skipped;std::cout<<filename<<" preserved "<<hash->sha256<<"\n";continue;}
            Snapshot baseline;baseline.revision=revision;baseline.evidence=Evidence::EmptyBaseline;const auto result=store.Publish(baseline,error);
            if(result==WriteResult::Written){++seeded;std::cout<<filename<<" seeded "<<hash->sha256<<"\n";}
            else if(result==WriteResult::AlreadyPresent){++skipped;std::cout<<filename<<" preserved "<<hash->sha256<<"\n";}
            else {++failed;std::cerr<<filename<<": baseline refused: "<<error<<"\n";}
        }
        std::cout<<"found="<<found<<" seeded="<<seeded<<" preserved="<<skipped<<" errors="<<failed<<"\n";return failed?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
