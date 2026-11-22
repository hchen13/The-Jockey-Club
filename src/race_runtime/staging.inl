// Private staging implementation; included by race_system.cpp.
void QueueAcceptedLine()
{
    if(!CurrentRules().Hosted() || g_accept_line_queued)return;
    // Not a word until the picture is back. A line queued at acceptance starts
    // in the moment before the fade and is swallowed by it; one queued under
    // the fade simply expires. For a staged race the honest cue is the first
    // visible countdown second - three seconds is room for any of these lines.
    if(CAM::IS_SCREEN_FADED_OUT() || CAM::IS_SCREEN_FADING_IN())return;
    if(g_state==State::PreparingCircuit && !g_circuit_start.CountdownVisible())return;
    g_mary.Queue(MISC::GET_RANDOM_INT_IN_RANGE(0,3)==0 ? marybeth_dialogue::Event::Start :
        marybeth_dialogue::Event::Confirm,MaryNow(),10.0);
    g_accept_line_queued=true;
}
// The yellow line and the host's accepted/start line belong to the moment the
// player chooses to race, not to the end of the countdown: during the blackout
// and the grid countdown the minimap must already show where the race goes.
// Drawing early never reveals more than the race itself will: circuits still
// use the same continuous race window, and the lap engine is untouched.
void ShowEarlyGuidance()
{
    if(g_guidance_early || !CurrentRules().Hosted())return;
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(CurrentRules().uses_lap_engine) {
        if(!g_laps.Frozen() || g_laps.plan.course.path.points.size()<2)return;
        if(!g_lap_guide.Draw(g_laps.plan,g_laps.player,MISC::GET_HASH_KEY("COLOR_YELLOW"),
            "TJC_GUIDE_"+std::to_string(GetCurrentProcessId()),g_course_serial,true))return;
    } else if(HasRecordedCourse()) {
        if(!UsesSadieRemainingGps())MAP::START_GPS_MULTI_ROUTE(MISC::GET_HASH_KEY("COLOR_YELLOW"),true,false);
        if(!g_course_gps.Register(g_course,"TJC_COURSE_"+std::to_string(GetCurrentProcessId())+
            "_"+std::to_string(++g_course_serial)))return;
        course::Progress opening;opening.Update(g_course,ENTITY::GET_ENTITY_COORDS(player,true,false));
        if(!g_course_gps.Draw(g_course,opening,MISC::GET_HASH_KEY("COLOR_YELLOW"),true,false,UsesSadieRemainingGps()))return;
        if(!UsesSadieRemainingGps())MAP::SET_GPS_MULTI_ROUTE_RENDER(true);
    } else return;
    g_guidance_early=true;
    Log("route guidance drawn at acceptance; visible through the blackout and countdown");
}

