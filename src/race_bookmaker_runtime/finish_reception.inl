// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Reviewed finish/burst dialogue and finishing rider exit tasks.
std::span<const marybeth_dialogue::Clip> ReviewedClips(characters::Id id){
    switch(id){
    case characters::Id::MaryBeth:return marybeth_dialogue::kClips;
    case characters::Id::Sadie:return sadie_dialogue::kClips;
    case characters::Id::Charles:return charles_dialogue::kClips;
    case characters::Id::Uncle:return gang_dialogue::kUncle;
    case characters::Id::Karen:return gang_dialogue::kKaren;
    case characters::Id::Pearson:return gang_dialogue::kPearson;
    case characters::Id::Arthur:return gang_dialogue::kArthur;
    case characters::Id::John:return gang_dialogue::kJohn;
    case characters::Id::Trelawny:return gang_dialogue::kTrelawny;
    case characters::Id::RainsFall:return gang_dialogue::kRainsFall;
    case characters::Id::Tilly:return tilly_dialogue::kClips;
    case characters::Id::Javier:return javier_dialogue::kClips;
    case characters::Id::Bill:return bill_dialogue::kClips;
    default:return {};
    }
}
Vector3 FinisherExit(const course::Path& path){
    if(path.points.size()<2)return {};
    const Vector3 finish=path.points.back();
    Vector3 forward{};
    for(std::size_t i=path.points.size()-1;i>0;--i){
        const auto& previous=path.points[i-1];
        const float length=std::hypot(finish.x-previous.x,finish.y-previous.y);
        if(length>=6){forward={(finish.x-previous.x)/length,(finish.y-previous.y)/length,0};break;}
    }
    if(std::hypot(forward.x,forward.y)<.5F)return finish;
    // The final tangent points through the line; leave to its outside, not
    // back along the lane into the next arrivals. Prefer a nearby road node
    // only if it is still outside the entire recorded circuit corridor.
    for(float lateral:{22.0F,30.0F,40.0F}){
        Vector3 best{};float best_score=-1;
        for(float side:{-1.0F,1.0F}){
            const Vector3 raw{finish.x+forward.x*8-side*forward.y*lateral,
                finish.y+forward.y*8+side*forward.x*lateral,finish.z};
            Vector3 road{};const bool has_road=PATHFIND::GET_CLOSEST_VEHICLE_NODE(raw,&road,1,3.0F,0.0F) &&
                destination::Distance(raw,road)<10;
            for(int variant=0;variant<(has_road?2:1);++variant){
                const Vector3 candidate=variant==0 && has_road?road:raw;
                const auto projected=course::Project(path,candidate);
                if(!projected.valid || projected.distance<18)continue;
                // Prefer a navigable road, then the side farther from the full
                // course. Only widen the exit when both nearer sides are unsafe.
                const float score=projected.distance+(variant==0 && has_road?5.0F:0.0F);
                if(score>best_score){best=candidate;best_score=score;}
            }
        }
        if(best_score>=0)return best;
    }
    return Vector3{finish.x+forward.x*10-forward.y*30,
        finish.y+forward.y*10+forward.x*30,finish.z};
}
void TickFinishedJockey(std::size_t index,double now){
    auto& j=jockeys[index];
    if(!Same(j.rider_id) || !Same(j.horse_id) || ENTITY::IS_ENTITY_DEAD(j.rider) ||
        ENTITY::IS_ENTITY_DEAD(j.horse) || PED::GET_MOUNT(j.rider)!=j.horse)return;
    if(j.finish_goodbye_pending &&
        !AUDIO::IS_ANY_SPEECH_PLAYING(j.rider) && !AUDIO::IS_SCRIPTED_SPEECH_PLAYING(j.rider)){
        j.finish_goodbye_pending=false;
        const unsigned listener=marybeth_dialogue::PlayerMask(ENTITY::GET_ENTITY_MODEL(PLAYER::PLAYER_PED_ID()));
        const auto clips=ReviewedClips(j.profile->id);
        std::vector<std::size_t> choices;
        for(std::size_t k=0;k<clips.size();++k)
            if(clips[k].event==marybeth_dialogue::Event::Goodbye && (clips[k].players&listener))choices.push_back(k);
        if(!choices.empty()){
            bool played=false;
            const auto start=static_cast<std::size_t>(MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(choices.size())));
            for(std::size_t trial=0;trial<choices.size();++trial){
                const auto& clip=clips[choices[(start+trial)%choices.size()]];
                const char* voice=clip.native_voice?clip.native_voice:j.profile->voice;
                AUDIO::SET_AMBIENT_VOICE_NAME(j.rider,voice);
                if(!AUDIO::DOES_CONTEXT_EXIST_FOR_THIS_PED(j.rider,clip.context,false) ||
                    !horse_races::PlayCampSpeech(j.rider,PLAYER::PLAYER_PED_ID(),clip.context,voice,clip.variation))continue;
                played=true;
                Log("finisher 07 voice rider="+std::to_string(index)+" clip="+clip.id);
                break;
            }
            if(!played)Log("finisher 07 voice unavailable rider="+std::to_string(index));
        }else Log("finisher has no reviewed 07/Goodbye clip rider="+std::to_string(index));
    }
    if(j.finish_goodbye_pending && now-j.finished_at<2)return;
    if(field.Riders()[index].survival_win){
        // This rider won where the last opponent was eliminated, not at the
        // recorded finish. End its race task here rather than sending it on a
        // cross-town departure while the final result is already displayed.
        if(!j.finish_departing){j.follower.Reset();j.finish_departing=true;}
        return;
    }
    const bool speed_override=race_speed_policy::Enabled();
    if(!j.finish_departing || (!speed_override && j.finish_speed_overridden) || (now>=j.next_departure_retry &&
        destination::Distance(ENTITY::GET_ENTITY_COORDS(j.horse,true,false),
            plan.course.path.points.back())<12 && ENTITY::GET_ENTITY_SPEED(j.horse)<.6F)){
        if(!j.finish_departing){j.follower.Reset();j.finish_exit=FinisherExit(plan.course.path);}
        // The same mounted traffic task used by the game's race rivals. The
        // target is beyond the full circuit's lane, so finishers do not park on
        // the line or meet oncoming competitors head-on.
        invoke<Void>(0xDCA3A13F7A45338B,j.rider,j.finish_exit.x,j.finish_exit.y,j.finish_exit.z,
            2.0F,speed_override?2.5F:-1.F,8704,0);
        j.finish_speed_overridden=speed_override;
        PED::SET_PED_KEEP_TASK(j.rider,true);
        j.finish_departing=true;j.next_departure_retry=now+3;
        Log("finisher departing rider="+std::to_string(index)+" exit="+
            std::to_string(j.finish_exit.x)+","+std::to_string(j.finish_exit.y)+","+
            std::to_string(j.finish_exit.z));
    }
}
void TickBurstVoices(){
    if(phase!=Phase::Racing || HUD::IS_PAUSE_MENU_ACTIVE())return;
    const Ped player=PLAYER::PLAYER_PED_ID();
    const unsigned listener=marybeth_dialogue::PlayerMask(ENTITY::GET_ENTITY_MODEL(player));
    if(!listener)return;
    for(std::size_t i=0;i<jockeys.size();++i){
        auto& j=jockeys[i];
        if(!j.burst_pending)continue;
        if(!j.burst_active || Now()>j.burst_expires || !Same(j.rider_id) ||
            field.Riders().size()<=i || field.Riders()[i].status!=bm::Field::Status::Racing){
            j.burst_pending=false;continue;
        }
        if(AUDIO::IS_ANY_SPEECH_PLAYING(j.rider) || AUDIO::IS_SCRIPTED_SPEECH_PLAYING(j.rider))continue;
        const auto clips=ReviewedClips(j.profile->id);
        std::vector<int> choices;
        for(std::size_t k=0;k<clips.size();++k)
            if(clips[k].event==marybeth_dialogue::Event::Start && (clips[k].players&listener))
                choices.push_back(static_cast<int>(k));
        if(choices.size()>1)choices.erase(std::remove(choices.begin(),choices.end(),j.last_burst_clip),choices.end());
        if(choices.empty()){j.burst_pending=false;Log("burst has no reviewed 02/Start clip rider="+std::to_string(i));continue;}
        const auto offset=static_cast<std::size_t>(MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(choices.size())));
        for(std::size_t trial=0;trial<choices.size();++trial){
            const int selected=choices[(offset+trial)%choices.size()];
            const auto& clip=clips[static_cast<std::size_t>(selected)];
            const char* voice=clip.native_voice?clip.native_voice:j.profile->voice;
            AUDIO::SET_AMBIENT_VOICE_NAME(j.rider,voice);
            if(!AUDIO::DOES_CONTEXT_EXIST_FOR_THIS_PED(j.rider,clip.context,false))continue;
            if(!horse_races::PlayCampSpeech(j.rider,player,clip.context,voice,clip.variation))continue;
            j.last_burst_clip=selected;j.burst_pending=false;
            Log("burst 02 voice rider="+std::to_string(i)+" clip="+clip.id+
                " context="+clip.context+" variation="+std::to_string(clip.variation));
            break;
        }
        if(j.burst_pending){j.burst_pending=false;Log("burst 02 voice unavailable rider="+std::to_string(i));}
    }
}
