// Private implementation of arthur_camp.cpp; included inside its original namespace.
// Scenario and map assets, outside dressing hides and bounded hide reissue.
// The game's own map files for the camp: switched on while the camp is up,
// off again when it is struck; one that was already on is left alone.
// A scenario's props - the cigar in his hand, the notebook - come with the
// scenario type, not with the ped, and have to be asked for (2026-09-16: he
// smoked with nothing between his fingers).
void RequestScenarioAssets() {
    g_scenario_assets.clear();
    for(const auto& s:g_layout.scenes) {
        const Hash h=Key(s.scenario);
        if(!h || std::find(g_scenario_assets.begin(),g_scenario_assets.end(),h)!=g_scenario_assets.end())continue;
        STREAMING::_REQUEST_SCENARIO_TYPE(h,15,0,0);
        g_scenario_assets.push_back(h);
    }
}
void ReleaseScenarioAssets() {
    for(const Hash h:g_scenario_assets)STREAMING::_REMOVE_SCENARIO_ASSET(h);
    g_scenario_assets.clear();
}
void RequestIpls() {
    for(const int hash:g_layout.ipls) {
        const Hash h=static_cast<Hash>(hash);
        Vector3 centre{};float radius=0;
        const bool sphere=STREAMING::_GET_IPL_BOUNDING_SPHERE(h,&centre,&radius);
        if(STREAMING::IS_IPL_ACTIVE_HASH(h)){Log("IPL "+std::to_string(hash)+" already active; left as it is");continue;}
        STREAMING::REQUEST_IPL_HASH(h);g_ipls.push_back(hash);
        Log("IPL "+std::to_string(hash)+" requested"+(sphere?" (centre "+std::to_string(centre.x)+","+std::to_string(centre.y)+","+
            std::to_string(centre.z)+" radius "+std::to_string(radius)+")":""));
    }
}
void ReleaseIpls() {
    // Only while the player is still John. The camp is struck the moment a
    // chapter 2 save comes up, and there these same map files are the game's
    // own, switched on by its camp script: taking them away then strips the
    // story's camp (2026-09-16).
    const Ped player=PLAYER::PLAYER_PED_ID();
    const bool ours=Exists(player) && static_cast<unsigned>(ENTITY::GET_ENTITY_MODEL(player))==kJohnModel;
    if(!ours && !g_ipls.empty())Log(std::to_string(g_ipls.size())+" map files left switched on: the player is no longer John and they are the story's own");
    for(const int hash:g_ipls) {
        const Hash h=static_cast<Hash>(hash);
        if(ours && STREAMING::IS_IPL_ACTIVE_HASH(h))STREAMING::REMOVE_IPL_HASH(h);
    }
    g_ipls.clear();
}
Any FloatArg(float v){return static_cast<Any>(std::bit_cast<std::uint32_t>(v));}
// A hide whose model is one this camp places itself is a swap: the map's copy
// goes and ours stands in its place (the tent's own woodwork, whose shelves
// the books stand on). If ours was not created - an unknown model, a model
// that would not load - the swap is off, or the tent would simply be missing.
bool SwapReady(const Hide& h) {
    bool ours=false;
    for(const auto& p:g_layout.props) {
        if(!EqualNoCase(p.model,h.model))continue;
        ours=true;
        if(g_placed.contains(p.key))return true;
    }
    return !ours;
}
// Everything the map put inside the camp but outside the circles walked
// round goes out of sight: no model list can do this, because the models the
// gang's other tents are made of are the models Dutch's tent is made of, and
// a sphere over the camp took his floor, his pillar and his awning with
// theirs (2026-09-16). What this camp placed itself is a mission entity and
// is never touched.
bool AlreadyUnseen(Hash model,const Vector3& p) {
    for(const auto& u:g_unseen)
        if(u.model==model && std::hypot(u.at.x-p.x,u.at.y-p.y,u.at.z-p.z)<=kUnseenRadius)return true;
    return false;
}
void Learn(const Unseen& u){g_known.push_back(u);g_known_index[u.model].push_back(u.at);}
// Is this a piece the table knows? By model first, so the thousands of
// objects that are nothing of ours cost one lookup each.
bool KnownAt(Hash model,const Vector3& p) {
    const auto it=g_known_index.find(model);
    if(it==g_known_index.end())return false;
    for(const auto& at:it->second)if(std::hypot(at.x-p.x,at.y-p.y,at.z-p.z)<=kUnseenRadius)return true;
    return false;
}
// The player's own camp is not the map's to take away. Its tent and stool are
// made where he pitches it, which may be inside the bowl, and a hide by model
// would take them with the map's: the tent goes invisible while the game still
// lets you walk in and out of it, which is what the player sees (2026-09-17).
// These are the three the game's own camp script places.
bool PlayerCampPiece(Hash model) {
    static const Hash kTheirs[]{Key("P_AMB_TENT01X"),Key("P_AMB_TENT03X"),Key("P_STOOLFOLDING01X")};
    return model && std::find(std::begin(kTheirs),std::end(kTheirs),model)!=std::end(kTheirs);
}
// A piece is named by a model and a place, never by a handle, so putting it
// back works in any world - including the one a save brought up underneath
// the camp, which took every handle with it.
void ShowOutside(bool handles) {
    g_hide_reissue_next=0;g_hide_reissue_active=false;
    for(const auto& u:g_unseen)
        ENTITY::REMOVE_MODEL_HIDE(FloatArg(u.at.x),FloatArg(u.at.y),FloatArg(u.at.z),FloatArg(kUnseenRadius),static_cast<Any>(u.model),1);
    if(!g_unseen.empty())Log(std::to_string(g_unseen.size())+" of the map's pieces put back");
    // The handles are only ours in the world they were hidden in: after a save
    // the numbers belong to whatever that world put in their place, so they are
    // forgotten and never touched.
    int shown=0;
    if(handles)for(const auto& [handle,id]:g_neutralised)
        if(SameDressing(handle,id)){Restore(handle);++shown;}
    if(shown)Log(std::to_string(shown)+" pieces of the camp's dressing shown again");
    g_neutralised.clear();
    g_unseen.clear();g_known_applied=false;
}
// ArthurCamp.sweep, beside the ini: one line per piece the sweep learned,
// model hash and place. It is read when a save comes up, and never written.
std::string SweepPath() {
    const std::string tail="ArthurCamp.ini";
    return g_path.size()>tail.size()?g_path.substr(0,g_path.size()-tail.size())+"ArthurCamp.sweep":std::string();
}
void LoadKnown() {
    g_known.clear();g_known_index.clear();g_known_loaded=true;
    std::ifstream in(SweepPath());
    std::string line;
    while(std::getline(in,line)) {
        std::istringstream first(line);
        std::string word;
        if(!(first>>word))continue;
        if(word=="done")continue;   // The line that said the learning had finished. It had.
        std::istringstream row(line);
        unsigned long long h=0;float x=0,y=0,z=0;
        if(!(row>>h>>x>>y>>z) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))continue;
        Learn({static_cast<Hash>(h),{x,y,z}});
    }
    if(!g_known.empty())Log(std::to_string(g_known.size())+" of the map's pieces known from the sweep file");
}

