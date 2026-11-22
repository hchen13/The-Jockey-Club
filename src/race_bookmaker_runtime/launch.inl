void LaunchRace(){
    std::array<Vector3,kRacers> positions{};
    for(std::size_t i=0;i<jockeys.size();++i)positions[i]=ENTITY::GET_ENTITY_COORDS(jockeys[i].horse,true,false);
    if(!field.Start(plan,std::span<const Vector3>(positions.data(),jockeys.size()),Now())){
        ReturnFromRace(bm::Finish::AccidentalVoid,0,"field start failed");return;}
    scene.Started();scene_started=true;phase=Phase::Racing;go_at=Now();++token;
    far_simulation_enabled=horse_races::ReadFarCatchUpSetting();
    native_far_speed_enabled=horse_races::ReadNativeFarSpeedSetting();
    native_speed_table_enabled=horse_races::ReadNativeSpeedTableSetting();
    native_move_rate_trial_enabled=horse_races::ReadNativeMoveRateTrialSetting();
    native_vector_trial_enabled=horse_races::ReadNativeVectorTrialSetting();
    const bool override_at_go=race_speed_policy::Enabled();
    Log(std::string("far jockey policy at GO: ")+
        (!override_at_go?"stock native locomotion":far_simulation_enabled?"off-screen simulation":"native locomotion")+
        " native_far_speed="+std::to_string(override_at_go && native_far_speed_enabled)+
        " private_speed_table_trial="+std::to_string(override_at_go && native_speed_table_enabled)+
        " native_move_rate_ab="+std::to_string(override_at_go && native_move_rate_trial_enabled)+
        " native_vector_speed="+std::to_string(override_at_go && native_vector_trial_enabled));
    spectator_hud.Reset();
    horse_races::BeginBookmakerAudio(Venue().key);music_started=true;
    for(std::size_t i=0;i<jockeys.size();++i){
        auto& j=jockeys[i];ENTITY::FREEZE_ENTITY_POSITION(j.horse,false);
        j.chase.Reset();j.command.catchup_fraction=0;
        j.racing_assist.Begin(j.horse);
        if(!j.follower.Start(j.rider,j.avoidance.path,"TJC_BOOK_"+std::to_string(token)+"_"+std::to_string(i),
            0,go_at,&j.avoidance.reference,&positions[i])){
            ReturnFromRace(bm::Finish::AccidentalVoid,0,"navigation start failed");return;
        }
        Log("spectator grid rider="+std::to_string(i)+" profile="+j.profile->key+
            " slot="+std::to_string(j.grid_slot)+" horse avoidance="+race_avoidance::kRaceName+" xyz="+
            std::to_string(positions[i].x)+","+std::to_string(positions[i].y)+","+
            std::to_string(positions[i].z));
    }
    countdown.Go();race_starter.Fire();
    Log("spectator exhibition GO token="+std::to_string(token));
}
