#pragma once
#include <windows.h>
#include <compressapi.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include "kernel/files.h"

// A finished recording is compressed in place with the Windows Compression
// API (Cabinet.dll, MSZIP - deflate in 32 KB blocks): the motion telemetry
// is JSON lines of the same keys ten times a second, and measured on a
// 17 MB race it goes 19x smaller in 0.2 s, back in 0.05 s. No library is
// shipped; every Windows since 8 has the API. The file keeps its name with
// .mszip appended, so race-*.jsonl.mszip is a recording exactly as the
// .jsonl was - the retention rule counts both, and tools/telemetry_read.py
// opens both. A recording is never compressed while it is being written.
namespace horse_telemetry {
inline constexpr const char* kCompressedSuffix=".mszip";
inline constexpr char kMagic[8]={'T','J','C','M','S','Z','1','\0'};
struct Header {char magic[8];std::uint64_t size;}; // Little-endian, 16 bytes, then the API's buffer.

inline bool CompressBuffer(const std::string& in,std::string& out) {
    COMPRESSOR_HANDLE handle=nullptr;
    if(!CreateCompressor(COMPRESS_ALGORITHM_MSZIP,nullptr,&handle))return false;
    SIZE_T need=0;Compress(handle,in.data(),in.size(),nullptr,0,&need);
    std::string buffer(need,'\0');SIZE_T got=0;
    const bool ok=need>0 && Compress(handle,in.data(),in.size(),buffer.data(),buffer.size(),&got);
    CloseCompressor(handle);
    if(!ok)return false;
    buffer.resize(got);
    Header header{};std::memcpy(header.magic,kMagic,sizeof header.magic);header.size=in.size();
    out.assign(reinterpret_cast<const char*>(&header),sizeof header);out+=buffer;
    return true;
}
inline bool DecompressBuffer(const std::string& in,std::string& out) {
    if(in.size()<sizeof(Header))return false;
    Header header{};std::memcpy(&header,in.data(),sizeof header);
    if(std::memcmp(header.magic,kMagic,sizeof kMagic)!=0 || header.size>(1ull<<32))return false;
    DECOMPRESSOR_HANDLE handle=nullptr;
    if(!CreateDecompressor(COMPRESS_ALGORITHM_MSZIP,nullptr,&handle))return false;
    out.assign(static_cast<std::size_t>(header.size),'\0');SIZE_T got=0;
    const bool ok=Decompress(handle,in.data()+sizeof header,in.size()-sizeof header,out.data(),out.size(),&got) && got==header.size;
    CloseDecompressor(handle);
    if(!ok)out.clear();
    return ok;
}
inline bool ReadFile(const std::filesystem::path& path,std::string& bytes) {
    std::ifstream in(path,std::ios::binary);if(!in)return false;
    bytes.assign((std::istreambuf_iterator<char>(in)),{});return true;
}
// Source -> source.mszip, then remove the exact source handle. Existing
// destinations/interrupted stages are preserved for explicit review. A
// failed final removal may leave two complete copies, never lose the source.
inline bool CompressFile(const std::filesystem::path& source,std::filesystem::path* result=nullptr) {
    if(!horse_races::files::RegularFile(source))return false;
    auto target=source;target+=kCompressedSuffix;
    auto partial=target;partial+=".part";
    std::error_code ec;
    if(std::filesystem::exists(target,ec)||ec||std::filesystem::exists(partial,ec)||ec)return false;
    const HANDLE input=CreateFileW(source.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(input==INVALID_HANDLE_VALUE)return false;
    struct Close {HANDLE handle;~Close(){CloseHandle(handle);}} close{input};
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    constexpr std::uint64_t capacity=256ull*1024*1024;
    if(!GetFileInformationByHandle(input,&info)||info.nNumberOfLinks!=1||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||
        !GetFileSizeEx(input,&size)||size.QuadPart<=0||static_cast<std::uint64_t>(size.QuadPart)>capacity)return false;
    std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0'),packed;DWORD got=0;
    if(!::ReadFile(input,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)||got!=bytes.size()||
        !CompressBuffer(bytes,packed)||packed.size()>=bytes.size())return false;
    // Peak includes both complete source and compressed stage. Recorder
    // admission waits while this one worker owns its source.
    if(!horse_races::files::AdmitGenerated(source.parent_path(),[](const std::string& name){
        return name.starts_with("race-")&&(name.ends_with(".jsonl")||name.ends_with(".jsonl.mszip")||
            name.ends_with(".jsonl.mszip.part"));},1,packed.size(),26,capacity))return false;
    if(!horse_races::files::WriteNew(partial,packed))return false;
    if(!MoveFileExW(partial.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))return false;
    FILE_DISPOSITION_INFO disposition{};disposition.DeleteFile=TRUE;
    if(!SetFileInformationByHandle(input,FileDispositionInfo,&disposition,sizeof disposition))return false;
    if(result)*result=target;
    return true;
}
// A recording's text, whichever form it is in.
inline bool ReadRecording(const std::filesystem::path& path,std::string& text) {
    std::string bytes;
    if(!ReadFile(path,bytes))return false;
    if(path.extension()==kCompressedSuffix)return DecompressBuffer(bytes,text);
    text=std::move(bytes);return true;
}
} // namespace horse_telemetry
