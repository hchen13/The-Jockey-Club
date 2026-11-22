// Private camp api implementation; included by race_system.cpp.
double GameSeconds(){return CircuitNow();}
int SpawnBookmakerPed(unsigned model,float x,float y,float z,float heading){
    return PED::CREATE_PED(model,{x,y,z},heading,false,true,true,true);
}
int SpawnMarketObject(unsigned model,float x,float y,float z){
    return OBJECT::CREATE_OBJECT_NO_OFFSET(model,x,y,z,false,true,false,false);
}
bool ReadFarCatchUpSetting(){return GetPrivateProfileIntA("RaceAI","FarCatchUp",0,g_ini_path.c_str())!=0;}
bool ReadNativeFarSpeedSetting(){return GetPrivateProfileIntA("RaceAI","NativeFarSpeed",1,g_ini_path.c_str())!=0;}
bool ReadNativeSpeedTableSetting(){return GetPrivateProfileIntA("RaceAI","NativeSpeedTable",0,g_ini_path.c_str())!=0;}
bool ReadNativeMoveRateTrialSetting(){return GetPrivateProfileIntA("RaceAI","NativeMoveRateTrial",0,g_ini_path.c_str())!=0;}
bool ReadNativeVectorTrialSetting(){
    // The public production key supersedes the old opt-in experiment. Old
    // installations keep explicit opt-outs; a clean install enables all racers.
    const int configured=static_cast<int>(GetPrivateProfileIntA("RaceAI","NativeVectorSpeed",-1,g_ini_path.c_str()));
    const int legacy=static_cast<int>(GetPrivateProfileIntA("RaceAI","NativeVectorTrial",-1,g_ini_path.c_str()));
    return native_far_speed::VectorSpeedEnabled(configured,legacy);
}
bool ReadNativeStandingsPortraitsSetting(){return GetPrivateProfileIntA("UI","NativeStandingsPortraits",1,g_ini_path.c_str())!=0;}
unsigned NativeStandingsResourceLoadMask(){
    static bookmaking::NativeResourceProbe probe;
    const auto path=std::filesystem::path(g_directory)/"vfs.log";
    FILETIME created{},exited{},kernel{},user{};
    WIN32_FILE_ATTRIBUTE_DATA info{};
    const bool current=GetModuleHandleA("vfs.asi") &&
        GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user) &&
        GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&info) &&
        CompareFileTime(&info.ftLastWriteTime,&created)>=0;
    return probe.Poll(path,current);
}
void BeginBookmakerAudio(const char* venue_key){StopRaceMusic(false,"bookmaker_begin");StartRaceMusic(venue_key);}
void EndBookmakerAudio(bool result_fade){StopRaceAudio(result_fade);}
void BeginBookmakerReturnFade(){CAM::DO_SCREEN_FADE_OUT(450);}
bool BookmakerReturnIsBlack(){return CAM::IS_SCREEN_FADED_OUT();}
bool BookmakerReturnIsVisible(){return CAM::IS_SCREEN_FADED_IN();}
void EndBookmakerReturnFade(){CAM::DO_SCREEN_FADE_IN(450);}