// Everything learned goes on at once - before the player is near enough for
// any of it to stream in, so nothing is ever seen to vanish. Only what is
// still outside the circles of the current layout, and never the player's own.
void ApplyKnown() {
    g_known_applied=true;
    int put_away=0;
    for(const auto& u:g_known) {
        if(g_unseen.size()>=kUnseenLimit)break;
        const Vector3 p{u.at.x,u.at.y,u.at.z};
        if(PlayerCampPiece(u.model) || !IsOutside(g_layout.outside,u.at.x,u.at.y) || AlreadyUnseen(u.model,p))continue;
        ENTITY::CREATE_MODEL_HIDE_EXCLUDING_SCRIPT_OBJECTS(u.at.x,u.at.y,u.at.z,kUnseenRadius,u.model,true);
        g_unseen.push_back(u);++put_away;
    }
    if(put_away)Log(std::to_string(put_away)+" of the map's known pieces taken away before they could be seen");
}
// The dressing the camp's own map files bring back with Dutch's tent - some
// 220 chapter 2 props - is the one thing a model hide does not reach: they
// are objects with handles, and 2026-09-17 watched them stand through a hide
// issued every 100 ms. They are hidden where they stand instead.
//
// One pass, caused by the build or by the crossing back into reach, and by
// nothing else. The object pool is enumerated once for each, never on a
// timer (C-07 A). Only a piece the sweep file already knows is touched, so
// nothing that turns up later - a hat knocked off, something dropped - can
// be taken by mistake.
void HideDressing(const char* why) {
    const Outside& o=g_layout.outside;
    if(o.radius<=0 || g_known.empty())return;
    std::vector<int> handles(16384,0);
    const int count=std::min(worldGetAllObjects(handles.data(),static_cast<int>(handles.size())),static_cast<int>(handles.size()));
    int gone=0,renewed=0,stale=0;
    for(int i=0;i<count;++i) {
        const int handle=handles[i];
        if(!Exists(handle))continue;
        const Vector3 p=ENTITY::GET_ENTITY_COORDS(handle,true,false);
        if(!std::isfinite(p.x) || !IsOutside(o,p.x,p.y))continue;   // Where it stands decides first: it is cheapest.
        if(ENTITY::IS_ENTITY_A_MISSION_ENTITY(handle) || ENTITY::IS_ENTITY_ATTACHED(handle))continue;
        const Hash model=static_cast<Hash>(ENTITY::GET_ENTITY_MODEL(handle));
        if(!model || PlayerCampPiece(model) || !KnownAt(model,p))continue;
        const auto old=g_neutralised.find(handle);
        if(old!=g_neutralised.end()) {
            if(old->second.model!=model ||
                std::hypot(p.x-old->second.at.x,p.y-old->second.at.y,p.z-old->second.at.z)>kUnseenRadius) {
                g_neutralised.erase(old);++stale;
            } else {
                // A model hide does not persist over streaming. The engine can
                // reuse the same number for the same prop while its visibility
                // and collision revert to their defaults; reassert our state.
                if(ENTITY::IS_ENTITY_VISIBLE(handle) || !ENTITY::_IS_ENTITY_FROZEN(handle) ||
                    !ENTITY::GET_ENTITY_COLLISION_DISABLED(handle)) {
                    Neutralise(handle);++renewed;
                }
                continue;
            }
        }
        Neutralise(handle);g_neutralised.emplace(handle,DressingIdentity{model,{p.x,p.y,p.z}});++gone;
    }
    // Handles not in this streamed snapshot may now belong to unrelated
    // objects. Do not restore them when the temporary camp is dismantled.
    for(auto it=g_neutralised.begin();it!=g_neutralised.end();) {
        if(!SameDressing(it->first,it->second)){it=g_neutralised.erase(it);++stale;}
        else ++it;
    }
    Log(std::to_string(gone)+" pieces of the camp's dressing hidden where they stand (one pass "+why+", over "+
        std::to_string(count)+" objects; rehidden="+std::to_string(renewed)+" stale="+std::to_string(stale)+")");
}
// Survey-marked IPL furniture can be a script object rather than static map
// geometry. The excluding variant leaves those exact tables and frames in
// view, even though they have a [Hide] entry. Use the including variant only
// for these measured spots, and only when none of this camp's own props of
// the same model can be inside the hide sphere.
bool IncludeScriptObjects(const Hide& h) {
    if(h.key.rfind("Marked",0)!=0)return false;
    const Hash model=Key(h.model);
    if(PlayerCampPiece(model))return false;
    for(const auto& own:g_objects)if(own.model==model &&
        std::hypot(own.at.x-h.position.x,own.at.y-h.position.y,own.at.z-h.position.z)<=h.radius+.25F)return false;
    return true;
}
void CreateCampHide(const Hide& h) {
    if(IncludeScriptObjects(h))
        ENTITY::CREATE_MODEL_HIDE(h.position.x,h.position.y,h.position.z,h.radius,Key(h.model),true);
    else ENTITY::CREATE_MODEL_HIDE_EXCLUDING_SCRIPT_OBJECTS(h.position.x,h.position.y,h.position.z,h.radius,Key(h.model),true);
}
void HideMapPieces() {
    // The variant that leaves script objects alone: the camp's own props near a hide are never taken.
    g_hides.clear();g_hide_reissue_next=0;g_hide_reissue_active=false;
    for(const auto& h:g_layout.hides) {
        if(!SwapReady(h)){Log("hide "+h.key+" ("+h.model+") left off: this camp places that model and did not");continue;}
        g_hides.push_back(h);
        CreateCampHide(h);
    }
    if(!g_hides.empty())Log(std::to_string(g_hides.size())+" map pieces asked to be hidden");
}
// A hide takes only what is instantiated when it is issued, and it does not
// survive the area streaming out and in again - the same finding as for the
// dressing, and the tent shells these name are map geometry, which no pass
// over the object pool can reach, so nothing else would catch them coming
// back. Sent again on the crossing back into reach, and only then.
void ReissueHides() {
    if(g_hides.empty() && g_unseen.empty())return;
    g_hide_reissue_next=0;g_hide_reissue_active=true;
    Log(std::to_string(g_hides.size()+g_unseen.size())+" map hides queued for replacement on returning");
}
void TickReissueHides() {
    if(!g_hide_reissue_active)return;
    const auto total=g_hides.size()+g_unseen.size();
    const auto end=std::min(g_hide_reissue_next+kHideReissuePerFrame,total);
    for(;g_hide_reissue_next<end;++g_hide_reissue_next) {
        // CREATE is a registration, not a verified idempotent setter. After
        // repeated trips back to camp, issuing another 1,194 registrations
        // without releasing the prior set can grow engine-side work even
        // though our own Tick remains cheap. Replace each registration in the
        // same script frame; there is no visible interval between the calls.
        if(g_hide_reissue_next<g_hides.size()) {
            const auto& h=g_hides[g_hide_reissue_next];
            ENTITY::REMOVE_MODEL_HIDE(FloatArg(h.position.x),FloatArg(h.position.y),FloatArg(h.position.z),FloatArg(h.radius),static_cast<Any>(Key(h.model)),1);
            CreateCampHide(h);
        } else {
            const auto& u=g_unseen[g_hide_reissue_next-g_hides.size()];
            ENTITY::REMOVE_MODEL_HIDE(FloatArg(u.at.x),FloatArg(u.at.y),FloatArg(u.at.z),FloatArg(kUnseenRadius),static_cast<Any>(u.model),1);
            ENTITY::CREATE_MODEL_HIDE_EXCLUDING_SCRIPT_OBJECTS(u.at.x,u.at.y,u.at.z,kUnseenRadius,u.model,true);
        }
    }
    if(g_hide_reissue_next==total) {
        g_hide_reissue_active=false;
        Log(std::to_string(total)+" map hides replaced on returning (old registrations removed first; at most "+
            std::to_string(kHideReissuePerFrame)+" per frame)");
    }
}
void ShowMapPieces() {
    g_hide_reissue_next=0;g_hide_reissue_active=false;
    // REMOVE_MODEL_HIDE takes the same six arguments; the SDK header types them Any.
    for(const auto& h:g_hides)ENTITY::REMOVE_MODEL_HIDE(FloatArg(h.position.x),FloatArg(h.position.y),FloatArg(h.position.z),FloatArg(h.radius),static_cast<Any>(Key(h.model)),1);
    g_hides.clear();
}