void StartRaceImmediately()
{
    g_start_lines={};
    // Every hosted race starts under an owned blackout; what that blackout
    // clears is the recorded line where there is one, the start area otherwise.
    const auto& rules=CurrentRules();
    const bool staged=rules.blackout_clearance;
    if(!staged) {
        // No staged blackout to hand it to; give the screen back before the flag.
        if(g_camp_fade){CAM::DO_SCREEN_FADE_IN(500);g_camp_fade=false;}
        StartRaceNow();return;
    }
    DestroyOfferPrompts();
    if(MISC::GET_MISSION_FLAG() || g_circuit_scene.Busy()) {
        AbortRace("circuit preparation unavailable: another activity or prior street cleanup",true);return;
    }
    const Ped player=PLAYER::PLAYER_PED_ID();
    const auto& definition=SelectedFixedRoute().circuit;
    // The opening nodes: a ring's or a trip's whole line is its shared list, a
    // recorded-laps course has an entry lap of its own (a single trip's entry
    // list is empty, and the one-on-one's grid failed on it, 2026-09-18).
    const std::vector<Vector3> entry=rules.uses_lap_engine?
        ((definition->single_loop || definition->single_trip)?definition->shared:definition->entry):
        (HasRecordedCourse()?g_course.points:std::vector<Vector3>{g_route_start,g_route_finish});
    const std::vector<Ped> roster{player,g_player_mount,g_opponent_rider,g_opponent_horse};
    const bool scene=rules.uses_lap_engine?
        g_circuit_scene.Begin(*definition,g_laps.plan,roster,CircuitNow(),Log,rules.continuous_clearance):
        (entry.size()>=2 && g_circuit_scene.BeginDestination(CircuitNow(),Log,roster,entry[0],entry[1]));
    if(!scene) {
        AbortRace("circuit preparation failed: "+g_circuit_scene.error,true);return;
    }
    if(g_camp_fade){g_circuit_scene.AdoptBlackout();g_camp_fade=false;Log("camp race: the starting scene has the blackout");}
    // A destination route has no recorded line, and its grid faced the finish
    // as the crow flies: at Van Horn South that stood it on the embankment
    // beside the road (slot ground 1.5 m down at 45 degrees, every attempt,
    // 2026-09-18). Faced along the road at the start instead, the way the
    // finish lies, and centred on that road; a start with no road node within
    // reach keeps the old facing.
    std::vector<Vector3> opening=entry;
    if(!rules.uses_lap_engine && !HasRecordedCourse() && opening.size()>=2) {
        Vector3 node{};float heading=0;
        if(PATHFIND::GET_CLOSEST_VEHICLE_NODE_WITH_HEADING(opening[0],&node,&heading,1,3,0) &&
            destination::Finite(node) && std::isfinite(heading) &&
            destination::Distance(node,opening[0])<=12 && std::abs(node.z-opening[0].z)<=4) {
            const float radians=heading*.01745329252F;
            float fx=-std::sin(radians),fy=std::cos(radians);
            const float dx=opening[1].x-opening[0].x,dy=opening[1].y-opening[0].y;
            if(fx*dx+fy*dy<0){fx=-fx;fy=-fy;}
            const float offset=(opening[0].x-node.x)*fx+(opening[0].y-node.y)*fy;
            const Vector3 centre{node.x+fx*offset,node.y+fy*offset,node.z};
            opening[0]=centre;opening[1]={centre.x+fx*20,centre.y+fy*20,centre.z};
            Log("destination grid faced along the road: node="+std::to_string(node.x)+","+std::to_string(node.y)+","+std::to_string(node.z)+
                " heading="+std::to_string(heading)+" centre="+std::to_string(centre.x)+","+std::to_string(centre.y));
        } else Log("destination grid: no road node within 12 m of the start; faced at the finish as before");
    }
    if(!CurrentRules().Field() && (opening.size()<2 || !g_circuit_start.Begin(g_player_mount,g_opponent_horse,opening[0],opening[1],
        MISC::GET_RANDOM_INT_IN_RANGE(0,2)!=0,Log,definition && definition->road_start?&g_laps.plan.course:nullptr))) {
        AbortRace("circuit starting grid failed: "+g_circuit_start.error,true);return;
    }
    MISC::SET_MISSION_FLAG(true);g_owns_mission_flag=true;
    ShowObjective(i18n::Text("Preparing the race...","正在准备比赛……"));
    cues::PrepareAll(); // Load the cue soundsets now, not when the countdown needs them.
    if(cues::Settings().starter_shot)g_starter.Request(MISC::GET_RANDOM_INT_IN_RANGE(0,4));
    g_circuit_start.SetCue([](int digit){if(digit)g_starter.Aim();else g_starter.Fire();});
    SetState(State::PreparingCircuit); // Racing dialogue phase; the line may play now.
    ShowEarlyGuidance();
    QueueAcceptedLine(); // A race with no blackout answers right away; a staged
                         // one is refused here and queued at the countdown.
}
void TickPreparingCircuit()
{
    TickMaryVoice(); // Only tick in this state; racing banter stays in Racing.
    QueueAcceptedLine(); // Retries until the countdown is on screen, then once.
    // The starter is built in the same blackout as the grid, and registered as
    // a participant so the street clearance leaves him where he is standing.
    if(cues::Settings().starter_shot && !g_starter.ped && g_circuit_start.GridReady() &&
        CAM::IS_SCREEN_FADED_OUT()) {
        const auto& grid=g_circuit_start.CurrentGrid();
        if(!grid.slots.empty() && g_starter.Create(grid.slots.front(),grid.heading)) {
            if(!g_circuit_scene.AddParticipants({g_starter.ped})) {
                Log("starter could not be registered with the street clearance; racing without a shot");
                g_starter.Retire();g_starter.Release();
            } else Log("race starter placed beside the grid with a Cattleman");
        } else if(!g_starter.error.empty()) {
            Log("no race starter this time: "+g_starter.error+"; the race is unaffected");
            g_starter.error.clear();
        }
    }
    const Ped player=PLAYER::PLAYER_PED_ID();
    if(!player || !ENTITY::DOES_ENTITY_EXIST(player) || ENTITY::IS_ENTITY_DEAD(player) ||
        !ENTITY::DOES_ENTITY_EXIST(g_player_mount) || !ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) ||
        !ENTITY::DOES_ENTITY_EXIST(g_opponent_horse) || ENTITY::IS_ENTITY_DEAD(g_opponent_rider) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_horse) || PED::GET_MOUNT(player)!=g_player_mount ||
        PED::GET_MOUNT(g_opponent_rider)!=g_opponent_horse ||
        // Accepted beside the host, who may stand up to 50 m from the start
        // line (HostPoint): John's post is 44 m off, and measuring from the
        // line alone called every acceptance there an interruption (2026-09-21).
        (destination::Distance(ENTITY::GET_ENTITY_COORDS(player,true,false),g_route_start)>30 &&
            destination::Distance(ENTITY::GET_ENTITY_COORDS(player,true,false),g_variant->encounter_anchor)>30 &&
            !g_circuit_start.AtPlacedGrid(ENTITY::GET_ENTITY_COORDS(player,true,false)))) {
        AbortRace("circuit staging interrupted; no win/loss charge",true);return;
    }
    const auto result=g_circuit_scene.Prepare(CircuitNow(),[&]{
        return g_circuit_start.StageGroup(GridRiders()) && PrepareStartingLineFrame();
    },PrepareExtraActors);
    // A width edit can invalidate the lines after the scene has faded in.
    // Keep using the same one-rider-per-frame builder, and hold the existing
    // scene-ready countdown gate until every line matches the current plan.
    const bool ready=result==circuit_scene::Preparation::Ready && PrepareStartingLineFrame();
    if(!g_start_lines.error.empty()) {
        AbortRace("starting line preparation failed: "+g_start_lines.error,true);return;
    }
    if(!g_extra_error.empty()){AbortRace("Blackwater roster: "+g_extra_error,true);return;}
    if(!g_circuit_start.error.empty()) {
        AbortRace("circuit starting grid failed: "+g_circuit_start.error,true);return;
    }
    if(result==circuit_scene::Preparation::Failed) {
        AbortRace("circuit preparation failed: "+g_circuit_scene.error,true);return;
    }
    const bool go=g_circuit_start.Tick(ready);
    if(!g_circuit_start.error.empty()) {
        AbortRace("circuit starting grid failed: "+g_circuit_start.error,true);return;
    }
    if(go) {
        Log("race GO: native-grounded equal-progress grid; one-shot freeze after physics settle; 3 visible seconds; buffered native input; all horses released count="+std::to_string(GridRiders().size()));
        StartRaceNow();
    }
}

