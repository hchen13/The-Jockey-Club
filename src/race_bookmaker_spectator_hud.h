#pragma once

namespace horse_races::bookmaking {

enum class SpectatorPromptMode {None,Rankings,HorseInfo,FollowRankings};

// Ranking preference belongs to the race, not the camera. The follow card
// temporarily covers rankings without changing what the spectator selected.
class SpectatorHudState {
    bool rankings_visible_=true;
public:
    bool RankingsVisible() const{return rankings_visible_;}
    void ToggleRankings(){rankings_visible_=!rankings_visible_;}
    void Reset(){rankings_visible_=true;}

    bool ShouldShowRankings(bool cinematic,bool follow_requested,bool horse_info_busy) const{
        return !cinematic && !horse_info_busy &&
            (follow_requested || rankings_visible_);
    }

    SpectatorPromptMode PromptMode(bool input_ready,bool cinematic,bool follow_requested,
        bool follow_rider_visible,bool horse_info_active,bool horse_info_busy) const{
        if(!input_ready || cinematic)return SpectatorPromptMode::None;
        if(follow_requested){
            if(!follow_rider_visible)return SpectatorPromptMode::None;
            if(horse_info_active)return SpectatorPromptMode::FollowRankings;
            return horse_info_busy?SpectatorPromptMode::None:SpectatorPromptMode::HorseInfo;
        }
        return horse_info_busy?SpectatorPromptMode::None:SpectatorPromptMode::Rankings;
    }
};

} // namespace horse_races::bookmaking
