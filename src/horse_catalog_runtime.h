#pragma once
#include "horse_catalog.h"
#include "game_natives.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <chrono>
#include <windows.h>

namespace horse_catalog::runtime {
inline std::string root;
inline bool initialized=false;
inline bool provider_applied=false;
inline std::chrono::steady_clock::time_point next_provider_poll{};
inline std::string ReadFile(const std::filesystem::path& path,std::uintmax_t limit) {
    std::error_code error;const auto size=std::filesystem::file_size(path,error);
    if(error || size>limit)return {};
    std::ifstream input(path,std::ios::binary);
    return input?std::string(std::istreambuf_iterator<char>{input},{}):std::string{};
}
// Resolve only a provider that VFS says was actually loaded in THIS process.
// Merely installing/enabling SUHM is not evidence its replacement won. A stale
// previous-run log or an escaping path never enables an identity adapter.
inline std::filesystem::path EffectiveCompendium(const std::filesystem::path& directory) {
    const auto log=directory/"vfs.log";
    WIN32_FILE_ATTRIBUTE_DATA attributes{};FILETIME created{},exited{},kernel{},user{};
    if(!GetFileAttributesExW(log.c_str(),GetFileExInfoStandard,&attributes) ||
        !GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user) ||
        CompareFileTime(&attributes.ftLastWriteTime,&created)<0)return {};
    const auto bytes=ReadFile(log,16*1024*1024);
    const auto lower=[](std::string value){for(char& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;};
    const auto normalized=lower(bytes);
    std::string selected;std::size_t at=0;
    constexpr std::string_view prefix="global full replace: addondlc:/";
    while((at=normalized.find(prefix,at))!=std::string::npos) {
        at+=prefix.size();const auto end=normalized.find_first_of("\r\n",at);
        const auto path=bytes.substr(at,end==std::string::npos?std::string::npos:end-at);
        if(!lower(path).ends_with("/compendium.meta"))continue;
        const auto previous=bytes.rfind('\n',at);
        const auto line=bytes.substr(previous==std::string::npos?0:previous+1,19);
        unsigned y=0,m=0,d=0,h=0,minute=0,s=0;
        if(std::sscanf(line.c_str(),"%u-%u-%u %u:%u:%u",&y,&m,&d,&h,&minute,&s)!=6)continue;
        const SYSTEMTIME local{static_cast<WORD>(y),static_cast<WORD>(m),0,static_cast<WORD>(d),
            static_cast<WORD>(h),static_cast<WORD>(minute),static_cast<WORD>(s),0};
        SYSTEMTIME utc{};FILETIME stamp{};
        if(TzSpecificLocalTimeToSystemTime(nullptr,&local,&utc) && SystemTimeToFileTime(&utc,&stamp) &&
            CompareFileTime(&stamp,&created)>=0)selected=path;
    }
    if(selected.empty())return {};
    const auto relative=std::filesystem::path(selected);
    if(relative.is_absolute() || relative.has_root_name())return {};
    for(const auto& part:relative)if(part==".." || part==".")return {};
    return directory/"lml"/relative;
}
template<class Log> bool Initialize(const std::string& directory,Log log) {
    bool changed=false;
    if(!initialized || root!=directory) {
        root=directory;initialized=true;provider_applied=false;next_provider_poll={};
        current.definitions.clear();horse_identity::effective.clear();current.Seed();changed=true;
    }
    const auto now=std::chrono::steady_clock::now();
    if(provider_applied || now<next_provider_poll)return changed;
    next_provider_poll=now+std::chrono::seconds(5);
    const auto provider=EffectiveCompendium(directory);
    if(!provider.empty()) {
        const auto text=ReadFile(provider,8*1024*1024);
        const auto entries=CompendiumBreeds(text).size();
        if(!entries)return changed;
        current.ApplyCompendium(text);provider_applied=true;changed=true;
        log("horse environment effective_compendium="+provider.string()+" entries="+
            std::to_string(entries));
    }else if(changed)log("horse environment: no current-session compendium replacement yet; stock identities only");
    return changed;
}
template<class Log> std::vector<fixed_horses::Choice> Read(const std::string& directory,Log log) {
    Initialize(directory,log);
    auto choices=current.Read([](const char* name){return MISC::GET_HASH_KEY(name);},
        [](Hash model){return STREAMING::IS_MODEL_IN_CDIMAGE(model) && STREAMING::IS_MODEL_VALID(model) && STREAMING::IS_MODEL_A_PED(model);},
        [](Hash model){return horse_attributes::ReadModelDefaults(model);},HUD::DOES_TEXT_LABEL_EXIST);
    std::string audit="horse environment metadata_ready="+std::to_string(choices.size())+
        "; streaming/instance still verified before entry; stable persistence not certified";
    for(const auto& c:choices) {
        const auto* labels=current.Labels(c.name);
        audit+="\n  "+std::string(c.name)+" coat_key="+horse_identity::Key(c.name)+
            " raw="+horse_attributes::Pair(c.raw)+" breed_label="+(labels?labels->breed:"")+
            " coat_label="+(labels?labels->coat:"");
    }
    log(audit);return choices;
}
}
