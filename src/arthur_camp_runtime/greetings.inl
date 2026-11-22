// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Resident identity checks, greeting selection and shared randomness.
bool EqualNoCase(std::string_view a,std::string_view b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](char x,char y) {
        return std::tolower(static_cast<unsigned char>(x))==std::tolower(static_cast<unsigned char>(y));});
}
Hash Key(const std::string& s){return MISC::GET_HASH_KEY(s.c_str());}
bool Exists(int handle){return handle && ENTITY::DOES_ENTITY_EXIST(handle);}
// Still the entity we made: the same model, and within a couple of metres of
// where we left it. A handle that has been handed out again fails both.
bool StillOurs(int handle,Hash model,const Point& at,float reach=3.0F) {
    if(!Exists(handle) || static_cast<Hash>(ENTITY::GET_ENTITY_MODEL(handle))!=model)return false;
    if(reach<=0)return true;
    const Vector3 p=ENTITY::GET_ENTITY_COORDS(handle,true,false);
    return std::isfinite(p.x) && std::hypot(p.x-at.x,p.y-at.y,p.z-at.z)<=reach;
}
// John locks on to Arthur (the right-hand button) and Arthur greets him in
// his own voice, using only the user's exact selected clips, and looks his
// way, as a camp member would; once per lock-on, no oftener than every
// twenty seconds. No prompt: the greeting is the interaction.
Clock::time_point g_greeted{};
bool g_targeted=false;
std::string_view g_last_greeting;
void TickGreeting(const Clock::time_point& now) {
    if(!Exists(g_arthur))return;
    const Ped player=PLAYER::PLAYER_PED_ID();
    const bool targeted=PLAYER::IS_PLAYER_TARGETTING_ENTITY(PLAYER::PLAYER_ID(),g_arthur,false)!=0;
    const bool began=targeted && !g_targeted;
    g_targeted=targeted;
    if(!began)return;
    TASK::TASK_LOOK_AT_ENTITY(g_arthur,player,4000,0,51,0);
    if(now-g_greeted<20s)return;
    if(AUDIO::IS_ANY_SPEECH_PLAYING(g_arthur) || AUDIO::IS_SCRIPTED_SPEECH_PLAYING(g_arthur) ||
        AUDIO::IS_ANY_SPEECH_PLAYING(player) || AUDIO::IS_SCRIPTED_SPEECH_PLAYING(player))return;
    g_greeted=now;
    const int hour=CLOCK::GET_CLOCK_HOURS();
    const auto choices=arthur_greeting::Choose(arthur_greeting::kClips,hour,g_last_greeting,
        [](int begin,int end){return MISC::GET_RANDOM_INT_IN_RANGE(begin,end);});
    for(std::size_t i=0;i<choices.size;++i) {
        const auto& clip=*choices.clips[i];
        const bool played=horse_races::PlayCampSpeech(g_arthur,player,clip.context,arthur_greeting::kVoice,clip.variation);
        Log(std::string("greeting clip=")+clip.id+" context="+clip.context+" variation="+std::to_string(clip.variation)+
            " voice="+arthur_greeting::kVoice+" hour="+std::to_string(hour)+" period="+arthur_greeting::Label(clip.time)+
            " attempt="+std::to_string(i+1)+(played?" started":" refused"));
        if(played){g_last_greeting=clip.id;return;}
    }
}
double Unit(){return static_cast<double>(MISC::GET_RANDOM_INT_IN_RANGE(0,1000000))/1000000.0;}
