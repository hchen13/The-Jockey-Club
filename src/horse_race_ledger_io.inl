// All temporary publication operations use the exact open file, never a path
// which a reentrant save fence or another process could have replaced.
namespace {
bool RenameOwnedFile(HANDLE file,const std::filesystem::path& target,bool replace){
    const auto name=std::filesystem::absolute(target).lexically_normal().native();
    const auto size=offsetof(FILE_RENAME_INFO,FileName)+(name.size()+1)*sizeof(wchar_t);
    std::vector<unsigned char> storage(size);
    auto* info=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    info->ReplaceIfExists=replace?TRUE:FALSE;info->RootDirectory=nullptr;
    info->FileNameLength=static_cast<DWORD>(name.size()*sizeof(wchar_t));
    std::copy(name.begin(),name.end(),info->FileName);
    return SetFileInformationByHandle(file,FileRenameInfo,info,static_cast<DWORD>(size))!=0;
}
bool RemoveOwnedFile(HANDLE file){
    FILE_DISPOSITION_INFO remove{TRUE};
    return SetFileInformationByHandle(file,FileDispositionInfo,&remove,sizeof remove)!=0;
}
Bytes ReadOwnedFile(HANDLE file,std::size_t maximum=kMax){
    BY_HANDLE_FILE_INFORMATION info{};
    Need(GetFileInformationByHandle(file,&info)!=0&&info.nNumberOfLinks==1&&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),
        "unsafe archive commit file");
    const auto length=(std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
    Need(length<=maximum,"archive commit exceeds capacity");
    LARGE_INTEGER zero{};Need(SetFilePointerEx(file,zero,nullptr,FILE_BEGIN)!=0,"cannot seek archive commit");
    Bytes bytes(static_cast<std::size_t>(length));std::size_t at=0;
    while(at<bytes.size()){
        const auto want=static_cast<DWORD>(std::min<std::size_t>(65536,bytes.size()-at));DWORD read=0;
        Need(ReadFile(file,bytes.data()+at,want,&read,nullptr)!=0&&read==want,"cannot read archive commit");at+=read;
    }
    return bytes;
}
void FinishArchiveCommit(const std::filesystem::path& ready,const std::filesystem::path& target,const Bytes& expected){
    Handle file{CreateFileW(ready.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,FILE_SHARE_READ,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_WRITE_THROUGH,nullptr)};
    Need(file.h!=INVALID_HANDLE_VALUE,"cannot lock archive commit decision");
    Need(ReadOwnedFile(file.h)==expected,"archive commit decision changed before replacement");
    Need(RenameOwnedFile(file.h,target,true),"cannot finish previously committed archive replacement");
    Need(FlushFileBuffers(file.h)!=0,"cannot flush completed archive replacement");
}
// Global per-installation budgets also bound newly introduced Rockstar profile
// identities. Enumerate only on publication, never in the gameplay tick loop.
// Existing unknown/oversized files stay intact and consume the admission budget.
void ArchiveAdmission(const std::filesystem::path& directory,std::size_t incoming){
    const bool slots=directory.filename()==L"slots";
    Need(slots||directory.filename()==L"audit","unknown bounded archive family");
    const std::size_t maximum_files=slots?512:256;
    const std::uint64_t maximum_bytes=slots?2ull*1024*1024*1024:128ull*1024*1024;
    std::size_t count=0;std::uint64_t total=0;
    for(const auto& entry:std::filesystem::directory_iterator(directory)){
        const auto attributes=GetFileAttributesW(entry.path().c_str());
        Need(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),
            "unsafe entry in archive capacity inventory");
        const auto size=entry.file_size();
        Need(++count<maximum_files&&size<=maximum_bytes-total,"installation archive capacity reached");total+=size;
    }
    Need(incoming<=maximum_bytes-total,"installation archive byte capacity reached");
}
}
