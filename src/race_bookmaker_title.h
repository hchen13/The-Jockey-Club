#pragma once

#include "game_natives.h"
#include <array>
#include <string_view>

// GenericShop.Title is a localized text-label HASH, not literal/debug text.
// LML loads our namespaced labels from the text-only tjc_titles.gxt2 data file.
// The existing native panel owns font, colour, centering, clipping and layers.
namespace horse_races::bookmaker_title {
struct Labels {
    std::string_view venue;
    const char* english;
    const char* chinese;
    const char* place_english;
    const char* place_chinese;
};
inline constexpr std::array<Labels,4> kLabels{{
    {"valentine-circuit","TJC_TITLE_VALENTINE_EN","TJC_TITLE_VALENTINE_ZH",
        "TJC_TITLE_PLACE_VALENTINE_EN","TJC_TITLE_PLACE_VALENTINE_ZH"},
    {"strawberry-wfa","TJC_TITLE_STRAWBERRY_EN","TJC_TITLE_STRAWBERRY_ZH",
        "TJC_TITLE_PLACE_STRAWBERRY_EN","TJC_TITLE_PLACE_STRAWBERRY_ZH"},
    {"rhodes-handicap","TJC_TITLE_RHODES_EN","TJC_TITLE_RHODES_ZH",
        "TJC_TITLE_PLACE_RHODES_EN","TJC_TITLE_PLACE_RHODES_ZH"},
    {"macfarlane-wfa","TJC_TITLE_MACFARLANE_EN","TJC_TITLE_MACFARLANE_ZH",
        "TJC_TITLE_PLACE_MACFARLANE_EN","TJC_TITLE_PLACE_MACFARLANE_ZH"}
}};
inline const char* LabelFor(std::string_view venue,bool chinese){
    for(const auto& row:kLabels)if(row.venue==venue)return chinese?row.chinese:row.english;
    return nullptr;
}
inline const char* PlaceLabelFor(std::string_view venue,bool chinese){
    for(const auto& row:kLabels)if(row.venue==venue)
        return chinese?row.place_chinese:row.place_english;
    return nullptr;
}
inline bool Available(const char* label){
    if(!label || !*label)return false;
    // LML's added strings may not be present in vanilla's label-existence
    // index. Check the actual hash lookup consumed by the native panel.
    const char* text=HUD::GET_STRING_FROM_HASH_KEY(MISC::GET_HASH_KEY(label));
    if(!text || !*text)return false;
    const std::string_view value=text;
    return value!="NULL" && value!=label;
}
struct Binding {Hash hash=0;const char* label="";bool exact=false;};
inline Binding ResolveType(bool handicap,bool chinese){
    const char* label=handicap?(chinese?"TJC_TITLE_TYPE_HANDICAP_ZH":"TJC_TITLE_TYPE_HANDICAP_EN"):
        (chinese?"TJC_TITLE_TYPE_WFA_ZH":"TJC_TITLE_TYPE_WFA_EN");
    if(Available(label))return {MISC::GET_HASH_KEY(label),label,true};
    if(chinese){
        const char* english=handicap?"TJC_TITLE_TYPE_HANDICAP_EN":"TJC_TITLE_TYPE_WFA_EN";
        if(Available(english))return {MISC::GET_HASH_KEY(english),english,false};
    }
    return {};
}
inline Binding Resolve(std::string_view venue,bool chinese,bool results=false){
    const char* requested=LabelFor(venue,chinese);
    if(Available(requested))return {MISC::GET_HASH_KEY(requested),requested,true};
    // Missing/disabled optional text registration must not strand results,
    // block repayment or take over another menu. Remain wholly native and
    // report the fallback to the caller; never resurrect a debug overlay.
    const char* english=LabelFor(venue,false);
    if(chinese && Available(english))return {MISC::GET_HASH_KEY(english),english,false};
    for(const char* stock:{results?"RESULTS":"HORSE_RACE","HORSE_RACE","RACE","GUNSMITH"})
        if(Available(stock))return {MISC::GET_HASH_KEY(stock),stock,false};
    return {};
}
inline Binding ResolveVenue(std::string_view venue,bool chinese,bool results=false){
    const char* requested=PlaceLabelFor(venue,chinese);
    if(Available(requested))return {MISC::GET_HASH_KEY(requested),requested,true};
    const char* english=PlaceLabelFor(venue,false);
    if(chinese && Available(english))return {MISC::GET_HASH_KEY(english),english,false};
    // If the optional new place labels have not loaded, keep the original
    // native race-name label instead of leaving the shop category empty.
    return Resolve(venue,chinese,results);
}
} // namespace horse_races::bookmaker_title
