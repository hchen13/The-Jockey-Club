// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Arthur activity start, transitions and pending scenario exits.
void StartActivity(std::size_t a,bool teleport,const char* why) {
    if(a>=g_layout.scenes.size())a=0;
    for(std::size_t tried=0;tried<g_layout.scenes.size() && g_layout.scenes[a].sitting &&
        (MaryIsAt(g_layout.scenes[a].anchor) || SeatTaken(SeatPoint(g_layout.scenes[a]),g_arthur));++tried) {
        Log(std::string("someone has ")+ActivityName(a)+"; he takes the next one");
        a=(a+1)%g_layout.scenes.size();
    }
    const Scene& s=g_layout.scenes[a];
    const Pose pose=ActivityPose(s);
    const int point=SeatPoint(s);
    const bool place_beside_seat=point && teleport;
    if(point)teleport=false; // He walks to a seat and sits in it; the point owns the pose.
    g_walking=false;g_walk_seat=0;g_walking_to_door=false;
    if(Exists(g_arthur)) {
        TASK::CLEAR_PED_TASKS(g_arthur,false,false);
        if(point && !teleport)if(const auto approach=SeatApproach(s)) {
            // A step in front of the seat first; TickCamp hands him to the point when he gets there.
            // First creation/recovery starts at the activity's pose, which for a
            // bed is inside the cot. Put him on its known open floor before
            // asking for a walk; walking out of the cot to the door can trap
            // him against the canvas even though the entrance route is valid.
            if(place_beside_seat) {
                const Pose& stand=approach->second;
                ENTITY::SET_ENTITY_COORDS(g_arthur,stand.position.x,stand.position.y,stand.position.z,false,false,true,false);
                ENTITY::SET_ENTITY_HEADING(g_arthur,stand.heading);
            }
            const auto door=place_beside_seat?std::optional<Pose>{}:TentDoorFor(s);
            const Pose first=door.value_or(approach->second);
            TASK::TASK_GO_TO_COORD_ANY_MEANS(g_arthur,first.position.x,first.position.y,first.position.z,1.0F,0,false,0,0);
            PED::SET_PED_KEEP_TASK(g_arthur,true);
            g_walking=true;g_walk_to=first;g_walk_after_door=approach->second;g_walking_to_door=door.has_value();
            g_walk_by=Clock::now()+(door?30s:kWalkPatience);g_walk_seat=point;
        }
        if(g_walking){}
        else if(point)TASK::TASK_USE_SCENARIO_POINT(g_arthur,point,s.anim.empty()?nullptr:s.anim.c_str(),-1,false,kWalkToPoint,0,false,-1.0F,false);
        else if(teleport) {
            ENTITY::SET_ENTITY_COORDS(g_arthur,pose.position.x,pose.position.y,pose.position.z,false,false,true,false);
            ENTITY::SET_ENTITY_HEADING(g_arthur,pose.heading);
            TASK::TASK_START_SCENARIO_IN_PLACE_HASH(g_arthur,Key(s.scenario),-1,false,s.anim.empty()?0:Key(s.anim),pose.heading,false);
        } else {
            // Walk there on his own feet; the scenario begins when he arrives.
            TASK::TASK_GO_TO_COORD_ANY_MEANS(g_arthur,pose.position.x,pose.position.y,pose.position.z,1.0F,0,false,0,0);
            g_walking=true;g_walk_to=pose;g_walk_by=Clock::now()+kWalkPatience;
        }
        PED::SET_PED_KEEP_TASK(g_arthur,true); // The scenario is his until we say otherwise.
    }
    g_activity=a;g_activity_placed=teleport;g_activity_started=Clock::now();
    g_activity_until=g_activity_started+std::chrono::milliseconds(static_cast<long long>(Duration(g_layout,Unit())*1000.0));
    g_activity_verified=false;
    if(std::string(why)!="asked for" && std::string(why)!="on his feet again" && std::string(why)!="on his way out")g_asked=false;
    g_verify_after=g_activity_started+(teleport?12s:25s);
    if(g_walking)g_verify_after=g_walk_by+12s;   // Judged after he has had his walk, not during it.
    g_scenario_check=g_activity_started;g_scenario_missing=0;g_activity_given_up=false;
    Log(std::string("activity ")+ActivityName(a)+" ("+why+"): "+s.scenario+(s.anim.empty()?"":" anim "+s.anim)+" at "+Describe(pose)+
        (point?" (his seat's own point)":teleport?" (placed)":" (walks over)")+", for "+
        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(g_activity_until-g_activity_started).count())+" s");
}
void AskActivityAt(std::size_t a,const Clock::time_point& now,const char* why) {
    if(a>=g_layout.scenes.size())a=0;
    if(!Exists(g_arthur) || !PED::IS_PED_USING_ANY_SCENARIO(g_arthur)){StartActivity(a,false,why);return;}
    // The scenario's own way out: the cigar goes on the ground, the legs come
    // off the bed, and he is on his feet before he is asked for anything else.
    PED::SET_PED_SHOULD_PLAY_NORMAL_SCENARIO_EXIT(g_arthur);
    // A second ask while the first is still playing out changes where he is
    // going, never how long he has: the deadline is set once.
    if(!g_has_pending)g_exit_by=now+ExitPatience(ActivityName(g_activity));
    g_pending=a;g_has_pending=true;
    Log(std::string("leaving ")+ActivityName(g_activity)+" for "+ActivityName(a)+" ("+why+")");
}
// True while the way out is still playing, so nothing else touches him.
bool TickPending(const Clock::time_point& now) {
    if(!g_has_pending)return false;
    const bool out=!PED::IS_PED_USING_ANY_SCENARIO(g_arthur) && !TASK::IS_PED_EXITING_SCENARIO(g_arthur,false);
    if(!out && now<g_exit_by)return true;
    const std::size_t a=g_pending;g_has_pending=false;
    StartActivity(a,false,out?"on his feet again":"on his way out");
    return true;
}
Pose ActivityPose(const Scene& s);
bool ArthurIsAt(std::string_view anchor) {
    if(anchor.empty() || g_layout.scenes.empty())return false;
    const Scene& his=g_layout.scenes[std::min(g_activity,g_layout.scenes.size()-1)];
    return his.sitting && his.anchor==anchor;
}
