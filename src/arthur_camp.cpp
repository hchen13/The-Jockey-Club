// Arthur's camp at Horseshoe Overlook: the natives behind arthur_camp.h.
// Everything decided here is decided by the header - where a prop goes, when
// the camp is built or struck, which activity comes next - and this file only
// asks the game to do it and reports what happened.
#include "arthur_camp.h"
#include "runtime_paths.h"
#include "race_progression_runtime.h"
#include "file_stamp.h"
#include "arthur_camp_runtime.h"
#include "horse_individual_identity.h"
#include "horse_ledger_runtime.h"
#include "world/identity.h"
#include "data/arthur_camp_greetings.h"
#include "game_natives.h"
#include "game_clock.h"
#include "race_appearance.h"
#include "race_character_policy.h"
#include "race_firearm_guard.h"
#include "race_host_profiles.h"
#include "race_localization.h"
#include "race_system.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <array>
#include <chrono>
#include <bit>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace horse_races::arthur_camp {
namespace {
#include "arthur_camp_runtime/state.inl"

#include "arthur_camp_runtime/greetings.inl"
#include "arthur_camp_runtime/horse_ownership.inl"
#include "arthur_camp_runtime/race_offers.inl"

#include "arthur_camp_runtime/configuration_assets.inl"
#include "arthur_camp_runtime/scene_geometry.inl"
#include "arthur_camp_runtime/arthur_activities.inl"
#include "arthur_camp_runtime/mary_activities.inl"
#include "arthur_camp_runtime/map_lifecycle.inl"
#include "arthur_camp_runtime/wagon_interactions.inl"
#include "arthur_camp_runtime/horse_residents.inl"
#include "arthur_camp_runtime/construction.inl"
#include "arthur_camp_runtime/recovery.inl"
#include "arthur_camp_runtime/maintenance.inl"
} // namespace

