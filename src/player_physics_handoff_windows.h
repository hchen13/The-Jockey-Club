#pragma once
#include "player_physics_handoff.h"
#include "horse_motion_native.h"

namespace horse_motion::player_physics {
struct WindowsApi {
    static const engine_limits::Layout& Engine() {
        static const auto layout=[] {
            auto result=engine_limits::ValidateLayout();
            if(result.valid)result.valid=ValidateCode([&](std::uintptr_t rva,auto expected){
                decltype(expected) actual{};
                return engine_limits::ReadBytes(result.base+rva,actual.data(),actual.size()) && actual==expected;
            });
            return result;
        }();
        return layout;
    }
    Snapshot Capture(std::uintptr_t entity) const {
        const auto& e=Engine();if(!e.valid)return {};
        return ReadSnapshot(e.base,entity,[](std::uintptr_t p,auto& v){return engine_limits::Read(p,v);});
    }
    bool Wake(std::uintptr_t mode_owner) const {
        // Called only by the ASI script at a settled occupied-player hand-back,
        // no yield, injection, global patch, velocity/coordinate/task writes.
        // The native transition itself may defer if physical overlap is unsafe.
        static bool faulted=false;
        if(faulted || !Engine().valid || !ValidateCode([&](std::uintptr_t rva,auto expected){
            decltype(expected) actual{};
            return engine_limits::ReadBytes(Engine().base+rva,actual.data(),actual.size()) && actual==expected;
        }))return false;
        __try {
            reinterpret_cast<void(*)(void*,bool)>(Engine().base+kWake)(reinterpret_cast<void*>(mode_owner),false);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER){faulted=true;return false;}
    }
};
inline Result RecoverPlayer(const Identity& id) {
    const auto player=PLAYER::PLAYER_PED_ID();
    if(!id.Valid() || Identify(id.horse)!=id || !PED::IS_PED_ON_MOUNT(player) ||
        PED::GET_MOUNT(player)!=id.horse)return {};
    WindowsApi api;return Recover(api,id.address);
}
} // namespace horse_motion::player_physics
