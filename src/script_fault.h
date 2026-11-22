#pragma once
#include "native_diagnostics.h"
#include "runtime_paths.h"
#include <cstdio>
#include <cstdint>
#include <cwchar>

namespace horse_diagnostics {
inline wchar_t fault_path[MAX_PATH]{};
inline volatile LONG fault_written=0;
inline constexpr std::uint64_t kFaultRotateBytes=16ull*1024*1024;
// Called only on healthy startup. A suspect path or failed rename disables fault
// logging for the session rather than appending without limit or deleting a backup.
inline bool PrepareFaultLog(const wchar_t* path) {
    if(!path || !path[0])return false;
    const DWORD attributes=GetFileAttributesW(path);
    if(attributes==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND;
    if(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))return false;
    const HANDLE file=CreateFileW(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    const bool safe=GetFileInformationByHandle(file,&info) &&
        !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) &&
        GetFileSizeEx(file,&size) && size.QuadPart>=0;
    CloseHandle(file);
    if(!safe)return false;
    // Leave room for either fixed 4096-byte crash message on a fresh session.
    if(static_cast<std::uint64_t>(size.QuadPart)<kFaultRotateBytes-4096)return true;
    wchar_t previous[MAX_PATH]{};
    if(wcscpy_s(previous,path)!=0 || wcscat_s(previous,L".1")!=0)return false;
    const DWORD backup=GetFileAttributesW(previous);
    if(backup==INVALID_FILE_ATTRIBUTES) {
        if(GetLastError()!=ERROR_FILE_NOT_FOUND)return false;
    } else if(backup&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))return false;
    return MoveFileExW(path,previous,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
template<std::size_t N> inline void ModuleNameUtf8(const wchar_t* source,char (&output)[N]) {
    // A Unicode module path makes printf's locale-dependent %ls fail with
    // EILSEQ, losing the whole fault log. Convert without heap allocation.
    if(!WideCharToMultiByte(CP_UTF8,0,source,-1,output,static_cast<int>(N),nullptr,nullptr))
        strcpy_s(output,"unknown");
}
inline void ConfigureFaultLog(HMODULE module) {
    // ScriptHook may enter ScriptMain again without unloading the ASI. Keep the
    // path stable once the process-wide vectored handler can observe it.
    static bool configured=false;
    if(configured)return;
    configured=true;
    // This runs on healthy ScriptMain, never in DllMain or the fault filter.
    // Resolve before registering the handler; fault handling only uses the
    // fixed buffer and retains the existing rotation and reparse checks.
    try {
        wchar_t module_path[MAX_PATH]{};
        const auto size=GetModuleFileNameW(module,module_path,MAX_PATH);
        if(!size || size>=MAX_PATH)return;
        const auto paths=horse_races::runtime_paths::Resolve(std::filesystem::path(module_path).parent_path());
        const auto path=paths.Log("TheJockeyClub-fault.log");
        if(!horse_races::runtime_paths::EnsureDirectory(path.parent_path()) ||
            wcscpy_s(fault_path,path.c_str())!=0 || !PrepareFaultLog(fault_path))fault_path[0]=0;
    }catch(...){fault_path[0]=0;}
}
// Observe an unhandled exception, do not continue a possibly corrupted script.
// No native calls, allocations, entity cleanup, stack walking or memory dumps in
// this filter. ScriptHook retains its normal exception handler and error dialog.
inline LONG RecordFault(EXCEPTION_POINTERS* fault) {
    if(!fault || !fault->ExceptionRecord || !fault->ContextRecord || !fault_path[0] ||
        InterlockedCompareExchange(&fault_written,1,0)!=0)return EXCEPTION_CONTINUE_SEARCH;
    const auto& e=*fault->ExceptionRecord;
    const auto& c=*fault->ContextRecord;
    HMODULE fault_module=nullptr;
    static wchar_t module_name[MAX_PATH]=L"unknown";
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(c.Rip),&fault_module);
    if(fault_module)GetModuleFileNameW(fault_module,module_name,MAX_PATH);
    static char module_utf8[MAX_PATH*4]{};ModuleNameUtf8(module_name,module_utf8);
    SYSTEMTIME time{};GetLocalTime(&time);
    static char message[4096]{};
    const int size=_snprintf_s(message,sizeof(message),_TRUNCATE,
        "[%04u-%02u-%02u %02u:%02u:%02u] TheJockeyClub 1.18.0 pid=%lu thread=%lu\r\n"
        "code=0x%08lX rip=0x%016llX module=%s rva=0x%llX\r\n"
        "stage=%s rsp=0x%016llX own_native_executing=%u last_own_native=0x%016llX argc=%u\r\n"
        "args(first8)=%016llX,%016llX,%016llX,%016llX,%016llX,%016llX,%016llX,%016llX\r\n"
        "exception_info=%016llX,%016llX rcx=%016llX rdx=%016llX r8=%016llX r9=%016llX\r\n"
        "Not recovered; forwarding to ScriptHook. Last native is NOT the faulting call unless executing=1.\r\n",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,
        GetCurrentProcessId(),GetCurrentThreadId(),e.ExceptionCode,c.Rip,module_utf8,
        fault_module?c.Rip-reinterpret_cast<std::uintptr_t>(fault_module):0,trace.stage,c.Rsp,
        unsigned(trace.executing),trace.hash,trace.count,
        trace.arguments[0],trace.arguments[1],trace.arguments[2],trace.arguments[3],
        trace.arguments[4],trace.arguments[5],trace.arguments[6],trace.arguments[7],
        e.NumberParameters>0?e.ExceptionInformation[0]:0,e.NumberParameters>1?e.ExceptionInformation[1]:0,
        c.Rcx,c.Rdx,c.R8,c.R9);
    if(size>0) {
        const auto file=CreateFileW(fault_path,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file!=INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};
            const auto capacity=kFaultRotateBytes;
            if(GetFileInformationByHandle(file,&info) && info.nNumberOfLinks==1 &&
                !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) &&
                ((static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow)<=capacity &&
                static_cast<std::uint64_t>(size)<=capacity-((static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow)) {
                DWORD written=0;WriteFile(file,message,static_cast<DWORD>(size),&written,nullptr);FlushFileBuffers(file);
            }
            CloseHandle(file);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
// Every ASI shares the game's thread, and when another one faults ScriptHook
// says only that it did. A vectored handler sees every exception in the
// process first: a hardware fault (access violation and its kin), a heap
// failure or a C++ throw that lands in some other .asi is written to the
// same fault log with the module, the offset in it, what was touched and a
// short backtrace - the one line that says what that mod was doing when it
// went (2026-09-18: SUHM.asi, near the race sites). Ours are covered by
// RecordFault; this never handles anything, only observes, and at most a
// couple of dozen times a session.
inline volatile LONG foreign_faults_written=0;
inline bool ModuleOf(std::uintptr_t address,wchar_t* name,std::size_t count,std::uintptr_t* base) {
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address),&module) || !module)return false;
    const auto size=GetModuleFileNameW(module,name,static_cast<DWORD>(count));
    if(!size || size>=count)return false;
    *base=reinterpret_cast<std::uintptr_t>(module);
    return true;
}
inline const wchar_t* FileOf(const wchar_t* path) {
    const auto slash=wcsrchr(path,L'\\');
    return slash?slash+1:path;
}
inline bool EndsWithAsi(const wchar_t* name) {
    const auto length=wcslen(name);
    return length>=4 && _wcsicmp(name+length-4,L".asi")==0;
}
inline bool IsOurModule(const wchar_t* name) { return _wcsicmp(FileOf(name),L"TheJockeyClub.asi")==0; }
// The type a C++ exception carries, from the throw information the runtime
// hands the filter (x64: [0] magic, [1] object, [2] ThrowInfo, [3] image base).
inline void CppExceptionType(const EXCEPTION_RECORD& e,char* out,std::size_t count) {
    out[0]=0;
    if(e.NumberParameters<4 || e.ExceptionInformation[0]!=0x19930520 || !e.ExceptionInformation[2] || !e.ExceptionInformation[3])return;
    __try {
        const auto base=e.ExceptionInformation[3];
        const auto throw_info=reinterpret_cast<const std::int32_t*>(e.ExceptionInformation[2]); // attributes, pmfnUnwind, pForwardCompat, pCatchableTypeArray (RVAs)
        const auto array=reinterpret_cast<const std::int32_t*>(base+static_cast<std::uint32_t>(throw_info[3]));
        if(array[0]<1)return;
        const auto type=reinterpret_cast<const std::int32_t*>(base+static_cast<std::uint32_t>(array[1])); // properties, pType (RVA), ...
        const auto descriptor=reinterpret_cast<const char*>(base+static_cast<std::uint32_t>(type[1]));
        const char* name=descriptor+16; // vftable, spare, then the decorated name
        std::size_t i=0;for(;i+1<count && name[i];++i)out[i]=name[i];out[i]=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { out[0]=0; }
}
inline LONG WINAPI ForeignFault(EXCEPTION_POINTERS* fault) {
    if(!fault || !fault->ExceptionRecord || !fault->ContextRecord || !fault_path[0])return EXCEPTION_CONTINUE_SEARCH;
    const auto& e=*fault->ExceptionRecord;const auto& c=*fault->ContextRecord;
    const auto code=e.ExceptionCode;
    const bool hardware=code==0xC0000005 || code==0xC0000006 || code==0xC000001D || code==0xC0000094 || code==0xC0000096 || code==0xC000008C;
    const bool heap=code==0xC0000374;
    const bool cpp=code==0xE06D7363;
    if(!hardware && !heap && !cpp && code!=0x80000003)return EXCEPTION_CONTINUE_SEARCH;
    // Whose is it: the faulting instruction's module for a hardware fault,
    // otherwise the first .asi on the stack.
    void* frames[40]{};
    const auto count=CaptureStackBackTrace(0,40,frames,nullptr);
    wchar_t module_name[MAX_PATH]=L"";std::uintptr_t base=0,at=0;
    bool attributed=false;
    if(hardware && ModuleOf(c.Rip,module_name,MAX_PATH,&base)){at=c.Rip;attributed=true;}
    for(USHORT i=0;i<count && !attributed;++i) {
        wchar_t name[MAX_PATH]=L"";std::uintptr_t b=0;
        const auto address=reinterpret_cast<std::uintptr_t>(frames[i]);
        if(ModuleOf(address,name,MAX_PATH,&b) && EndsWithAsi(name)){wcscpy_s(module_name,name);base=b;at=address;attributed=true;}
    }
    if(!attributed || IsOurModule(module_name))return EXCEPTION_CONTINUE_SEARCH;   // Ours go through RecordFault.
    if(!EndsWithAsi(module_name) && !heap)return EXCEPTION_CONTINUE_SEARCH;
    if(InterlockedIncrement(&foreign_faults_written)>24)return EXCEPTION_CONTINUE_SEARCH;
    char type_name[160];CppExceptionType(e,type_name,sizeof type_name);
    char trace_text[1200]{};std::size_t used=0;
    for(USHORT i=0;i<count && i<12 && used+96<sizeof trace_text;++i) {
        wchar_t name[MAX_PATH]=L"";std::uintptr_t b=0;
        const auto address=reinterpret_cast<std::uintptr_t>(frames[i]);
        int wrote=0;
        if(ModuleOf(address,name,MAX_PATH,&b)) {
            char utf8[MAX_PATH*4]{};ModuleNameUtf8(FileOf(name),utf8);
            wrote=_snprintf_s(trace_text+used,sizeof trace_text-used,_TRUNCATE," %s+0x%llX",utf8,static_cast<unsigned long long>(address-b));
        }
        else wrote=_snprintf_s(trace_text+used,sizeof trace_text-used,_TRUNCATE," %016llX",static_cast<unsigned long long>(address));
        if(wrote<=0)break;used+=static_cast<std::size_t>(wrote);
    }
    SYSTEMTIME time{};GetLocalTime(&time);
    char module_utf8[MAX_PATH*4]{};ModuleNameUtf8(FileOf(module_name),module_utf8);
    char message[4096]{};
    const int size=_snprintf_s(message,sizeof(message),_TRUNCATE,
        "[%04u-%02u-%02u %02u:%02u:%02u] FOREIGN fault in another module (observed only, not handled) thread=%lu\r\n"
        "code=0x%08lX module=%s rva=0x%llX rip=0x%016llX%s%s\r\n"
        "access=%s target=0x%016llX rcx=%016llX rdx=%016llX r8=%016llX r9=%016llX\r\n"
        "stack:%s\r\n",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,GetCurrentThreadId(),
        code,module_utf8,static_cast<unsigned long long>(at-base),static_cast<unsigned long long>(c.Rip),
        type_name[0]?" cpp_type=":"",type_name,
        hardware && e.NumberParameters>0?(e.ExceptionInformation[0]==0?"read":e.ExceptionInformation[0]==1?"write":"execute"):"-",
        static_cast<unsigned long long>(hardware && e.NumberParameters>1?e.ExceptionInformation[1]:0),
        static_cast<unsigned long long>(c.Rcx),static_cast<unsigned long long>(c.Rdx),
        static_cast<unsigned long long>(c.R8),static_cast<unsigned long long>(c.R9),trace_text);
    if(size>0) {
        const auto file=CreateFileW(fault_path,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file!=INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};
            const auto capacity=kFaultRotateBytes;
            if(GetFileInformationByHandle(file,&info) && info.nNumberOfLinks==1 &&
                !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) &&
                ((static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow)<=capacity &&
                static_cast<std::uint64_t>(size)<=capacity-((static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow)) {
                DWORD written=0;WriteFile(file,message,static_cast<DWORD>(size),&written,nullptr);FlushFileBuffers(file);
            }
            CloseHandle(file);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace horse_diagnostics
