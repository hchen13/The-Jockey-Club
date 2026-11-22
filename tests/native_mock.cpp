// Test-only native bridge. Never load this DLL into the game or deploy it with the ASI.
#include <windows.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <chrono>

using Handler = void (*)(UINT64 hash, const UINT64* args, UINT64* result);
namespace {
Handler handler = nullptr;
using GlobalHandler=UINT64* (*)(int);
GlobalHandler global_handler=nullptr;
UINT64 current_hash = 0;
std::array<UINT64, 64> arguments{};
std::array<UINT64, 16> result{};
std::size_t argument_count = 0;
std::unordered_map<int,int> default_proofs;
}

__declspec(dllexport) void setTestNativeHandler(Handler callback) { handler = callback; default_proofs.clear(); }
__declspec(dllexport) void setTestGlobalHandler(GlobalHandler callback) { global_handler=callback; }
__declspec(dllexport) void nativeInit(UINT64 hash)
{
    current_hash = hash;
    argument_count = 0;
    arguments.fill(0);
}
__declspec(dllexport) void nativePush64(UINT64 value)
{
    if (argument_count >= arguments.size()) {
        throw std::runtime_error("mock native argument overflow");
    }
    arguments[argument_count++] = value;
}
__declspec(dllexport) PUINT64 nativeCall()
{
    result.fill(0);
    // Stateful default for old fixtures that don't otherwise use entity proofs.
    // A fixture's handler below may override this to test failed writes/readbacks.
    if(current_hash==0xFAEE099C6F890BB8)default_proofs[static_cast<int>(arguments[0])]=static_cast<int>(arguments[1]);
    if(current_hash==0x6CF0DAD7FA1088EA)result[0]=default_proofs[static_cast<int>(arguments[0])];
    // The race deadlines now measure GAME time, which stops when the game is
    // paused. Nothing pauses in a test, so the engine timer is wall time here -
    // which keeps every existing fixture's notion of elapsed time intact. A
    // fixture that wants to drive it explicitly still overrides the hash below.
    if(current_hash==0x483B8C542103AD72 || current_hash==0x4F67E8ECA7D3F667) {
        static const auto started=std::chrono::steady_clock::now();
        result[0]=static_cast<UINT64>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now()-started).count()+1);
    }
    if (handler) {
        handler(current_hash, arguments.data(), result.data());
    }
    return result.data();
}
__declspec(dllexport) void scriptWait(DWORD) {}
__declspec(dllexport) BYTE* getScriptHandleBaseAddress(int handle)
{
    return reinterpret_cast<BYTE*>(static_cast<std::uintptr_t>(handle)*4096);
}
// Private test dispatch tags, not game native hashes. Exercise the real SDK pool
// entry points instead of pretending a population-volume query includes all peds.
// The script globals: one page of zeroes unless a test writes into it. A zeroed
// bitset has no length, which the race system reads as "unknown build".
__declspec(dllexport) UINT64* getGlobalPtr(int globalId)
{
    if(global_handler)return global_handler(globalId);
    static std::array<UINT64,32768> globals{};
    return globalId>=0 && static_cast<std::size_t>(globalId)<globals.size()-4096?globals.data()+globalId:nullptr;
}
__declspec(dllexport) int worldGetAllPeds(int* arr,int size)
{
    const UINT64 args[]{reinterpret_cast<UINT64>(arr),static_cast<UINT64>(size)};UINT64 out[16]{};
    if(handler)handler(0x48534C504F4F4C01,args,out);return static_cast<int>(out[0]);
}
__declspec(dllexport) int worldGetAllVehicles(int* arr,int size)
{
    const UINT64 args[]{reinterpret_cast<UINT64>(arr),static_cast<UINT64>(size)};UINT64 out[16]{};
    if(handler)handler(0x48534C504F4F4C02,args,out);return static_cast<int>(out[0]);
}
__declspec(dllexport) int worldGetAllObjects(int* arr,int size)
{
    const UINT64 args[]{reinterpret_cast<UINT64>(arr),static_cast<UINT64>(size)};UINT64 out[16]{};
    if(handler)handler(0x48534C504F4F4C03,args,out);return static_cast<int>(out[0]);
}
