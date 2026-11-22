#pragma once
#include "horse_native_speed_table.h"
#include "horse_native_move_rate.h"
#include "horse_native_vector.h"
#include "race_control_telemetry.h"
#include "race_physical_readback.h"

#include <string>
#include <cstdint>
#include <array>
#include <optional>
#include <vector>
#include "race_lap_view.h"

namespace horse_races {

struct OpponentCurveTarget {
    int horse{};
    int speed_level{5};
    int acceleration_level{5};
    bool active{};
};
// Whether the host bonus is being granted, reported only when it changes.
// Inferring it from a force histogram after the fact is what let a bonus
// that never once fired look like a tuning question.
// Seconds of GAME time, which does not advance while the game is paused. Every
// race deadline is measured on this; see the note at CircuitNow().
double GameSeconds();
// F11 opt-in for MOD-selected encounter anchors, never native event locations.
bool RandomRaceWorldMapMarkers();
// Persist before applying; false leaves the previous setting and markers intact.
bool SetRandomRaceWorldMapMarkers(bool enabled);
// Independent, read-only native rewards. Unreadable/unsupported/loading is Unknown.
enum class NativeRaceRewardState {Unknown, No, Yes};
struct NativeRaceRewardStatus {
    NativeRaceRewardState bonding=NativeRaceRewardState::Unknown;
    NativeRaceRewardState speed=NativeRaceRewardState::Unknown;
};
NativeRaceRewardStatus NativeRaceRewards();
// Native candidate diagnostics. Missing or unverified reads remain Unknown.
// Race cooldown units are native -90 deductions; the shared deadline uses runtime seconds.
struct NativeEncounterConditions {
    NativeRaceRewardStatus rewards{};
    NativeRaceRewardState daytime=NativeRaceRewardState::Unknown;
    NativeRaceRewardState mounted=NativeRaceRewardState::Unknown;
    NativeRaceRewardState bonding=NativeRaceRewardState::Unknown;
    NativeRaceRewardState horse_attributes=NativeRaceRewardState::Unknown;
    NativeRaceRewardState other_native_conditions=NativeRaceRewardState::Unknown;
    std::uint32_t native_block_reasons=0;
    bool native_reasons_unknown=true;
    std::optional<int> race_cooldown_steps;
    std::optional<int> shared_cooldown_seconds;
};
NativeEncounterConditions NativeEncounterState();
// Shared race-start snapshot for both hosted and bookmaker native-only trials.
bool ReadFarCatchUpSetting();
bool ReadNativeFarSpeedSetting();
bool ReadNativeSpeedTableSetting();
bool ReadNativeMoveRateTrialSetting();
bool ReadNativeVectorTrialSetting();
bool ReadNativeStandingsPortraitsSetting();
unsigned NativeStandingsResourceLoadMask();
// Temporary bridge for the spectator feature while actor/world ownership is
// consolidated: the existing race runtime is the only legacy creator/fader.
int SpawnBookmakerPed(unsigned model,float x,float y,float z,float heading);
int SpawnMarketObject(unsigned model,float x,float y,float z);
void BeginBookmakerAudio(const char* venue_key);
void EndBookmakerAudio(bool result_fade=false);
void BeginBookmakerReturnFade();
bool BookmakerReturnIsBlack();
bool BookmakerReturnIsVisible();
void EndBookmakerReturnFade();
// Scripted speech on the race's proven path (PLAY_PED_AMBIENT_SPEECH_NATIVE
// with the conversational parameter set), for the camp: Arthur greeting John.
bool PlayCampSpeech(int speaker,int listener,const char* speech_name,const char* voice_name,int variation=0);
// True while a fixed station whose host is Arthur is live: the camp stands
// its own Arthur down rather than show two of him beside one tent.
bool FixedHostIsArthurHere();
// The camp asks that the host's next race be ridden on this horse model: the
// one the player locked on to among Arthur's own. Spent by the next plan.
void AskHostHorse(const std::string& model_name);
// The player chose a race at the camp - on Arthur for his stakes race
// ("arthur-camp"), or on one of his horses for a one-on-one
// ("arthur-camp-duel", with that horse's model). The race runs from there as
// any other does; false if that station has no route file yet.
bool BeginCampRace(const char* station_key,const std::string& horse_model,bool practice,
    std::optional<float> horse_dirt={});
// One of the camp's own races is being set up or run: the camp keeps its
// Arthur and the chosen horse out of the way meanwhile.
bool CampRaceUnderway();
// Progress controls may close an idle offer, but cannot mutate an accepted,
// staged, running or unsettled race.
bool ProgressionBusy();
// Before a bookmaker quotation binds shared course evidence, retire an idle
// offer/ready map into the writer. Accepted races and unpaid results refuse.
bool PrepareBookmakerHandoff();
// An explicit F11 progress change may finish already-paid result presentation.
// Returns false without changing the race while any transaction/event is busy.
bool PrepareProgressionChange(bool other_activity_busy);
// Victory notices can appear while the opponent finishes or says goodbye.
// This must not relax the separate progress-edit / sale transaction guard.
bool ProgressionToastBusy();
// The words to put on a camp host's two prompts, in the form every fixed host
// uses - including "Practice (requires Thoroughbred)" when the player is not
// on one. The player's mount model decides that.
std::string CampPromptLabel(const char* station_key,bool practice,int player_mount_model);
// Whether the camp would take that press. The prompts are greyed out when it
// would not, the way every other host greys its own.
bool CampOfferReady(const char* station_key,int player_mount_model);
// True while the mission flag is this mod's own - a race being staged or run.
// The camp is not struck for it, as it is for the story's missions.
bool OwnsMissionFlag();
// Where a camp station's map marker belongs, or nowhere (nullptr). The station
// is on the map exactly while it has a place.
void SetCampMarker(const char* station_key,bool on,float x,float y,float z);
// The horse info card every host offers, opened by the camp on one of its own
// horses; the camp keeps it open until it closes it or the game does.
bool ShowCampHorseInfo(int horse,const std::string& model);
bool CampHorseInfoActive();
void CloseCampHorseInfo();
void LogHostBoost(bool active,float gait,float bar,float force,bool started);
// The player's stimulant burst began (seconds) or ended: a line on screen.
void PlayerBurstChanged(bool active,double seconds);
// The minimap's earned-burst colour, independent of the physical gate's start cue.
void HostBoostChanged(bool active);
// One true earned-gate start; the existing host dialogue owns playback/range.
void HostBoostStarted();
// Control qualification is independent of presentation. Awake notification
// withdraws any sleeping vector lease before the caller applies near motion.
void SetMotionBoostState(int horse,bool earned,bool awake);
void ResetCatchup();
void LogExemptionRefusal(const char* reason,std::size_t roster);
struct MotionContext {
    int horse=0, rider=0, site=-1, phase=0;
    std::uint64_t race_token=0;
    bool racing=false, present=false;
    // Town speed volumes are suspended for every actor in the race, for as long
    // as the race is running. Not a circuit feature: a race through Armadillo
    // or Valentine is decided by the horses, not by a street volume.
    bool town_speed_exempt=false;
    bool circuit_staging=false; // Owns stationary grid: no movement overrides until GO.
    bool npc_braking=false;
    bool npc_override_intent=false; // Verified cached race-owned active speed override.
    bool eliminated=false;
    bool catchup_active=false,catchup_engaged=false; // Qualified output vs retained pursuit state.
    float catchup_fraction=0,catchup_gap_m=0;
    bool catchup_gap_valid=false;
    // A field host's earned edge, as declared by the route. Zero for everyone
    // who is not the host of a field race, always. The rank and the curve
    // numbers are not carried here: they are read where they live.
    float host_level_bonus=0;
    // How long one burst may last before it has to be earned again. Zero
    // holds it for as long as the gallop does, which is Sadie's.
    float host_boost_seconds=0;
    // A timed burst's independent recovery; high-gait charge can overlap it.
    float host_boost_cooldown_seconds=0;
    // The race's own term of the bonus: racing and not braking for a bend.
    // Whether she is galloping flat out is read from the horse itself.
    bool host_boost_eligible=false;
    // Race/roster-owned presentation permission; movement qualification and
    // catch-up remain independent of which rider may show a red burst dot.
    bool real_burst_display_eligible=false;
};

// Observation only. No field here is consulted by route/speed control.
struct NavigationTelemetry {
    // Present only for a recent read of this race's exact host mount.
    std::optional<physical_readback::Sample> physical_readiness;
    bool valid=false,recorded=false,circuit=false,gate_recovery=false;
    bool player_continuity_lost=false,npc_continuity_lost=false;
    std::uint64_t command_serial=0;
    int command_reason=0; // 1=start, 2=advance, 3=stall recovery.
    int previous_target=-1,target=-1,candidate=-1,node_count=0;
    int decision=0; // 0=no check, 1=too far, 2=500ms cooldown, 3=same target, 4=changed, 5=regression suppressed.
    int driver_mode=0,skill_percent=100,native_waypoint_progress=-1; // 0=traffic,1=original recording,2=custom recording,3=traffic fallback.
    int native_entry_index=-1,native_fallback_reason=0;
    float native_effort_readback=0,pace_target_mps=0,pace_effort=3;
    bool native_absolute_speed=false;
    float native_requested_mps=0;
    horse_motion::native_speed_table::Observation native_table{};
    horse_motion::native_move_rate::Observation native_rate{};
    horse_motion::native_vector::Observation native_vector{};
    bool pace_braking=false;
    bool terrain_limited=false;
    bool following_limited=false;
    int following_horse=0;
    float following_gap_m=0;
    float terrain_caution=-1,terrain_height_error_m=0;
    int route_retries=0,player_lap=0,npc_lap=0,total_laps=0,npc_next_gate=-1,player_next_gate=-1;
    double command_age_s=0;
    std::array<float,3> goal{};
    float nav_speed_mps=0,player_along_m=0,npc_along_m=0;
    // The creep watchdog's own state. A recording could previously show a host
    // crawling and a route being reissued without showing what the watchdog
    // thought at the time, which is the difference between a slow horse and a
    // stale goal.
    bool creeping=false;
    float progress_anchor_m=0;
    double progress_age_s=0;
    float reference_speed_mps=0,nominal_lookahead_m=0,advance_threshold_m=0;
    float player_deviation_m=0,npc_deviation_m=0,course_length_m=0;
    // The runtime racing line (race_racing_line.h): the knob in force, the
    // line's largest planned offset, and at the host's along the planned
    // offset against the one actually ridden (signed, + = left of travel).
    int racing_line_percent=100;
    float line_max_offset_m=0,line_planned_offset_m=0,npc_line_offset_m=0;
    std::optional<float> remaining_to_target_m,target_adjacent_cos,next_gate_along_m,player_next_gate_along_m;
    drive_diagnostics::Snapshot driving;
    void Command(int node,std::array<float,3> destination,int reason) {
        previous_target=target;target=node;goal=destination;command_reason=reason;++command_serial;
    }
};

// One per field entrant, observation only: what the planner asked of the
// rider and which driver was carrying it out. The host has the same facts in
// NavigationTelemetry; without these a jockey pinned at a walking gait could
// be seen in a recording and never explained.
struct EntrantTelemetry {
    int index=-1;                   // Roster position; the HUD's "Jockey N" is index+1.
    int horse=0,rider=0;
    bool out=false;
    bool pace_braking=false;
    float pace_target_mps=0,pace_effort=3;
    bool terrain_limited=false;
    bool following_limited=false;
    int following_horse=0;
    float following_gap_m=0;
    float terrain_caution=-1,terrain_height_error_m=0;
    int driver_mode=0;              // NavigationTelemetry::driver_mode codes: 0 traffic, 2 native playback, 3 fallback.
    bool follower_control=false;
    int native_fallback_reason=0;   // NavigationTelemetry::native_fallback_reason codes.
    int native_waypoint_progress=-1,native_entry_index=-1;
    float native_effort_readback=0;
    bool native_absolute_speed=false;
    float native_requested_mps=0;
    horse_motion::native_speed_table::Observation native_table{};
    horse_motion::native_move_rate::Observation native_rate{};
    horse_motion::native_vector::Observation native_vector{};
    float along_m=0;
    int next_gate=-1;              // Ordered gate expected by the circuit adjudicator.
    float route_deviation_m=0;      // Distance to its qualified local segment.
    int restarts=0;                 // Native driver restarts attempted so far.
    double stalled_s=-1;            // Seconds below walking pace; -1 while moving.
    int traffic_target=0;           // Node the traffic fallback was last sent to; 0 = none yet.
    std::array<int,2> raw_target{}; // The locked quote: raw speed and acceleration ranks.
    float line_aggression=0;        // This rider's racing-line aggression (host's, scaled by its jitter).
    float line_max_offset_m=0;      // The largest planned offset of this rider's line.
    float line_offset_m=0;          // Signed lateral distance from the centreline now, + = left of travel.
    drive_diagnostics::Snapshot driving;
};

void Initialize(const std::string& directory);
// One-frame menu commands. The retained F10 key feeds the same end-activity
// path; menu focus suppresses outfit paging while its arrows select rows.
struct ControlInput {
    bool end_activity=false;
    bool toggle_showcase=false;
    bool menu_open=false;
};
void Tick(const ControlInput& input={});
OpponentCurveTarget GetOpponentCurveTarget();
MotionContext GetMotionContext();
std::vector<MotionContext> GetAdditionalMotionContexts();
std::vector<EntrantTelemetry> GetAdditionalEntrantTelemetry();
NavigationTelemetry GetNavigationTelemetry();
// [RaceAI] RacingLinePercent, 0..100: how much of the legal corridor the
// rivals' racing line uses (0 = the recorded centreline). Read at race start;
// setting it during a running race rebuilds every line live.
void SetRacingLinePercent(int percent);
int GetRacingLinePercent();
// Recompute every rival's line for the corridor width now in force and restart
// the live native drivers on it (creep-recovery / extra-restart paths). Called
// on a live width change; a no-op outside a running hosted race.
void RebuildRacingLines(const char* why);
// Takes one queued line per frame. RebuildRacingLines only queues them: building the host's line and
// every jockey's in one frame stopped the game for 609 ms (measured 2026-09-22).
void TickLineRebuild();
circuit::View GetLapView();
// The judged corridor of the selected station, for the F11 page. Nothing here
// names a station: applicable is rules::RaceRules::corridor_dq, the key is the
// station's slot in the table, and the width is the frozen plan's.
struct CorridorView {
    bool applicable=false;                // CurrentRules().corridor_dq; the page edits nothing otherwise.
    std::string station_key;              // The [CorridorHalfWidth] key this view belongs to.
    float route_default=0;                // The route's own TrackHalfWidth.
    std::optional<float> override_width;  // The persisted override, when the file has one.
    float effective=0;                    // The width drawn by the highlight (the pending narrowed width, if any).
    float judged=0;                       // The width the frozen plan judges by right now.
    bool racing=false;                    // A narrowing now waits for the next race.
    bool deferred=false;                  // A narrowed width is highlighted but not yet judged.
    int highlight=0;                      // 0 off, 1 edges, 2 fill.
};
// What the F11 page changed this frame; persisted by the page, applied here.
struct CorridorRequest {
    std::optional<int> highlight;         // Absent: unchanged.
    bool width_changed=false;
    std::optional<float> width;           // With width_changed: absent means the route default.
    std::optional<int> racing_line_percent; // [RaceAI] RacingLinePercent, the second difficulty knob; absent: unchanged.
    std::optional<int> racing_line_highlight; // [UI] RacingLineHighlight: draw the host's computed line on the ground; absent: unchanged.
};
CorridorView GetCorridorView();
void ApplyCorridorRequest(const CorridorRequest& request);
// F11 > Race debug > Auto survey: the armed trial ride. The state the row
// shows; the switch arms one, disarms it, or stops the one running.
struct AutoSurveyState {
    bool running=false;     // Host rides alone; the parked player watches through native follow.
    bool armed=false;       // The next race started is a trial.
    int riders=0,incidents=0;   // The trial's tally.
};
AutoSurveyState GetAutoSurveyState();
// Absent outside a trial; zero during an invalidated trial hides stale HUD
// data rather than falling back to the parked player's mount.
std::optional<Ped> GetTrialObservedHorse();
void ToggleAutoSurvey();

} // namespace horse_races