void TickLoadingModels()
{
    if (!PlayerInSelectedSpawnBand()) {
        AbortRace("player left safe spawn band while opponent streamed", false);
        return;
    }
    if (!CurrentRules().Hosted() && (g_player_mount == 0 || !ENTITY::DOES_ENTITY_EXIST(g_player_mount) ||
        PED::GET_MOUNT(PLAYER::PLAYER_PED_ID()) != g_player_mount)) {
        AbortRace("player changed mount", true);
        return;
    }
    // Streaming can span several ticks; recheck before any new actor allocation.
    if(!CurrentRules().Hosted() && native_progress::HoldsOurEncounters())return;
    STREAMING::REQUEST_MODEL(g_rider_model, false);
    STREAMING::REQUEST_MODEL(g_horse_model, false);
    const bool rider_loaded = STREAMING::HAS_MODEL_LOADED(g_rider_model);
    const bool horse_loaded = STREAMING::HAS_MODEL_LOADED(g_horse_model);
    if (!rider_loaded || !horse_loaded) {
        if (StateSeconds() > 15.0) {
            Log("opponent model timeout: rider=" + std::to_string(rider_loaded) +
                " horse=" + std::to_string(horse_loaded));
            AbortRace("opponent models timed out", true);
        }
        return;
    }
    if (!g_use_default_rider_outfit) {
        if (!g_rider_outfit_requested) {
            g_rider_outfit_request = PED::_REQUEST_META_PED_OUTFIT(g_rider_model, g_rival->rider_outfit);
            g_rider_outfit_requested = true;
            g_rider_outfit_requested_at = Clock::now();
            const bool valid = PED::_IS_META_PED_OUTFIT_REQUEST_VALID(g_rider_outfit_request);
            Log("models loaded; original rider outfit requested: model=" + std::string(g_rival->rider_model) +
                " outfit=" + std::to_string(g_rival->rider_outfit) +
                " request=" + std::to_string(g_rider_outfit_request) + " valid=" + std::to_string(valid));
            if (!valid) {
                UseDefaultRiderOutfit("request invalid after model loading");
            }
            // Preserve the original streaming yield, even for an already cached outfit.
            return;
        }
        if (!PED::_IS_META_PED_OUTFIT_REQUEST_VALID(g_rider_outfit_request)) {
            UseDefaultRiderOutfit("request became invalid while loading");
        } else if (!PED::_HAS_META_PED_OUTFIT_LOADED(g_rider_outfit_request)) {
            if (std::chrono::duration<double>(Clock::now() - g_rider_outfit_requested_at).count() <= 15.0) {
                return;
            }
            // Bound a stalled stream without cancelling an otherwise usable encounter.
            UseDefaultRiderOutfit("outfit loading timed out");
        }
    }
    if (!SpawnOpponent()) {
        if(g_actor_allocation_wait)return;
        if(CurrentRules().Hosted() && !g_opponent_horse && !g_opponent_rider &&
            (g_fixed_spawn_visibility_wait || g_fixed_spawn_site_wait))return;
        AbortRace("opponent could not be spawned", true);
        return;
    }
    g_rider_appearance.Reset();
    SetState(State::PreparingFixedOutfit); // Legacy enum value; now shared by every rival.
}

