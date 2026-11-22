#pragma once
#include "race_character_policy.h"
#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace horse_races::player_voice {
// beat_horse_race.c's post-race interaction: branch 65 is Greet/Talk,
// branch 66 is Antagonize, in each of its three encounter families.
// PLYR_WIN/LOSE are automatic finish reactions, not these interaction lines;
// GOAL_PLYR_WIN/LOSE belong to the rival and must never enter this pool.
// Keep the player's own voice (nullptr) and variation 0 at the native call.
inline constexpr std::array<const char*,6> kWinContexts{
    "RE_HR_UNI_V1_NOVICE_ILO_END_WON_A", "RE_HR_UNI_V2_CONFIDENT_ILO_END_WON_A",
    "RE_HR_UNI_V3_WEIRD_ILO_END_WON_A", "RE_HR_UNI_V1_NOVICE_ILO_END_WON_B",
    "RE_HR_UNI_V2_CONFIDENT_ILO_END_WON_B", "RE_HR_UNI_V3_WEIRD_ILO_END_WON_B"};
inline constexpr std::array<const char*,6> kLoseContexts{
    "RE_HR_UNI_V1_NOVICE_ILO_END_LOST_A", "RE_HR_UNI_V2_CONFIDENT_ILO_END_LOST_A",
    "RE_HR_UNI_V3_WEIRD_ILO_END_LOST_A", "RE_HR_UNI_V1_NOVICE_ILO_END_LOST_B",
    "RE_HR_UNI_V2_CONFIDENT_ILO_END_LOST_B", "RE_HR_UNI_V3_WEIRD_ILO_END_LOST_B"};
inline bool IncludeAntagonize(characters::Id host){
    return host==characters::Id::Bill || host==characters::Id::Uncle || host==characters::Id::Javier;
}
inline std::span<const char* const> Pool(bool won,bool antagonize){
    return {(won?kWinContexts:kLoseContexts).data(),antagonize?6U:3U};
}
struct Candidates {std::array<const char*,6> lines{};std::size_t size=0;};
// Randomize the entire allowed set before trying it. A refused context gets
// one attempt, without falling back to another outcome, speaker or tone pool.
// Avoid the last successful line when the pool has alternatives.
template<class Random> Candidates Shuffled(bool won,bool antagonize,std::string_view last,Random random){
    const auto pool=Pool(won,antagonize);Candidates result;result.size=pool.size();
    std::copy(pool.begin(),pool.end(),result.lines.begin());
    for(std::size_t i=result.size-1;i>0;--i)
        std::swap(result.lines[i],result.lines[static_cast<std::size_t>(random(0,static_cast<int>(i+1)))]);
    if(result.lines[0]==last)
        std::swap(result.lines[0],result.lines[static_cast<std::size_t>(random(1,static_cast<int>(result.size)))]);
    return result;
}
} // namespace horse_races::player_voice
