// Per-offer roster. Private extra actors appear only in an owned blackout;
// each owns its own rank lease, appearance audit, AI recording and progress.
horse_attributes::Ranks ExtraDifficultyTarget(const opponent_difficulty::Rule& rule,
    horse_attributes::Ranks natural)
{
    const auto& route=SelectedFixedRoute();
    const hosts::Profile* profile=&FixedProfile();
    // Thoroughbred jockeys share the host's configured speed band, but keep
    // their own model's acceleration. Legacy route-only field bands and host
    // acceleration overrides/bonuses do not apply to these entrants.
    if(CurrentBreed().id=="Thoroughbred") {
        if(!rule.Valid() || !natural.Valid())return {};
        const auto speed=rule.speed.enabled?rule.speed:
            opponent_difficulty::Range{false,profile->minimum,profile->maximum};
        return {opponent_difficulty::Draw(speed,
            [](int low,int high){return MISC::GET_RANDOM_INT_IN_RANGE(low,high);}),natural.acceleration};
    }
    auto rank=DifficultyTarget(rule);
    // Other fields may explicitly choose a route-specific speed band while
    // retaining their selected horses' natural acceleration.
    if(route.field_speed_min>0) {
        const int low=route.field_speed_min-1,high=route.field_speed_max-1;
        rank.speed=low==high?low:MISC::GET_RANDOM_INT_IN_RANGE(low,high+1);
        rank.acceleration=-1;profile=nullptr;
    }
    return opponent_difficulty::ResolveNaturalAcceleration(rank,profile,natural,
        [](int low,int high){return MISC::GET_RANDOM_INT_IN_RANGE(low,high);});
}
bool PrepareExtraPlan(const opponent_difficulty::Rule& rule)
{
    if(!g_extra_rivals.empty())return false;
    g_extra_error.clear();g_multi_grid.reset();g_extra_grid_ready=false;g_main_rival_out=false;
    const auto& field=SelectedFixedRoute().field;
    if(!field)return false;
    const auto& breed=CurrentBreed();
    auto coats=g_horse_catalog;
    std::erase_if(coats,[&](const auto& c){return !c.raw.Valid() ||
        !field_race::AccompanimentMayRide(breed,FixedProfile().id,c.name,g_fixed_selected_horse_name);});
    // Count appearances, not model aliases. When the legal coats are fewer
    // than entrants, the draw exhausts each bag before a balanced repeat.
    std::set<std::string> coat_keys;
    std::erase_if(coats,[&](const auto& c){return !coat_keys.insert(horse_identity::Key(c.name)).second;});
    if(coats.empty())return false;
    // One distinct townsfolk model per entrant, so seven riders do not read as
    // clones on the grid. These are identifiers, and an identifier written from
    // memory is a guess: every one is checked against the game and the field is
    // sized to how many survive. A name that does not exist must never be able
    // to cancel a race, which is what three invented ones did on any draw that
    // reached them.
    constexpr std::array<const char*,7> candidates{"A_M_M_BLWTOWNFOLK_01","A_M_M_RHDTOWNFOLK_01",
        "A_M_M_STRTOWNFOLK_01","A_M_M_VALTOWNFOLK_01","A_M_M_ARMTOWNFOLK_01",
        "A_M_M_BIVFANCYTRAVELLERS_01","A_M_M_TUMTOWNFOLK_01"};
    std::vector<Hash> people;std::string rejected;
    for(const char* name:candidates) {
        const Hash model=MISC::GET_HASH_KEY(name);
        if(model && STREAMING::IS_MODEL_IN_CDIMAGE(model) && STREAMING::IS_MODEL_VALID(model))people.push_back(model);
        else rejected+=(rejected.empty()?"":",")+std::string(name);
    }
    if(!rejected.empty())Log("field rider models unavailable and skipped: "+rejected);
    if(people.size()<2){g_extra_error="no usable rival rider models";return false;}
    // Never ask for more rivals than there are distinct riders to mount them.
    const int ceiling=std::min<int>(field->maximum,static_cast<int>(people.size()));
    const int floor=std::min(field->minimum,ceiling);
    const int count=ceiling>floor?MISC::GET_RANDOM_INT_IN_RANGE(floor,ceiling+1):ceiling;
    if(count<1 || count>ceiling)return false;
    if(ceiling<field->maximum)
        Log("field capped at "+std::to_string(ceiling)+" rivals by available rider models");
    const auto drawn=field_race::DrawCoats(coats,g_fixed_selected_horse_name,count,
        [](int n){return MISC::GET_RANDOM_INT_IN_RANGE(0,n);});
    if(static_cast<int>(drawn.size())!=count)return false;
    for(int i=0;i<count;++i) {
        field_race::Entrant e;e.choice=drawn[static_cast<std::size_t>(i)];
        e.inspection_name=field_race::EntrantName(static_cast<std::size_t>(i));
        e.target=ExtraDifficultyTarget(rule,e.choice.raw);
        e.rider_model=people[static_cast<std::size_t>(i)];
        if(!e.target.Valid())return false;
        g_extra_rivals.push_back(std::move(e));
    }
    for(const auto& e:g_extra_rivals)for(Hash model:{e.rider_model,e.choice.model})
        if(std::find(g_extra_requested_models.begin(),g_extra_requested_models.end(),model)==g_extra_requested_models.end()) {
            STREAMING::REQUEST_MODEL(model,false);g_extra_requested_models.push_back(model);
        }
    Log("field roster locked breed="+std::string(breed.id)+" extras="+std::to_string(count)+
        "; host coat "+g_fixed_selected_horse_name+" kept out of a pool of "+std::to_string(coats.size())+
        " distinct metadata-ready coats; repeats="+(count>static_cast<int>(coats.size())?std::string("balanced bags"):std::string("none")));
    for(std::size_t i=0;i<g_extra_rivals.size();++i)Log("extra="+std::to_string(i)+" coat="+g_extra_rivals[i].choice.name+
        " raw_target="+horse_attributes::Pair(g_extra_rivals[i].target)+
        " model_raw="+horse_attributes::Pair(g_extra_rivals[i].choice.raw));
    return true;
}
bool RestoreExtraRanks()
{
    bool safe=true;
    for(auto& e:g_extra_rivals)if(!e.lease.Restore(HorseIdentity,ReadBaseRank,WriteBaseRank))safe=false;
    return safe;
}
bool ExtraHorseClaimed(const field_race::Entrant& e)
{
    const Ped player=PLAYER::PLAYER_PED_ID(),horse=e.horse.horse;
    if(!player || !horse || HorseIdentity(horse)!=e.horse)return false;
    return PED::GET_MOUNT(player)==horse || PED::_GET_RIDER_OF_MOUNT(horse,true)==player ||
        PLAYER::_GET_ACTIVE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID())==horse ||
        PLAYER::_GET_TEMP_PLAYER_HORSE(PLAYER::PLAYER_ID())==horse ||
        (e.rider.Valid() && HorseIdentity(e.rider.horse)==e.rider && PED::GET_PEDS_JACKER(e.rider.horse)==player);
}
void RetireExtraRivals()
{
    for(auto& e:g_extra_rivals) {
        if(!e.retiring){
            e.racing_assist.Reset();
            e.retiring=true;e.follower.Reset();e.appearance.Reset();RemoveBlip(e.blip);
            // The station this field raced at, captured now: the selection may
            // have moved on by the time the lease lets the pair retire.
            e.source_site=CurrentRules().Hosted()?g_variant->id:-1;
        }
        e.lease.Restore(HorseIdentity,ReadBaseRank,WriteBaseRank);
    }
    g_multi_grid.reset();g_extra_grid_ready=false;
    for(Hash model:g_extra_requested_models)STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    g_extra_requested_models.clear();
    TickExtraRetirement();
}
void TickExtraRetirement()
{
    for(auto it=g_extra_rivals.begin();it!=g_extra_rivals.end();) {
        auto& e=*it;
        if(!e.retiring){++it;continue;}
        if(!e.lease.Restore(HorseIdentity,ReadBaseRank,WriteBaseRank)){++it;continue;}
        if(HorseIdentity(e.rider.horse)!=e.rider)e.rider={};
        if(HorseIdentity(e.horse.horse)!=e.horse)e.horse={};
        const bool claimed=ExtraHorseClaimed(e);
        for(auto* id:{&e.rider,&e.horse})if(id->Valid() && !claimed &&
            (CAM::IS_SCREEN_FADED_OUT() || !ENTITY::IS_ENTITY_VISIBLE(id->horse))) {
            Ped ped=id->horse;PED::DELETE_PED(&ped);*id={};
        }
        if(e.rider.Valid() || e.horse.Valid()) {
            RetiredOpponent old{e.rider,e.horse,claimed,false,{}};old.source_site=e.source_site;
            old.inspection_model=e.choice.name?e.choice.name:"";old.inspection_name=e.inspection_name;
            old.departure_started=StartNaturalDeparture(e.rider.horse,e.horse.horse,claimed);
            g_retired_opponents.push_back(old);
        }
        it=g_extra_rivals.erase(it);
    }
}
std::vector<Ped> GridRiders()
{
    std::vector<Ped> riders{PLAYER::PLAYER_PED_ID(),g_opponent_rider};
    for(const auto& e:g_extra_rivals)if(!e.retiring)riders.push_back(e.rider.horse);
    return riders;
}
// The game's ped pool holds about 150. Where the world is already crowded -
// John's stakes race starts beside Valentine station, townsfolk and the stockyard's animals all round:
// 147 in the pool on 2026-09-21 02:36 - CREATE_PED returns nothing for the
// field's riders, and the race used to be called off on the spot. Under the
// blackout nobody sees the world, so room is made instead: ambient peds that
// belong to no script, are not the player's and are not in the race are
// deleted, farthest first, and creation is tried again for a few seconds.
// Another script's peds (the gang, a mission) are never touched; if they alone
// fill the pool the race is still called off, and the log says by how much.
constexpr double kPoolReliefSeconds=6;
int RelievePedPool(int wanted)
{
    const Ped player=PLAYER::PLAYER_PED_ID();
    const Vector3 at=ENTITY::GET_ENTITY_COORDS(player,true,false);
    const auto id=PLAYER::PLAYER_ID();
    const auto theirs=[&](Ped e){return e==player || e==PED::GET_MOUNT(player) || e==g_player_mount || e==PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(id) ||
        e==PLAYER::_GET_TEMP_PLAYER_HORSE(id) || e==PED::_GET_LAST_MOUNT(player);};
    std::vector<int> handles(2048,0);
    const int count=std::min(worldGetAllPeds(handles.data(),static_cast<int>(handles.size())),static_cast<int>(handles.size()));
    std::vector<std::pair<float,Ped>> ambient;
    for(int i=0;i<count;++i) {
        const Ped ped=handles[static_cast<std::size_t>(i)];
        if(!ped || !ENTITY::DOES_ENTITY_EXIST(ped) || PED::IS_PED_A_PLAYER(ped) || theirs(ped))continue;
        if(ENTITY::IS_ENTITY_A_MISSION_ENTITY(ped) || ped==g_opponent_rider || ped==g_opponent_horse)continue;
        const Vector3 p=ENTITY::GET_ENTITY_COORDS(ped,true,false);
        ambient.emplace_back(std::hypot(p.x-at.x,p.y-at.y),ped);
    }
    std::sort(ambient.begin(),ambient.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    int freed=0;
    for(const auto& [distance,ped]:ambient) {
        if(freed>=wanted)break;
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped,true,true);Ped handle=ped;PED::DELETE_PED(&handle);
        if(!ENTITY::DOES_ENTITY_EXIST(ped))++freed;
    }
    Log("field roster: ped pool full ("+std::to_string(count)+" in it, "+std::to_string(ambient.size())+" of them ambient); "+
        std::to_string(freed)+" ambient peds deleted under the blackout to make room");
    return freed;
}
// True while creation should simply be tried again next tick.
bool PoolReliefRetry(const char* what)
{
    auto& r=g_extra_pool_relief;
    const auto now=Clock::now();
    if(!r.refusals)r.since=now;
    ++r.refusals;
    if(std::chrono::duration<double>(now-r.since).count()>kPoolReliefSeconds) {
        g_extra_error=std::string(what)+" (ped pool full: "+std::to_string(r.freed)+" ambient peds freed over "+
            std::to_string(r.refusals)+" tries and still no room; the rest belong to other scripts)";
        return false;
    }
    // Two per missing entrant and a few spare, every time it is refused.
    r.freed+=RelievePedPool(static_cast<int>(g_extra_rivals.size())*2+4);
    return true;
}
bool PrepareExtraActors()
{
    // Returning true here means "no field to stage". Getting that wrong is
    // silent: the blackout proceeds, the grid is never begun, and the scene
    // waits for a staging that will never happen until the preparation times
    // out blaming the audience. It cost a full acceptance cycle to find once.
    if(!CurrentRules().Field())return true;
    if(!CAM::IS_SCREEN_FADED_OUT() || !g_extra_error.empty())return false;
    if(g_extra_grid_ready)return true;
    const auto& roster=SelectedFixedRoute().field;
    if(!roster || g_extra_rivals.size()<static_cast<std::size_t>(roster->minimum) ||
        g_extra_rivals.size()>static_cast<std::size_t>(roster->maximum)){g_extra_error="missing locked roster";return false;}
    for(Hash model:g_extra_requested_models)if(!STREAMING::HAS_MODEL_LOADED(model))return false;
    if(!g_multi_grid) {
        const auto& route=SelectedFixedRoute();
        if(!route.grid_heading){g_extra_error="missing recorded grid heading";return false;}
        const auto count=g_extra_rivals.size()+2;
        g_multi_grid=circuit_start::GroupGrid(route.start.position,*route.grid_heading,count,route.grid_width,
            static_cast<std::size_t>(MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(count))));
        if(!g_multi_grid){g_extra_error="invalid group grid";return false;}
    }
    bool ready=true;
    for(std::size_t i=0;i<g_extra_rivals.size();++i) {
        auto& e=g_extra_rivals[i];
        if(!e.horse.Valid() || !e.rider.Valid()) {
            const auto slot=g_multi_grid->slots[i+2];
            const float radians=g_multi_grid->heading*.01745329252F;
            // Temporary hidden assembly row behind the grid; stage horses only
            // after the shared appearance pipeline has attached every rider.
            const Vector3 spawn{slot.x+std::sin(radians)*8,slot.y-std::cos(radians)*8,slot.z};
            // Horse and rider are created one at a time and each may be refused
            // by a full pool: what exists is kept, room is made, the rest is
            // tried again next tick.
            if(!e.horse.Valid()) {
                const Ped made=PED::CREATE_PED(e.choice.model,spawn,g_multi_grid->heading,false,true,true,true);
                e.horse=HorseIdentity(made);
                if(!e.horse.Valid()){if(PoolReliefRetry("extra horse creation failed"))return false;return false;}
                ENTITY::SET_ENTITY_AS_MISSION_ENTITY(made,true,true);appearance::HidePair(0,made);
                // Registered in the frame it is created. A full pool makes its rider wait frames, and
                // a horse standing on the course that nobody has registered is a bystander to the
                // sweep, which parks it - and a parked horse cannot then be registered at all, which
                // cancelled an accepted race (the user, 2026-09-22).
                if(!g_circuit_scene.AddParticipants({made})) {
                    g_extra_error="extra clearance registration failed: "+g_circuit_scene.error;return false;
                }
            }
            const Ped horse=e.horse.horse;
            Ped rider=PED::CREATE_PED(e.rider_model,spawn,g_multi_grid->heading,false,true,true,true);
            e.rider=HorseIdentity(rider);
            if(!e.rider.Valid()){if(PoolReliefRetry("extra rider creation failed"))return false;return false;}
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(rider,true,true);appearance::HidePair(rider,horse);
            if(!g_circuit_scene.AddParticipants({rider,horse})) {
                g_extra_error="extra clearance registration failed: "+g_circuit_scene.error;return false;
            }
            const auto s=horse_attributes::Read(horse);
            if(s.model!=e.choice.model || s.model_default!=e.choice.raw){g_extra_error="extra runtime model ranks changed";return false;}
            if(s.entity_base==horse_attributes::Ranks{0,0} && s.current==horse_attributes::Ranks{0,0} && s.model_default!=horse_attributes::Ranks{0,0}) {
                WriteBaseRank(horse,5,s.model_default.speed);WriteBaseRank(horse,6,s.model_default.acceleration);
            }
            if(!e.lease.Apply(e.horse,e.choice.raw,e.target,HorseIdentity,ReadBaseRank,WriteBaseRank)) {
                g_extra_error="extra temporary attribute readback failed";return false;
            }
            appearance::InitializeHorse(horse);appearance::InitializeRider(rider,true,false);
            {
                const auto hair=appearance::RandomHair(horse);
                char text[96];std::snprintf(text,sizeof text,"extra horse hair mane=0x%08X tail=0x%08X",hair.mane,hair.tail);Log(text);
            }
            // Aim at one of them and the prompt names the rider the lap HUD
            // calls the leader, not the crowd model they were built from.
            PED::_SET_PED_PROMPT_NAME(rider,i18n::Literal(field_race::EntrantName(i)));
            for(Ped p:{rider,horse})PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(p,true);
            PED::SET_PED_KEEP_TASK(rider,true);
            e.appearance.Begin(rider,horse,e.rider_model,e.choice.model,-1,{},-1,0,Log);
        }
        const auto status=e.appearance.Tick();
        if(status==appearance::Status::Failed){g_extra_error="extra appearance: "+e.appearance.issue;return false;}
        if(status!=appearance::Status::Ready){ready=false;continue;}
        if(ATTRIBUTE::GET_ATTRIBUTE_RANK(e.horse.horse,5)!=e.target.speed ||
            ATTRIBUTE::GET_ATTRIBUTE_RANK(e.horse.horse,6)!=e.target.acceleration) {
            g_extra_error="extra displayed attributes differ from locked quote";return false;
        }
    }
    if(!ready)return false;
    for(auto& e:g_extra_rivals)if(!e.appearance.Reveal())return false;
    std::vector<Ped> horses{g_player_mount,g_opponent_horse};
    for(const auto& e:g_extra_rivals)horses.push_back(e.horse.horse);
    g_extra_grid_ready=g_circuit_start.BeginGroup(horses,g_multi_grid,Log);
    if(!g_extra_grid_ready)g_extra_error=g_circuit_start.error;
    return g_extra_grid_ready;
}
// A stable per-rider 80-100% share of the surveyed usable corridor. Scale
// the solved offsets, not the legal corridor itself: every rider is judged
// against the same boundary, with the solver's safety margin intact.
