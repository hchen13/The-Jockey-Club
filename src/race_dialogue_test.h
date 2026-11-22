#pragma once
#include <cstdint>
#include <string_view>

namespace horse_races::dialogue_test {
enum class Listener { Auto, Arthur, John };
inline Listener Parse(std::string_view value) {
    return value=="Arthur"?Listener::Arthur:(value=="John"?Listener::John:Listener::Auto);
}
inline const char* Name(Listener value) {
    return value==Listener::Arthur?"Arthur":(value==Listener::John?"John":"Auto");
}
inline Listener Step(Listener value,int step) {
    return static_cast<Listener>((static_cast<int>(value)+3+step)%3);
}
// Presentation only. Never use this model for ownership, horse ranks, cash,
// appearance, game globals, story state or the actual player entity.
inline std::uint32_t Model(Listener value,std::uint32_t actual_model) {
    return value==Listener::Arthur?0x0D7114C9U:(value==Listener::John?0x00B69710U:actual_model);
}
}
