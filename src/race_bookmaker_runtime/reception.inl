// Private implementation of race_bookmaker_runtime.cpp; included inside its original namespace.
// Venue model readiness, markers, clerk lifecycle and race entry interaction.
void RequestModels(){
    for(const auto& j:jockeys){STREAMING::REQUEST_MODEL(j.horse_hash,false);STREAMING::REQUEST_MODEL(j.rider_hash,false);}
    cues::PrepareAll();
    if(cues::Settings().starter_shot)race_starter.Request(MISC::GET_RANDOM_INT_IN_RANGE(0,4));
}
bool ModelsReady(){
    const bool racers_ready=std::all_of(jockeys.begin(),jockeys.end(),[](const Jockey& j){return
        STREAMING::HAS_MODEL_LOADED(j.horse_hash)&&STREAMING::HAS_MODEL_LOADED(j.rider_hash);});
    // Give the tiny stock starter model a bounded loading opportunity too.
    // Never turn its absence into the old audience-dependent start failure.
    return racers_ready && (!cues::Settings().starter_shot || !race_starter.requested ||
        STREAMING::HAS_MODEL_LOADED(race_starter.model) || Now()-entered_at>=2);
}
void TickMarkers(){
    const Hash previous=markers.Sprite();
    if(progression::runtime::Betting())markers.Tick(Now());
    else markers.Reset();
    if(previous!=markers.Sprite())Log(std::string("venue map icons: ")+
        (markers.Sprite()==bookmaker_map::kRaceSprite?"MP horse ready; shared texture retained":
         markers.Sprite()==bookmaker_map::kFallbackSprite?"SP horse fallback ready; awaiting MP glyph":
         "hidden until a horse glyph is ready or betting is unlocked"));
}
void TickClerk(){
    if(!progression::runtime::Betting()) {
        RemovePrompts();DeleteOwnedClerk();
        return;
    }
    if(last_language!=tr::language){
        last_language=tr::language;
        if(Same(clerk_id) && clerk_revealed)CreatePrompts();
    }
    if(phase!=Phase::Idle || !PlayerOkay() || horse_races::ProgressionBusy() || horse_races::GetMotionContext().present ||
        horse_races::CampRaceUnderway()){EnablePrompts(false);return;}
    const auto player=PLAYER::PLAYER_PED_ID();const auto location=ENTITY::GET_ENTITY_COORDS(player,true,false);
    std::size_t nearest=active_venue;
    for(std::size_t i=0;i<bm::kVenues.size();++i)
        if(destination::Distance(location,Point(bm::kVenues[i].clerk))<
            destination::Distance(location,Point(bm::kVenues[nearest].clerk)))nearest=i;
    if(nearest!=active_venue){
        // The sites are far apart. Only retire a clerk that is safely outside
        // the player's visible neighbourhood, never an active race's cast.
        if(clerk && (destination::Distance(location,Point(Venue().clerk))<=90 ||
            ENTITY::IS_ENTITY_ON_SCREEN(clerk)))return;
        RemovePrompts();DeleteOwnedClerk();
        if(clerk_model_requested)STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(MISC::GET_HASH_KEY(Venue().clerk_model));
        active_venue=nearest;clerk_model_requested=false;
        clerk_retry_after={};last_language=-1;
        Log(std::string("reception venue selected: ")+Venue().key);
    }
    const Vector3 clerk_point=Point(Venue().clerk);
    const float distance=destination::Distance(location,clerk_point);
    if(distance>65){
        EnablePrompts(false);
        if(clerk && distance>90){RemovePrompts();DeleteOwnedClerk();}
        return;
    }
    if(!Same(clerk_id)){
        if(clerk){RemovePrompts();DeleteOwnedClerk();Log("Clay entity lost before departure; waiting for an unseen respawn");}
        if(Clock::now()<clerk_retry_after)return;
        const Hash model=MISC::GET_HASH_KEY(Venue().clerk_model);
        if(!STREAMING::IS_MODEL_VALID(model)){
            Log(std::string("reception model unavailable: ")+Venue().clerk_model);
            clerk_retry_after=Clock::now()+std::chrono::seconds(30);return;
        }
        if(!clerk_model_requested){STREAMING::REQUEST_MODEL(model,false);clerk_model_requested=true;return;}
        if(!STREAMING::HAS_MODEL_LOADED(model) ||
            (distance<35 && CAM::IS_SPHERE_VISIBLE(clerk_point,2.0F)))return;
        STREAMING::REQUEST_COLLISION_AT_COORD(clerk_point);
        if(distance<2.0F)return;
        const auto surface=bm::RecordedReceptionGround(clerk_point.z,[&](float probe_z)
            ->std::optional<bm::ReceptionGround>{
            Vector3 normal{};float ground=0;
            if(!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(clerk_point.x,clerk_point.y,probe_z,
                &ground,&normal) || !destination::Finite(normal))return {};
            return bm::ReceptionGround{ground,normal.z};
        });
        if(!surface){
            Log("reception recorded-ground mismatch venue="+std::string(Venue().key)+
                " captured_z="+std::to_string(clerk_point.z)+"; retrying after collision streams");
            clerk_retry_after=Clock::now()+std::chrono::seconds(5);return;
        }
        const float ground=surface->z;
        if(surface->inferred)Log("reception roof rejected; using recorded standing height venue="+
            std::string(Venue().key)+" ground="+std::to_string(ground));
        clerk=horse_races::SpawnBookmakerPed(model,clerk_point.x,clerk_point.y,ground+.15F,Venue().clerk.heading);
        if(!Exists(clerk))return;clerk_id=Identify(clerk);
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(clerk,true,true);
        PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(clerk,true);
        ENTITY::FREEZE_ENTITY_POSITION(clerk,true);
        ENTITY::SET_ENTITY_VISIBLE(clerk,false);
        const int outfit_count=PED::GET_NUM_META_PED_OUTFITS(clerk);
        int preset=0;
        if(!Venue().clerk_outfits.empty()){
            const auto approved=bm::ReceptionOutfits(Venue().clerk_outfits,outfit_count);
            if(approved.empty()){
                Log("reception approved outfits unavailable: "+std::string(Venue().clerk_model));
                DeleteOwnedClerk();clerk_retry_after=Clock::now()+std::chrono::seconds(5);return;
            }
            preset=approved[static_cast<std::size_t>(MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(approved.size())))];
        }
        if(outfit_count>0 && outfit_count<=64)PED::_EQUIP_META_PED_OUTFIT_PRESET(clerk,preset,false);
        appearance::InitializeRider(clerk,outfit_count<=0,false);
        clerk_created_at=Clock::now();clerk_stable_since={};clerk_assets={};clerk_revealed=false;
        Log("Reception stand created hidden; ground="+std::to_string(ground)+
            " outfits="+std::to_string(outfit_count)+" preset="+std::to_string(preset)+" awaiting render audit");
    }
    if(!clerk_revealed){
        const auto now=Clock::now();
        const auto assets=PED::IS_PED_READY_TO_RENDER(clerk)?appearance::ReadAssets(clerk):appearance::Assets{};
        if(PED::IS_PED_READY_TO_RENDER(clerk) && assets.error.empty() && assets.count>=2){
            if(!(assets==clerk_assets) || clerk_stable_since==Clock::time_point{}){
                clerk_assets=assets;clerk_stable_since=now;
            }else if(now-clerk_stable_since>=std::chrono::milliseconds(250)){
                ENTITY::SET_ENTITY_VISIBLE(clerk,true);
                if(ENTITY::IS_ENTITY_VISIBLE(clerk)){
                    clerk_revealed=true;CreatePrompts();
                    clerk_prompt_retry_after=now+std::chrono::seconds(5);
                    const auto placed=ENTITY::GET_ENTITY_COORDS(clerk,true,false);
                    Log("Clay stand revealed; components="+std::to_string(assets.count)+
                        " prompts="+std::to_string(bool(watch_prompt&&bet_prompt))+
                        " position="+std::to_string(placed.x)+","+std::to_string(placed.y)+","+std::to_string(placed.z));
                }
            }
        }else {clerk_assets={};clerk_stable_since={};}
        if(!clerk_revealed && now-clerk_created_at>=std::chrono::seconds(8)){
            Log("Clay render audit timed out; ready="+std::to_string(PED::IS_PED_READY_TO_RENDER(clerk))+
                " components="+std::to_string(assets.count)+" issue="+assets.error);
            RemovePrompts();DeleteOwnedClerk();clerk_retry_after=now+std::chrono::seconds(5);
        }
        return;
    }
    if((!watch_prompt || !bet_prompt) && Clock::now()>=clerk_prompt_retry_after){
        CreatePrompts();clerk_prompt_retry_after=Clock::now()+std::chrono::seconds(5);
    }
    if(clerk_revealed && distance<=20 &&
        (!ENTITY::IS_ENTITY_VISIBLE(clerk) || !PED::IS_PED_READY_TO_RENDER(clerk))){
        RemovePrompts();clerk_revealed=false;clerk_stable_since={};
        Log("Clay became invisible or unrenderable near the player; interaction withdrawn");return;
    }
    EnablePrompts(distance<=15);
    if(Venue().clerk_host!=characters::Id::OtherRival){
        Entity target=0;
        const bool focused=distance<=15 && !HUD::IS_PAUSE_MENU_ACTIVE() &&
            PLAYER::GET_PLAYER_TARGET_ENTITY(PLAYER::PLAYER_ID(),&target) && target==clerk;
        if(clerk_greeting.Observe(focused,Now())){
            const unsigned mask=marybeth_dialogue::PlayerMask(ENTITY::GET_ENTITY_MODEL(player));
            const auto* profile=hosts::Find(Venue().clerk_host);
            const auto clips=ReviewedClips(Venue().clerk_host);
            std::vector<const marybeth_dialogue::Clip*> greetings;
            for(const auto& clip:clips)if(clip.event==marybeth_dialogue::Event::Greeting &&
                (clip.players&mask))greetings.push_back(&clip);
            bool spoken=false;
            if(profile && !greetings.empty()){
                const auto* clip=greetings[static_cast<std::size_t>(MISC::GET_RANDOM_INT_IN_RANGE(0,static_cast<int>(greetings.size())))];
                spoken=horse_races::PlayCampSpeech(clerk,player,clip->context,
                    clip->native_voice?clip->native_voice:profile->voice,clip->variation);
                if(spoken)Log(std::string("reception 01 greeting: ")+profile->key+" clip="+clip->id);
            }
            clerk_greeting.Attempt(Now(),spoken);
        }
    }
    if(!watch_prompt || !bet_prompt || distance>15)return;
    // REGISTER_INTERACTION_LOCKON_PROMPT owns when the group is displayed.
    // Forcing the group active each frame showed Watch/Bet without a lock-on,
    // including while Clay's drawable had not become visible.
    watch_just_pressed=HUD::_UI_PROMPT_IS_PRESSED(watch_prompt);
    bet_just_pressed=HUD::_UI_PROMPT_IS_PRESSED(bet_prompt);
    if(!watch_just_pressed && !bet_just_pressed)return;
    if(ticket.Phase()!=bm::Ticket::State::Empty && ticket.Phase()!=bm::Ticket::State::Closed){
        Notice(tr::Text("Previous wager still settling","上一笔赌注尚未结算"));return;
    }
    // A watch-only show must not inherit the selected jockey from an old
    // closed ticket and accidentally void on that rider's navigation error.
    if(ticket.Phase()==bm::Ticket::State::Closed)ticket={};
    if(PED::IS_PED_ON_MOUNT(player)){
        Notice(tr::Text("Dismount to watch or bet","下马后才能观赛或下注"));return;
    }
    if(!horse_races::PrepareBookmakerHandoff()){
        Notice(tr::Text("Current race is still busy","当前赛马尚未结束"));return;
    }
    if(!ChooseRoster()){
        ReleaseObstacleProfiles("quotation unavailable");Notice(tr::Text("Race unavailable","赛马暂不可用"));return;
    }
    player_at_accept=player;return_point=location;return_heading=ENTITY::GET_ENTITY_HEADING(player);
    Ped home_horse=PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(PLAYER::PLAYER_ID());
    if(!Exists(home_horse))home_horse=PLAYER::_GET_TEMP_PLAYER_HORSE(PLAYER::PLAYER_ID());
    return_horse=Exists(home_horse)?Identify(home_horse):race_horses::Identity{};
    if(Venue().greeting)horse_races::PlayCampSpeech(player,clerk,"GREET_CLAY",nullptr);
    if(bet_just_pressed){
        std::vector<characters::Id> chosen_roster;
        for(const auto& j:jockeys)chosen_roster.push_back(j.profile->id);
        if(!menu.Open(std::move(chosen_roster),MONEY::_MONEY_GET_CASH_BALANCE())){
            ReleaseObstacleProfiles("quotation declined");
            Notice(tr::Text("At least $1 is required","至少需要一美元"));jockeys.clear();return;
        }
        if(!OpenCatalog()){
            Log("native shop unavailable; refusing imitation catalog");menu.Close();jockeys.clear();
            ReleaseObstacleProfiles("quotation catalog unavailable");
            Notice(tr::Text("The catalog is unavailable","目录暂不可用"));return;
        }
        EnablePrompts(false);phase=Phase::Menu;StartSlowdown();
        catalog_diagnostics_left=3;catalog_diagnostic_at=Now()+.4;
        Log("native bookmaker shop opened");
    }else {EnablePrompts(false);phase=Phase::Loading;entered_at=Now();candidate_bet=false;}
}
