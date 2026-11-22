// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Camp scene ownership, resident state and shared identity helpers.
using Clock=horse_time::GameClock;
using Wall=horse_time::WallClock;
using namespace std::chrono_literals;

LogFn g_log=nullptr;
void Log(const std::string& s){if(g_log)g_log("arthur camp: "+s);}

std::string g_path;
Layout g_layout;
bool g_valid=false;          // The layout is parsed and captured.
std::string g_issue;
std::optional<std::string> g_observed;
std::string g_parsed;
Wall::time_point g_poll{};
bool g_rebuild=false;

enum class Phase { Idle, Loading, Live };
Phase g_phase=Phase::Idle;
Clock::time_point g_phase_at{},g_retry_after{},g_view_wait_logged{};
// A handle alone is not proof: the engine hands the same number out again,
// and after a save load the number we kept pointed at something the chapter 2
// camp had just made. The model and the place it was put in are checked
// before anything is deleted (2026-09-16: the phonograph, the stove and the
// hide rack went missing from the chapter 2 tent, deleted by this camp's
// strike as the save came up).
struct Placed { int handle=0; std::string key; float z=0; Hash model=0; Point at; };
std::vector<Placed> g_objects;
std::vector<Hide> g_hides;              // The map pieces hidden by this build, removed with it.
std::size_t g_hide_reissue_next=0;
bool g_hide_reissue_active=false;
constexpr std::size_t kHideReissuePerFrame=60;
// A piece the map put down inside the camp that this camp does not want:
// the model and the place, which is how it is taken away (a hide of that
// model in a small sphere there) and how it is put back.
struct Unseen { Hash model=0; Point at; };
std::vector<Unseen> g_unseen;   // Taken away while the camp stands; given back when it is struck.
std::vector<Unseen> g_known;    // The whole table of them, read from ArthurCamp.sweep.
std::unordered_map<Hash,std::vector<Point>> g_known_index;   // ...by model.
constexpr float kUnseenRadius=2.0F;    // Round each piece taken away.
constexpr std::size_t kUnseenLimit=2000;
// What a model hide is good for here, found out the hard way (2026-09-17):
// it takes the map's static pieces - and keeps them away, issued before they
// stream in or after - and it does nothing at all to the camp's dressing,
// the chapter 2 props the requested map files bring back with Dutch's tent:
// some 220 of them stood through a hide issued every 100 ms. Nature is
// never touched: a bush is two models, near and far, and hiding one flickers.
//
// So the whole table goes on at once, before the player is near enough for
// any of it to stream in, and comes off when the camp is struck. The pass
// that learned it by walking the object pool every half second is gone with
// the learning: the map has been seen entire, and what it saw is in the file.
// Nothing here walks the world on a timer (C-07 A). The dressing those map
// files bring with them, which no hide reaches, is taken by one pass at the
// build instead - HideDressing below, made once and never made again.
// A piece hidden where it stands, and put back the same way when the camp is
// struck in the same world. Never deleted: the game keeps its own list of
// what a map file placed, and deleting from under it, then letting that file
// be unloaded and loaded again, was followed within seconds by a heap fault
// in the game, three times on 2026-09-18.
// A streamed-out prop's handle may be reused by a newly streamed-in prop.
// Keep its model and position as well: the handle alone is not ownership.
struct DressingIdentity { Hash model=0; Point at{}; };
std::map<int,DressingIdentity> g_neutralised;
bool Exists(int handle);
bool SameDressing(int handle,const DressingIdentity& id) {
    if(!Exists(handle) || ENTITY::IS_ENTITY_A_MISSION_ENTITY(handle) || ENTITY::IS_ENTITY_ATTACHED(handle) ||
        ENTITY::GET_ENTITY_MODEL(handle)!=id.model)return false;
    const auto p=ENTITY::GET_ENTITY_COORDS(handle,true,false);
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        std::hypot(p.x-id.at.x,p.y-id.at.y,p.z-id.at.z)<=kUnseenRadius;
}
void Neutralise(int handle) {
    ENTITY::SET_ENTITY_VISIBLE(handle,false);
    ENTITY::SET_ENTITY_COLLISION(handle,false,true);
    ENTITY::FREEZE_ENTITY_POSITION(handle,true);
}
void Restore(int handle) {
    ENTITY::SET_ENTITY_VISIBLE(handle,true);
    ENTITY::SET_ENTITY_COLLISION(handle,true,true);
    ENTITY::FREEZE_ENTITY_POSITION(handle,false);
}
// The one pass the camp makes over the world, and when it is due: the
// dressing a map file brings with it is not there in the frame the file is
// asked for. The log of 2026-09-22 has the files requested at 02:16:42 and
// the whole hundred standing a second later, so the pass waits out that
// second and runs once. It is never run again.
constexpr auto kDressingSettle=2s;
// A model hide does not survive the area streaming out and in again: on
// 2026-09-22 at 03:37:30 the player rode back to a camp that had stood the
// whole time and twenty-five of its pieces were up again. So the same pass
// is made once more on the crossing back into reach - the distance the
// larger pieces arrive from - and never while he is simply standing there.
constexpr float kReachRadius=600.0F;
bool g_within_reach=false;
Clock::time_point g_dressing_at{};
bool g_dressing_done=true;
// Entering the 600 m streaming band is too early to enumerate the map-file
// dressing: it is often not instantiated until much closer. One event-driven
// pass at 180 m, then one settle pass at 100 m, never a periodic pool scan.
int g_return_dressing_stage=0;
Clock::time_point g_return_dressing_at{};
bool CampPlaces(Hash model);   // Whether this camp places that model itself.
// What the sweep has learned is the map's, kept in ArthurCamp.sweep beside
// the ini and put back on the moment a save comes up for John: learned once,
// applied before the player is near enough to see anything arrive.
bool g_known_loaded=false,g_known_applied=false;
bool g_markers_on=false;
std::map<std::string,Pose> g_placed;   // Where each prop actually landed (ground-probed).
std::vector<int> g_seat_points;        // Scenario points on the seats, the player's to use.
std::map<std::string,int> g_seat_point_of; // ...and which prop each one rides on.
std::map<std::string,int> g_activity_point; // A sitting activity's own point on its anchor, by activity name.
std::vector<Hash> g_scenario_assets;       // Scenario types asked for, so their props (a cigar) are there.
std::vector<int> g_ipls;               // Map files this camp switched on.
Ped g_arthur=0;
// Mary Linton, who lives here with him (the user, 2026-09-21). A companion, not
// a second Arthur: no race offer, no recovery dance - a rotation of her own
// scenarios on seats he does not use, and she is made again if she is gone.
struct Companion {
    Ped ped=0;std::size_t activity=0;bool leaving=false,checked=false,targeted=false,walking=false;
    std::optional<std::size_t> asked;   // Chosen in F11; it wins over the rotation once she is out.
    int walk_seat=0;                    // The scenario point she is walking to a seat for.
    Pose walk_to{},walk_after_door{};
    bool walking_to_door=false;
    Clock::time_point until{},exit_by{},check_at{},walk_by{},remake_at{};
    std::map<std::string,int> points;   // A sitting activity's own point on its anchor.
} g_mary;
// A meadow slot outlives its current resident. A lost or claimed horse must
// not erase the slot, and a player-owned copy of the same coat is not it.
struct Horse {
    Ped ped=0; Pose stand; Hash model=0;
    std::size_t slot=0;
    std::uintptr_t address=0;
    Clock::time_point retry_after{},requested_at{},flee_recheck_after{};
    bool model_requested=false;
    individual_identity::Ownership ownership=individual_identity::Ownership::Unknown;
    individual_identity::OwnershipIssue ownership_issue=individual_identity::OwnershipIssue::None;
    bool guard_suspended=false,flee_checked=false,flee_blocked=false;
    std::string state;
};
std::vector<Horse> g_horses;
struct PreservedHorse {
    firearm_guard::Identity id{};
    Point at{};
    std::uint64_t world=0;
};
// Independent of the current scene: handoffs and unknown ownership can
// survive F10/layout/mission teardown and must still occupy their real spots.
std::vector<PreservedHorse> g_preserved_horses;
std::uint64_t g_horse_world=0;
bool g_horse_world_invalidated=false;
Blip g_blip=0;
// Whenever g_markers_on is set back to "off" without TickMarkers having done
// it, the race system must be told too: its stations outlive a save load, and
// a John save's two camp markers stayed on the map of the Arthur save loaded
// after it (2026-09-21) because TickMarkers only acts on a change.
void ForgetRaceMarkers() {
    horse_races::SetCampMarker("arthur-camp",false,0,0,0);
    horse_races::SetCampMarker("arthur-camp-duel",false,0,0,0);
}
std::vector<Hash> g_models;
bool CampPlaces(Hash model){return std::find(g_models.begin(),g_models.end(),model)!=g_models.end();}
int g_outfit_request=-1;
bool g_outfit_requested=false,g_outfit_given_up=false;
int g_outfit_waited=0;              // Frames the outfit has been waited for since its request.
constexpr int kOutfitPatienceFrames=600; // Ten seconds, then he is dressed by hash instead.
Clock::time_point g_where_check{};
std::size_t g_activity=0;            // Index into g_layout.scenes.
constexpr int kActivityAttempts=3;    // Different activities tried before he is held by the fire.
bool g_activity_placed=false;         // The current activity was started by placing him (no walk).
const char* ActivityName(std::size_t i){return i<g_layout.scenes.size()?g_layout.scenes[i].name.c_str():"?";}
Clock::time_point g_activity_started{},g_activity_until{},g_horse_check{},g_fast_horse_check{};
int g_activity_attempts=0;
bool g_activity_verified=false,g_f10_was_down=false;
// When the start of an activity is judged: a walk across the camp takes
// longer than the eight seconds a placement needs, and judging it too early
// placed him instead, which the player saw as a teleport (2026-09-16).
Clock::time_point g_verify_after{};
// Walking to an activity, and the pose it ends in. The scenario is started
// where he stands once he is there, with its own way in played, so the arms
// do not simply fold the moment he arrives (2026-09-16).
bool g_walking=false;
int g_walk_seat=0;     // The scenario point he is walking to a seat for, 0 when he is not.
Pose g_walk_to{},g_walk_after_door{};
bool g_walking_to_door=false;
Clock::time_point g_walk_by{};
constexpr auto kWalkPatience=45s;
constexpr float kArrived=1.3F;
// A scenario ends by itself - the cigar is finished and dropped - and he
// stood where it left him until the activity's own timer ran out. Once it is
// running it is watched, and three checks out of it start the next one.
Clock::time_point g_scenario_check{};
int g_scenario_missing=0;
bool g_activity_given_up=false;      // The fire fallback: he stands there until the next change.
constexpr int kSceneMissingChecks=2;
constexpr auto kSceneCheckEvery=1s;
// Arthur lost (killed, or streamed away): the camp stands and he is fetched back.
bool g_recovering=false;
Clock::time_point g_recover_since{};
// A fixed station whose host is Arthur puts its own Arthur at its host point,
// beside this camp. Two of him is one too many, so the camp's own steps out of
// sight - and comes back, at his activity, when the race is over.
bool g_stood_down=false;
// A change of activity plays the scenario's own way out - the cigar dropped,
// the legs swung off the bed, then standing - before the next one is asked
// for. Until that is over the next activity waits here.
std::size_t g_pending=0;
bool g_has_pending=false;
// An activity the player asked for by name is his to keep: it is never
// swapped for another when its scenario is slow to start, and a second ask
// within a moment of the first is the same press arriving twice.
bool g_asked=false;
Clock::time_point g_asked_at{};
constexpr auto kAskCooldown=1500ms;
Clock::time_point g_exit_by{};
// Long enough for the cigar to go on the ground and for him to come off the
// bed, short enough that a change asked for looks immediate. He does not
// always leave the scenario when asked to, so this is a deadline, not a wait.
constexpr auto kExitPatience=1500ms;
// Getting off a cot takes appreciably longer than leaving a standing or seated
// scenario. Clearing the task halfway through the bed's exit pushes the ped
// through the headboard and tent wall instead of letting the animation finish.
constexpr auto kBedExitPatience=8s;
auto ExitPatience(std::string_view activity) {
    return activity=="bed"?kBedExitPatience:kExitPatience;
}
Presence g_presence=Presence::Absent;

// Ped config flag (ePedScriptConfigFlags): Arthur's horses are not for the
// taking, so the mount prompt never comes; everything else about them - the
// lock-on, patting, brushing - is as with any camp horse.
constexpr int kBlockMountHorsePrompt=419;
constexpr Hash kBlipStyle=0xD792CF71;   // BLIP_STYLE_MINIGAME, as the stations use.
constexpr unsigned kJohnModel=0x00B69710U; // PLAYER_THREE
constexpr float kNearSpeck=160;
constexpr float kBuildAtOnceRadius=60;   // Inside this the camp is built in view rather than left empty.         // Beyond this the tent is a speck: building it is invisible anyway.
constexpr float kTendRadius=250;        // Arthur's activities are tended while the player is this close.
