#pragma once
#include "game_natives.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <cmath>

namespace horse_races::cues {
// Short one-shot cues for the start countdown and each completed lap.
//
// Every default below is a sound/soundset pair that ships in the game's own
// scripts, so it exists; how each one actually sounds in this context is a
// judgement the acceptance run makes, not something offline tests can show.
// Both halves are read from the INI so an alternative can be tried without a
// rebuild - PLAY_SOUND_FRONTEND returns nothing, so an unknown name is simply
// silent rather than an error.
struct Cue {
    std::string name,set; // An empty set means a global sound name, played on its own.
    bool Valid() const {return !name.empty();}
    bool Global() const {return set.empty();}
};
struct Table {
    // Chosen by ear in the F11 audition, 2026-09-10. The countdown and lap
    // pair live in a MISSION soundset, so they are only audible while that set
    // is loaded - see Ensure() below, which is what keeps them working.
    Cue tick{"Small_Safe_Tumbler","Mud5_Sounds"}; // Each visible countdown second.
    Cue go{"HUD_DRAW","HUD_DUEL_SOUNDSET"};       // The moment the field is released.
    Cue lap{"choice_appears","mud5_sounds"};      // One completed lap.
    // Extra seconds the player's own controls stay disabled after GO, so their
    // mount finishes a longer rear and they leave last. 0 disables the handicap.
    // The player's own rear now runs about three seconds, which already makes
    // him the last away, so no artificial hold is needed on top of it.
    float player_rear_seconds=0;
    bool rear_enabled=true;
    // A ped beside the grid raises a Cattleman and fires it into the air on GO.
    bool starter_shot=true;
    // TASK_HORSE_ACTION(ped, action, target, p3). All six combinations were
    // performed in game on 2026-09-11: action 7 does nothing at either variant,
    // and actions 1 and 5 give the same rear at either variant. Rivals draw one
    // of the four at random each, so a field is not four identical puppets;
    // the player keeps a fixed one because his is the one being extended.
    int player_rear_action=5,player_rear_variant=0;
    // 0 keeps the random draw above. Set it to try one action on the whole
    // field: 3 is QUICK_STOP and 8 is SKID, the ones a cliff-edge stop would
    // plausibly be made of, and a shipped ambush script uses 8 on the PLAYER's
    // own mount. 2, 9 and 10 throw a rider off and are never accepted here.
    int npc_rear_action_override=0,npc_rear_variant_override=0;
    // Rivals rear BEFORE the countdown ends - each its own draw of up to this
    // many seconds early, in hundredths. Delaying them after GO only gave the
    // player a second of free riding while they stood there; starting them
    // early keeps everyone released together and still breaks up the unison.
    float npc_rear_lead=.2F;
    // The player rears on the instant the countdown ends. Kept as a key, but a
    // delay here is what let him ride away before rearing at all.
    float player_rear_delay=0;
    // ...and his rear is commanded a second time this long after it starts,
    // which is the only lever left for making his visibly the longest.
};
inline Table& Settings() {static Table table;return table;}
inline void Load(const std::string& ini) {
    if(ini.empty())return;
    auto& table=Settings();
    const auto read=[&](const char* key,Cue& cue,const Cue& fallback) {
        std::array<char,64> name{},set{};
        GetPrivateProfileStringA("Cues",(std::string(key)+"Sound").c_str(),fallback.name.c_str(),
            name.data(),static_cast<DWORD>(name.size()),ini.c_str());
        GetPrivateProfileStringA("Cues",(std::string(key)+"SoundSet").c_str(),fallback.set.c_str(),
            set.data(),static_cast<DWORD>(set.size()),ini.c_str());
        cue={name.data(),set.data()};
        // An emptied NAME restores the shipped pair; an emptied SET is a
        // deliberate request for a global sound, so it is honoured.
        if(!cue.Valid())cue=fallback;
    };
    const Table defaults;
    read("Countdown",table.tick,defaults.tick);
    read("Start",table.go,defaults.go);
    read("Lap",table.lap,defaults.lap);
    std::array<char,32> value{};
    GetPrivateProfileStringA("Cues","PlayerRearSeconds","0",value.data(),
        static_cast<DWORD>(value.size()),ini.c_str());
    try {
        const float parsed=std::stof(value.data());
        if(std::isfinite(parsed) && parsed>=0 && parsed<=3)table.player_rear_seconds=parsed;
    } catch(...) {}
    GetPrivateProfileStringA("Cues","StartRear","1",value.data(),static_cast<DWORD>(value.size()),ini.c_str());
    table.rear_enabled=std::string(value.data())!="0";
    GetPrivateProfileStringA("Cues","StarterShot","1",value.data(),static_cast<DWORD>(value.size()),ini.c_str());
    table.starter_shot=std::string(value.data())!="0";
    const auto action=[&](const char* key,const char* fallback,int& out) {
        GetPrivateProfileStringA("Cues",key,fallback,value.data(),static_cast<DWORD>(value.size()),ini.c_str());
        const std::string text(value.data());
        // Only ids a shipped script actually passes: 1 and 5 are documented
        // rear-ups, 7 is used alongside them with the same fourth argument.
        if(text=="1" || text=="5" || text=="7")out=std::stoi(text);
    };
    const auto variant=[&](const char* key,const char* fallback,int& out) {
        GetPrivateProfileStringA("Cues",key,fallback,value.data(),static_cast<DWORD>(value.size()),ini.c_str());
        const std::string text(value.data());
        if(text=="0" || text=="1")out=std::stoi(text);
    };
    action("PlayerRearAction","5",table.player_rear_action);
    variant("PlayerRearVariant","0",table.player_rear_variant);
    const auto seconds=[&](const char* key,const char* fallback,float& out,float limit) {
        GetPrivateProfileStringA("Cues",key,fallback,value.data(),static_cast<DWORD>(value.size()),ini.c_str());
        try {
            const float parsed=std::stof(value.data());
            if(std::isfinite(parsed) && parsed>=0 && parsed<=limit)out=parsed;
        } catch(...) {}
    };
    const auto horse_action=[&](const char* key,const char* fallback,int& out) {
        GetPrivateProfileStringA("Cues",key,fallback,value.data(),static_cast<DWORD>(value.size()),ini.c_str());
        const std::string text(value.data());
        // Documented actions that do not dismount anybody.
        if(text=="0" || text=="1" || text=="3" || text=="5" || text=="7" || text=="8")out=std::stoi(text);
    };
    horse_action("NpcRearAction","0",table.npc_rear_action_override);
    variant("NpcRearVariant","0",table.npc_rear_variant_override);
    seconds("NpcRearLead","0.2",table.npc_rear_lead,2);
    seconds("PlayerRearDelay","0",table.player_rear_delay,3);
}
// A soundset makes no sound until it is loaded, and only the HUD ones are
// always resident - the chosen countdown and lap cues are not. PREPARE_SOUNDSET
// looks like a reference-counted request, so asking on every cue would climb
// all race long; ask ONCE per distinct set and remember that we did.
inline std::vector<std::string>& Prepared() {static std::vector<std::string> sets;return sets;}
inline void Ensure(const Cue& cue) {
    if(!cue.Valid() || cue.Global())return;
    auto& sets=Prepared();
    if(std::find(sets.begin(),sets.end(),cue.set)!=sets.end())return;
    AUDIO::PREPARE_SOUNDSET(cue.set.c_str(),false);
    sets.push_back(cue.set);
}
inline bool Play(const Cue& cue) {
    if(!cue.Valid())return false;
    if(cue.Global()) { // Nothing to load: the name resolves by itself.
        AUDIO::PLAY_SOUND_FRONTEND(cue.name.c_str(),nullptr,true,0);
        return true;
    }
    Ensure(cue);
    AUDIO::PLAY_SOUND_FRONTEND(cue.name.c_str(),cue.set.c_str(),true,0);
    return true;
}
// Called at load and again when a race is accepted, so a mission soundset is
// resident long before the countdown needs it rather than mid-countdown.
inline void PrepareAll() {
    const auto& table=Settings();
    for(const auto* cue:{&table.tick,&table.go,&table.lap})Ensure(*cue);
}
// A cue reassigned from the menu points at a set nobody has asked for yet.
inline void Forget(){Prepared().clear();}
inline void Tick(){Play(Settings().tick);}
inline void Go(){Play(Settings().go);}
inline void Lap(){Play(Settings().lap);}
// HORSE_ACTION_TYPE_REAR_UP. The native takes an action id, a target ped and a
// fourth argument of unknown meaning; it exposes NO duration, so how long a
// horse stays up is the engine's, not ours.
// The four combinations that actually produce a rear. A rival draws one of
// these; nothing here is a guess, all four were performed in game.
inline constexpr std::array<std::array<int,2>,4> kVerifiedRears{{{1,0},{1,1},{5,0},{5,1}}};
// Game time (ms) of the last rear the mod commanded, -1 for none: the gait
// probe writes the age of it beside every sample.
inline int& LastRearGameMs(){static int ms=-1;return ms;}
// Our own rear at the start raises the horse's agitation, and deep-snow gait is gated on it: the horse
// then sprints through snow as if it were the plains (the user, 2026-09-22). So the value is read
// before the rear and written back once the animation is over. This is the ONE place the mod writes
// agitation (ruling C-04 A otherwise stands: the start-speed experiment that wrote it is deleted).
constexpr int kAgitation=3;
constexpr double kRearSeconds=3.2;   // The animation the verified actions play; measured at about 3 s.
struct Calming {Ped horse=0;float agitation=0;int at_game_ms=0;};
inline std::array<Calming,12>& CalmingAfterRear() {static std::array<Calming,12> pending;return pending;}
// Called every frame while a race is under way; cheap and does nothing once the list is empty.
inline int CalmRearedHorses() {
    const int now=MISC::GET_GAME_TIMER();
    int calmed=0;
    for(auto& entry:CalmingAfterRear()) {
        if(!entry.horse || now-entry.at_game_ms<static_cast<int>(kRearSeconds*1000))continue;
        if(ENTITY::DOES_ENTITY_EXIST(entry.horse) && !ENTITY::IS_ENTITY_DEAD(entry.horse)) {
            PED::_SET_PED_MOTIVATION(entry.horse,kAgitation,entry.agitation,0);
            ++calmed;
        }
        entry={};
    }
    return calmed;
}
inline void ForgetRearedHorses() {CalmingAfterRear().fill({});}
inline void ForgetRearedHorse(Ped horse) {
    // A short/aborted exhibition may delete a mount before its rear finishes.
    // Do not later calm a recycled handle, or erase another race's pending cue.
    for(auto& entry:CalmingAfterRear())if(entry.horse==horse)entry={};
}
inline void Rear(Ped horse,int action,int variant) {
    // Never an action that throws a rider off (2, 9, 10).
    if(action!=1 && action!=3 && action!=5 && action!=7 && action!=8)return;
    if(variant!=0 && variant!=1)return;
    if(horse && ENTITY::DOES_ENTITY_EXIST(horse) && !ENTITY::IS_ENTITY_DEAD(horse)) {
        // Remembered before the action, because the action is what raises it.
        const float agitation=PED::_GET_PED_MOTIVATION(horse,kAgitation,0);
        TASK::TASK_HORSE_ACTION(horse,action,0,variant);
        LastRearGameMs()=MISC::GET_GAME_TIMER();
        if(std::isfinite(agitation) && agitation>=0 && agitation<=1)
            for(auto& entry:CalmingAfterRear())
                if(!entry.horse){entry={horse,agitation,MISC::GET_GAME_TIMER()};break;}
    }
}
} // namespace horse_races::cues
