#pragma once

#include "game_natives.h"
#include <array>
#include <cstddef>

namespace horse_races::map_textures {
enum class Dictionary {Story, Multiplayer};
enum class Owner {RaceMarkers, BettingMarkers, StimulantHud, BettingStandings, HorseMarket, ProgressionToast};

inline constexpr const char* Name(Dictionary dict){
    return dict==Dictionary::Story?"blips":"blips_mp";
}
inline constexpr Hash DictionaryHash(Dictionary dict){
    return dict==Dictionary::Story?0xE5EBC699:672378911;
}
inline constexpr Hash HorseSprite(Dictionary dict){
    return dict==Dictionary::Story?0xD99F0C2B:564457427;
}
struct State {
    unsigned owners=0;
    bool horse_ready=false;
};
inline std::array<State,2> states{};

inline void BeginScriptSession(){
    // ScriptHook can re-enter after loading a save while C++ globals survive.
    // Native requests belong to that script session, so retained HUD owners
    // must not suppress the new session's request, even during initial load.
    for(std::size_t i=0;i<states.size();++i){
        auto& state=states[i];
        if(state.owners)TXD::REQUEST_STREAMED_TEXTURE_DICT(Name(static_cast<Dictionary>(i)),false);
        state.horse_ready=false;
    }
}

// These consumers run on the same script fiber. A subsystem's cleanup must
// not unpin a dictionary that another subsystem still displays. Keep one
// script request across their overlapping lifetimes, regardless of whether
// the engine treats repeated requests as independent references.
inline void Acquire(Dictionary dict,Owner owner){
    auto& state=states[static_cast<std::size_t>(dict)];
    if(!state.owners)TXD::REQUEST_STREAMED_TEXTURE_DICT(Name(dict),false);
    state.owners|=1U<<static_cast<unsigned>(owner);
}
inline void Release(Dictionary dict,Owner owner){
    auto& state=states[static_cast<std::size_t>(dict)];
    const unsigned bit=1U<<static_cast<unsigned>(owner);
    if(!(state.owners&bit))return;
    state.owners&=~bit;
    if(!state.owners){
        TXD::SET_STREAMED_TEXTURE_DICT_AS_NO_LONGER_NEEDED(Name(dict));
        state.horse_ready=false;
    }
}
inline bool HorseIconReady(Dictionary dict){
    auto& state=states[static_cast<std::size_t>(dict)];
    if(!state.owners)return false;
    const bool ready=TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(Name(dict)) &&
        GRAPHICS::_IS_TEXTURE_IN_DICT(DictionaryHash(dict),HorseSprite(dict));
    // An external streaming loss can invalidate an otherwise retained asset.
    // Reassert once per loss transition, shared by all consumers; never issue
    // a new request on every loading frame or assign an unready glyph.
    if(state.horse_ready && !ready)TXD::REQUEST_STREAMED_TEXTURE_DICT(Name(dict),false);
    state.horse_ready=ready;
    return ready;
}
} // namespace horse_races::map_textures
