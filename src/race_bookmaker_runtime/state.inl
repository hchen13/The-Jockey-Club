// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Bookmaker session state, owned identities and common value helpers.
namespace bm=horse_races::bookmaking;
namespace tr=horse_races::i18n;
std::size_t active_venue=0;
const bm::Venue& Venue(){return bm::kVenues[active_venue];}
Vector3 Point(const bm::VenuePose& pose){return {pose.x,pose.y,pose.z};}
const char* RaceName(){return tr::Text(Venue().title,Venue().chinese);}
const char* StandingsName(){
    return std::string_view(Venue().key)=="macfarlane-wfa"?
        tr::Text("MACFARLANE'S WFA","麦克法伦平磅赛"):RaceName();
}
constexpr double kLoadTimeout=35.0;
constexpr double kRaceTimeout=1800.0;
constexpr std::size_t kRacers=bm::kExhibitionRacers;
using Clock=horse_time::GameClock;
enum class Phase {Idle,Menu,Loading,Preparing,Countdown,Racing,Results,ReturnFade};
struct Jockey {
    const hosts::Profile* profile=nullptr;
    std::string horse_model;
    Hash horse_hash=0,rider_hash=0;
    horse_attributes::Ranks natural_ranks{},ranks{};
    opponent_difficulty::Rule difficulty_rule;
    bm::odds::Prediction prediction;
    int win_odds=0;
    Ped rider=0,horse=0;
    race_horses::Identity horse_id{},rider_id{};
    bookmaker_blips::Dot dot;
    race_horses::Lease rank_lease;
    appearance::Session appearance;
    native_follower::Follower follower;
    live_overtake::Session avoidance;
    race_avoidance::Lease racing_assist;
    course::Path grid_line;
    std::vector<float> grid_reference;
    racing_line::Result personal_line;
    race_pace::Plan pace;
    race_pace::Dynamics dynamics;
    float line_utilization=1;
    float cornering=14;
    int grid_slot=-1;
    race_pace::Controller controller;
    race_pace::Command command;
    far_catchup::Runner catchup;
    horse_races::catchup::State chase;
    native_far_probe::Probe native_far_probe;
    double far_slow_since=0;
    BelowWalkingTelemetry below_walking;
    bool burst_active=false,burst_pending=false;
    bool burst_earned=false;
    double burst_expires=0;
    int last_burst_clip=-1;
    native_follower::RetryBudget retries;
    native_follower::Recovery navigation;
    int outfit=-1;
    double last_task_at=0;
    bool spawned=false,revealed=false,line_ready=false,start_lag_logged=false;
    std::string recording_out_reason;
    bool finish_goodbye_pending=false,finish_departing=false,finish_speed_overridden=false;
    collision_learner::Learner collision;
    int collision_profile=0;
    passage_learner::Learner passage;
    int passage_lap=-1;
    double finished_at=0,next_departure_retry=0;
    Vector3 finish_exit{};
    int spawn_attempts=0;
    double spawn_retry_after=0;
};
Phase phase=Phase::Idle;
std::string directory;
runtime_paths::Paths paths;
LogFn logger=nullptr;
Ped clerk=0,player_at_accept=0;
race_horses::Identity clerk_id{};
appearance::Assets clerk_assets{};
Clock::time_point clerk_created_at{},clerk_stable_since{},clerk_retry_after{},clerk_prompt_retry_after{};
bool clerk_revealed=false;
bm::ReceptionGreeting clerk_greeting;
Prompt watch_prompt=0,bet_prompt=0;
bookmaker_map::Markers markers;
bool mission_owned=false;
world::MenuTimeRequest menu_time;
int last_language=-1;
bool player_sabotaged=false,return_fade_owned=false;
Clock::time_point next_spawn{},next_marker{},next_cash{},next_log{};
bm::Menu menu;
bookmaker_shop::Catalog catalog;
double catalog_retry_until=0;
double catalog_diagnostic_at=0;
int catalog_diagnostics_left=0;
bm::AmountRepeat amount_repeat;
std::array<Prompt,4> stake_prompts{};
bm::SpectatorCameraPrompts<Prompt> camera_prompts;
Prompt leave_race_prompt=0;
bm::SpectatorLeaveHold leave_race_hold;
bm::SpectatorHudState spectator_hud;
bm::NativeStandingsHud native_standings;
std::string native_standings_status;
std::string native_standings_gate;
bool leader_focus_owned=false;
bool native_cinema_owned=false;
world::CameraBlackout camera_blackout;
bool camera_revealed=false;
bm::CameraDirector camera_director;
bm::RiderHorseInfo rider_horse_info;
bm::CameraHold aim_hold,cinematic_hold;
std::optional<std::size_t> camera_focus;
std::optional<std::size_t> camera_failed;
double camera_failed_at=0;
double camera_focus_at=0,camera_motion_since=0;
bm::Ticket ticket;
transaction_feed::CashReceipts cash_receipts;
bm::ExhibitionCountdown countdown;
starter::Starter race_starter;
bool starter_staged=false;
std::vector<Jockey> jockeys;
bm::SharedRanks shared;
bm::odds::Calibration odds_calibration;
bool odds_store_writable=true,learning_excluded=false,learning_done=false;
std::string odds_course_key;
bm::Field field;
lap::Definition definition;
lap::Plan plan;
circuit_scene::StreetScene scene;
obstacle_map::Map line_obstacles;
bm::QuoteObstacleEvidence obstacle_evidence;
std::array<obstacle_survey::Survey,3> online_surveys;
double obstacle_save_at=0;
double entered_at=0,go_at=0,return_at=0,sabotage_until=0;
double next_visibility_audit=0;
std::uint64_t token=0,recording_serial=0;
Vector3 return_point{};
float return_heading=0;
race_horses::Identity return_horse{};
bool return_prepared=false,return_release_pending=false;
double return_prepared_at=0;
int final_place=0;
bool candidate_bet=false;
bool bet_just_pressed=false,watch_just_pressed=false;
std::string current_notice;
double notice_until=0,player_throw_at=-100;
bool clerk_model_requested=false,menu_lock=false,spectator_lock=false,player_returned=false;
bool scene_started=false,actors_created=false,stand_ready=false,transaction_debited=false;
bool settle_requested=false;
bool result_catalog_opened=false;
double result_catalog_retry_at=0;
bool music_started=false;
bool far_simulation_enabled=true;
bool native_far_speed_enabled=true;
bool native_speed_table_enabled=false;
bool native_move_rate_trial_enabled=false;
bool native_vector_trial_enabled=false;
std::vector<Ped> scene_actors;
std::string abort_reason;
int pending_result_place=0;
int pending_result_credit=0;
bm::Finish pending_finish=bm::Finish::AccidentalVoid;

