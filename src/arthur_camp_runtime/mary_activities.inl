// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Mary resident creation, activity transitions and recovery.
void StartMaryActivity(std::size_t a,bool place,const char* why) {
    auto& m=g_mary;
    const auto& scenes=g_layout.companion_scenes;
    if(scenes.empty() || !Exists(m.ped))return;
    if(a>=scenes.size())a=0;
    // Two people on one crate: the seats carry points anyone may use, Arthur included. She takes the
    // next activity instead (the user, 2026-09-22).
    const auto hers=[&m](const Scene& scene){const auto it=m.points.find(scene.name);return it==m.points.end()?0:it->second;};
    for(std::size_t tried=0;tried<scenes.size() && scenes[a].sitting &&
        (ArthurIsAt(scenes[a].anchor) || SeatTaken(hers(scenes[a]),m.ped));++tried) {
        Log(std::string("Mary: ")+scenes[a].name+" is taken; trying the next");
        a=(a+1)%scenes.size();
    }
    const Scene& s=scenes[a];
    const Pose pose=ActivityPose(s);
    const auto point=m.points.find(s.name);
    TASK::CLEAR_PED_TASKS(m.ped,false,false);
    m.walking=false;m.walking_to_door=false;   // Set again below only if this activity is one she walks to.
    m.walk_seat=0;
    const auto approach=SeatApproach(s);
    if(s.sitting && point!=m.points.end() && !place && approach) {
        // A step in front of the seat first, then the point takes her the last step and sits her down.
        const auto door=TentDoorFor(s);
        const Pose first=door.value_or(approach->second);
        TASK::TASK_GO_TO_COORD_ANY_MEANS(m.ped,first.position.x,first.position.y,first.position.z,1.0F,0,false,0,0);
        m.walking=true;m.walk_to=first;m.walk_after_door=approach->second;m.walking_to_door=door.has_value();
        m.walk_by=Clock::now()+(door?30s:kWalkPatience);m.walk_seat=point->second;
    } else if(s.sitting && point!=m.points.end()) {
        // The first time she is put beside her seat, so the walk to it is a step and not a hike.
        if(place) {
            const Pose stand=approach?approach->second:pose;
            ENTITY::SET_ENTITY_COORDS(m.ped,stand.position.x,stand.position.y,stand.position.z,false,false,true,false);
            ENTITY::SET_ENTITY_HEADING(m.ped,stand.heading);
        }
        TASK::TASK_USE_SCENARIO_POINT(m.ped,point->second,s.anim.empty()?nullptr:s.anim.c_str(),-1,false,kWalkToPoint,0,false,-1.0F,false);
    } else if(place) {
        ENTITY::SET_ENTITY_COORDS(m.ped,pose.position.x,pose.position.y,pose.position.z,false,false,true,false);
        ENTITY::SET_ENTITY_HEADING(m.ped,pose.heading);
        TASK::TASK_START_SCENARIO_IN_PLACE_HASH(m.ped,Key(s.scenario),-1,false,s.anim.empty()?0:Key(s.anim),pose.heading,false);
    } else {
        // She walks over on her own feet; TickMary starts the scenario the moment she arrives.
        TASK::TASK_GO_TO_COORD_ANY_MEANS(m.ped,pose.position.x,pose.position.y,pose.position.z,1.0F,0,false,0,0);
        m.walking=true;m.walk_to=pose;m.walk_by=Clock::now()+kWalkPatience;
    }
    PED::SET_PED_KEEP_TASK(m.ped,true);
    const auto now=Clock::now();
    m.activity=a;m.leaving=false;m.checked=false;

    m.until=now+std::chrono::milliseconds(static_cast<long long>(Duration(g_layout,Unit())*1000.0));
    m.check_at=now+(place?12s:30s);
    Log(std::string("Mary: ")+s.name+" ("+why+"): "+s.scenario+(s.anim.empty()?"":" anim "+s.anim)+" at "+Describe(pose)+
        (s.sitting && point!=m.points.end()?" (her seat's own point)":place?" (placed)":" (walks over)"));
}
bool MakeMary() {
    auto& m=g_mary;
    const auto& scenes=g_layout.companion_scenes;
    if(!g_layout.companion || scenes.empty())return false;
    const Hash model=Key(g_layout.companion_model);
    if(!STREAMING::HAS_MODEL_LOADED(model)){STREAMING::REQUEST_MODEL(model,false);return false;}
    const Pose spot=ActivityPose(scenes.front());
    const Ped mary=PED::CREATE_PED(model,spot.position.x,spot.position.y,spot.position.z,spot.heading,false,true,true,true);
    if(!Exists(mary)){Log("Mary could not be created");return false;}
    PED::_EQUIP_META_PED_OUTFIT_PRESET(mary,0,false);
    PED::_UPDATE_PED_VARIATION(mary,false,true,true,true,false);
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(mary,true,true);
    WEAPON::REMOVE_ALL_PED_WEAPONS(mary,true,true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(mary,true);
    // As Arthur: a horse at a gallop must not be the end of her.
    ENTITY::SET_ENTITY_INVINCIBLE(mary,true);
    PED::SET_PED_CAN_RAGDOLL(mary,false);
    // She could not be locked on to (the user, 2026-09-21): the flags Arthur
    // carries for that - 297 forces the lock-on, 130 DisableTalkTo cleared -
    // had not been given to her. The game's own Greet / Antagonize follow.
    PED::SET_PED_CAN_BE_TARGETTED(mary,true);
    for(int on:{297,315})PED::SET_PED_CONFIG_FLAG(mary,on,true);
    for(int off:{130,178,301,317,331,493})PED::SET_PED_CONFIG_FLAG(mary,off,false);
    PED::SET_PED_RELATIONSHIP_GROUP_HASH(mary,Key("REL_GANG_DUTCHS"));
    PED::_SET_PED_PROMPT_NAME(mary,i18n::Literal(i18n::Text("Mary Linton","玛丽·林顿")));
    firearm_guard::Track(mary,false);
    m.ped=mary;
    Log("Mary created ("+g_layout.companion_model+"), ped="+std::to_string(mary));
    StartMaryActivity(static_cast<std::size_t>(MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(scenes.size()))),true,"first");
    return true;
}
void ForgetMary(bool remove) {
    auto& m=g_mary;
    if(m.ped) {
        firearm_guard::Discard(m.ped); // Deleted or forgotten below; never retain a retry for an old allocation.
        if(remove && StillOurs(m.ped,Key(g_layout.companion_model),Point{},0)){Ped p=m.ped;PED::DELETE_PED(&p);}
    }
    m=Companion{};
}
void TickMary(const Clock::time_point& now) {
    auto& m=g_mary;
    const auto& scenes=g_layout.companion_scenes;
    if(!g_layout.companion || scenes.empty())return;
    if(!Exists(m.ped) || ENTITY::IS_ENTITY_DEAD(m.ped)) {
        if(m.ped) {
            // The seat points are the camp's and outlive her.
            Log("Mary is gone; fetching her back");
            const auto points=m.points;ForgetMary(true);g_mary.points=points;g_mary.remake_at=now+5s;
        }
        if(now>=g_mary.remake_at && !MakeMary())g_mary.remake_at=now+2s;
        return;
    }
    // Locked on to, she looks the player's way, as he does.
    {
        const bool targeted=PLAYER::IS_PLAYER_TARGETTING_ENTITY(PLAYER::PLAYER_ID(),m.ped,false)!=0;
        if(targeted && !m.targeted)TASK::TASK_LOOK_AT_ENTITY(m.ped,PLAYER::PLAYER_PED_ID(),4000,0,51,0);
        m.targeted=targeted;
    }
    const Scene& s=scenes[std::min(m.activity,scenes.size()-1)];
    if(m.leaving) {
        const bool out=!PED::IS_PED_USING_ANY_SCENARIO(m.ped) && !TASK::IS_PED_EXITING_SCENARIO(m.ped,false);
        if(!out && now<m.exit_by)return;
        const bool chosen=m.asked.has_value();
        const std::size_t next=chosen?*m.asked:Next(m.activity,scenes.size(),Unit());
        m.asked.reset();
        StartMaryActivity(next,false,chosen?"asked for":"next");
        return;
    }
    if(m.walking) {
        const Vector3 at=ENTITY::GET_ENTITY_COORDS(m.ped,true,false);
        const float away=std::hypot(at.x-m.walk_to.position.x,at.y-m.walk_to.position.y);
        const bool there=std::isfinite(away) && away<1.5F;
        if(!there && now<m.walk_by)return;
        if(m.walking_to_door) {
            if(!there) {
                Log("Mary: tent entrance not reachable; choosing another activity without placing her through the wall");
                m.walking=false;m.walking_to_door=false;
                StartMaryActivity(Next(m.activity,scenes.size(),Unit()),false,"tent entrance blocked");
                return;
            }
            m.walking_to_door=false;m.walk_to=m.walk_after_door;m.walk_by=now+kWalkPatience;
            TASK::TASK_GO_TO_COORD_ANY_MEANS(m.ped,m.walk_to.position.x,m.walk_to.position.y,m.walk_to.position.z,1.0F,0,false,0,0);
            PED::SET_PED_KEEP_TASK(m.ped,true);
            Log("Mary: through tent entrance; walking to bed side");
            return;
        }
        m.walking=false;
        if(!there && TentDoorFor(s)) {
            Log("Mary: bed side not reachable after entering tent; choosing another activity");
            StartMaryActivity(Next(m.activity,scenes.size(),Unit()),false,"bed side blocked");
            return;
        }
        if(!there) {
            ENTITY::SET_ENTITY_COORDS(m.ped,m.walk_to.position.x,m.walk_to.position.y,m.walk_to.position.z,false,false,true,false);
            Log(std::string("Mary: ")+s.name+": she could not walk there in time and was placed");
        }
        ENTITY::SET_ENTITY_HEADING(m.ped,m.walk_to.heading);
        if(const int seat=m.walk_seat) {
            m.walk_seat=0;
            TASK::TASK_USE_SCENARIO_POINT(m.ped,seat,s.anim.empty()?nullptr:s.anim.c_str(),-1,false,kWalkToPoint,0,false,-1.0F,false);
            PED::SET_PED_KEEP_TASK(m.ped,true);
            // Still to be looked at: handing her to the point is not the same as her taking it, and
            // counting it as done left her standing beside the bed for the whole activity.
            m.checked=false;m.check_at=now+12s;
            m.until=now+std::chrono::milliseconds(static_cast<long long>(Duration(g_layout,Unit())*1000.0));
            Log(std::string("Mary: ")+s.name+": she is at her seat; sitting down");
            return;
        }
        TASK::TASK_START_SCENARIO_IN_PLACE_HASH(m.ped,Key(s.scenario),-1,false,s.anim.empty()?0:Key(s.anim),m.walk_to.heading,false);
        PED::SET_PED_KEEP_TASK(m.ped,true);
        m.checked=true;
        // Her time at an activity starts when she gets there, not when she set off.
        m.until=now+std::chrono::milliseconds(static_cast<long long>(Duration(g_layout,Unit())*1000.0));
        return;
    }
    if(!m.checked && now>=m.check_at) {
        m.checked=true;
        if(!PED::IS_PED_USING_ANY_SCENARIO(m.ped)) {
            // She walked over (or never got going): start it where the activity is.
            const Pose pose=ActivityPose(s);
            const Vector3 p=ENTITY::GET_ENTITY_COORDS(m.ped,true,false);
            const bool there=std::hypot(p.x-pose.position.x,p.y-pose.position.y)<2.0F;
            if(s.sitting){Log(std::string("Mary: ")+s.name+": she did not reach her seat; on to another");m.until=now;}
            else {
                if(!there)ENTITY::SET_ENTITY_COORDS(m.ped,pose.position.x,pose.position.y,pose.position.z,false,false,true,false);
                ENTITY::SET_ENTITY_HEADING(m.ped,pose.heading);
                TASK::TASK_START_SCENARIO_IN_PLACE_HASH(m.ped,Key(s.scenario),-1,false,s.anim.empty()?0:Key(s.anim),pose.heading,false);
                PED::SET_PED_KEEP_TASK(m.ped,true);
                Log(std::string("Mary: ")+s.name+(there?": she is there; starting it where she stands":": she could not walk there and was placed"));
            }
        } else Log(std::string("Mary: ")+s.name+" running");
    }
    if(now>=m.until && scenes.size()>1) {
        if(PED::IS_PED_USING_ANY_SCENARIO(m.ped))PED::SET_PED_SHOULD_PLAY_NORMAL_SCENARIO_EXIT(m.ped);
        m.leaving=true;m.exit_by=now+ExitPatience(s.name);
    }
}
