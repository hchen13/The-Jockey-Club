// Private racing line implementation; included by race_system.cpp.

#include "../race_retirement.inl"
// ---- Runtime racing line ----------------------------------------------------
// Every rival is driven on a line built at race start from g_course and the
// corridor half-width in force (race_racing_line.h). The host's line and the
// knob live here, next to the roster runtime and the host driver that use
// them; the entrants' lines live on their Entrant.
racing_line::Result g_racing_line;   // The host's line; node i is g_course node i moved sideways.
terrain::Profile g_terrain_profile;
float g_host_terrain_caution=-1;
far_catchup::Runner g_host_far_runner;
bool g_far_catchup_enabled=false;
bool g_native_far_speed=true;
bool g_native_speed_table=false;
bool g_native_move_rate_trial=false;
bool g_native_vector_trial=false;
native_far_probe::Probe g_host_native_far_probe;
std::vector<native_far_probe::Probe> g_extra_native_far_probes;
std::uint64_t g_line_build_serial=0; // Diagnostic: a native-driver restart must not solve the same line again.
struct StartLines {
    bool active=false,ready=false;
    bool host_failed=false;
    std::size_t next=0; // 0: host; then one additional entrant per blackout frame.
    int percent=100;
    float margin_metres=tracking_margin::kDefaultMetres;
    bool gap_caps=false,continuous=true;
    float gap_crawl_mps=obstacle_learning::kCrawlMps;
    std::string error;
} g_start_lines;
// The host's racing line, as the mod computed it for this race, drawn on the
// ground in magenta around the viewed rider - the debugging view for the line the
// rivals are actually riding. Shown only while racing, like the fence.
void DrawRacingLineHighlight() {
    horse_diagnostics::Scope stage_cost("race: racing line highlight");
    g_racing_line_markers=0;
    if(g_racing_line_highlight<=0 || !CurrentRules().has_recorded_line || !HasRecordedCourse() || g_state!=State::Racing)return;
    if(HUD::IS_HUD_HIDDEN() || CAM::IS_SCREEN_FADED_OUT() || HUD::IS_PAUSE_MENU_ACTIVE())return;
    const auto& line=g_racing_line.path;
    if(line.points.size()<2)return;
    const bool trial=GetAutoSurveyState().running;
    const float along=g_racing_line.ToLine(g_laps.Frozen()?
        (trial?g_laps.opponent:g_laps.player).Metres(g_laps.plan.course):
        (trial?g_course_rival:g_course_player).current.along);
    const bool loop=destination::Distance(line.points.front(),line.points.back())<.1F;
    corridor_overlay::Style style;
    style.r=255;style.g=0;style.b=255;style.a=200;   // magenta
    style.rail_diameter_m=.25F;style.ground_probes_per_frame=32;style.pitch_sign=g_ground_line_pitch_sign;
    if(!g_racing_line_view.Matches(line) && !g_racing_line_view.Build(line,style.ground_step_m,loop))return;
    g_racing_line_markers=g_racing_line_view.Draw(along,style);
}
int g_racing_line_percent=100;       // [RaceAI] RacingLinePercent: 0 = centreline, 100 = full line.
float g_racing_line_margin=tracking_margin::kDefaultMetres; // Frozen for this race, including later obstacle/width rebuilds.
// [RaceAI] GapSpeedCaps: slow the plan where the line threads a gap narrower
// than 6 m (race_obstacle_learning.h). Off by default: a well-laid line cuts
// into and out of a gap at speed, and a tight bend inside one is the bend
// model's to slow for (2026-09-19, the user's call). Learning from a
// collision stays on either way.
bool g_gap_speed_caps=false;
float g_gap_crawl_mps=obstacle_learning::kCrawlMps;   // [RaceAI] GapCrawlKmh: the plan's floor where a gap leaves no room.
float HostLineAggression(){return static_cast<float>(std::clamp(g_racing_line_percent,0,100))/100.0F;}
// Build a rival's line for the corridor in force. Which line is a matter of
// the RULES, never of the station: every owned recorded line uses its native
// space at `aggression`; a free destination without a recorded course has no
// geometry to solve. Destination judging remains independent. An unusable
// centreline or unresolved confirmed obstruction cannot produce a valid line.
bool BuildRacingLine(const course::Path& centre,float aggression,racing_line::Result& out,
    const race_pace::Dynamics& dynamics=ReadRacePaceDynamics(),float caution=g_host_terrain_caution)
{
    ++g_line_build_serial;
    horse_diagnostics::Scope stage_cost("race: build racing line");
    racing_line::Params p;
    const bool corridor=CurrentRules().Hosted() && CurrentRules().has_recorded_line && HasRecordedCourse();
    if(corridor&&g_line_pins_required_refused) {
        out={};Log("pinned line: required recorded centreline projection refused; no alternate line");return false;
    }
    p.half_width=corridor?(g_laps.Frozen()?g_laps.plan.course.strict_half_width:g_recorded_line_half_width):0;
    p.margin=g_racing_line_margin;
    p.aggression=corridor?std::clamp(aggression,0.0F,1.0F):0;
    p.closed=g_laps.Frozen()&&g_laps.plan.single_loop;
    p.dynamics=dynamics;   // Model-time diagnostics; geometry does not depend on these estimates.
    // No survey bound for a corridor race is a fault, not a licence: the line
    // stays on the centreline (unsurveyed rows pin it there row by row anyway).
    p.obstacles=corridor && !g_obstacles.key.empty()?&g_obstacles:nullptr;
    if(p.obstacles&&g_laps.Frozen())p.obstacle_path_starts=&g_laps.plan.offsets;
    if(corridor && !p.obstacles)p.aggression=0;
    g_obstacles_line_revision=g_obstacles.revision;
    // The line is shaped node by node, and a recorded centreline runs 15-20 m
    // between nodes on the straights: at a tight bend that is a few straight
    // chords, and the line came out sharp and the rider steered hard. Where the
    // line may bend at all it is built on the same course with every segment
    // longer than the step below subdivided - every recorded node kept, its
    // heights and endpoints with it - so it has nodes to bend on. The obstacle
    // rows are read by distance, so they still apply. Until 2026-09-20
    // course::Path held 1024 nodes at most and this fell back, silently, to the
    // recorded nodes on every course over 4 km - New Austin, the Veteran trail,
    // the camp duel's 656 nodes 17 m apart - and the line bent only at those and
    // came out sharp (the lap races, whose sections are short, always had the
    // dense line). course::kMaxNodes; the follower rides the long line a chunk
    // at a time (native_follower::kChunkNodes).
    //
    // The step was 4 m until 2026-09-22, when the stage was measured at 609 ms
    // in one frame. Eight is both faster and BETTER, on all three long courses
    // (tests/race_racing_line_tests.cpp, the Paint course with the survey the
    // user rode for it):
    //     Paint       4 m 2014 nodes 199 ms model 293.1 s | 8 m 1168 nodes  81 ms model 292.6 s
    //     New Austin  4 m 1247 nodes  84 ms model 226.6 s | 8 m  707 nodes  33 ms model 224.4 s
    //     Veteran     4 m 1478 nodes  95 ms model 268.5 s | 8 m  861 nodes  50 ms model 264.3 s
    // The solver spends a fixed iteration budget per pass, so twice the nodes is
    // half the convergence each: the 4 m line reached its cap (312 iterations
    // against 200) and came out slower than the 8 m one. A course thinned finely
    // enough already - the Paint course has no leg over 6 m - is untouched.
    course::Path dense;
    const course::Path* base=&centre;
    if(corridor && centre.points.size()>=2 && racing_line::Densify(centre,8.0F,dense) &&
        dense.points.size()>centre.points.size())base=&dense;
    const std::vector<line_pins::Stretch> no_pins;
    const line_pins::File no_pins_file;
    recorded_reference::Prepared prepared;
    if(!prepared.Prepare(centre,*base,p,corridor?CorridorStationKey():"",corridor?g_line_pins:no_pins,
        corridor?g_line_pins_file:no_pins_file)) {
        out={};Log("recorded reference refused: "+prepared.Status());return false;
    }
    base=&prepared.Path();p=prepared.Policy();const auto& pins=prepared.Pins();
    line_pins::Applied applied;
    if(!line_pins::Build(*base,p,pins,out,&applied)){out={};return false;}
    // Authored stretches and their approaches participate in the same solve.
    if(!pins.empty()) {
        char b[300];
        if(applied.ok)std::snprintf(b,sizeof b,"pinned line: %d recorded nodes applied by %s, %d offsets boxed to the corridor; max offset %.1f m",
            applied.pinned,p.Aggression()>0?"joint optimization":"recorded overlay",applied.clamped,static_cast<double>(applied.max_offset));
        else std::snprintf(b,sizeof b,"pinned line: NOT applied, the computed line stands: %s",applied.why.c_str());
        Log(b);
        if(applied.ok&&!applied.why.empty())Log("pinned line: "+applied.why);
    }
    if(corridor && caution>=.5F) {
        terrain::Profile profile;
        if(!profile.Build(*base) || !terrain::FollowRecorded(*base,p,profile,out)) {
            // FollowRecorded is an optional overlay and leaves out intact on
            // refusal. Keep the verified line instead of dropping to traffic.
            Log("terrain: recorded-line preference refused; keep verified computed line");
        }
        if(out.authored_constraints&&!line_pins::Matches(*base,p,pins,out)) {
            out.authored_constraints=false;
            Log("pinned line: terrain preference changed authored nodes; authored room-cap exemption removed");
        }
    }
    if(!line_pins::Matches(*base,p,pins,out,true)) {
        out={};Log("pinned line: required recorded centreline changed after overlay; line refused");return false;
    }
    if(!prepared.Finish(out)){Log("recorded reference refused: "+prepared.Status());return false;}
    if(prepared.Active())Log("recorded reference: "+prepared.Status()+"; "+std::to_string(prepared.CoreNodes().size())+
        " actual source points fixed, shoulders jointly optimized; canonical progress retained");
    return true;
}
// The per-race log's account of the final line and its validation/fallback.
// The pace plan on the host's line, slowed where the survey leaves that line
// little room (race_obstacle_learning.h).
void LogRoomCaps(const char* who,const std::vector<float>& caps,const std::vector<float>& along,float top_mps) {
    std::size_t capped=0;float slowest=top_mps;std::string where;int listed=0;
    for(std::size_t i=0;i<caps.size();++i)if(caps[i]<top_mps-.01F) {
        ++capped;slowest=std::min(slowest,caps[i]);
        if(listed<8 && i<along.size()){where+=(listed?",":"")+std::to_string(static_cast<int>(along[i]))+"m@"+std::to_string(static_cast<int>(caps[i]*3.6F));++listed;}
    }
    if(capped)Log(std::string("room caps: rider=")+who+" nodes_capped="+std::to_string(capped)+" of "+std::to_string(caps.size())+
        " slowest_kmh="+std::to_string(static_cast<int>(slowest*3.6F))+" first="+where+(capped>8?",...":"")+
        " (a gap narrower than "+std::to_string(static_cast<int>(2*obstacle_learning::kRoomFullMetres))+" m slows the plan)");
}
// The caps a line's plan is built with: the room caps when they are on,
// lifted inside a pinned stretch, where the recorded ride is the proof of
// passage. Empty when they are off, which is every cap the plan has.
std::vector<float> LineCaps(const racing_line::Result& line,const race_pace::Dynamics& dynamics) {
    if(!g_gap_speed_caps)return {};
    std::vector<float> room=obstacle_learning::RoomCaps(line,g_obstacles,dynamics.top_mps,g_gap_crawl_mps,&g_course);
    if(line.authored_constraints)for(std::size_t i=0;i<room.size() && i<line.reference.size();++i)
        if(line_pins::Covers(g_line_pins,line.reference[i]))room[i]=dynamics.top_mps;
    return room;
}
bool BuildHostPacePlan(const race_pace::Dynamics& dynamics) {
    g_host_navigation.passing.Reset();g_competitive_follower.RequestRouteOffset(0);
    std::vector<float> caps=LineCaps(g_racing_line,dynamics);
    const bool any=std::any_of(caps.begin(),caps.end(),[&](float c){return c<dynamics.top_mps-.01F;});
    if(!g_pace_plan.Build(g_racing_line.path,dynamics,g_racing_line.reference,any?&caps:nullptr,
        g_host_terrain_caution>=0?&g_terrain_profile:nullptr,g_host_terrain_caution) ||
        !g_host_avoidance.Reset(g_pace_plan.path,g_racing_line.reference))return false;
    if(any)LogRoomCaps("host",caps,g_racing_line.reference,dynamics.top_mps);
    if(g_host_terrain_caution>=0)Log("terrain plan: rider=host caution="+std::to_string(g_host_terrain_caution)+
        " recorded_nodes="+std::to_string(g_racing_line.pinned_nodes));
    return true;
}
bool HasRecordedCourse();
// A horse the mod drives lost most of its speed in a tick: what it ran into
// is learned (race_collision_learner.h) and kept for the next race. Do not
// rebuild the whole field during a race: even one solve per frame can stall
// several frames, and the still-mounted riders keep their current lines.
void LearnFromCollision(collision_learner::Learner& learner,Ped horse,Ped rider,const char* who,
    const circuit::Progress& progress,bool observe,bool* save_collision_evidence=nullptr) {
    // A probe in flight is read back even once the race is over: the fall
    // that ends a race is the one there is most to learn from.
    observe=observe && g_state==State::Racing;
    if(!observe)learner.ResetMotion();
    if(g_obstacles.key.empty() || !HasRecordedCourse()){learner.Cancel();return;}
    if(!observe && !learner.Busy())return;
    const int part=g_laps.Frozen()?g_laps.plan.Current(progress):-1;
    const float from=part>=0?g_laps.plan.offsets[part]:0;
    const float to=part>=0&&part+1<g_laps.plan.Total()?std::nextafter(g_laps.plan.offsets[part+1],-INFINITY):g_course.Length();
    // A result may return just after the rider crossed a lap line. Retain
    // the qualified part where that asynchronous collision probe began.
    const float evidence_from=learner.Busy()?learner.probe_from:from;
    const float evidence_to=learner.Busy()?learner.probe_to:to;
    std::string note;
    if(learner.Tick(g_obstacles,g_course,horse,CircuitNow(),&note,from,to,observe,rider)) {
        SyncObstacleProfileEvidence(evidence_from,evidence_to);
        g_obstacles_learned=true;
        Log(std::string("obstacle learned from a collision: rider=")+who+" path_part="+std::to_string(part)+" "+note+
            (g_state==State::Racing?"; current lines unchanged; evidence applies next race":""));
        if(save_collision_evidence)*save_collision_evidence=true;else SaveObstacles("obstacle learned");
        if(g_state!=State::Racing)
            // A late probe may arrive during preparation: preserve the
            // existing pre-GO invalidation so starting lines use new evidence.
            RebuildRacingLines("static collision replaced survey gap estimate");
    } else if(!note.empty())Log(std::string("collision probe: rider=")+who+" "+note);
}
// A delayed, grounded ride proves only the physical strip that this horse
// crossed. Keep it in this recorded profile and persist on the normal queue.
void LearnFromPassage(passage_learner::Learner& learner,Ped horse,const char* who,
    const circuit::Progress& progress,bool observe) {
    if(g_obstacles.key.empty() || !HasRecordedCourse()) {learner.Reset();return;}
    observe=observe && g_state==State::Racing && (!g_laps.Frozen()||(!progress.off_course && !progress.Finished()));
    const int part=g_laps.Frozen()?g_laps.plan.Current(progress):-1;
    const float from=part>=0?g_laps.plan.offsets[part]:0;
    const float to=part>=0&&part+1<g_laps.plan.Total()?std::nextafter(g_laps.plan.offsets[part+1],-INFINITY):g_course.Length();
    std::string note;
    if(learner.Tick(g_obstacles,g_course,horse,CircuitNow(),from,to,observe,&note)) {
        SyncObstacleProfileEvidence(from,to);g_obstacles_learned=true;
        if(learner.accepted==1 || learner.accepted%50==0)
            Log(std::string("obstacle learned from horse passage: rider=")+who+" path_part="+std::to_string(part)+" "+note);
    }
}
std::string RacingLineSummary(const racing_line::Result& line,float aggression)
{
    auto f=[](double v){std::ostringstream o;o<<std::fixed<<std::setprecision(2)<<v;return o.str();};
    return " racing_line_percent="+std::to_string(g_racing_line_percent)+" aggression="+f(aggression)+
        " tracking_margin_m="+f(g_racing_line_margin)+
        " max_offset_m="+f(line.max_offset)+" peak_curvature="+f(line.peak_curvature_before)+"->"+f(line.peak_curvature_after)+
        " length_m="+f(line.length_before)+"->"+f(line.length_after)+" ("+(line.length_after>=line.length_before?"+":"")+
        f(line.length_after-line.length_before)+") survey_coverage="+std::to_string(static_cast<int>(100*g_obstacles.Coverage()))+
        "% optimizer="+line.optimizer+" curvature_sweeps="+std::to_string(line.curvature_sweeps)+
        " seam_curvature_sweeps="+std::to_string(line.seam_curvature_sweeps)+
        " diagnostic=curvature_l4 quartic_passes="+std::to_string(line.accepted_passes)+
        " score="+std::to_string(line.objective_before)+"->"+std::to_string(line.objective_after)+"->"+std::to_string(line.objective_final)+
        " node_turn_deg="+f(racing_line::detail::PeakNodeAngleDegrees(line.path))+
        " model_s="+f(line.nominal_seconds_before)+"->"+f(line.nominal_seconds_after)+
        " recorded_fallback="+std::to_string(line.recorded_fallback)+
        " recorded_segments="+std::to_string(line.recorded_segments)+
        " obstacle_verified="+std::to_string(line.obstacle_verified)+
        " estimated_rows="+std::to_string(line.estimated_rows)+
        " estimated_links="+std::to_string(line.estimated_links)+
        " obstacle_constraints="+std::to_string(line.obstacle_constraints)+
        " verified="+std::to_string(line.verified)+" pinned_nodes="+std::to_string(line.pinned_nodes)+" build_ms="+f(line.milliseconds);
}
// Snapshot the bounded owned field once per game tick. Host and extras see
