#pragma once
#include "kernel/files.h"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <cwctype>

namespace horse_races::obstacle_map {struct Map;}
namespace horse_races::obstacle_storage {
// One immutable initial copy and one replaceable recovery generation per
// course. Pre-policy content-named backups remain untouched for explicit
// review; new sessions no longer create another historical filename.
inline constexpr std::uint64_t kMaximumEvidenceBytes=64ull*1024*1024;
inline bool EvidenceCapacity(const std::filesystem::path& target,std::size_t bytes,bool backup=false) {
    auto temporary=target;temporary+=".tmp";
    std::error_code error;
    if(std::filesystem::exists(temporary,error)||error)return false;
    // Reserve the complete new stage before replacing anything. Legacy
    // content-addressed backups are sealed history, never an expanding input
    // to this fixed recovery family or a reason to reject new observations.
    return files::AdmitGenerated(target.parent_path(),[backup](const std::string& name){
        return backup?(name.ends_with(".initial.bak")||name.ends_with(".recovery.bak")||
            name.ends_with(".initial.bak.tmp")||name.ends_with(".recovery.bak.tmp")):
            (name.ends_with(".txt")||name.ends_with(".txt.tmp"));},1,bytes,
        backup?2048:1024,backup?1024ull*1024*1024:512ull*1024*1024);
}
inline bool SaveBytes(const std::filesystem::path& target,const std::string& bytes,bool& backup_pending) {
    if(bytes.size()>kMaximumEvidenceBytes)return false;
    std::error_code error;
    if(!files::CanCreateDirectory(target.parent_path()))return false;
    std::filesystem::create_directories(target.parent_path(),error);
    if(error||!files::UnredirectedDirectory(target.parent_path()))return false;
    const bool target_exists=std::filesystem::exists(target,error);
    if(error||(target_exists&&!files::RegularFile(target)))return false;
    if(!EvidenceCapacity(target,bytes.size()))return false;
    if(backup_pending) {
        const bool exists=std::filesystem::exists(target,error);
        if(error)return false;
        if(exists) {
            const auto original_size=std::filesystem::file_size(target,error);
            if(error||original_size>kMaximumEvidenceBytes)return false;
            std::ifstream input(target,std::ios::binary);
            if(!input)return false;
            const std::string original(std::istreambuf_iterator<char>{input},{});
            if(input.bad())return false;
            const auto folder=target.parent_path()/"backups";
            if(!files::CanCreateDirectory(folder))return false;
            std::filesystem::create_directories(folder,error);
            if(error||!files::UnredirectedDirectory(folder))return false;
            const auto initial=folder/(target.filename().string()+".initial.bak");
            const bool saved=std::filesystem::exists(initial,error);
            if(error)return false;
            if(saved) {
                if(!files::RegularFile(initial)||std::filesystem::file_size(initial,error)>kMaximumEvidenceBytes||error)return false;
                std::ifstream check(initial,std::ios::binary);
                if(!check)return false;
                const std::string kept(std::istreambuf_iterator<char>{check},{});
                if(check.bad())return false;
                if(kept!=original) {
                    const auto recovery=folder/(target.filename().string()+".recovery.bak");
                    if(std::filesystem::exists(recovery,error)&&(!files::RegularFile(recovery)||error))return false;
                    if(error||!EvidenceCapacity(recovery,original.size(),true)||!files::ReplaceAtomically(recovery,original))return false;
                }
            } else if(!EvidenceCapacity(initial,original.size(),true)||!files::ReplaceAtomically(initial,original))return false;
        }
    }
    if(!files::ReplaceAtomically(target,bytes))return false;
    backup_pending=false;
    return true;
}
// An open evidence handle must permit the writer's atomic rename. A read
// then sees one complete old or new file, never a partly replaced payload.
inline bool Read(const std::filesystem::path& file,std::string& bytes,std::uint32_t* failure=nullptr) {
    if(failure)*failure=0;
#ifdef _WIN32
    const HANDLE handle=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE){if(failure)*failure=GetLastError();return false;}
    LARGE_INTEGER size{};bool okay=GetFileSizeEx(handle,&size)&&size.QuadPart>=0&&
        static_cast<unsigned long long>(size.QuadPart)<=bytes.max_size();
    if(!okay&&failure)*failure=GetLastError();
    if(okay) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));std::size_t position=0;
        while(okay&&position<bytes.size()) {
            const auto count=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-position,MAXDWORD));
            DWORD got=0;okay=ReadFile(handle,bytes.data()+position,count,&got,nullptr)&&got>0;
            if(!okay&&failure)*failure=GetLastError();
            position+=got;
        }
    }
    const bool closed=CloseHandle(handle)!=0;if(!closed&&failure)*failure=GetLastError();return okay&&closed;
