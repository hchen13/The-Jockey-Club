#pragma once
#include "game_natives.h"

namespace horse_races::horse_market_target {
struct Target {
    int horse{};
    bool leading{};
    bool operator==(const Target&)const=default;
};
inline bool ValidHorse(int horse){
    return horse>0&&ENTITY::DOES_ENTITY_EXIST(horse)&&ENTITY::IS_ENTITY_A_PED(horse)&&
        PED::_IS_THIS_MODEL_A_HORSE(ENTITY::GET_ENTITY_MODEL(horse))&&
        !PED::IS_PED_DEAD_OR_DYING(horse,true);
}
// Clay-style current interaction only. No saddle, proximity, or history fallback.
// Vanilla shop_horse_shop_sp.c func_2820 (91638) gates the current-led native
// behind IS_PED_LEADING_HORSE; GET_LAST_LED_MOUNT is historical and unsuitable.
inline Target CurrentTarget(int player){
    if(player<=0||!ENTITY::DOES_ENTITY_EXIST(player))return {};
    if(PED::IS_PED_ON_MOUNT(player)){
        const int horse=PED::GET_MOUNT(player);
        return ValidHorse(horse)?Target{horse,false}:Target{};
    }
    if(!TASK::_IS_PED_LEADING_HORSE(player))return {};
    const int horse=TASK::_GET_LED_HORSE_FROM_PED(player);
    return ValidHorse(horse)?Target{horse,true}:Target{};
}
inline Target CurrentTarget(){return CurrentTarget(PLAYER::PLAYER_PED_ID());}
enum class LeadRelease { Clear, Requested, WrongHorse, Invalid };
// Caller has already confirmed full ownership GUID. "Owned" here additionally
// checks the exact current player-to-horse lead relationship before task mutation.
// Requested is asynchronous: wait for Clear, then recapture approved ownership.
// Never stop the player leading another horse or clear unrelated player tasks.
inline LeadRelease ReleaseLeadIfOwned(int player,int horse){
    if(player<=0||!ENTITY::DOES_ENTITY_EXIST(player)||!ValidHorse(horse))return LeadRelease::Invalid;
    if(!TASK::_IS_PED_LEADING_HORSE(player))return LeadRelease::Clear;
    if(TASK::_GET_LED_HORSE_FROM_PED(player)!=horse)return LeadRelease::WrongHorse;
    TASK::TASK_STOP_LEADING_HORSE(player);
    return LeadRelease::Requested;
}
}
