#pragma once
#include "game_natives.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace horse_races::transaction_feed {
// Native transaction feed used by generic_wagon_fence_core.c func_425 and
// coachrobberies.c func_882. Script struct members are eight-byte slots.
// This is the right-side money/item feed, not an objective or homemade HUD.
inline int Post(const char* text,const char* dictionary,Hash icon,bool negative) {
    // VAR_STRING produces an engine player-string token, not a C string we own.
    // A non-null token may not be a readable address. Pass it back unchanged.
    if(!text)return 0;
    static constexpr char sounds[]="Transaction_Feed_Sounds";
    static constexpr char positive[]="Transaction_Positive", loss[]="Transaction_Negative";
    std::array<std::uint64_t,4> params{450,reinterpret_cast<std::uint64_t>(sounds),
        reinterpret_cast<std::uint64_t>(negative?loss:positive),0};
    std::array<std::uint64_t,7> data{0,reinterpret_cast<std::uint64_t>(text),
        reinterpret_cast<std::uint64_t>(dictionary),icon,negative?1ULL:0ULL,
        MISC::GET_HASH_KEY(negative?"COLOR_RED":"COLOR_PURE_WHITE"),0};
    return invoke<int>(0xB249EBCB30DD88E0,params.data(),data.data(),true);
}
inline int Cash(int signed_cents) {
    // A legitimate bookmaker return can exceed $5,000. The old arbitrary cap
    // silently suppressed its native toast; only abs(INT_MIN) is unsafe here.
    if(!signed_cents || signed_cents==std::numeric_limits<int>::min())return 0;
    // The original subtitle formatter takes cents (same units as MONEY natives).
    const char* text=invoke<const char*>(0xFA925AC00EB830B9,2,
        signed_cents>0?"FEED_MONEY_EARN":"FEED_MONEY_SPEND",std::abs(signed_cents));
    return Post(text,"ITEMTYPE_TEXTURES",MISC::GET_HASH_KEY("ITEMTYPE_CASH"),signed_cents<0);
}
} // namespace horse_races::transaction_feed