#else
    std::ifstream input(file,std::ios::binary);if(!input)return false;
    bytes.assign(std::istreambuf_iterator<char>{input},{});return !input.bad();
#endif
}
struct Snapshot {
    std::string key,why;
    std::uint64_t revision=0,epoch=0;
    std::size_t rows=0;
    // Both callbacks own immutable evidence. Neither can touch game natives.
    std::function<std::string()> serialize;
    std::function<bool(obstacle_map::Map&)> restore;
    std::shared_ptr<const std::string> serialized;
};
struct Completion {
    std::string key,why;
    std::uint64_t revision=0,sequence=0,epoch=0;
    std::size_t rows=0,bytes=0;
    bool saved=false;
    double serialize_ms=0,write_ms=0;
    std::uint32_t error=0;
};
namespace detail {
struct Entry {
    std::uint64_t epoch=0,sequence=0,committed_epoch=0,committed_revision=0;
    std::shared_ptr<const Snapshot> latest;
    bool pending=false,backup=false,failed=false;
};
struct State {
    std::mutex mutex,io;
    std::condition_variable idle;
    std::map<std::filesystem::path,Entry> entries;
    std::vector<Completion> completions;
    std::uint64_t serial=0;
    bool running=false;
};
// No static thread/mutex destruction or loader-lock joins. A worker exits as
// soon as its queue drains; failed evidence stays available for a later owner.
inline State& Shared(){static auto* state=new State;return *state;}
inline std::filesystem::path Target(const std::filesystem::path& path) {
    auto normalized=std::filesystem::absolute(path).lexically_normal().make_preferred();
#ifdef _WIN32
    auto name=normalized.native();std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    return std::filesystem::path(std::move(name));
#else
    return normalized;
#endif
}
inline void Run() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
    auto& state=Shared();
    for(;;) {
        std::filesystem::path target;std::shared_ptr<const Snapshot> snapshot;
        std::uint64_t sequence=0;bool backup=false;
        {
            std::lock_guard lock(state.mutex);
            auto next=state.entries.end();
            for(auto it=state.entries.begin();it!=state.entries.end();++it)
                if(it->second.pending&&(next==state.entries.end()||it->second.sequence<next->second.sequence))next=it;
            if(next==state.entries.end()){state.running=false;state.idle.notify_all();return;}
            target=next->first;auto& entry=next->second;
            entry.pending=false;snapshot=entry.latest;sequence=entry.sequence;backup=entry.backup;
        }
        Completion result{snapshot->key,snapshot->why,snapshot->revision,sequence,snapshot->epoch,snapshot->rows};
        try {
            const auto start=std::chrono::steady_clock::now();
            const auto bytes=snapshot->serialize();result.bytes=bytes.size();
            result.serialize_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            // One committer also fences explicit synchronous tools/tests.
            // Superseded pending jobs never reach disk; an in-flight commit
            // finishes before the next generation's commit can begin.
            std::lock_guard io(state.io);
            bool current=false;
            {std::lock_guard lock(state.mutex);current=state.entries[target].sequence==sequence;}
            if(!current)continue;
            const auto write_start=std::chrono::steady_clock::now();
            result.saved=SaveBytes(target,bytes,backup);
#ifdef _WIN32
            if(!result.saved)result.error=GetLastError();
#endif
            result.write_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-write_start).count();
        } catch(...) {result.saved=false;}
        {
            std::lock_guard lock(state.mutex);auto& entry=state.entries[target];
            if(entry.sequence==sequence) {
                entry.backup=backup;entry.failed=!result.saved;
                if(result.saved){entry.committed_epoch=snapshot->epoch;entry.committed_revision=snapshot->revision;entry.latest.reset();}
            }
            // Diagnostics are bounded independently of retained observations.
            if(state.completions.size()==64)state.completions.erase(state.completions.begin());
            state.completions.push_back(std::move(result));
        }
    }
}
inline bool StartLocked(State& state) {
    if(state.running)return true;
#ifdef _WIN32
    HMODULE own{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Run),&own))return false;
