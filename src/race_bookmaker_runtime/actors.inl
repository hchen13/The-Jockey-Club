// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Actor cleanup and reset, spawning, staging and reveal.
void DropActors(){
    for(auto& j:jockeys){
        j.racing_assist.Reset();
        j.dot.Reset();
        j.follower.Reset();
        if(j.follower.speed_table.Status()!=horse_motion::native_speed_table::State::Idle)
            Log(std::string("private speed table jockey stop horse=")+std::to_string(j.horse)+" "+
                horse_motion::native_speed_table::Name(j.follower.speed_table.Status())+
                " restore_mask="+std::to_string(j.follower.speed_table.RestoreMask()));
        j.rank_lease.Restore(Identify,ReadRank,WriteRank);
        j.appearance.Reset();
        cues::ForgetRearedHorse(j.horse);
        DeleteOwned(j.rider,j.rider_id);DeleteOwned(j.horse,j.horse_id);
        j.spawned=j.revealed=false;
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(j.rider_hash);
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(j.horse_hash);
    }
    if(race_starter.Retire())race_starter.Release();
    starter_staged=false;countdown.Reset();
    scene_actors.clear();field.Reset();
}
void ResetRace(bool keep_return_lock=false){
    horse_motion::speed_learning::runtime::Flush();
    StopLeaderCamera(false);RemoveCameraPrompts();camera_director.Reset();aim_hold.Reset();cinematic_hold.Reset();
    spectator_hud.Reset();
    native_standings.Close(Now());
    CAM::SET_CINEMATIC_BUTTON_ACTIVE(true);
    camera_blackout.Restore();
    camera_failed.reset();
    catalog.Close();RemoveStakePrompts();amount_repeat.Reset();
    ReleaseObstacleProfiles("show ended");scene.Finish(Now());DropActors();
    if(!keep_return_lock)UnlockPlayer();
    UnlockMenu();StopSlowdown();ReleaseMission();
    phase=Phase::Idle;entered_at=go_at=return_at=0;candidate_bet=false;
    next_visibility_audit=0;
    scene_started=actors_created=player_returned=settle_requested=false;
    result_catalog_opened=false;result_catalog_retry_at=0;
    pending_result_credit=0;
    transaction_debited=false;player_at_accept=0;abort_reason.clear();jockeys.clear();
    player_sabotaged=false;player_throw_at=-100;player_returned=false;
    return_prepared=false;return_prepared_at=0;return_horse={};
    if(music_started){horse_races::EndBookmakerAudio();music_started=false;}
}
void ReturnFromRace(bm::Finish finish,int place,const std::string& why){
    Settle(finish,place);abort_reason=why;
    if(phase==Phase::ReturnFade)return;
    for(auto& j:jockeys)j.racing_assist.Reset();
    native_standings.Close(Now());
    catalog.Close();StopSlowdown();
    StopLeaderCamera(false);RemoveCameraPrompts();
    CAM::SET_CINEMATIC_BUTTON_ACTIVE(true);
    phase=Phase::ReturnFade;return_at=Now();return_prepared=false;return_prepared_at=0;
    horse_races::BeginBookmakerReturnFade();return_fade_owned=true;
}
float GridHeading(){const auto a=plan.course.path.points[0],b=plan.course.path.points[1];
    return MISC::GET_HEADING_FROM_VECTOR_2D(b.x-a.x,b.y-a.y);}
