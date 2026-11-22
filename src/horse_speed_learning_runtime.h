#pragma once
#include "horse_speed_learning_store.h"
#include "kernel/files.h"
#include "runtime_paths.h"
#include <chrono>
#include <functional>

namespace horse_motion::speed_learning::runtime {
inline Store store;
inline std::filesystem::path path;
inline std::function<void(const std::string&)> logger;
inline bool initialized=false,write_blocked=false;
inline std::uint64_t saved_revision=0;
inline std::chrono::steady_clock::time_point next_save{};
inline void Log(const std::string& text){if(logger)logger(text);}
inline bool SaveEncoded(const std::string& bytes) {
    if(write_blocked)return false;
    if(bytes.size()>Store::kMaxBytes) {
        // The previous on-disk history stays intact; in-memory learning continues.
        // Block repeated attempts/log spam until the next initialization.
        Log("speed learning profile exceeds 1 MiB; previous file retained, disk writes blocked");
        write_blocked=true;return false;
    }
    return horse_races::runtime_paths::EnsureDirectory(path.parent_path()) &&
        horse_races::files::ReplaceAtomically(path,bytes);
}
inline bool Flush() {
    if(!initialized || write_blocked)return false;
    if(saved_revision==store.Revision())return true;
    const bool saved=SaveEncoded(store.Encode());
    if(write_blocked)return false;
    if(saved)saved_revision=store.Revision();
    Log(std::string("speed learning profiles ")+(saved?"saved":"save pending")+
        " entries="+std::to_string(store.Size())+" path="+path.string());
    return saved;
}
inline void Tick() {
    const auto now=std::chrono::steady_clock::now();
    if(now<next_save)return;
    next_save=now+std::chrono::seconds(15);Flush();
}
inline void Initialize(const std::string& directory,std::function<void(const std::string&)> log) {
    // ScriptHook re-enters on save loads while globals may survive.
    // Provisional measurements are never imported across save boundaries.
    store.ClearProvisional();
    // v4 adds material identity. Legacy files remain untouched and their rows
    // retain material=0 so they are never mistaken for measured material data.
    const auto paths=horse_races::runtime_paths::Resolve(directory);
    const auto next=paths.Learning("TheJockeyClub-speed-learning-v4.dat");
    logger=std::move(log);
    // ScriptHook can initialize again on save load without unloading globals.
    if(initialized && next==path){Flush();return;}
    if(initialized)Flush();
    path=next;store={};saved_revision=0;write_blocked=false;initialized=true;
    next_save=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    std::error_code error;
    const bool existing=std::filesystem::exists(path,error);
    if(error){write_blocked=true;Log("speed learning profile status unreadable; disk writes blocked");return;}
    auto source=existing?path:paths.Learning("TheJockeyClub-speed-learning-v3.dat");
    bool v2=false;
    if(!existing && !std::filesystem::exists(source,error) && !error){
        source=paths.Learning("TheJockeyClub-speed-learning-v2.dat");v2=true;
    }
    if(!existing && !std::filesystem::exists(source,error) && !error){
        saved_revision=std::numeric_limits<std::uint64_t>::max();
        Log("speed learning: new shared persistent profile store");Flush();return;
    }
    const auto size=std::filesystem::file_size(source,error);
    if(!error && size<=Store::kMaxBytes) {
        std::ifstream input(source,std::ios::binary);
        std::string bytes(static_cast<std::size_t>(size),'\0');
        if(input && input.read(bytes.data(),static_cast<std::streamsize>(bytes.size())))
            if(auto decoded=v2?Store::DecodeV2(bytes):Store::Decode(bytes)) {
                store=std::move(*decoded);
                if(!existing){
                    saved_revision=std::numeric_limits<std::uint64_t>::max();
                    Log("speed learning legacy profiles imported as unclassified v4 fallback entries="+std::to_string(store.Size()));Flush();
                }else saved_revision=store.Revision();
                Log("speed learning profiles loaded entries="+std::to_string(store.Size()));return;
            }
    }
    // Never overwrite a malformed/unknown-version file with an empty history.
    write_blocked=true;Log("speed learning profile file invalid/unreadable; original retained, disk writes blocked");
}
} // namespace horse_motion::speed_learning::runtime