void ObserveWorld(){SyncPreservedHorseWorld();}
void Initialize(const std::string& directory,LogFn log) {
    g_log=log;
    if(SyncPreservedHorseWorld() && g_phase!=Phase::Idle)Forget("loaded-world timeline changed before initialize");
    else Unload("initialize");
    g_greeted={};g_targeted=false;g_last_greeting={};
    g_path=runtime_paths::Resolve(directory).Config("ArthurCamp.ini").string();
    g_layout={};g_valid=false;g_issue.clear();g_observed.reset();g_parsed.clear();g_poll={};g_rebuild=false;
    g_retry_after={};g_presence=Presence::Absent;
    g_known.clear();g_known_index.clear();g_known_loaded=false;g_known_applied=false;g_markers_on=false;
    g_neutralised.clear();g_dressing_done=true;g_within_reach=false;
    g_return_dressing_stage=0;g_return_dressing_at={};
    g_f10_was_down=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
    ForgetRaceMarkers();
    PollLayout(true);
    Log(g_valid?"ready; the camp is built when John comes within "+std::to_string(static_cast<int>(g_layout.spawn_radius))+" m of the tent":
        "waiting for a captured layout");
}
// The map marker is the layout's, not the built camp's: it is on the map
// from the moment a save comes up for John, whether or not he has yet been
// near enough for the camp to be built (2026-09-17). And with it the two
// race markers, the stakes race on Arthur and the one-on-one on his horses,
// which the race system draws where the camp says. All go when the layout is
// not for this player.
bool LayoutForThisPlayer() {
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player) || !g_valid || !g_layout.enabled ||
        !progression::runtime::Camp())return false;
    const bool john=static_cast<unsigned>(ENTITY::GET_ENTITY_MODEL(player))==kJohnModel;
    return (!g_layout.only_for_john || john) && !NETWORK::NETWORK_IS_SESSION_ACTIVE();
}
void TickMarkers(bool wanted) {
    if(wanted && g_layout.map_marker && !g_blip) {
        const auto& t=g_layout.tent.position;
        g_blip=MAP::BLIP_ADD_FOR_COORDS(kBlipStyle,t.x,t.y,t.z);
        if(g_blip) {
            MAP::SET_BLIP_SPRITE(g_blip,Key("blip_camp"),true);
            if(const char* name=MISC::VAR_STRING(10,"LITERAL_STRING",i18n::Text("Arthur's camp","亚瑟的营地")))MAP::_SET_BLIP_NAME(g_blip,name);
            Log("map marker on");
        }
    } else if((!wanted || !g_layout.map_marker) && g_blip) {
        if(MAP::DOES_BLIP_EXIST(g_blip)){Blip b=g_blip;MAP::REMOVE_BLIP(&b);}
        g_blip=0;Log("map marker off");
    }
    if(wanted!=g_markers_on) {
        g_markers_on=wanted;
        const auto& t=g_layout.tent.position;const auto& h=g_layout.horses;
        horse_races::SetCampMarker("arthur-camp",wanted,t.x,t.y,t.z);
        horse_races::SetCampMarker("arthur-camp-duel",wanted,h.x,h.y,h.z);
        Log(wanted?"race markers on Arthur and on his horses":"race markers off");
    }
}
// What the sweep learned is put on for John's world and lifted for any other.
void TickKnown(bool wanted) {
    if(!g_known_loaded)LoadKnown();
    if(wanted && !g_known_applied)ApplyKnown();
    else if(!wanted && g_known_applied)ShowOutside(false);
}
void Tick(bool end_activity) {
    if(g_path.empty())return;
    const bool world_changed=SyncPreservedHorseWorld();
    if((world_changed || !PreservedHorseWorldCurrent()) && g_phase!=Phase::Idle) {
        Forget("loaded-world timeline changed or unavailable; old entity references discarded");return;
    }
    if(!PreservedHorseWorldCurrent())return;
    PollLayout(false);
    {
        const bool wanted=LayoutForThisPlayer();
        TickMarkers(wanted);
        TickKnown(wanted);
    }
    const bool f10=(GetAsyncKeyState(VK_F10)&0x8000)!=0;
    DWORD foreground_process=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground_process);
    const bool f10_pressed=foreground_process==GetCurrentProcessId() && f10 && !g_f10_was_down;
    g_f10_was_down=f10;
    if(HUD::IS_PAUSE_MENU_ACTIVE())return;
    const auto now=Clock::now();
    const Conditions c=Observe();
    switch(g_phase) {
    case Phase::Idle:
        g_rebuild=false;
        if(now<g_retry_after || !g_valid)return;
        // Assets stream while the player is still on the way in; only the
        // building itself waits for the tent to be off camera.
        if(Conditions ahead=c;(ahead.out_of_view=true,Decide(Presence::Absent,ahead,g_layout)==Action::Spawn)) {
            RequestAssets();g_phase=Phase::Loading;g_phase_at=now;g_view_wait_logged={};
            Log("player within "+std::to_string(static_cast<int>(c.distance))+" m; loading "+std::to_string(g_models.size())+" models");
        }
        return;
    case Phase::Loading: {
        if(g_rebuild || !c.eligible || c.distance>=g_layout.despawn_radius){Unload("loading abandoned");return;}
        std::string missing;
        if(!AssetsReady(missing)) {
            if(now-g_phase_at>20s){Log("assets did not load in 20 s ("+missing+"); retrying in 30 s");Unload("assets");g_retry_after=now+30s;}
            return;
        }
        // Waiting for the tent to be off camera is what keeps a camp from
        // appearing in front of the player on the way in. A save that comes
        // up with him already inside it is the other case: he stood in an
        // empty camp for 82 s looking at where it should be (2026-09-17).
        // Inside it, the camp is built at once and the pop-in is the lesser evil.
        if(!c.out_of_view && c.distance>kBuildAtOnceRadius) {
            if(g_view_wait_logged==Clock::time_point{}){g_view_wait_logged=now;Log("assets loaded; waiting for the tent to be off camera");}
            return;
        }
        if(!c.out_of_view)Log("player already inside the camp ("+std::to_string(static_cast<int>(c.distance))+" m); built in view");
        if(Build()){g_phase=Phase::Live;g_presence=Presence::Present;}
        else {Unload("build failed");g_retry_after=now+30s;}
        ReleaseAssets();
        return;
    }
    case Phase::Live:
        // A save load is the one thing that can put a different world under a
        // live camp, and it always takes Arthur with it. Nothing is deleted
        // then: the handles are the new world's (2026-09-16, the chapter 2
        // tent lost its phonograph, stove and hide rack to this).
        if(WorldReplaced()){Forget("a save came up; nothing this camp made is here any more");return;}
        if(f10_pressed || end_activity){Unload(end_activity?"F11 end/reset activity":"F10");g_retry_after=now+10s;return;}
        if(g_rebuild){Unload("layout changed; rebuilding");return;}
        if(!g_dressing_done && now>=g_dressing_at){
            g_dressing_done=true;HideDressing("at the build");
            g_within_reach=c.distance<=kReachRadius;
            // The first hide and pool pass can predate the IPL's furniture.
            // Arm the same near-camp settle sequence used on a later return;
            // otherwise a build made inside 600 m never crosses the return
            // boundary and late tables/tent frames stay visible indefinitely.
            if(g_within_reach){g_return_dressing_stage=1;g_return_dressing_at=now+2s;}
        }
        // A return is armed at the 600 m boundary, but its pool pass waits
        // until the camp's IPL dressing has streamed in closer to the player.
        if(const bool within=c.distance<=kReachRadius;within!=g_within_reach) {
            g_within_reach=within;
            if(within && g_dressing_done){g_return_dressing_stage=1;g_return_dressing_at=now+2s;}
            else if(!within){g_return_dressing_stage=0;g_hide_reissue_active=false;}
        }
        if(g_return_dressing_stage==1 && c.distance<=180.0F && now>=g_return_dressing_at) {
            ReissueHides();HideDressing("on returning after streaming");
            g_return_dressing_stage=2;g_return_dressing_at=now+2s;
        } else if(g_return_dressing_stage==2 && c.distance<=100.0F && now>=g_return_dressing_at) {
            HideDressing("return settle near tent");g_return_dressing_stage=0;
        }
        TickReissueHides();
        if(Decide(Presence::Present,c,g_layout)==Action::Despawn) {
            Unload(!c.eligible?"the player is no longer eligible (mission, session, or not John)":
                "player "+std::to_string(static_cast<int>(c.distance))+" m away");
            return;
        }
        // A camp far behind the player is left to itself: nothing
        // there needs tending, and a ped that far away answers no questions.
        if(c.distance<=kTendRadius){TickInteractions(now);Maintain(now);}
        else CancelHorseModels("player_away");
        return;
    }
}
std::vector<std::string> ActivityNames() {
    std::vector<std::string> names;
    for(const auto& s:g_layout.scenes)names.push_back(s.name);
    return names;
}
std::vector<std::string> MaryActivityNames() {
    std::vector<std::string> names;
    for(const auto& s:g_layout.companion_scenes)names.push_back(s.name);
    return names;
}
// She plays her way out of what she is doing and walks to the next, as Arthur does.
void AskMaryActivity(std::size_t index) {
    if(g_phase!=Phase::Live || index>=g_layout.companion_scenes.size() || !Exists(g_mary.ped))return;
    auto& m=g_mary;
    if(m.leaving && m.activity==index)return;
    if(PED::IS_PED_USING_ANY_SCENARIO(m.ped)) {
        PED::SET_PED_SHOULD_PLAY_NORMAL_SCENARIO_EXIT(m.ped);
        m.leaving=true;m.exit_by=Clock::now()+ExitPatience(
            g_layout.companion_scenes[std::min(m.activity,g_layout.companion_scenes.size()-1)].name);m.asked=index;
        Log("Mary: asked for "+g_layout.companion_scenes[index].name+"; leaving "+
            g_layout.companion_scenes[std::min(m.activity,g_layout.companion_scenes.size()-1)].name+" first");
        return;
    }
    StartMaryActivity(index,false,"asked for");
}
void AskActivity(std::size_t index) {
    if(g_phase!=Phase::Live || index>=g_layout.scenes.size())return;
    const auto now=Clock::now();
    if(g_asked && now-g_asked_at<kAskCooldown && (g_has_pending?g_pending:g_activity)==index)return;
    g_asked=true;g_asked_at=now;g_activity_attempts=0;
    AskActivityAt(index,now,"asked for");
}
bool Present(){return g_phase==Phase::Live;}
// The map's clutter was learned once and lives in ArthurCamp.sweep; nothing
// is scanned any more. What F11 can still do is read the file again, for a
// table that has been added to while the game runs.
bool SweepLearning(){return false;}
std::size_t KnownPieces(){return g_known.size();}
void RelearnSweep() {
    ShowOutside(false);g_known_loaded=false;
    Log("camp clutter: the sweep file will be read again");
}
void Shutdown(){Unload("shutdown");}
} // namespace horse_races::arthur_camp
