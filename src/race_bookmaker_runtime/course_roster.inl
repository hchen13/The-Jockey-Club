// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Snapshot only the two verified barn-door models by the Strawberry track.
// World collision and the door state are left untouched. The temporary
// projection lives with this quote, never in the persisted obstacle evidence.
void ProjectStrawberryStableDoors() {
    if(std::string_view(Venue().key)!="strawberry-wfa")return;
    const Vector3 impact=bm::strawberry_doors::VerifiedRayHit();
    constexpr std::array<Hash,2> models{0x6438DB02U,0x70B0446CU};
    std::size_t found=0,projected=0;
    const auto project=[&](const std::vector<authored_obstacles::Post>& footprint,const std::string& source) {
        std::size_t changed=0;
        for(std::size_t profile=0;profile<obstacle_evidence.profiles.maps.size();++profile) {
            auto& map=obstacle_evidence.profiles.maps[profile];
            const auto result=bm::strawberry_doors::Project(map,obstacle_evidence.profiles.paths[profile],footprint);
            changed+=result.section_rows+result.link_rows;
            Log("Strawberry stable door "+source+" profile="+std::to_string(profile)+
                " changed_sections="+std::to_string(result.section_rows)+" changed_links="+std::to_string(result.link_rows));
        }
        return changed;
    };
    for(const Hash model:models) {
        const Object object=OBJECT::GET_CLOSEST_OBJECT_OF_TYPE(impact.x,impact.y,impact.z,10.0F,model,false,false,false);
        if(!object || !ENTITY::DOES_ENTITY_EXIST(object) ||
            static_cast<Hash>(ENTITY::GET_ENTITY_MODEL(object))!=model || ENTITY::GET_ENTITY_COLLISION_DISABLED(object)) {
            Log("Strawberry stable door snapshot unavailable model="+std::to_string(model));continue;
        }
        const auto origin=ENTITY::GET_ENTITY_COORDS(object,true,false);
        if(!std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z)||
            std::hypot(origin.x-impact.x,origin.y-impact.y)>10.0F || std::abs(origin.z-impact.z)>5.0F) {
            Log("Strawberry stable door outside expected location model="+std::to_string(model));continue;
        }
        ++found;
        Vector3 lo{},hi{};MISC::GET_MODEL_DIMENSIONS(model,&lo,&hi);
        if(!std::isfinite(lo.x)||!std::isfinite(lo.y)||!std::isfinite(lo.z)||
            !std::isfinite(hi.x)||!std::isfinite(hi.y)||!std::isfinite(hi.z)||
            hi.x<=lo.x||hi.y<=lo.y||hi.z<=lo.z||hi.z-lo.z>8.0F) {
            Log("Strawberry stable door invalid model dimensions model="+std::to_string(model));continue;
        }
        const auto bottom=ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(object,lo.x,lo.y,lo.z);
        const auto top=ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(object,lo.x,lo.y,hi.z);
        if(!std::isfinite(bottom.x)||!std::isfinite(bottom.y)||!std::isfinite(top.x)||!std::isfinite(top.y)||
            std::hypot(top.x-bottom.x,top.y-bottom.y)>.15F) {
            Log("Strawberry stable door tilted or invalid transform model="+std::to_string(model));continue;
        }
        const float z=lo.z+std::min(.9F,(hi.z-lo.z)*.5F);
        const std::array<Vector3,4> corners{{
            ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(object,lo.x,lo.y,z),
            ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(object,hi.x,lo.y,z),
            ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(object,lo.x,hi.y,z),
            ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(object,hi.x,hi.y,z)}};
        const auto footprint=bm::strawberry_doors::Footprint(corners);
        if(footprint.empty()){Log("Strawberry stable door invalid footprint model="+std::to_string(model));continue;}
        const auto changed=project(footprint,"model="+std::to_string(model));
        if(changed)++projected;
        Log("Strawberry stable door footprint model="+std::to_string(model)+" entity="+std::to_string(object)+
            " origin="+std::to_string(origin.x)+","+std::to_string(origin.y)+","+std::to_string(origin.z)+
            " discs="+std::to_string(footprint.size())+" changed_rows="+std::to_string(changed));
    }
    // A loaded door can swing after quotation. Always preserve the bounded
    // previously observed ray-hit location, even if today's pose is closed.
    // It is not asserted to be the physical collision point or the entire
    // swing arc, and does not invent geometry for the other door.
    const auto changed=project(bm::strawberry_doors::RayHitFallback(),"observed-ray-hit");
    Log("Strawberry stable door observed-ray-hit safeguard changed_rows="+std::to_string(changed)+
        "; absent/unmapped leaf geometry remains unconfirmed");
    Log("Strawberry stable door snapshot found="+std::to_string(found)+" projected="+std::to_string(projected)+
        "/2; unobserved doors keep existing native/impact evidence");
}
// Obstacle profile lifecycle, frozen circuit loading and roster choice.
auto ReportObstacleSave(const char* why){
    std::array<std::string,3> descriptions;
    for(std::size_t profile=0;profile<descriptions.size();++profile) {
        const auto& map=obstacle_evidence.profiles.maps[profile];
        descriptions[profile]="key="+map.key+" rows="+std::to_string(map.rows.size())+" revision="+std::to_string(map.revision);
    }
    return [why,descriptions=std::move(descriptions)](std::size_t profile,bool queued){
        Log(std::string("physical evidence save ")+(queued?"requested (":"queue unavailable (")+why+") "+descriptions[profile]);
    };
}
void SaveObstacleProfiles(const char* why){
    obstacle_evidence.Save(ReportObstacleSave(why));
}
void ReleaseObstacleProfiles(const char* why){
    if(!obstacle_evidence.Release(ReportObstacleSave(why)))
        Log("physical evidence queue unavailable on release; previous files preserved; retained writer snapshots remain available to a fresh binding");
    line_obstacles={};online_surveys={};obstacle_save_at=0;
}
bool BindObstacleProfiles(const lap::Plan& course){
    // Called once for each new quotation. A prior idle preview or failed save
    // must never replace evidence another mode wrote between shows.
    if(!obstacle_evidence.LoadFresh(Venue().key,course,
        paths.Courses(),
        obstacle_survey::kSurveyHalfWidth,obstacle_survey::kSurveyVersion))return false;
    obstacle_save_at=Now()+15;
    for(std::size_t profile=0;profile<3;++profile){
        const auto& map=obstacle_evidence.profiles.maps[profile];
        const auto& loaded=obstacle_evidence.loaded[profile];
        Log("native corridor loaded key="+map.key+" rows="+std::to_string(map.Surveyed())+
            " native="+std::to_string(loaded.native_loaded)+" evidence="+std::to_string(loaded.evidence_loaded)+
            (loaded.error.empty()?"":"; "+loaded.error));
    }
    return true;
}
// Read pending collision probes even after a fall or during final results.
// The next quotation consumes the learned geometry; neither attributes,
// standing order nor the quoted personal line is rerolled under the blackout.
void TickObstacleProfiles(bool collisions){
    auto& profiles=obstacle_evidence.profiles;
    if(profiles.station!=Venue().key || !PlayerOkay())return;
    const double now=Now();
    bool learned=false;
    if(collisions && field.Riders().size()==jockeys.size())for(std::size_t index=0;index<jockeys.size();++index){
        auto& jockey=jockeys[index];const auto& rider=field.Riders()[index];
        if(rider.status!=bm::Field::Status::Racing && !jockey.collision.Busy()) {jockey.passage.Break();continue;}
        if(!Same(jockey.horse_id)){jockey.collision.Reset();jockey.passage.Reset();continue;}
        // Off-screen position advancement is not simulated impact physics.
        // Do not teach a wall from a handover's artificial velocity change.
        const bool observe=rider.status==bm::Field::Status::Racing && !jockey.catchup.active &&
            !HUD::IS_PAUSE_MENU_ACTIVE() && Foreground();
        const int lap=plan.Current(rider.progress),profile=plan.Profile(lap);
        // Even two consecutive regular copies have a real qualification seam.
        // A delayed trail must never certify a bridge across that boundary.
        if(jockey.passage_lap!=lap) {jockey.passage.Break();jockey.passage_lap=lap;}
        if(!observe){jockey.collision.ResetMotion();if(!jockey.collision.Busy()){jockey.passage.Break();continue;}}
        if(profile!=jockey.collision_profile && !jockey.collision.Busy()){
            jockey.collision.Reset();jockey.collision_profile=profile;
        }
        // Progress is from the previous field update. Keep enough room for
        // the short physical sweep while excluding a remote crossing of the
        // same shape. Never reacquire globally when qualified progress is lost.
        constexpr float impact_window=25.0F;
        const float local=rider.progress.Metres(plan.course)-plan.offsets[lap];
        const auto length=profiles.paths[profile].Length();
        const bool collision_observe=observe && rider.progress.total_laps>0 &&
            !rider.progress.continuity_lost && !rider.progress.off_course && !rider.progress.boundary.Outside() &&
            std::isfinite(local) && local>=-impact_window && local<=length+impact_window;
        const float anchor=std::clamp(local,0.0F,length);
        std::string note;
        const bool collision_changed=jockey.collision.Tick(profiles.maps[jockey.collision_profile],
            profiles.paths[jockey.collision_profile],jockey.horse,now,&note,
            std::max(0.0F,anchor-impact_window),std::min(length,anchor+impact_window),collision_observe,jockey.rider);
        const int shared=jockey.collision.confirmed_hit?obstacle_profiles::TransferConfirmedImpact(
            profiles,jockey.collision_profile,*jockey.collision.confirmed_hit,obstacle_survey::kShoulderMetres,
            jockey.collision.probe_from,jockey.collision.probe_to):0;
        if(collision_changed || shared) {
            learned=true;Log(std::string("obstacle learned rider=")+jockey.profile->key+
                " profile="+std::to_string(jockey.collision_profile)+" shared_rows="+std::to_string(shared)+" "+note+"; next show's lines use it");
        }else if(!note.empty())Log(std::string("collision probe rider=")+jockey.profile->key+" "+note);
        if(!observe)jockey.collision.ResetMotion();
        std::string passage_note;
        if(jockey.passage.Tick(profiles.maps[profile],profiles.paths[profile],jockey.horse,now,
            0,profiles.paths[profile].Length(),observe && !rider.progress.off_course && !jockey.collision.Busy(),&passage_note)) {
            if(jockey.passage.accepted==1 || jockey.passage.accepted%50==0)
                Log(std::string("obstacle learned from horse passage rider=")+jockey.profile->key+
                    " profile="+std::to_string(profile)+" "+passage_note+"; next show's lines use it");
        }
    }
    {
        const obstacle_survey::QueryQueue::Charge charge{std::chrono::steady_clock::now()};
        obstacle_evidence.Pump(obstacle_survey::QueryQueue::RemainingBudget(.1));
    }
    if(!HUD::IS_PAUSE_MENU_ACTIVE()&&Foreground()) {
        // The player is parked during the spectator camera. Reuse its actual
        // followed horse's fresh physical sample; sleeping/far horses supply
        // no anchor and no clearance is inferred from their coordinates.
        std::optional<std::size_t> selected=camera_focus;
        Vector3 anchor;
        if(!selected||*selected>=jockeys.size()||!jockeys[*selected].passage.PhysicalAnchor(now,anchor)) {
            selected.reset();
            for(std::size_t i=0;i<jockeys.size();++i)if(jockeys[i].passage.PhysicalAnchor(now,anchor)){selected=i;break;}
        }
        if(selected&&*selected<field.Riders().size()) {
            const int profile=plan.Profile(plan.Current(field.Riders()[*selected].progress));
            auto& survey=online_surveys[profile];
            if(survey.Tick(profiles.maps[profile],profiles.paths[profile],{anchor},obstacle_survey::kOnlineBudgetMs,plan.course.strict_half_width)) {
                if(survey.rows==1||survey.rows%25==0)
                    Log("online physical width key="+profiles.maps[profile].key+" rows="+std::to_string(survey.rows)+survey.QueryStats()+"; next show's lines use it");
            }
        }
    }
    if(learned || now>=obstacle_save_at){
        obstacle_save_at=now+15;SaveObstacleProfiles(learned?"collision learned":"periodic");
    }
}
bool LoadCircuit(){
    const auto path=paths.Config(Venue().route_file);
    std::ifstream input(path,std::ios::binary);
    if(!input){Log("route missing: "+path.string());return false;}
    std::string bytes(std::istreambuf_iterator<char>{input},{}),error;
    const auto route=route_config::Parse(bytes,error);
    if(!route || !route->circuit){Log("route invalid: "+error);return false;}
    definition=*route->circuit;
    bm::ApplyLapPolicy(Venue(),definition);
    const int extra=definition.extra_min==definition.extra_max?definition.extra_min:
        MISC::GET_RANDOM_INT_IN_RANGE(definition.extra_min,definition.extra_max+1);
    if(definition.spectators!=Venue().crowd || !plan.Build(definition,extra)){
        Log(std::string(Venue().key)+" needs a valid recorded circuit with its configured lap/crowd policy");return false;
    }
    Log(std::string(Venue().key)+" laps locked before quotation: "+std::to_string(plan.Total())+
        " (regular="+std::to_string(extra)+")");
    if(!BindObstacleProfiles(plan) || !line_obstacles.Bind(Venue().key,plan.course.path,
        obstacle_survey::kSurveyHalfWidth,obstacle_survey::kSurveyVersion))return false;
    ProjectStrawberryStableDoors();
    if(!obstacle_evidence.profiles.Expand(plan,line_obstacles))return false;
    Log(std::string(Venue().key)+" personal-line survey="+std::to_string(line_obstacles.Surveyed())+
        "/"+std::to_string(line_obstacles.rows.size())+" rows");
    std::ifstream config(paths.MainConfig(),std::ios::binary);
    const std::string config_bytes(std::istreambuf_iterator<char>{config},{});
    // Changing route, local tuning or the motion/sample policy seeds a new
    // history instead of importing observations from a different ruleset.
    // The payout curve is deliberately NOT part of the observation key.
    odds_course_key=bm::odds::Key(std::string("motion-20261004/host-bands-min6/odds-")+
        std::to_string(bm::odds::kPolicyVersion)+"/"+bytes+"/"+config_bytes);
    return true;
}
bool ChooseRoster(){
    const Ped player=PLAYER::PLAYER_PED_ID();
    const Hash model=ENTITY::GET_ENTITY_MODEL(player);
    const auto protagonist=world::ProtagonistOf(model)==world::Protagonist::Arthur?characters::Id::Arthur:
        world::ProtagonistOf(model)==world::Protagonist::John?characters::Id::John:characters::Id::OtherRival;
    const auto random=[](int minimum,int maximum){return MISC::GET_RANDOM_INT_IN_RANGE(minimum,maximum);};
    const auto roster=bm::SelectRoster(protagonist,Venue().entrants,random);
    if(roster.size()!=Venue().entrants){Log("protagonist not Arthur or John; exhibition unavailable");return false;}
    jockeys.clear();jockeys.reserve(roster.size());learning_excluded=learning_done=false;
    const auto ini=(paths.MainConfig()).string();
    const float shared_cornering=opponent_difficulty::SharedCornering(ini);
    // One quotation owns all personal lines. F11/INI changes apply to the
    // next quotation, never to part of this roster or its accepted ticket.
    const float tracking_reserve=tracking_margin::Read(ini);
    Log("personal-line settings locked: tracking_margin_m="+std::to_string(tracking_reserve));
    const auto catalog=horse_catalog::runtime::Read(directory,[](const std::string& line){Log(line);});
    const int own_percent=opponent_difficulty::OwnHorsePercent(ini,host_horses::kOwnHorseChancePercent);
    for(const auto id:roster){
        const auto* profile=hosts::Find(id);if(!profile)return false;
        Jockey jockey;jockey.profile=profile;jockey.rider_hash=MISC::GET_HASH_KEY(profile->model);
        jockey.cornering=shared_cornering;
        if(const auto index=opponent_difficulty::Find(profile->key)){
            jockey.difficulty_rule=opponent_difficulty::Read(ini,*index);
            if(jockey.difficulty_rule.cornering>0)
                jockey.cornering=static_cast<float>(jockey.difficulty_rule.cornering);
        }else {Log(std::string("host difficulty profile missing: ")+profile->key);return false;}
        const auto pool=host_horses::ChoosePool(id,catalog,0,own_percent,random);
        const auto& options=pool.choices;
        if(options.empty() || !STREAMING::IS_MODEL_VALID(jockey.rider_hash)){
            Log(std::string("no metadata-ready eligible mount/rider for ")+profile->key);return false;
        }
        const int draw=random(0,static_cast<int>(options.size()));
        if(draw<0 || static_cast<std::size_t>(draw)>=options.size())return false;
        const auto& selected=options[static_cast<std::size_t>(draw)];
        jockey.horse_model=selected.name;jockey.horse_hash=selected.model;
        Log(std::string("roster mount profile=")+profile->key+" options="+
            std::to_string(options.size())+" selected="+jockey.horse_model+
            " category="+pool.group+" own_percent="+std::to_string(pool.own_percent));
        jockeys.push_back(std::move(jockey));
    }
    shared=Venue().shared_attributes?bm::DrawSharedRanks(random):bm::SharedRanks{};
    if((Venue().shared_attributes && !shared.Valid()) || !LoadCircuit())return false;
    std::vector<double> predicted_seconds;
    for(auto& j:jockeys){
        j.natural_ranks=horse_attributes::ReadModelDefaults(j.horse_hash);
        if(!j.natural_ranks.Valid()){Log("runtime natural horse ranks unavailable: "+j.horse_model);return false;}
        j.ranks=Venue().shared_attributes?horse_attributes::Ranks{shared.speed,shared.acceleration}:
            opponent_difficulty::BookmakerTarget(j.difficulty_rule,*j.profile,j.natural_ranks,random);
        if(!j.ranks.Valid()){Log("runtime horse ranks unavailable: "+j.horse_model);return false;}
        j.line_utilization=static_cast<float>(MISC::GET_RANDOM_INT_IN_RANGE(800,1001))/1000.0F;
        j.dynamics=race_pace::ForAttributes(j.ranks.speed+1,j.ranks.acceleration+1);
        j.dynamics.lateral=j.cornering;
        if(!bm::BuildPersonalLine(plan.course.path,line_obstacles,
            plan.course.strict_half_width,j.line_utilization,j.dynamics,j.personal_line,j.pace,&plan.offsets,tracking_reserve))return false;
        j.grid_line=j.personal_line.path;j.grid_reference=j.personal_line.reference;
        if(!j.avoidance.Reset(j.grid_line,j.grid_reference))return false;
        j.line_ready=true;
        const int handling=invoke<int>(0x958DD43D41F89A47,j.horse_hash,4);
        j.prediction={bm::odds::Key(odds_course_key+"/"+j.profile->key+"/"+j.horse_model+"/"+
            horse_attributes::Pair(j.ranks)+"/"+std::to_string(handling)+"/"+
            std::to_string(j.cornering)+"/"+std::to_string(static_cast<int>(protagonist))+
            "/personal-line-v2"),j.pace.nominal_seconds};
        predicted_seconds.push_back(j.prediction.seconds);
    }
    const auto order=bm::StartingOrder(predicted_seconds,random);
    if(order.size()!=jockeys.size())return false;
    std::vector<Jockey> ordered;ordered.reserve(order.size());
    for(const auto index:order){
        auto& j=jockeys[index];j.grid_slot=static_cast<int>(ordered.size());
        Log(std::string("grid locked slot=")+std::to_string(j.grid_slot)+" row="+
            std::to_string(bookmaker_grid::Row(j.grid_slot,Venue().columns))+" column="+
            std::to_string(j.grid_slot%Venue().columns)+" profile="+j.profile->key+
            " predicted_s="+std::to_string(j.prediction.seconds)+" utilization="+
            std::to_string(j.line_utilization)+" cornering="+std::to_string(j.cornering)+
            " recorded_segments="+std::to_string(j.personal_line.recorded_segments)+
            " obstacle_verified="+std::to_string(j.personal_line.obstacle_verified)+
            " estimated_rows="+std::to_string(j.personal_line.estimated_rows)+
            " estimated_links="+std::to_string(j.personal_line.estimated_links)+
            " score_final="+std::to_string(j.personal_line.objective_final)+
            " optimizer="+j.personal_line.optimizer+
            " curvature_sweeps="+std::to_string(j.personal_line.curvature_sweeps)+
            " seam_curvature_sweeps="+std::to_string(j.personal_line.seam_curvature_sweeps)+
            " solve_ms="+std::to_string(j.personal_line.milliseconds));
        ordered.push_back(std::move(j));
    }
    jockeys=std::move(ordered);
    std::vector<bm::odds::Prediction> predictions;
    for(const auto& j:jockeys)predictions.push_back(j.prediction);
    const auto seed_probabilities=Venue().shared_attributes?std::vector<double>{}:
        bm::odds::Probabilities(predictions,bm::odds::Calibration{});
    const auto probabilities=Venue().shared_attributes?std::vector<double>{}:
        bm::odds::Probabilities(predictions,odds_calibration);
    if(!Venue().shared_attributes && (probabilities.size()!=jockeys.size() ||
        seed_probabilities.size()!=jockeys.size()))return false;
    for(std::size_t i=0;i<jockeys.size();++i){
        auto& j=jockeys[i];
        const int base=Venue().shared_attributes?static_cast<int>(jockeys.size())*100:
            bm::odds::BaseQuote(j.ranks.speed+1,j.ranks.acceleration+1);
        j.win_odds=Venue().shared_attributes?static_cast<int>(jockeys.size())*100:
            bm::odds::AttributeQuote(j.ranks.speed+1,j.ranks.acceleration+1,seed_probabilities[i],probabilities[i]);
        if(j.win_odds<=0)return false;
        Log(std::string("quote locked for field venue=")+Venue().key+" profile="+j.profile->key+
            " model="+j.horse_model+" displayed="+horse_attributes::Pair({j.ranks.speed+1,j.ranks.acceleration+1})+
            " natural="+horse_attributes::Pair({j.natural_ranks.speed+1,j.natural_ranks.acceleration+1})+
            " predicted_s="+std::to_string(j.prediction.seconds)+" base_win="+bm::odds::Label(base)+
            " local_price_x="+std::to_string(static_cast<double>(j.win_odds)/base)+
            " win="+bm::odds::Label(j.win_odds)+
            " key="+j.prediction.key);
    }
    Log(std::string("roster locked venue=")+Venue().key+" riders="+std::to_string(jockeys.size())+
        " laps="+std::to_string(plan.Total())+" grid_forward_m="+std::to_string(Venue().grid_forward_metres)+" attributes="+
        (Venue().shared_attributes?horse_attributes::Pair({shared.speed+1,shared.acceleration+1}):
            "per-host difficulty draw; displayed speed minimum six"));
    return true;
}