void Log(const std::string& line){if(logger)logger("bookmaker: "+line);}
bool Exists(Entity e){return e && ENTITY::DOES_ENTITY_EXIST(e);}
bool Same(race_horses::Identity id){return world::Still({id.horse,id.model,id.address});}
race_horses::Identity Identify(Ped ped){const auto id=world::Identify(ped);
    return {id.entity,id.model,id.address};}
int ReadRank(Ped ped,int index){return ATTRIBUTE::GET_ATTRIBUTE_BASE_RANK(ped,index);}
void WriteRank(Ped ped,int index,int value){ATTRIBUTE::SET_ATTRIBUTE_BASE_RANK(ped,index,value);}
double Now();
void Notice(const std::string& message){current_notice=message;notice_until=Now()+3;Log("notice: "+message);}
std::string Dollars(int cents){return "$"+std::to_string(cents/100)+"."+(cents%100<10?"0":"")+std::to_string(cents%100);}
bool Foreground(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
double Now(){return horse_races::GameSeconds();}
bool PlayerOkay(){const Ped p=PLAYER::PLAYER_PED_ID();return p && Exists(p) && !ENTITY::IS_ENTITY_DEAD(p) &&
    !NETWORK::NETWORK_IS_SESSION_ACTIVE();}
void ReleaseMission(){if(mission_owned){MISC::SET_MISSION_FLAG(false);mission_owned=false;}}
bool ClaimMission(){if(mission_owned)return true;if(MISC::GET_MISSION_FLAG())return false;
    MISC::SET_MISSION_FLAG(true);mission_owned=true;return true;}
void StopSlowdown(){menu_time.End();}
void StartSlowdown(){menu_time.Begin();}
void DeleteOwned(Ped& ped,race_horses::Identity& id){if(Same(id)){firearm_guard::Release(ped);PED::DELETE_PED(&ped);}ped=0;id={};}
void DeleteOwnedClerk(){if(Same(clerk_id)){Ped owned=clerk;PED::DELETE_PED(&owned);}
    clerk=0;clerk_id={};clerk_assets={};clerk_created_at=clerk_stable_since=clerk_prompt_retry_after={};clerk_revealed=false;clerk_greeting={};}
