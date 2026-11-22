// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Stable layout reload, session conditions and requested model assets.
// The ini, polled once a second and applied only once its bytes have held
// still for two polls (an editor writes in pieces). A file that does not
// parse keeps the last good layout; one whose captures are still TODO keeps
// the camp down; a change to a live camp strikes and rebuilds it.
file_stamp::Stamp g_layout_stamp;
void PollLayout(bool initial) {
    horse_diagnostics::Scope stage_cost("camp: layout file");
    if(g_path.empty() || (!initial && Wall::now()<g_poll))return;
    g_poll=Wall::now()+1s;
    // 82 KB re-read and compared every second, anywhere on the map: 7.9 ms in one frame
    // (measured 2026-09-21). The write time and size answer the same question - but asking even that
    // belongs behind the once-a-second gate, not in front of it: asking it every frame was 0.155 ms a
    // frame, the largest thing the mod did outside a race (measured 2026-09-22, 23:31).
    {
        const auto stamp=file_stamp::Of(g_path);
        const bool settled=g_observed && *g_observed==g_parsed && g_issue.empty();
        if(!initial && settled && g_layout_stamp.Known() && stamp==g_layout_stamp)return;
        g_layout_stamp=stamp;
    }
    const auto issue=[&](const std::string& reason){if(g_issue==reason)return;g_issue=reason;Log(reason);};
    std::ifstream input(g_path,std::ios::binary);
    if(!input) {
        issue("ArthurCamp.ini not found; the camp stays down until tools/capture_camp_layout.py writes one");
        if(g_valid){g_valid=false;g_rebuild=true;}
        g_observed.reset();g_parsed.clear();return;
    }
    // A quarter of a megabyte: the hide list alone runs to eighty thousand
    // bytes, and at 64 KiB the whole file was rejected and the camp never
    // built (2026-09-16).
    static std::array<char,262145> buffer{};
    input.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
    if(input.bad() || input.gcount()>262144){issue("ArthurCamp.ini unreadable or over 256 KiB; keeping the last layout");return;}
    const std::string bytes(buffer.data(),static_cast<std::size_t>(input.gcount()));
    if(!initial && (!g_observed || *g_observed!=bytes)){g_observed=bytes;return;}
    g_observed=bytes;
    if(g_parsed==bytes && g_issue.empty())return;
    g_parsed=bytes;
    std::string error;
    const auto parsed=Parse(bytes,error);
    if(!parsed){issue("ArthurCamp.ini rejected: "+error+"; keeping the last valid layout");return;}
    if(!Captured(*parsed)) {
        issue("ArthurCamp.ini has no captured tent/meadow yet (TentX.. and HorsesX.. in [Camp]); the camp stays down");
        if(g_valid){g_valid=false;g_rebuild=true;}
        return;
    }
    g_issue.clear();
    const bool changed=!g_valid || !(g_layout==*parsed);
    g_layout=*parsed;g_valid=true;
    if(changed) {
        Log("layout loaded: tent "+std::to_string(g_layout.tent.position.x)+","+std::to_string(g_layout.tent.position.y)+","+
            std::to_string(g_layout.tent.position.z)+" heading "+std::to_string(g_layout.tent.heading)+"; meadow "+
            std::to_string(g_layout.horses.x)+","+std::to_string(g_layout.horses.y)+" r="+std::to_string(g_layout.horses_radius)+
            "; "+std::to_string(g_layout.props.size())+" props, "+
            std::to_string(g_layout.horse_models.size())+" horses, "+std::to_string(g_layout.ipls.size())+" IPLs; enabled="+
            std::to_string(g_layout.enabled));
        if(g_phase!=Phase::Idle)g_rebuild=true;
        // A new layout may have moved the tent: the markers are made again.
        if(g_blip && MAP::DOES_BLIP_EXIST(g_blip)){Blip b=g_blip;MAP::REMOVE_BLIP(&b);}
        g_blip=0;g_markers_on=false;ForgetRaceMarkers();
    }
}

