// Private driving state implementation; included by race_system.cpp.
const OriginalRaceVariant* g_variant = &kOriginalRaceVariants[2];
const OriginalRivalDefinition* g_rival = nullptr;
std::string g_route_name = "rehor_heartlands_a";
std::string g_shortcut_route_name = "rehor_heartlands_shortcut_a";
bool g_enabled = false;
bool g_show_start_markers = true;
bool g_show_random_race_world_map_markers = false; // F11 opt-in; map only, no extra radar launcher.
Clock::time_point g_encounter_marker_retry_at{};
bool g_fixed_race_enabled = false; // Initialize reads the installed/missing-key default.
const OriginalRaceVariant* g_fixed_return_variant = &kOriginalRaceVariants[2];
Vector3 g_fixed_spawn{};
// The staging point wait (see ClearFixedSpawnOccupants): when the point was first found occupied, and when the wait began.
Clock::time_point g_fixed_spawn_occupied_since{},g_fixed_spawn_wait_since{};
Vector3 g_fixed_previous_player{}, g_fixed_previous_rival{};
bool g_fixed_samples_ready = false;
Clock::time_point g_fixed_abandon_since{}, g_fixed_stall_since{}, g_fixed_last_task{};
Clock::time_point g_fixed_next_spawn_log{};
int g_fixed_route_retries = 0;
course::Path g_course;
course::Progress g_course_player,g_course_rival;
course::NativeGps g_course_gps;
native_follower::Follower g_competitive_follower;
live_overtake::Session g_host_avoidance;
race_pace::Plan g_pace_plan;
race_pace::Controller g_pace_controller;
race_pace::Command g_pace_command;
int g_rider_skill=100;
bool g_host_traffic_speed_owned=false;
bool g_continuous_route=true,g_continuous_attempted=false;
lap::Session g_laps;
boundary_warning::Effect g_boundary_warning;
circuit_scene::StreetScene g_circuit_scene;
circuit_start::Start g_circuit_start;
starter::Starter g_starter;
lap::NativeGuidance g_lap_guide;
bool g_guidance_early=false; // Route drawn at acceptance, before the blackout.
bool g_accept_line_queued=false; // Host's accepted/start line already queued.
int g_lap_guide_current=-1;
std::size_t g_course_target=0;
// A racer who has not covered this much course in this long has stopped
// racing, whatever the speedometer says.
constexpr float kRivalProgressMetres=15.0F;
constexpr int kRivalProgressSeconds=4;
// Stall history is diagnostic only. Ordinary recovery must never relocate a
// competitive rider backwards; the explicit trial redispatch is separate.
float g_fixed_stall_along=-1000;int g_fixed_stall_repeats=0;
bool g_host_burst_shown=false;   // The host's speed burst is currently shown on the minimap.
bool g_host_burst_earned=false;  // Actual gate eligibility; minimap display never controls far pace.
float g_fixed_progress_along=0;
Clock::time_point g_fixed_progress_at{};
NavigationTelemetry g_navigation_telemetry;
physical_readback::History g_host_physical_history;
bool g_host_physical_incident_emitted=false;
course::CommandPace g_navigation_pace;
float g_course_recovery_progress=0;
std::uint64_t g_course_serial=0;
std::uint64_t g_race_epoch=0; // A race identity, not the serial of its changing native route registrations.
catchup::State g_host_catchup;
bool HasRecordedCourse(){return !g_course.points.empty();}
std::optional<float> PlayerProgressGap(const course::Progress& rival,const circuit::Progress* laps=nullptr) {
    if(GetAutoSurveyState().running)return {}; // The parked spectator is not a competing rider.
    if(laps)return horse_motion::speed_learning::Gap(g_laps.player.Metres(g_laps.plan.course),
        laps->Metres(g_laps.plan.course),g_laps.player.total_laps>0 && laps->total_laps>0 &&
        !g_laps.player.continuity_lost && !g_laps.player.off_course &&
        !laps->continuity_lost && !laps->off_course);
    return horse_motion::speed_learning::Gap(g_course_player.current.along,rival.current.along,
        g_course_player.ready && rival.ready && g_course_player.current.valid && rival.current.valid &&
        !g_course_player.continuity_lost && !rival.continuity_lost &&
        g_course_player.current.distance<25 && rival.current.distance<25);
}
bool RivalTrafficCreeping(bool recorded,bool released,bool continuous,
    Clock::time_point progress_at,Clock::time_point now) {
    return recorded && released && !continuous && progress_at!=Clock::time_point{} &&
        now-progress_at>=std::chrono::seconds(kRivalProgressSeconds);
}
Hash g_opponent_original_horse_relationship=0;
// Game seconds: the shared GameClock as a double, for the lap engine and the
// native follower which take time as seconds rather than a time_point.
double CircuitNow(){return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();}
void Log(const std::string& message);
void LogBoundaryOffenses(const std::string& actor,Ped rider,const circuit::Progress& progress,int previous) {
    for(int count=previous+1;count<=progress.boundary.Offenses();++count)
        Log("boundary offense actor="+actor+" rider="+std::to_string(rider)+
            " count="+std::to_string(count)+" limit="+std::to_string(progress.boundary.Limit())+
            " continuous_outside_s="+std::to_string(boundary::kOutsideSeconds)+
            " terminal="+std::to_string(progress.off_course)+" along_m="+std::to_string(progress.Metres(g_laps.plan.course))+
            " game_ms="+std::to_string(MISC::GET_GAME_TIMER()));
}
void UpdateCircuitLapMarker();
void QueueAcceptedLine();
void ShowEarlyGuidance();
bool DrawCircuitGps()
{
    if(!g_laps.Running())return false;
    const auto& viewed=GetAutoSurveyState().running?g_laps.opponent:g_laps.player;
    const int current=g_laps.plan.Current(viewed);
    const bool changed=current!=g_lap_guide_current;
    const auto recovered=g_lap_guide.recoveries,repaired=g_lap_guide.repairs;
    if(!g_lap_guide.Draw(g_laps.plan,viewed,MISC::GET_HASH_KEY("COLOR_YELLOW"),
        "TJC_GUIDE_"+std::to_string(GetCurrentProcessId()),g_course_serial))return false;
    if(g_lap_guide.recoveries!=recovered)Log("circuit GPS missing channel restored from continuous race window; recovery="+
        std::to_string(g_lap_guide.recoveries)+"; lap qualification unchanged");
    if(g_lap_guide.repairs!=repaired)Log("circuit GPS recording no longer answered; restored as "+
        g_lap_guide.renderer.channel.name+" (repair "+std::to_string(g_lap_guide.repairs)+")");
    g_lap_guide_current=current;
    if(changed)UpdateCircuitLapMarker();
    return true;
}
// The judged corridor, drawn in the world (F11 "Course corridor"). The width is
// an instance parameter of the frozen plan: a station's [CorridorHalfWidth]
// override is picked up when the plan freezes and can be changed live from the
// page. Widening applies at once in every state; a narrowing made during a race
// is highlighted at once but judged only from the next race, because the sweep
// in circuit::Progress::Update would otherwise disqualify a rider standing
// between the old and the new edge on its very next sample.
int g_corridor_highlight=0;                      // [UI] CorridorHighlight: 0 off, 1 on (a fence along both borders).
std::optional<float> g_corridor_pending;         // Narrowed during a race: highlighted now, judged next race.
corridor_overlay::GroundCache g_corridor_ground; // One per frozen plan; reset with the route.
native_follower::RetryBudget g_follower_retries; // Host: bounded locally, renewable after 100 m of qualified progress.
native_follower::Recovery g_host_navigation;
constexpr int kFollowerRestarts=3;
// Canonical observations belong to the three recorded lap profiles. The
// frozen race exposes translated copies to the planner, learners and fence.
obstacle_map::Map g_obstacles;
obstacle_profiles::Bank g_obstacle_profiles;
std::array<obstacle_survey::Survey,3> g_online_surveys;
std::size_t g_online_survey_turn=0;
float g_recorded_line_half_width=14;
const char* CorridorStationKey();
extern Ped g_opponent_horse;
extern State g_state;
std::array<std::uint64_t,3> g_obstacles_saved_revisions{};
std::array<obstacle_storage::SnapshotBuilder,3> g_obstacle_snapshots;
std::size_t g_obstacle_snapshot_turn=0;
std::uint64_t g_obstacles_line_revision=0;      // Measurements consumed by the current racing lines.
std::array<bool,3> g_obstacles_writable{true,true,true},g_obstacles_backup_pending{};
bool g_obstacles_learned=false;                  // A collision narrowed the map since it was last saved.
collision_learner::Learner g_host_learner;       // The host's horse, driven on the line (race_collision_learner.h).
collision_learner::Learner g_player_learner;
std::map<Ped,collision_learner::Learner> g_entrant_learners;   // The field's, by horse.
passage_learner::Learner g_host_passage,g_player_passage;
std::map<Ped,passage_learner::Learner> g_entrant_passages;
Clock::time_point g_obstacles_next_save{};
// The user's own line where the computed one is not to be trusted
// (race_line_pins.h): read with the course from <courses>/<station>.pins.txt,
// projected onto the centreline in force, laid over every line built here.
line_pins::File g_line_pins_file;
std::vector<line_pins::Stretch> g_line_pins;
bool g_line_pins_required_refused=false;
bool g_recorded_reference_required=false;
bool RequiredRecordedLine() {return g_recorded_reference_required||g_line_pins_required_refused||line_pins::Required(g_line_pins);}
struct LinePinsStamp {std::string key;std::filesystem::file_time_type written{};std::uintmax_t size=0;float width=0;bool exists=false;};
LinePinsStamp g_line_pins_stamp;
Clock::time_point g_line_pins_checked{};