// Exposed for the camp: the same call the race's hosts speak through.
// The camp asks this before it puts its own Arthur out: a fixed station whose
// host is Arthur has one of its own standing at its host point, and two of him
// beside one tent is one too many.
void AskHostHorse(const std::string& model_name)
{
    g_asked_host_horse=model_name;
    Log("the camp asks for "+model_name+" under the host");
}
// The camp's own two races, chosen at the camp: his Thoroughbred stakes race
// on Arthur himself, and a one-on-one on whichever of his horses was picked.
// The station has to hold a route, or there is nothing to race on.
bool BeginCampRace(const char* station_key,const std::string& horse_model,bool practice,std::optional<float> horse_dirt)
{
    if(!progression::runtime::Open(station_key))return false;
    if(g_state!=State::Scanning && g_state!=State::Ready)  {Log("camp race: a race is already under way");return false;}
    Station* station=nullptr;
    for(auto& s:g_stations)if(std::string(s.key)==station_key)station=&s;
    if(!station || !station->route) {
        Log(std::string("camp race: ")+station_key+" has no route file yet; nothing to race on");
        return false;
    }
    // A breed race is closed to the wrong mount, practice included, exactly as
    // it is at every other host: the prompts say which breed, and pressing one
    // on the wrong horse does nothing (2026-09-17).
    const Ped player=PLAYER::PLAYER_PED_ID();
    const Ped mount=player?PED::GET_MOUNT(player):0;
    const bool mounted=mount && ENTITY::DOES_ENTITY_EXIST(mount) && !ENTITY::IS_ENTITY_DEAD(mount);
    const std::string* wanted=!station->route->breed.empty()?&station->route->breed:
        (station->route->field?&station->route->field->breed:nullptr);
    g_camp_horse=0;
    if(wanted) {
        // A breed race is judged on the horse under the rider, so it wants
        // them on it, as at every other host.
        const auto* breed=field_race::FindBreed(*wanted);
        if(!breed || !mounted ||
            !field_race::EligiblePlayerModel(*breed,static_cast<Hash>(ENTITY::GET_ENTITY_MODEL(mount)))) {
            Log(std::string("camp race: ")+station_key+" is limited to "+*wanted+" and the player is not on one");
            return false;
        }
    } else if(!mounted) {
        // A one-on-one limits nothing, so it may be chosen on foot, at the
        // horse's side: the rider's own horse comes along under the blackout
        // and they are put on it there. Their active horse first, then the
        // saddled one, then whatever they last rode.
        Ped own=PLAYER::_GET_ACTIVE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        const auto alive=[](Ped p){return p && ENTITY::DOES_ENTITY_EXIST(p) && !ENTITY::IS_ENTITY_DEAD(p);};
        if(!alive(own))own=PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
        if(!alive(own))own=PED::_GET_LAST_MOUNT(player);
        if(!alive(own)) {
            ShowObjective(i18n::Text("You need a horse of your own for this race.","这场比赛需要你自己的马。"));
            Log(std::string("camp race: ")+station_key+" asked for on foot and the player has no horse of their own");
            return false;
        }
        g_camp_horse=own;
    }
    ReleaseRoute();
    if(horse_dirt && std::isfinite(*horse_dirt) && *horse_dirt>=0.F && *horse_dirt<=1.F && !horse_model.empty()) {
        g_camp_horse_dirt=horse_dirt;g_camp_horse_dirt_model=MISC::GET_HASH_KEY(horse_model.c_str());
    }
    g_variant=&station->variant;
    g_rival=&FixedRivalDefinition();
    g_route_name=g_variant->full_route;
    g_shortcut_route_name=g_variant->shortcut_route;
    if(!horse_model.empty())AskHostHorse(horse_model);
    g_camp_choice=true;g_camp_practice=practice;g_camp_choice_by=Clock::now()+std::chrono::seconds(60);
    // Black on the press. It stays black through the journey, the host, the
    // grid and the clearance, and the starting scene takes it over and lifts
    // it on the countdown.
    g_camp_moved=false;
    if(!CAM::IS_SCREEN_FADED_OUT() && !CAM::IS_SCREEN_FADING_OUT()){CAM::DO_SCREEN_FADE_OUT(500);g_camp_fade=true;}
    Log(std::string("camp race: ")+station_key+(practice?" practice":" race")+
        (horse_model.empty()?std::string():" on "+horse_model)+" chosen at the camp");
    SetState(State::LoadingRoute);
    return true;
}
bool CampRaceUnderway(){return g_camp_choice || (CurrentRules().Hosted() && FixedProfile().id==characters::Id::Arthur);}
bool ProgressionBusy(){return g_state==State::Accepted || g_state==State::PreparingCircuit ||
    g_state==State::Racing || g_state==State::Result || g_trial.active;}
bool PrepareBookmakerHandoff(){
    if(ProgressionBusy()||CampRaceUnderway()||g_fixed_wager.open||g_fixed_wager.pending_delta!=0)return false;
    if(g_state==State::Disabled||g_state==State::Scanning)return true;
    if(g_state!=State::Ready&&g_state!=State::Offering)return false;
    // BeginLoad revokes an old binding's epoch. Transfer the final mutable
    // observations before ChooseRoster can create that new binding.
    if(!RetireObstacleSaves("bookmaker handoff"))return false;
    Log("idle host yields to bookmaker; final course observations retained");
    ReturnToReady();return true;
}
bool PrepareProgressionChange(bool other_activity_busy){
    if(other_activity_busy || g_trial.active)return false;
    if(g_state==State::Result){
        // FinishRace has recorded the outcome before entering Result. Do not
        // wait for a distant rival or farewell when F11 explicitly changes the
        // campaign, but never discard a payment that still needs committing.
        if(g_fixed_wager.open || g_fixed_wager.pending_delta!=0)return false;
        Log("progression edit: closing settled result presentation; payout unchanged");
        BeginDeparture();
    }
    return !ProgressionBusy();
}
bool ProgressionToastBusy(){return g_state==State::Accepted || g_state==State::PreparingCircuit ||
    g_state==State::Racing || g_trial.active;}