#endif
    try {state.running=true;std::thread(Run).detach();return true;}
    catch(...) {state.running=false;return false;}
}
}
struct LoadView {
    std::uint64_t epoch=0;
    std::shared_ptr<const Snapshot> pending;
    bool backup_pending=false;
};
// A new binding inherits pending observations and revokes older publishers.
// No disk wait is needed while another generation is being serialized/saved.
inline LoadView BeginLoad(const std::filesystem::path& target) {
    auto& state=detail::Shared();std::lock_guard lock(state.mutex);
    auto& entry=state.entries[detail::Target(target)];entry.epoch=++state.serial;
    return {entry.epoch,entry.latest,entry.backup};
}
inline bool Owns(const std::filesystem::path& target,std::uint64_t epoch) {
    auto& state=detail::Shared();std::lock_guard lock(state.mutex);
    const auto found=state.entries.find(detail::Target(target));
    return epoch&&found!=state.entries.end()&&found->second.epoch==epoch;
}
inline bool Submit(const std::filesystem::path& target,std::shared_ptr<const Snapshot> snapshot,bool backup_pending) {
    if(!snapshot||!snapshot->serialize||!snapshot->restore)return false;
    auto& state=detail::Shared();std::lock_guard lock(state.mutex);
    auto& entry=state.entries[detail::Target(target)];
    if(!snapshot->epoch||snapshot->epoch!=entry.epoch)return false;
    // Low-frequency coalescing may release a displaced snapshot on this
    // caller if the worker no longer holds it. This is not a constant-time
    // promise; the offline snapshot benchmark includes the full-map release.
    entry.sequence=++state.serial;entry.latest=std::move(snapshot);entry.pending=true;
    entry.backup|=backup_pending;entry.failed=false;
    // Acceptance transfers ownership even if the worker cannot start yet.
    // Retry or the next binding can restart it; BeginLoad already sees latest.
    detail::StartLocked(state);return true;
}
inline bool Retry(const std::filesystem::path& target) {
    auto& state=detail::Shared();std::lock_guard lock(state.mutex);
    auto found=state.entries.find(detail::Target(target));
    if(found==state.entries.end()||!found->second.latest)return true;
    if(found->second.failed)found->second.pending=true;
    return detail::StartLocked(state);
}
inline std::vector<Completion> DrainCompletions() {
    auto& state=detail::Shared();std::vector<Completion> result;
    {std::lock_guard lock(state.mutex);result.swap(state.completions);}return result;
}
inline bool Committed(const std::filesystem::path& target,std::uint64_t epoch,std::uint64_t revision) {
    auto& state=detail::Shared();std::lock_guard lock(state.mutex);
    const auto found=state.entries.find(detail::Target(target));
    return found!=state.entries.end()&&found->second.committed_epoch==epoch&&found->second.committed_revision==revision;
}
// Offline shutdown/tests only. Runtime race ticks never join or wait for I/O.
inline bool Flush(std::chrono::milliseconds timeout=std::chrono::seconds(10)) {
    auto& state=detail::Shared();std::unique_lock lock(state.mutex);
    if(!state.idle.wait_for(lock,timeout,[&]{return !state.running;}))return false;
    for(const auto& item:state.entries)if(item.second.latest)return false;
    return true;
}
inline bool Save(const std::filesystem::path& target,const std::string& bytes,bool& backup_pending) {
    auto& state=detail::Shared();std::lock_guard io(state.io);
    const auto key=detail::Target(target);std::uint64_t sequence=0;
    auto pending=std::make_shared<Snapshot>();pending->serialized=std::make_shared<const std::string>(bytes);
    pending->serialize=[value=pending->serialized]{return *value;};
    {std::lock_guard lock(state.mutex);auto& entry=state.entries[key];
     entry.epoch=entry.sequence=sequence=++state.serial;entry.latest=pending;
     entry.pending=entry.failed=false;entry.backup=backup_pending;entry.committed_epoch=entry.committed_revision=0;}
    const bool saved=SaveBytes(target,bytes,backup_pending);
    {std::lock_guard lock(state.mutex);auto& entry=state.entries[key];
     if(entry.sequence==sequence){entry.backup=backup_pending;entry.failed=!saved;if(saved)entry.latest.reset();}}
    return saved;
}
} // namespace horse_races::obstacle_storage