Conditions Observe() {
    Conditions c;
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player) || !g_valid || !progression::runtime::Camp())return c;
    const bool john=static_cast<unsigned>(ENTITY::GET_ENTITY_MODEL(player))==kJohnModel;
    // The story's missions strike the camp; this mod's own races carry the
    // same flag while they are staged and run, and the camp stays for those
    // (it was struck and 595 hides lifted every time Arthur raced, 2026-09-17).
    c.eligible=(!g_layout.only_for_john || john) && (!MISC::GET_MISSION_FLAG() || horse_races::OwnsMissionFlag()) &&
        !NETWORK::NETWORK_IS_SESSION_ACTIVE();
    const Vector3 p=ENTITY::GET_ENTITY_COORDS(player,true,false);
    c.distance=std::hypot(p.x-g_layout.tent.position.x,p.y-g_layout.tent.position.y);
    const auto& t=g_layout.tent.position;
    c.out_of_view=c.distance>=kNearSpeck || !CAM::IS_SPHERE_VISIBLE(t.x,t.y,t.z+2.0F,18.0F);
    return c;
}

bool Usable(Hash model,const std::string& name) {
    if(model && STREAMING::IS_MODEL_IN_CDIMAGE(model) && STREAMING::IS_MODEL_VALID(model))return true;
    Log("model unknown to this game, skipped: "+name);
    return false;
}
void Want(const std::string& name) {
    const Hash h=Key(name);
    if(std::find(g_models.begin(),g_models.end(),h)!=g_models.end() || !Usable(h,name))return;
    g_models.push_back(h);STREAMING::REQUEST_MODEL(h,false);
}
void RequestAssets() {
    g_models.clear();
    for(const auto& p:g_layout.props)Want(p.model);
    for(const auto& m:g_layout.horse_models)Want(m);
    Want("PLAYER_ZERO");
    if(g_layout.companion && !g_layout.companion_scenes.empty())Want(g_layout.companion_model);
    g_outfit_request=-1;g_outfit_requested=g_outfit_given_up=false;g_outfit_waited=0;
}
bool OutfitLoaded() {
    return g_outfit_requested && !g_outfit_given_up && PED::_IS_META_PED_OUTFIT_REQUEST_VALID(g_outfit_request) &&
        PED::_HAS_META_PED_OUTFIT_LOADED(g_outfit_request);
}
bool AssetsReady(std::string& missing) {
    for(Hash h:g_models)if(!STREAMING::HAS_MODEL_LOADED(h)){missing="model "+std::to_string(h);return false;}
    // The named outfit is asked for only once his model is in (a request
    // against an unloaded model never came back on 2026-09-15), and waited
    // for ten seconds at most: a camp without the cool-weather coat beats
    // no camp, and a bare PLAYER_ZERO is dressed by hash at creation.
    const Hash arthur=Key("PLAYER_ZERO"),outfit=appearance::FixedOutfit(characters::Id::Arthur);
    if(!g_outfit_requested && !g_outfit_given_up) {
        if(PED::_DOES_META_PED_OUTFIT_EXIST_FOR_PED_MODEL(outfit,arthur)) {
            g_outfit_request=PED::_REQUEST_META_PED_OUTFIT(arthur,outfit);g_outfit_requested=true;g_outfit_waited=0;
        } else {g_outfit_given_up=true;Log("Arthur's named outfit does not exist for his model here; he is dressed by hash");}
    }
    if(g_outfit_requested && !g_outfit_given_up && !OutfitLoaded()) {
        if(++g_outfit_waited<kOutfitPatienceFrames){missing="outfit";return false;}
        g_outfit_given_up=true;Log("Arthur's named outfit did not load in 10 s; he is dressed by hash");
    }
    return true;
}
void ReleaseAssets() {
    for(Hash h:g_models)STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(h);
    g_models.clear();
    if(g_outfit_requested && PED::_IS_META_PED_OUTFIT_REQUEST_VALID(g_outfit_request))
        PED::_RELEASE_META_PED_OUTFIT_REQUEST(g_outfit_request);
    g_outfit_request=-1;g_outfit_requested=false;
}