// The words a camp host shows, in the same form every fixed host uses. The
// entry stake is not among them: it is priced from the horse the host draws,
// and no horse has been drawn while the player is only looking at Arthur.
const route_config::Route* CampRoute(const char* station_key)
{
    for(const auto& station:g_stations)if(std::string(station.key)==station_key && station.route)return &*station.route;
    return nullptr;
}
const std::string* CampBreedId(const route_config::Route* route)
{
    if(!route)return nullptr;
    if(!route->breed.empty())return &route->breed;
    return route->field?&route->field->breed:nullptr;
}
// How many laps this race is, when the route fixes the number. A route that
// draws its regular laps from a range says nothing rather than a number that
// turns out to be wrong.
std::string CampLapSuffix(const route_config::Route* route,bool practice)
{
    if(!route || !route->circuit || route->circuit->extra_min!=route->circuit->extra_max)return {};
    // A single trip is judged with the lap machinery and is no lap race: the one-on-one's prompt read
    // "2 laps" (the user, 2026-09-21). A single loop is one lap, which is not worth saying either.
    if(route->circuit->single_trip || route->circuit->single_loop)return {};
    const int laps=route->circuit->extra_min+2;   // First lap, the regular ones, the final lap.
    if(laps<2)return {};
    return practice?" ("+std::to_string(laps)+i18n::Text(" laps)"," 圈)"):
        " - "+std::to_string(laps)+i18n::Text(" laps"," 圈");
}
// Whether pressing would be taken. The camp answers on the press rather than
// greying a prompt the visitor rode up to, so the prompt has to carry the same
// answer in advance or it reads as broken (2026-09-17).
bool CampOfferReady(const char* station_key,int player_mount_model)
{
    if(!progression::runtime::Open(station_key))return false;
    const auto* route=CampRoute(station_key);
    if(!route)return false;
    // Only a breed race needs the rider on a horse now: it is judged on that
    // horse. A one-on-one is taken on foot too - their own horse comes along.
    if(const auto* breed_id=CampBreedId(route)) {
        if(!player_mount_model)return false;
        const auto* breed=field_race::FindBreed(*breed_id);
        if(!breed || !field_race::EligiblePlayerModel(*breed,static_cast<Hash>(player_mount_model)))return false;
    }
    return true;
}
std::string CampPromptLabel(const char* station_key,bool practice,int player_mount_model)
{
    if(!progression::runtime::Open(station_key))return i18n::Text("Locked: win the previous challenge","未解锁：需赢得前一项挑战");
    const auto* route=CampRoute(station_key);
    const std::string* breed_id=CampBreedId(route);
    const std::string laps=CampLapSuffix(route,practice);
    if(practice && breed_id) {
        if(const auto* breed=field_race::FindBreed(*breed_id)) {
            if(!field_race::EligiblePlayerModel(*breed,static_cast<Hash>(player_mount_model)))
                return i18n::Chinese()?"练习（必须骑乘"+std::string(breed->name_zh)+"参赛）":
                    "Practice (requires "+std::string(breed->name)+")";
        }
    }
    return std::string(practice?i18n::Text("Practice","练习"):i18n::Text("Race","赛马"))+laps;
}
// The camp opens the same card every host offers, on one of the camp's own
// horses - Arthur's blood bay, at the ranks he races it at.
bool ShowCampHorseInfo(int horse,const std::string& model)
{
    if(g_horse_info_card.Busy()){
        Log("camp horse info unavailable model="+model+" reason=previous horse card still closing");
        return false;
    }
    const bool opened=g_horse_info_card.Open(HorseIdentity(horse),horse_catalog::current.Labels(model));
    g_camp_inspecting=opened;
    Log(std::string("camp horse info ")+(opened?"opened":"unavailable")+" model="+model+
        (opened?"":" reason=")+g_horse_info_card.LastOpenReason());
    return opened;
}
bool CampHorseInfoActive(){return g_camp_inspecting && g_horse_info_card.Active();}
void CloseCampHorseInfo(){if(g_camp_inspecting){g_horse_info_card.Close();g_camp_inspecting=false;}}
// The camp says where its two stations' markers belong, or that they do not
// belong anywhere just now.
void SetCampMarker(const char* station_key,bool on,float x,float y,float z)
{
    for(auto& station:g_stations) {
        if(std::string(station.key)!=station_key)continue;
        const bool had=station.camp_marker.has_value();
        if(on)station.camp_marker=Vector3{x,y,z};else station.camp_marker.reset();
        if(!on && station.start_blip){RemoveBlip(station.start_blip);station.icon_sprite=0;}
        else if(on && station.start_blip && had)MAP::SET_BLIP_COORDS(station.start_blip,*station.camp_marker);
        return;
    }
}
bool OwnsMissionFlag(){return g_owns_mission_flag;}
bool FixedHostIsArthurHere()
{
    if(g_state==State::Disabled || g_state==State::Scanning)return false;
    if(FixedStationIndex(g_variant)<0)return false;
    return FixedProfile().id==characters::Id::Arthur;
}

bool PlayCampSpeech(int speaker,int listener,const char* speech_name,const char* voice_name,int variation)
{
    return PlaySpeech(speaker,listener,speech_name,voice_name,true,variation);
}