bool SpawnOne(Jockey& j,std::string& issue){
    const auto grid=bookmaker_grid::GroundedPosition(plan.course.path,j.grid_slot,Venue().columns,
        Venue().grid_forward_metres,
        [](Vector3 candidate,float probe_z)->std::optional<bookmaker_grid::Surface>{
            STREAMING::REQUEST_COLLISION_AT_COORD(candidate);
            float ground=0;Vector3 normal{};
            if(!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(candidate.x,candidate.y,probe_z,
                &ground,&normal))return {};
            return bookmaker_grid::Surface{ground,normal};
        },[](Vector3 candidate){
            return plan.course.StrictProjection(candidate,0,plan.course.gates.front().along).valid;
        });
    if(!grid.position){
        const auto expected=bookmaker_grid::Position(plan.course.path,j.grid_slot,Venue().columns,
            Venue().grid_forward_metres);
        issue="grid ground or course projection unavailable slot="+std::to_string(j.grid_slot)+
            " xy="+std::to_string(expected.x)+","+std::to_string(expected.y)+
            " z="+std::to_string(expected.z)+
            " first="+bookmaker_grid::GroundRejectName(grid.first_rejection)+
            " last="+bookmaker_grid::GroundRejectName(grid.last_rejection)+
            " candidates="+std::to_string(grid.candidates)+
            " probes="+std::to_string(grid.probes);
        return false;
    }
    const auto spawn=*grid.position;
    if(grid.selected>0 || grid.probes>1)Log("grid slot "+std::to_string(j.grid_slot)+
        " recovered with bounded ground candidate="+std::to_string(grid.selected)+
        " probes="+std::to_string(grid.probes)+
        " first="+bookmaker_grid::GroundRejectName(grid.first_rejection)+
        " xyz="+std::to_string(spawn.x)+","+std::to_string(spawn.y)+","+
        std::to_string(spawn.z));
    const auto heading=GridHeading();
    j.horse=horse_races::SpawnBookmakerPed(j.horse_hash,spawn.x,spawn.y,spawn.z,heading);
    if(!Exists(j.horse)){issue="horse creation failed model="+j.horse_model;return false;}
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(j.horse,true,true);
    j.horse_id=Identify(j.horse);appearance::HidePair(0,j.horse);
    const auto defaults=horse_attributes::ReadModelDefaults(j.horse_hash);
    if(!defaults.Valid()){issue="horse model ranks unavailable model="+j.horse_model;return false;}
    if(defaults!=j.natural_ranks){
        issue="horse natural model ranks changed after field quote model="+j.horse_model;return false;
    }
    const auto snapshot=horse_attributes::Read(j.horse);
    if(snapshot.entity_base==horse_attributes::Ranks{0,0} && snapshot.current==horse_attributes::Ranks{0,0} &&
        defaults!=horse_attributes::Ranks{0,0}){
        WriteRank(j.horse,5,defaults.speed);WriteRank(j.horse,6,defaults.acceleration);
    }
    if(!j.rank_lease.Apply(j.horse_id,defaults,j.ranks,Identify,ReadRank,WriteRank)){
        issue="horse rank lease failed model="+j.horse_model;return false;
    }
    appearance::InitializeHorse(j.horse);
    if(horse_attributes::Read(j.horse).current!=j.ranks){
        issue="runtime instance ranks differ from locked field quote model="+j.horse_model+
            " quoted="+horse_attributes::Pair(j.ranks)+
            " actual="+horse_attributes::Pair(horse_attributes::Read(j.horse).current);return false;
    }
    j.rider=horse_races::SpawnBookmakerPed(j.rider_hash,spawn.x,spawn.y,spawn.z,heading);
    if(!Exists(j.rider)){issue=std::string("rider creation failed profile=")+j.profile->key;return false;}
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(j.rider,true,true);j.rider_id=Identify(j.rider);
    appearance::HidePair(j.rider,j.horse);
    appearance::InitializeRider(j.rider,false,false);
    AUDIO::SET_AMBIENT_VOICE_NAME(j.rider,j.profile->voice);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(j.rider,true);
    PED::SET_PED_KEEP_TASK(j.rider,true);
    if(!firearm_guard::ProtectPair(j.profile->id,j.rider,j.horse)){
        issue=std::string("rider/horse firearm protection failed profile=")+j.profile->key;return false;
    }
    const auto ini=paths.Config("HorseRaceRoutes.ini").string();
    const auto allowed=appearance::AllowedPool(ini,j.profile->id,PED::GET_NUM_META_PED_OUTFITS(j.rider));
    const int preset=appearance::FixedOutfit(j.profile->id) || allowed.empty()?-1:
        allowed[static_cast<std::size_t>(MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(allowed.size())))];
    j.appearance.Begin(j.rider,j.horse,j.rider_hash,j.horse_hash,preset,allowed,
        appearance::Preferred(j.profile->id),-1,logger,Clock::now(),appearance::FixedOutfit(j.profile->id));
    j.spawned=true;scene_actors.push_back(j.rider);scene_actors.push_back(j.horse);return true;
}
bool StageActors(){
    if(!actors_created){
        for(std::size_t i=0;i<jockeys.size();++i){
            auto& jockey=jockeys[i];
            if(!jockey.line_ready){
                // The menu and grid must use the same frozen estimate/line.
                // Losing it cancels; silently re-solving could change order.
                scene.error="quoted personal line missing for jockey "+std::to_string(i);
                return false;
            }
            if(jockey.spawned)continue;
            if(Now()<jockey.spawn_retry_after)return false;
            std::string issue;
            if(!SpawnOne(jockey,issue)){
                jockey.rank_lease.Restore(Identify,ReadRank,WriteRank);
                jockey.appearance.Reset();
                DeleteOwned(jockey.rider,jockey.rider_id);
                DeleteOwned(jockey.horse,jockey.horse_id);
                ++jockey.spawn_attempts;
                Log("jockey "+std::to_string(i)+" spawn attempt "+std::to_string(jockey.spawn_attempts)+
                    "/3: "+issue);
                if(jockey.spawn_attempts>=3)
                    scene.error="jockey "+std::to_string(i)+" could not be staged: "+issue;
                else jockey.spawn_retry_after=Now()+.3;
                return false;
            }
        }
        if(!scene.AddParticipants(scene_actors)){scene.error="participant registration: "+scene.error;return false;}
        actors_created=true;
    }
    bool all=true;
    for(auto& j:jockeys){
        const auto state=j.appearance.Tick();
        if(state==appearance::Status::Failed){scene.error="appearance failed: "+j.appearance.issue;Log(scene.error);return false;}
        if(state!=appearance::Status::Ready)all=false;
    }
    if(all && !starter_staged){
        // The starter is also exempt from track isolation. Never add an
        // unregistered gunman after the last blackout clearance audit.
        starter_staged=true;
        if(cues::Settings().starter_shot && race_starter.Create(
            bookmaker_grid::Origin(plan.course.path,Venue().grid_forward_metres),GridHeading())){
            if(!scene.AddParticipants({race_starter.ped})){
                scene.error="starter participant registration: "+scene.error;return false;
            }
            Log("exhibition starter placed beside the grid with a Cattleman");
        }else if(cues::Settings().starter_shot){
            Log("exhibition starter unavailable: "+race_starter.error+"; verified race is unaffected");
        }
    }
    return all;
}
bool RevealActors(){
    if(!actors_created)return false;
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || player!=player_at_accept || !Exists(player))return false;
    if(!player_returned){
        // The surveyed Z is the player's foot position. SET_ENTITY_COORDS
        // adds the ped bounds offset and made John stand at railing height.
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(player,Point(Venue().viewer),false,false,false);
        ENTITY::SET_ENTITY_HEADING(player,Venue().viewer.heading);
        CAM::SET_GAMEPLAY_CAM_RELATIVE_HEADING(0,1);
        player_returned=true;
    }
    for(auto& j:jockeys)if(!j.revealed){if(!j.appearance.Reveal())return false;
        ENTITY::FREEZE_ENTITY_POSITION(j.horse,true);j.revealed=true;
        Log(std::string("rider revealed profile=")+j.profile->key+
            " preset="+std::to_string(j.appearance.current)+
            " visible="+std::to_string(ENTITY::IS_ENTITY_VISIBLE(j.rider))+
            " alpha="+std::to_string(ENTITY::GET_ENTITY_ALPHA(j.rider))+
            " components="+std::to_string(PED::_GET_NUM_COMPONENTS_IN_PED(j.rider)));
    }
    for(auto& j:jockeys)j.dot.Update(Same(j.rider_id)?j.rider:0,
        ticket.Stake()>0 && j.profile->id==ticket.Selection(),false,
        tr::Text(j.profile->name,j.profile->chinese));
    LockPlayer();return true;
}
