#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <cstddef>
#include <cstring>
#include <vector>
#include <cstdint>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#endif

namespace horse_races::files {
inline bool CanCreateDirectory(const std::filesystem::path& path) {
    std::error_code error;auto current=std::filesystem::absolute(path,error).lexically_normal();
    if(error)return false;
    for(;;) {
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES) {
            const auto failure=GetLastError();
            if(failure!=ERROR_FILE_NOT_FOUND&&failure!=ERROR_PATH_NOT_FOUND)return false;
        } else if((attributes&FILE_ATTRIBUTE_REPARSE_POINT)||!(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
#else
        const auto status=std::filesystem::symlink_status(current,error);
        if(error&&error!=std::errc::no_such_file_or_directory)return false;
        if(!error&&std::filesystem::exists(status)&&(!std::filesystem::is_directory(status)||std::filesystem::is_symlink(status)))return false;
        error.clear();
#endif
        const auto parent=current.parent_path();if(parent==current||parent.empty())break;current=parent;
    }
    return true;
}
inline bool UnredirectedDirectory(const std::filesystem::path& path) {
    std::error_code error;auto current=std::filesystem::absolute(path,error).lexically_normal();
    if(error)return false;
    for(;;) {
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
            !(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
#else
        const auto status=std::filesystem::symlink_status(current,error);
        if(error||!std::filesystem::is_directory(status)||std::filesystem::is_symlink(status))return false;
#endif
        const auto parent=current.parent_path();if(parent==current||parent.empty())break;current=parent;
    }
    return true;
}
inline bool RegularFile(const std::filesystem::path& path) {
    if(!UnredirectedDirectory(path.parent_path()))return false;
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));
#else
    std::error_code error;const auto status=std::filesystem::symlink_status(path,error);
    return !error&&std::filesystem::is_regular_file(status)&&!std::filesystem::is_symlink(status);
#endif
}
struct DirectoryUsage {std::uint64_t files=0,bytes=0;bool valid=false;};
template<class Selected> inline DirectoryUsage MeasureGenerated(const std::filesystem::path& directory,
    Selected selected,std::uint64_t stop_files,std::uint64_t stop_bytes) {
    DirectoryUsage result;if(!UnredirectedDirectory(directory))return result;
    std::error_code error;std::filesystem::directory_iterator it(directory,error),end;
    if(error)return result;
    for(;it!=end;it.increment(error)) {
        if(error)return result;
        const auto path=it->path();if(!selected(path.filename().string()))continue;
        if(!RegularFile(path))return result;
        const auto size=it->file_size(error);if(error||size>UINT64_MAX-result.bytes)return result;
        ++result.files;result.bytes+=size;
        if(result.files>stop_files||result.bytes>stop_bytes){result.valid=true;return result;}
    }
    result.valid=!error;return result;
}
template<class Selected> inline bool AdmitGenerated(const std::filesystem::path& directory,Selected selected,
    std::uint64_t new_files,std::uint64_t new_bytes,std::uint64_t max_files,std::uint64_t max_bytes) {
    const auto usage=MeasureGenerated(directory,selected,max_files,max_bytes);
    return usage.valid&&usage.files<=max_files&&usage.bytes<=max_bytes&&
        new_files<=max_files-usage.files&&new_bytes<=max_bytes-usage.bytes;
}
inline bool WriteNew(const std::filesystem::path& path,const std::string& bytes) {
    if(!UnredirectedDirectory(path.parent_path()))return false;
#ifdef _WIN32
    if(bytes.size()>MAXDWORD)return false;
    const HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool okay=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&
        written==bytes.size()&&FlushFileBuffers(file);
    const bool closed=CloseHandle(file)!=0;return okay&&closed;
#else
    if(std::filesystem::exists(path))return false;
    std::ofstream output(path,std::ios::binary);output.write(bytes.data(),bytes.size());output.flush();return output.good();
#endif
}
#ifdef _WIN32
inline bool CommitTemporary(const std::filesystem::path& temporary,const std::filesystem::path& target) {
#ifdef FILE_RENAME_FLAG_POSIX_SEMANTICS
    // MoveFileEx cannot replace an open destination even when its readers
    // share DELETE. POSIX rename keeps their old handles valid while new
    // readers open the complete new file, without a missing-name interval.
    const HANDLE handle=CreateFileW(temporary.c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return false;
    std::error_code path_error;const auto absolute=std::filesystem::absolute(target,path_error);
    if(path_error){CloseHandle(handle);return false;}
    const auto& name=absolute.native();
    const auto size=offsetof(FILE_RENAME_INFO,FileName)+(name.size()+1)*sizeof(wchar_t);
    if(size>MAXDWORD){CloseHandle(handle);SetLastError(ERROR_FILENAME_EXCED_RANGE);return false;}
    std::vector<char> storage(size);
    auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->Flags=FILE_RENAME_FLAG_REPLACE_IF_EXISTS|FILE_RENAME_FLAG_POSIX_SEMANTICS;
    rename->RootDirectory=nullptr;rename->FileNameLength=static_cast<DWORD>(name.size()*sizeof(wchar_t));
    std::memcpy(rename->FileName,name.data(),rename->FileNameLength);
    const bool committed=SetFileInformationByHandle(handle,FileRenameInfoEx,rename,static_cast<DWORD>(size))!=0;
    const DWORD failure=committed?ERROR_SUCCESS:GetLastError();CloseHandle(handle);
    if(committed)return true;
    if(failure!=ERROR_INVALID_PARAMETER&&failure!=ERROR_NOT_SUPPORTED&&failure!=ERROR_INVALID_FUNCTION) {
        SetLastError(failure);return false;
    }
    // Older Windows/filesystems retain the previous atomic operation. An
    // unsupported replacement stays retryable; no delete/copy fallback.
#endif
    return MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
#endif
inline bool AppendLine(const std::filesystem::path& path,const std::string& line,
    std::uint64_t maximum=4ull*1024*1024){
    if(line.size()>=maximum||!UnredirectedDirectory(path.parent_path()))return false;
#ifdef _WIN32
    const HANDLE output=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(output==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};bool okay=false;
    if(GetFileInformationByHandle(output,&info)&&info.nNumberOfLinks==1&&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&
        GetFileSizeEx(output,&size)&&size.QuadPart>=0&&static_cast<std::uint64_t>(size.QuadPart)<=maximum&&
        line.size()+1<=maximum-static_cast<std::uint64_t>(size.QuadPart)) {
        const auto text=line+'\n';DWORD written=0;
        okay=WriteFile(output,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size();
    }
    CloseHandle(output);return okay;
#else
    std::error_code error;const auto exists=std::filesystem::exists(path,error);if(error)return false;
    const auto size=exists?std::filesystem::file_size(path,error):0;
    if(error||size>maximum||line.size()+1>maximum-size)return false;
    std::ofstream output(path,std::ios::app);
    if(!output)return false;
    output<<line<<'\n';
    return output.good();
#endif
}
// Commit a complete, explicitly named runtime data file without truncating
// the previous version first. Flushing the temporary file before the atomic
// replacement also limits lost history after an abnormal shutdown.
inline bool ReplaceAtomically(const std::filesystem::path& target,const std::string& bytes){
    auto temporary=target;temporary+=".tmp";
#ifdef _WIN32
    if(bytes.size()>MAXDWORD)return false;
    const HANDLE handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;
    const bool complete=WriteFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) &&
        written==bytes.size() && FlushFileBuffers(handle);
    const bool closed=CloseHandle(handle)!=0;
    return complete && closed && CommitTemporary(temporary,target);
#else
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
    output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));output.flush();
    const bool complete=output.good();output.close();
    if(!complete)return false;
    std::error_code error;std::filesystem::rename(temporary,target,error);return !error;
#endif
}
} // namespace horse_races::files
