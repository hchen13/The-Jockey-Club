#pragma once
#include "../../src/kernel/files.h"
#include <ostream>
#include <streambuf>
#include <stdexcept>

namespace horse_races::analysis_output {
inline constexpr std::size_t kMaximumBytes=32u*1024*1024;
class Buffer final:public std::streambuf {
    std::string bytes_;
    std::size_t maximum_;
    std::streamsize xsputn(const char* data,std::streamsize count) override {
        if(count<0||static_cast<std::uint64_t>(count)>maximum_-bytes_.size())return 0;
        bytes_.append(data,static_cast<std::size_t>(count));return count;
    }
    int_type overflow(int_type value) override {
        if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);
        if(bytes_.size()==maximum_)return traits_type::eof();
        bytes_.push_back(traits_type::to_char_type(value));return value;
    }
public:
    explicit Buffer(std::size_t maximum=kMaximumBytes):maximum_(maximum){}
    const std::string& Bytes()const{return bytes_;}
};

inline void Publish(const std::filesystem::path& destination,const std::string& bytes) {
    if(bytes.size()>kMaximumBytes)throw std::runtime_error("analysis output exceeds 32 MiB capacity");
    const auto target=std::filesystem::absolute(destination).lexically_normal();
    if(!files::UnredirectedDirectory(target.parent_path())||
        (std::filesystem::exists(target)&&!files::RegularFile(target)))
        throw std::runtime_error("unsafe analysis destination; previous output preserved");
    auto temporary=target;temporary+=".export.tmp";
    const HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE|DELETE,FILE_SHARE_READ|FILE_SHARE_DELETE,
        nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("analysis stage unavailable; existing files preserved");
    struct Stage {
        HANDLE file;bool committed=false;
        ~Stage(){if(!committed){FILE_DISPOSITION_INFO remove{TRUE};SetFileInformationByHandle(file,FileDispositionInfo,&remove,sizeof remove);}CloseHandle(file);}
    } stage{file};
    DWORD written=0;
    if(!WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||
        written!=bytes.size()||!FlushFileBuffers(file))throw std::runtime_error("analysis stage write failed");
    // Rename the exact handle we created. The old target remains usable by
    // current readers; no path-based removal can delete an unrelated stage.
    const auto& name=target.native();
    const auto size=offsetof(FILE_RENAME_INFO,FileName)+(name.size()+1)*sizeof(wchar_t);
    std::vector<char> storage(size);auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->Flags=FILE_RENAME_FLAG_REPLACE_IF_EXISTS|FILE_RENAME_FLAG_POSIX_SEMANTICS;
    rename->RootDirectory=nullptr;rename->FileNameLength=static_cast<DWORD>(name.size()*sizeof(wchar_t));
    std::memcpy(rename->FileName,name.data(),rename->FileNameLength);
    if(!SetFileInformationByHandle(file,FileRenameInfoEx,rename,static_cast<DWORD>(size)))
        throw std::runtime_error("analysis publication failed; previous output preserved");
    stage.committed=true;
}
}
