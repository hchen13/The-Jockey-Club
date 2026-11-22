// Loaded-world width measurement, after actual rider/trial state is declared.
// Work stays on one original recorded profile and shares the 0.4 ms budget;
// a ridden centre axis never implies clearance on unvisited shoulders.
void TickObstacleSurvey() {
    TickObstacleSaves();
    if(g_obstacles.key.empty() || !HasRecordedCourse())return;
    horse_diagnostics::Scope stage_cost("race: online physical width");
    if(!HUD::IS_PAUSE_MENU_ACTIVE()) {
        const auto anchor_started=std::chrono::steady_clock::now();
        struct Anchor {Vector3 point;int profile;};std::vector<Anchor> physical;
        const double now=CircuitNow();
        const auto append=[&](const passage_learner::Learner& rider,const circuit::Progress& progress) {
            Vector3 at;if(!rider.PhysicalAnchor(now,at))return;
            const int part=g_laps.Frozen()?g_laps.plan.Current(progress):0;
            physical.push_back({at,g_laps.Frozen()?g_laps.plan.Profile(part):0});
        };
        if(!g_trial.active)append(g_player_passage,g_laps.player);
        append(g_host_passage,g_laps.opponent);
        if(!g_trial.active)for(const auto& e:g_extra_rivals) {
            const auto found=g_entrant_passages.find(e.horse.horse);
            if(found!=g_entrant_passages.end())append(found->second,e.progress);
        }
        const int profile=physical.empty()?0:physical[g_online_survey_turn++%physical.size()].profile;
        auto& survey=g_online_surveys[profile];
        auto& map=g_obstacle_profiles.station.empty()?g_obstacles:g_obstacle_profiles.maps[profile];
        const auto& path=g_obstacle_profiles.station.empty()?g_course:g_obstacle_profiles.paths[profile];
        const float width=g_laps.Frozen()?g_laps.plan.course.strict_half_width:g_recorded_line_half_width;
        std::vector<Vector3> anchors;for(const auto& at:physical)if(at.profile==profile)anchors.push_back(at.point);
        const auto before=map.revision;
        obstacle_survey::QueryQueue::Debit(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-anchor_started).count());
        const int changed=anchors.empty()?(g_trial.active?0:survey.TickNearPlayer(map,path,PLAYER::PLAYER_PED_ID(),width)):
            survey.Tick(map,path,anchors,obstacle_survey::kOnlineBudgetMs,width);
        if(changed) {
            const obstacle_survey::QueryQueue::Charge publish{std::chrono::steady_clock::now()};
            if(g_laps.Frozen()) {
                g_obstacle_profiles.NoteCanonicalRows(profile,survey.changed_rows,before);
                g_obstacle_profiles.Refresh(g_laps.plan,g_obstacles);
            }
            g_obstacles_learned=true;
            if(survey.rows==1||survey.rows%25==0)
                Log("online physical width: key="+map.key+" rows="+std::to_string(survey.rows)+
                    " budget_ms="+std::to_string(obstacle_survey::kFrameBudgetMs)+survey.QueryStats());
        }
    }
    if(Clock::now()>=g_obstacles_next_save){g_obstacles_next_save=Clock::now()+std::chrono::seconds(15);SaveObstacles("periodic");}
}
