// Private course evidence implementation; included by race_system.cpp.
std::string ObstacleFile(const obstacle_map::Map& map) {
    return native_corridor::EvidencePath(map,g_paths.Courses()).string();
}
void SaveObstacles(const char* why) {
    const auto count=g_obstacle_profiles.station.empty()?std::size_t{1}:g_obstacle_profiles.maps.size();
    for(std::size_t profile=0;profile<count;++profile) {
        const auto& map=g_obstacle_profiles.station.empty()?g_obstacles:g_obstacle_profiles.maps[profile];
        if(map.key.empty() || !g_obstacles_writable[profile] || !map.Surveyed())continue;
        obstacle_storage::Retry(ObstacleFile(map));
        if(map.revision==g_obstacles_saved_revisions[profile])continue;
        g_obstacle_snapshots[profile].Request(map,why);
    }
    g_obstacles_learned=false;
}
void TickObstacleSaves() {
    for(const auto& saved:obstacle_storage::DrainCompletions()) {
        Log(std::string("physical evidence ")+(saved.saved?"saved (":"save failed (")+saved.why+") key="+saved.key+
            " rows="+std::to_string(saved.rows)+" revision="+std::to_string(saved.revision)+
            " bytes="+std::to_string(saved.bytes)+" serialize_ms="+std::to_string(saved.serialize_ms)+
            " write_ms="+std::to_string(saved.write_ms)+" worker=1"+(saved.saved?"":"; previous file preserved, observations retained"));
        if(!saved.saved)continue;
        for(std::size_t profile=0;profile<3;++profile) {
            const auto& map=g_obstacle_profiles.station.empty()?g_obstacles:g_obstacle_profiles.maps[profile];
            if(map.key==saved.key&&map.evidence_epoch==saved.epoch) {
                g_obstacles_saved_revisions[profile]=saved.revision;g_obstacles_backup_pending[profile]=false;
            }
            if(g_obstacle_profiles.station.empty())break;
        }
    }
    const auto count=g_obstacle_profiles.station.empty()?std::size_t{1}:g_obstacle_profiles.maps.size();
    const auto profile=g_obstacle_snapshot_turn++%count;
    const auto& map=g_obstacle_profiles.station.empty()?g_obstacles:g_obstacle_profiles.maps[profile];
    horse_diagnostics::Scope cost("race: evidence snapshot");
    const obstacle_survey::QueryQueue::Charge charge{std::chrono::steady_clock::now()};
    if(auto snapshot=g_obstacle_snapshots[profile].Tick(map,obstacle_survey::QueryQueue::RemainingBudget(.1)))
        if(!obstacle_storage::Submit(ObstacleFile(map),std::move(snapshot),g_obstacles_backup_pending[profile]))
            Log("physical evidence queue unavailable or geometry ownership superseded key="+map.key+"; previous file preserved");
}
bool RetireObstacleSaves(const char* why) {
    const auto count=g_obstacle_profiles.station.empty()?std::size_t{1}:g_obstacle_profiles.maps.size();
    // Check the complete bank before moving any profile. An explicit scene
    // handoff may proceed only once every dirty map can reach the next owner.
    for(std::size_t profile=0;profile<count;++profile) {
        const auto& map=g_obstacle_profiles.station.empty()?g_obstacles:g_obstacle_profiles.maps[profile];
        if(map.key.empty()||map.revision==g_obstacles_saved_revisions[profile])continue;
        if(!g_obstacles_writable[profile]||!obstacle_storage::Owns(ObstacleFile(map),map.evidence_epoch)) {
            Log("physical evidence retirement refused; observations still owned by host key="+map.key);return false;
        }
    }
    bool complete=true;
    for(std::size_t profile=0;profile<count;++profile) {
        auto& map=g_obstacle_profiles.station.empty()?g_obstacles:g_obstacle_profiles.maps[profile];
        if(!map.key.empty()&&g_obstacles_writable[profile]&&map.revision!=g_obstacles_saved_revisions[profile]) {
            const auto target=ObstacleFile(map);
            if(!g_obstacle_snapshots[profile].Retire(target,map,g_obstacles_backup_pending[profile],why)) {
                complete=false;
                Log("physical evidence retirement queue unavailable; previous file preserved "+target);
            }
        }
    }
    if(complete)g_obstacle_snapshots={};return complete;
}
// Route learner mutations back to the single recorded profile they measured,
// then propagate regular-profile observations to its other race copies.
void SyncObstacleProfileEvidence(float from,float to) {
    if(!g_laps.Frozen())return;
    g_obstacle_profiles.Capture(g_laps.plan,g_obstacles,from,to);
    g_obstacle_profiles.Refresh(g_laps.plan,g_obstacles);
}
// Bind canonical maps once per recording. A changed random lap total only
// builds another view of those same observations, without rereading caches.
void BindObstacles() {
    const std::string key=CorridorStationKey();
    if(!g_laps.Frozen()) {
        // Destination rules may own a recorded line without a lap judge.
        // That one geometry is its canonical map; only lap races expand a
        // profile bank. The player's free route choice remains unchanged.
        if(!CurrentRules().Hosted()||!CurrentRules().has_recorded_line||!HasRecordedCourse())return;
        const auto next=obstacle_map::Map::KeyFor(key,g_course);
        if(g_obstacle_profiles.station.empty()&&g_obstacles.key==next&&
            g_obstacles.geometry_hash==obstacle_map::Map::GeometryHash(g_course))return;
        RetireObstacleSaves("recorded geometry released");g_obstacle_profiles={};
        if(!g_obstacles.Bind(key,g_course,obstacle_survey::kSurveyHalfWidth,obstacle_survey::kSurveyVersion))return;
        const auto loaded=native_corridor::Load(g_obstacles,g_course,
            g_paths.Courses());
        g_recorded_line_half_width=corridor_override::Effective(corridor_override::Read(g_ini_path,key.c_str()),14.0F);
        g_obstacles_writable[0]=loaded.evidence_writable;g_obstacles_backup_pending[0]=loaded.evidence_backup_pending;
        g_obstacles_saved_revisions[0]=g_obstacles.revision;g_obstacles_line_revision=0;
        g_obstacle_snapshots={};
        g_host_learner.Reset();g_player_learner.Reset();g_entrant_learners.clear();g_obstacles_learned=false;
        g_host_passage.Reset();g_player_passage.Reset();g_entrant_passages.clear();
        g_obstacles_next_save=Clock::now()+std::chrono::seconds(15);
        Log("native recorded destination bound: "+g_obstacles.key+" native="+std::to_string(loaded.native_loaded)+
            " evidence="+std::to_string(loaded.evidence_loaded)+" planning_half_width="+std::to_string(g_recorded_line_half_width)+
            (loaded.error.empty()?"":"; "+loaded.error));
        return;
    }
    if(!CurrentRules().corridor_dq)return;
    const bool same_profiles=g_obstacle_profiles.station==key && g_obstacle_profiles.Matches(g_laps.plan) &&
        g_obstacle_profiles.maps[0].width==obstacle_survey::kSurveyHalfWidth &&
        g_obstacle_profiles.maps[0].version==obstacle_survey::kSurveyVersion;
    if(same_profiles && g_obstacles.key==obstacle_map::Map::KeyFor(key,g_laps.plan.course.path) &&
        g_obstacles.length==g_laps.plan.course.path.Length())return;
    if(!same_profiles) {
        RetireObstacleSaves("recorded profiles released");
        std::array<std::string,3> previous_keys;
        std::array<std::uint64_t,3> previous_revisions{};
        for(std::size_t profile=0;profile<3;++profile) {
            previous_keys[profile]=g_obstacle_profiles.maps[profile].key;
            previous_revisions[profile]=g_obstacle_profiles.maps[profile].revision;
        }
        if(!g_obstacle_profiles.Bind(key,g_laps.plan,obstacle_survey::kSurveyHalfWidth,obstacle_survey::kSurveyVersion))return;
        for(std::size_t profile=0;profile<3;++profile) {
            auto& map=g_obstacle_profiles.maps[profile];
            // Rerecording one profile does not reload or discard the other
            // two, including dirty evidence whose save temporarily failed.
            if(previous_keys[profile]==map.key && previous_revisions[profile]==map.revision)continue;
            const auto loaded=native_corridor::Load(map,g_obstacle_profiles.paths[profile],
                g_paths.Courses());
            g_obstacles_writable[profile]=loaded.evidence_writable;
            g_obstacles_backup_pending[profile]=loaded.evidence_backup_pending;
            g_obstacles_saved_revisions[profile]=map.revision;
            g_obstacle_snapshots[profile]={};
            Log("native corridor bound: "+map.key+" profile="+std::to_string(profile)+
                " rows="+std::to_string(map.rows.size())+" surveyed="+std::to_string(map.Surveyed())+
                " native="+std::to_string(loaded.native_loaded)+" evidence="+std::to_string(loaded.evidence_loaded)+
                " geometry="+map.geometry_hash+(loaded.error.empty()?"":"; "+loaded.error));
        }
        g_obstacle_profiles.LoadSeams(g_paths.Courses()/"native");
    }
    if(!g_obstacle_profiles.Expand(g_laps.plan,g_obstacles))return;
    g_obstacles_line_revision=0;
    g_host_learner.Reset();g_player_learner.Reset();g_entrant_learners.clear();g_obstacles_learned=false;
    g_host_passage.Reset();g_player_passage.Reset();g_entrant_passages.clear();
    g_obstacles_next_save=Clock::now()+std::chrono::seconds(15);
    Log("native corridor expanded: "+g_obstacles.key+" rows="+std::to_string(g_obstacles.rows.size())+" surveyed="+std::to_string(g_obstacles.Surveyed())+
        " coverage="+std::to_string(static_cast<int>(100*g_obstacles.Coverage()))+"% persistent_profiles=3 geometry="+g_obstacles.geometry_hash);
}
std::string LinePinsFile() {
    return (g_paths.Courses()/(std::string(CorridorStationKey())+".pins.txt")).string();
}
// The pinned stretches for the course bound now. Asked with every bind - every
// tick while a camp race travels - so the file is looked at every two seconds
// and read again only when it, the course or the width changed; the user may
// drop a new file between races.
void BindLinePins() {
    if(!g_laps.Frozen() || !CurrentRules().corridor_dq){g_line_pins.clear();g_line_pins_file={};g_line_pins_stamp={};g_line_pins_required_refused=g_recorded_reference_required=false;return;}
    const auto now=Clock::now();
    const float width=g_laps.plan.course.strict_half_width;
    const std::string key=obstacle_map::Map::KeyFor(CorridorStationKey(),g_laps.plan.course.path);
    auto& st=g_line_pins_stamp;
    if(st.key==key && st.width==width && now-g_line_pins_checked<std::chrono::seconds(2))return;
    g_line_pins_checked=now;
    g_recorded_reference_required=recorded_reference::Find(CorridorStationKey(),g_laps.plan.course.path)!=nullptr;
    const std::string file=LinePinsFile();
    std::error_code ec;
    const bool exists=std::filesystem::exists(file,ec) && !ec;
    const auto written=exists?std::filesystem::last_write_time(file,ec):std::filesystem::file_time_type{};
    const std::uintmax_t size=exists?std::filesystem::file_size(file,ec):0;
    if(st.key==key && st.width==width && st.exists==exists && st.written==written && st.size==size)return;
    const bool had=!g_line_pins.empty();
    st={key,written,size,width,exists};
    g_line_pins.clear();g_line_pins_file={};g_line_pins_required_refused=false;
    if(!exists){if(had)Log("pinned line: "+file+" is gone; every line is computed again");return;}
    std::ifstream in(file,std::ios::binary);std::string text((std::istreambuf_iterator<char>(in)),{});
    std::string error;
    if(!g_line_pins_file.Parse(text,&error)){g_line_pins_required_refused=g_line_pins_file.HasRequired();Log("pinned line: "+file+" refused: "+error);return;}
    if(g_line_pins_file.station!=CorridorStationKey()){g_line_pins_required_refused=g_line_pins_file.HasRequired();Log("pinned line: "+file+" is for station '"+g_line_pins_file.station+"', not '"+CorridorStationKey()+"'; refused");return;}
    const auto projected=line_pins::Project(g_laps.plan.course.path,g_line_pins_file,width);
    g_line_pins=projected.stretches;
    g_line_pins_required_refused=projected.required_refused;
    for(const auto& r:projected.refused)Log("pinned line: stretch refused: "+r);
    std::string summary;
    for(const auto& s:g_line_pins) {
        char b[240];
        std::snprintf(b,sizeof b,"%s'%s' %.0f-%.0f m (%zu pins, max offset %.1f m)",summary.empty()?"":"; ",s.note.c_str(),static_cast<double>(s.from),static_cast<double>(s.to),s.pins.size(),static_cast<double>(s.max_offset));
        summary+=b;
    }
    Log("pinned line: "+std::to_string(g_line_pins.size())+" stretch(es) from "+file+(summary.empty()?"":": "+summary)+
        "; recorded nodes constrain the racing-line solve; approaches are optimized with them");
}
std::optional<float> g_corridor_override;        // The selected station's persisted override, cached.
std::string g_corridor_key;                      // The station key g_corridor_override was read for.
int g_corridor_markers=0;                        // Markers issued by the last draw; logs and tests.
int g_racing_line_highlight=0;                   // [UI] RacingLineHighlight: the host's computed line, magenta on the ground.
int g_racing_line_markers=0;
float g_ground_line_pitch_sign=1;                // [UI] GroundLinePitchSign: -1 if the line tilts against the slope in game.
corridor_overlay::GroundLine g_racing_line_view; // The host's line cut to the terrain; rebuilt when the line changes.
void DrawCorridorHighlight(bool racing) {
    horse_diagnostics::Scope stage_cost("race: corridor highlight");
    g_corridor_markers=0;
    if(g_corridor_highlight<=0 || !CurrentRules().corridor_dq || !g_laps.Frozen())return;
    if(HUD::IS_HUD_HIDDEN() || CAM::IS_SCREEN_FADED_OUT() || HUD::IS_PAUSE_MENU_ACTIVE())return;
    const auto& plan=g_laps.plan;
    float along=0;
    if(racing)along=(GetAutoSurveyState().running?g_laps.opponent:g_laps.player).Metres(plan.course);
    else {
        // Nearest point within the FIRST lap only, so an overlapping later copy
        // of the ring is never lit before the rider has qualified for it.
        const Ped player=PLAYER::PLAYER_PED_ID();
        if(!player || !ENTITY::DOES_ENTITY_EXIST(player))return;
        const float first_lap=plan.offsets.size()>1?plan.offsets[1]:plan.course.path.Length();
        const auto projected=course::Project(plan.course.path,ENTITY::GET_ENTITY_COORDS(player,true,false),0,first_lap);
        along=projected.valid?projected.along:0;
    }
    corridor_overlay::Style style;
    g_corridor_markers=corridor_overlay::Draw(plan.course,g_corridor_pending.value_or(plan.course.strict_half_width),
        along,style,&g_corridor_ground);
}
