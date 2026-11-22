// Private encounter progress implementation; included by race_system.cpp.

std::string g_directory;
runtime_paths::Paths g_paths;
horse_attributes::BackupCatalog g_attribute_backup;
bool g_fixed_greeting_focused = false;
Clock::time_point g_fixed_greeting_next{};
appearance::Session g_rider_appearance;
std::string g_ini_path;
opponent_difficulty::Rule g_planned_difficulty;
Clock::time_point g_difficulty_poll{};
// Three riders roam the world, and each has several places they can turn up.
// Only ONE of each rider's points is live at a time; at the others they are
// simply not there. A fresh set is drawn after every finished race, so the map
// is a handful of chances rather than eleven standing appointments.
constexpr std::size_t kEncounterRiders=3;
std::array<int,kEncounterRiders> g_live_encounter_points{-1,-1,-1};
std::size_t EncounterRiderIndex(RivalArchetype archetype) {
    return archetype==RivalArchetype::Confident?0:(archetype==RivalArchetype::Novice?1:2);
}
void Log(const std::string& message);
// Our riders share the native encounter locations. Only the actual permanent
// speed-reward BOOL authorizes takeover, not played/result/retry counters.
// Unknown/unsupported state always waits. The old EncountersWaitForNative=0
// escape hatch is intentionally no longer consulted (user, 2026-11-07).
namespace native_progress {
using Status=native_reward::Status;
using Probe=Status(*)();
Probe g_probe=native_reward::ReadNativeReward;
Probe g_first_probe=native_reward::ReadFirstNativeReward;
struct State {Status status=Status::Unreadable;Clock::time_point next{},checked{};std::string logged;} g_state;
bool HoldsOurEncounters() {
    auto& s=g_state;
    // Loading may precede ScriptHook's next Initialize. Drop any previous
    // save's success immediately and never inspect its transitional globals.
    if(DLC::GET_IS_LOADING_SCREEN_ACTIVE()){s={};return true;}
    const auto now=Clock::now();
    // Negative results retain the old low-frequency poll. Permission is cheap
    // to revalidate (one guarded cell) once per game tick, never a ten-second
    // cached unlock that can survive a changed save or an unreadable page.
    if(now>=s.next || (s.status==Status::Awarded && now!=s.checked)) {
        s.status=g_probe();s.checked=now;
        s.next=now+std::chrono::seconds(10);
    }
    const bool hold=s.status!=Status::Awarded;
    const std::string say=std::string(native_reward::Name(s.status))+
        (hold?"; MOD encounters and their markers wait":"; MOD encounters may appear")+
        "; read-only Global_40.f_1095.f_3140.f_1 (4276), verified 1491.50";
    if(say!=s.logged){s.logged=say;Log("encounter races: "+say);}
    return hold;
}
}
bool EncounterPointLive(const OriginalRaceVariant& variant) {
    if(native_progress::HoldsOurEncounters())return false;
    return g_live_encounter_points[EncounterRiderIndex(variant.archetype)]==variant.id;
}
const rules::RaceRules& CurrentRules();
void InvalidateRules(const char* why);
void RollEncounterPoints(const char* why) {
    std::string chosen;
    for(std::size_t rider=0;rider<kEncounterRiders;++rider) {
        std::vector<int> points;
        for(const auto& variant:kOriginalRaceVariants)
            if(EncounterRiderIndex(variant.archetype)==rider)points.push_back(variant.id);
        if(points.empty()){g_live_encounter_points[rider]=-1;continue;}
        const int roll=MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(points.size()));
        g_live_encounter_points[rider]=points[static_cast<std::size_t>(
            std::clamp(roll,0,static_cast<int>(points.size())-1))];
        chosen+=(chosen.empty()?"":",")+std::to_string(g_live_encounter_points[rider]);
    }
    Log(std::string("encounter points redrawn (")+why+"): live sites "+chosen);
}
