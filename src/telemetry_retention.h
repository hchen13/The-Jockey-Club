#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include "kernel/files.h"

// Telemetry that cannot be deleted is a liability, not evidence. The motion
// directory reached 71 recordings and 369 MB before anything bounded it, and
// the log 9.5 MB. Both bounds live here, beside nothing else, so the recorder
// and the two loggers apply the same rule: prune the recordings oldest-first
// whenever a new one is opened, and move the log aside once it outgrows a limit.
namespace horse_telemetry {
struct Retention {
    unsigned keep_files=24;                   // Newest kept, the open one included. 0: no count limit. (Compressed, a race is ~1 MB.)
    std::uint64_t keep_bytes=256ull*1024*1024; // Directory budget, the open file at its current size. 0: no size limit.
};
struct Recording {
    std::filesystem::path path;
    std::uint64_t bytes=0;
    std::uint64_t written=0;
    DWORD volume=0,index_high=0,index_low=0;
};
inline std::string Megabytes(std::uint64_t bytes) {
    char text[32];std::snprintf(text,sizeof text,"%.1f MB",static_cast<double>(bytes)/(1024.0*1024.0));
    return text;
}
struct PruneResult {
    std::vector<std::string> deleted,failed;
    std::uint64_t freed=0,bytes_before=0,bytes_after=0;
    unsigned files_before=0,files_after=0,reserved_files=0;
    std::uint64_t reserved_bytes=0;
    bool over_count=false,over_size=false;
    bool Any() const {return !deleted.empty() || !failed.empty();}
    // One line: what went, and which bound it went for. A deletion that
    // cannot say why it happened is indistinguishable from a bug.
    std::string Describe(const Retention& policy) const {
        std::string why;
        if(over_count)why+=std::to_string(files_before)+(reserved_files?" files + "+std::to_string(reserved_files)+" reserved":" files")+" > "+std::to_string(policy.keep_files)+" kept";
        if(over_size)why+=std::string(why.empty()?"":", ")+Megabytes(bytes_before)+(reserved_bytes?" + "+Megabytes(reserved_bytes)+" reserved":"")+" > "+Megabytes(policy.keep_bytes);
        std::string line="motion recordings pruned ("+why+"): deleted "+std::to_string(deleted.size())+
            (deleted.size()==1?" file":" files")+" freeing "+Megabytes(freed);
        for(std::size_t i=0;i<deleted.size();++i)line+=(i?", ":": ")+deleted[i];
        if(!failed.empty()) {
            line+="; could not delete";
            for(std::size_t i=0;i<failed.size();++i)line+=(i?", ":" ")+failed[i];
        }
        return line+"; now "+std::to_string(files_after)+(files_after==1?" file":" files")+", "+Megabytes(bytes_after);
    }
};
// Only race-*.jsonl and race-*.jsonl.mszip regular files are recordings (the
// latter a finished one compressed, telemetry_compress.h). Anything renamed
// is kept: that is the way to hold on to the one race worth studying.
inline bool RecordingName(const std::string& name) {
    if(!name.starts_with("race-"))return false;
    const std::string_view suffix=name.ends_with(".jsonl.mszip")?".jsonl.mszip":".jsonl";
    if(!name.ends_with(suffix))return false;
    const auto parts=std::string_view(name).substr(5,name.size()-5-suffix.size());
    unsigned separators=0;bool digit=false;
    for(char c:parts) {
        if(c>='0' && c<='9'){digit=true;continue;}
        if(c!='-' || !digit || ++separators>2)return false;
        digit=false;
    }
    return digit && separators==2;
}
// Compare the actual opened file with the intended absolute path. Opening the
// leaf itself as a reparse point and comparing the final path also reject a
// junction in any parent component. Unverifiable files are always left alone.
inline bool ValidRecordingHandle(HANDLE file,const std::filesystem::path& path,BY_HANDLE_FILE_INFORMATION& info) {
    if(!GetFileInformationByHandle(file,&info) ||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) || info.nNumberOfLinks!=1)return false;
    wchar_t final_name[32768]{};
    const DWORD n=GetFinalPathNameByHandleW(file,final_name,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!n || n>=32768)return false;
    std::wstring actual(final_name,n);
    if(actual.starts_with(L"\\\\?\\UNC\\"))actual=L"\\\\"+actual.substr(8);
    else if(actual.starts_with(L"\\\\?\\"))actual.erase(0,4);
    std::error_code ec;
    const auto expected=std::filesystem::absolute(path,ec).lexically_normal().wstring();
    if(ec || _wcsicmp(actual.c_str(),expected.c_str())!=0)return false;
    char prefix[64]{};DWORD got=0;
    if(!::ReadFile(file,prefix,sizeof prefix,&got,nullptr))return false;
    constexpr std::string_view schema="{\"schema\":\"HorseSpeedLab.motion.v2\"";
    constexpr char magic[8]={'T','J','C','M','S','Z','1','\0'};
    if(path.extension()==".mszip")return got>=sizeof magic && std::memcmp(prefix,magic,sizeof magic)==0;
    return got>=schema.size() && std::string_view(prefix,schema.size())==schema;
}
inline bool IsRecording(const std::filesystem::directory_entry& entry) {
    if(!RecordingName(entry.path().filename().string()))return false;
    const HANDLE file=CreateFileW(entry.path().c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};const bool valid=ValidRecordingHandle(file,entry.path(),info);
    CloseHandle(file);return valid;
}
// Oldest first by modification time, ties by name, so a prune is deterministic.
inline std::vector<Recording> ListRecordings(const std::filesystem::path& directory) {
    std::vector<Recording> files;
    std::error_code ec;
    for(const auto& entry:std::filesystem::directory_iterator(directory,ec)) {
        if(!RecordingName(entry.path().filename().string()))continue;
        Recording r;r.path=entry.path();
        const HANDLE file=CreateFileW(r.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE)continue;
        BY_HANDLE_FILE_INFORMATION info{};const bool valid=ValidRecordingHandle(file,r.path,info);
        CloseHandle(file);if(!valid)continue;
        r.volume=info.dwVolumeSerialNumber;r.index_high=info.nFileIndexHigh;r.index_low=info.nFileIndexLow;
        r.bytes=(static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
        r.written=(static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime;
        files.push_back(std::move(r));
    }
    std::sort(files.begin(),files.end(),[](const Recording& a,const Recording& b){
        if(a.written!=b.written)return a.written<b.written;
        return a.path.filename()<b.path.filename();
    });
    return files;
}
// Admission is stricter than deletion: an unreadable/malformed owned name or
// interrupted compression cannot be mistaken for free capacity.
inline horse_races::files::DirectoryUsage RecordingUsage(const std::filesystem::path& directory) {
    horse_races::files::DirectoryUsage result;
    if(!horse_races::files::UnredirectedDirectory(directory))return result;
    std::error_code error;std::filesystem::directory_iterator it(directory,error),end;if(error)return result;
    for(;it!=end;it.increment(error)) {
        if(error)return result;
        const auto name=it->path().filename().string();
        if(name.ends_with(".part")&&RecordingName(name.substr(0,name.size()-5)))return result;
        if(!RecordingName(name))continue;
        if(!IsRecording(*it))return result;
        const auto bytes=it->file_size(error);if(error||bytes>UINT64_MAX-result.bytes)return result;
        ++result.files;result.bytes+=bytes;
    }
    result.valid=!error;return result;
}
// Delete oldest-first until the directory holds at most keep_files recordings
// and at most keep_bytes of them. `open` (a filename in that directory) is the
// recording being written and is never a candidate, whatever its timestamp.
inline PruneResult Prune(const std::filesystem::path& directory,const Retention& policy,
    const std::filesystem::path& open={},unsigned reserve_files=0,std::uint64_t reserve_bytes=0) {
    PruneResult result;
    result.reserved_files=reserve_files;result.reserved_bytes=reserve_bytes;
    const auto files=ListRecordings(directory);
    result.files_before=static_cast<unsigned>(files.size());
    for(const auto& f:files)result.bytes_before+=f.bytes;
    unsigned count=result.files_before;
    std::uint64_t bytes=result.bytes_before;
    const auto over=[&]{
        return (policy.keep_files && (count>policy.keep_files||reserve_files>policy.keep_files-count)) ||
            (policy.keep_bytes && (bytes>policy.keep_bytes||reserve_bytes>policy.keep_bytes-bytes));
    };
    result.over_count=policy.keep_files && (count>policy.keep_files||reserve_files>policy.keep_files-count);
    result.over_size=policy.keep_bytes && (bytes>policy.keep_bytes||reserve_bytes>policy.keep_bytes-bytes);
    for(const auto& f:files) {
        if(!over())break;
        const auto name=f.path.filename().string();
        if(!open.empty() && f.path.filename()==open.filename())continue;
        const HANDLE file=CreateFileW(f.path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        BY_HANDLE_FILE_INFORMATION info{};
        const bool valid=file!=INVALID_HANDLE_VALUE && ValidRecordingHandle(file,f.path,info) &&
            info.dwVolumeSerialNumber==f.volume && info.nFileIndexHigh==f.index_high && info.nFileIndexLow==f.index_low &&
            ((static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow)==f.bytes &&
            ((static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime)==f.written;
        FILE_DISPOSITION_INFO disposition{TRUE};
        const bool removed=valid && SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof disposition);
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
        if(removed) {
            result.deleted.push_back(name);result.freed+=f.bytes;--count;bytes-=f.bytes;
        } else result.failed.push_back(name);
    }
    result.files_after=count;result.bytes_after=bytes;
    return result;
}
} // namespace horse_telemetry