void ApplyCampHorseDirtSnapshot() {
    if(g_camp_horse_dirt && std::isfinite(*g_camp_horse_dirt) && *g_camp_horse_dirt>=0.F && *g_camp_horse_dirt<=1.F &&
        g_camp_horse_dirt_model && g_rider_appearance.IdentityValid() &&
        g_rider_appearance.horse==g_opponent_horse && g_camp_horse_dirt_model==g_horse_model &&
        ENTITY::GET_ENTITY_MODEL(g_opponent_horse)==g_camp_horse_dirt_model && !PlayerHasTakenOpponentHorse()) {
        PED::_SET_PED_DIRT_CLEANED(g_opponent_horse,*g_camp_horse_dirt,-1,true,true);
        Log("camp horse dirt copied to matching race horse");
    }
    g_camp_horse_dirt.reset();g_camp_horse_dirt_model=0;
}
void TickPreparingFixedOutfit()
{
    const auto& rules=CurrentRules();
    if(rules.Hosted() && HUD::IS_PAUSE_MENU_ACTIVE())return;
    const bool player_present=rules.Hosted()?PlayerInSelectedSpawnBand():
        (PlayerCanMeetRival() && DistanceSquared(ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(),true,false),
            g_variant->encounter_anchor)<=g_offer_abandon_radius*g_offer_abandon_radius);
    if (!ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) || !ENTITY::DOES_ENTITY_EXIST(g_opponent_horse) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_rider) || ENTITY::IS_ENTITY_DEAD(g_opponent_horse) ||
        !player_present) {
        AbortRace("appearance preparation interrupted", false); return;
    }
    if(!g_rider_appearance.active) {
        if(StateSeconds()>15.0){AbortRace("appearance initialization timed out",true);return;}
        if(!PED::IS_PED_READY_TO_RENDER(g_opponent_rider) || !PED::IS_PED_READY_TO_RENDER(g_opponent_horse))return;
        int preset=-1;
        std::vector<int> pool;
        if(rules.Hosted()) {
            const bool named=appearance::FixedOutfit(g_rival->character)!=0;
            const int count = named?0:PED::GET_NUM_META_PED_OUTFITS(g_opponent_rider);
            if (!named && count <= 0) return; // Named protagonist outfits need no indexed wardrobe list.
            pool=appearance::AllowedPool(g_ini_path,g_rival->character,count);
            if (pool.empty()) { AbortRace("fixed host configured outfit pool is invalid", true); return; }
            const int selected = pool.size() == 1 ? 0 : MISC::GET_RANDOM_INT_IN_RANGE(0, static_cast<int>(pool.size()));
            if (selected < 0 || static_cast<std::size_t>(selected) >= pool.size()) {
                AbortRace("fixed host outfit selection invalid", true); return;
            }
            preset=pool[static_cast<std::size_t>(selected)];
            Log(std::string("fixed host=")+g_rival->rider_model+" runtime outfits="+std::to_string(count)+" selected preset="+
                std::to_string(preset)+"; shared F9/production appearance path");
        }
        const bool female=rules.Hosted()?FixedProfile().female:g_variant->archetype==RivalArchetype::Confident;
        g_rider_appearance.Begin(g_opponent_rider,g_opponent_horse,g_rider_model,g_horse_model,preset,std::move(pool),
            appearance::Preferred(g_rival->character),appearance::FixedOutfit(g_rival->character)?-1:(female?1:0),Log,
            Clock::now(),appearance::FixedOutfit(g_rival->character));
    }
    const auto status=g_rider_appearance.Tick();
    if(status==appearance::Status::Failed){AbortRace("rider appearance: "+g_rider_appearance.issue,true);return;}
    if(status!=appearance::Status::Ready)return;
    if(rules.Hosted()) {
        // Reveal only the fully audited pair, in ordinary view or off-screen.
        // A real player/mount overlap remains a wait at the surveyed post.
        const Ped viewer=PLAYER::PLAYER_PED_ID();
        const float reveal_clearance=g_test_host_replace?1.5F:3.5F;
        if(!viewer || !ENTITY::DOES_ENTITY_EXIST(viewer))return;
        const bool overlap=FixedPlayerOverlaps(g_fixed_spawn,reveal_clearance);
        const bool view_wait=!g_test_host_replace && !FixedSpawnMayAppear(g_fixed_spawn);
        if(overlap || view_wait) {
            if(Clock::now()>=g_fixed_next_spawn_log) {
                const char* reason=overlap?"player/own mount overlaps surveyed post":
                    PLAYER::IS_PLAYER_FREE_AIMING(PLAYER::PLAYER_ID())?"free aim view":
                    !CAM::IS_GAMEPLAY_CAM_RENDERING()?"cinematic/non-gameplay view":"magnified/invalid camera FOV";
                Log("fixed host ready but reveal waiting: site="+std::to_string(g_variant->id)+
                    " reason="+reason+"; outfit audited; surveyed post retained");
                g_fixed_next_spawn_log=Clock::now()+std::chrono::seconds(3);
            }
            return;
        }
    }
    if(!g_rider_appearance.Reveal())return;
    // Reveal re-audits the final render-ready identity and can itself wait.
    // Apply after it succeeds, in the same script frame before approaching,
    // so asynchronous outfit preparation cannot overwrite the snapshot.
    ApplyCampHorseDirtSnapshot();
    if(rules.Hosted()) {
        PreviousFixedHorse()=g_horse_model;
        const auto player_position=ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(),true,false);
        Log("fixed host pair revealed: site="+std::to_string(g_variant->id)+
            " distance_m="+std::to_string(std::sqrt(DistanceSquared(player_position,g_fixed_spawn)))+
            (FixedSpawnOutOfView(g_fixed_spawn)?" gate=off-camera":" gate=normal-view"));
    }
    g_test_host_replace=false;
    BeginApproach();
}
