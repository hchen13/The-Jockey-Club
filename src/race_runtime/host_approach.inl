// Private host approach implementation; included by race_system.cpp.
// Model residency is not an entity allocation guarantee. Yield on a native
// refusal, keeping the frozen package and an already-owned horse.
void WaitForOpponentAllocation(const char* role,Vector3 spawn) {
    const auto now=Clock::now();
    if(g_actor_allocation_started==Clock::time_point{})g_actor_allocation_started=now;
    g_actor_allocation_next=now+std::chrono::milliseconds(250);
    g_actor_allocation_wait=true;++g_actor_allocation_attempts;
    if(g_actor_allocation_log!=Clock::time_point{} && now<g_actor_allocation_log)return;
    g_actor_allocation_log=now+std::chrono::seconds(2);
    std::array<Ped,1024> peds{};
    const int observed=world::scan::Peds(peds.data(),static_cast<int>(peds.size()));
    Vector3 normal{};float ground=0;
    const bool ground_ready=MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(
        spawn.x,spawn.y,spawn.z+10,&ground,&normal);
    Log(std::string("opponent allocation waiting role=")+role+" attempts="+std::to_string(g_actor_allocation_attempts)+
        " rider_model="+std::to_string(g_rider_model)+" horse_model="+std::to_string(g_horse_model)+
        " rider_loaded="+std::to_string(STREAMING::HAS_MODEL_LOADED(g_rider_model))+
        " horse_loaded="+std::to_string(STREAMING::HAS_MODEL_LOADED(g_horse_model))+
        " owned_horse="+std::to_string(g_opponent_horse)+" observed_peds="+std::to_string(observed)+
        " enumeration_capacity="+std::to_string(peds.size())+
        " ground_ready="+std::to_string(ground_ready)+" ground_z="+std::to_string(ground)+
        " ground_normal_z="+std::to_string(normal.z)+" spawn="+std::to_string(spawn.x)+","+std::to_string(spawn.y)+","+std::to_string(spawn.z)+
        "; native refusal does not identify pool exhaustion or a bad model");
}
bool SpawnOpponent()
{
    g_fixed_spawn_site_wait=false;g_actor_allocation_wait=false;
    if(CurrentRules().Hosted() && HUD::IS_PAUSE_MENU_ACTIVE()) {
        g_fixed_spawn_visibility_wait=true;g_actor_allocation_wait=true;return false;
    }
    const auto allocation_now=Clock::now();
    if(g_actor_allocation_started!=Clock::time_point{} &&
        std::chrono::duration<double>(allocation_now-g_actor_allocation_started).count()>=15.0) {
        Log("opponent allocation timed out after 15 s; attempts="+std::to_string(g_actor_allocation_attempts));
        return false;
    }
    if(allocation_now<g_actor_allocation_next){g_actor_allocation_wait=true;return false;}
    const Ped player = PLAYER::PLAYER_PED_ID();
    Vector3 spawn = g_test_host_replace?g_test_host_position:g_variant->encounter_anchor;
    const auto& rules=CurrentRules();
    g_fixed_spawn_visibility_wait=false;
    if (rules.Hosted() && !g_opponent_horse) {
        // Allocation is invisible throughout outfit/render preparation.
        // Camera direction must not prevent work from starting on approach.
        // Honour the user's surveyed staging point. Never snap a stationary host
        // back into traffic; if the point is occupied wait, without moving it.
        bool found = false, occupied=false;
        const char* reason = "surveyed ground unavailable";
        for (int attempt=0;attempt<1;++attempt) {
            Vector3 road=spawn;
            STREAMING::REQUEST_COLLISION_AT_COORD(road);
            Vector3 normal{}; float ground{};
            if (!MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(road.x, road.y, road.z+10.0F, &ground, &normal) ||
                !std::isfinite(ground) || !destination::Finite(normal) || normal.z < 0.82F ||
                std::abs(ground-road.z) > 8.0F) { reason = "ground not ready or unsafe"; continue; }
            road.z = ground + 0.15F;
            const float clearance=g_test_host_replace?1.5F:3.5F;
            if(FixedPlayerOverlaps(road,clearance)) {
                reason="player too close to surveyed staging point";continue;
            }
            occupied=MISC::IS_POSITION_OCCUPIED(road.x,road.y,road.z+1.0F,2.0F,false,true,true,false,false,0,false);
            if(occupied){reason="surveyed staging point occupied";continue;}
            spawn = g_fixed_spawn = road; found = true; break;
        }
        if (!found) {
            // No race has been accepted and no actor has been created. An
            // occupied/unstreamed surveyed point is a wait, not a failed race.
            // Keep this instance (horse, difficulty, laps) until the spot clears.
            g_fixed_spawn_site_wait=true;
            if(g_fixed_spawn_wait_since==Clock::time_point{})g_fixed_spawn_wait_since=Clock::now();
            else if(g_camp_fade && std::chrono::duration<double>(Clock::now()-g_fixed_spawn_wait_since).count()>=kSpawnBlackoutAbortSeconds) {
                g_fixed_spawn_wait_since={};g_fixed_spawn_occupied_since={};
                AbortRace(std::string("camp race: the host could not be placed at the start in ")+std::to_string(static_cast<int>(kSpawnBlackoutAbortSeconds))+
                    " s ("+reason+"); the blackout is released",true,Ender::Game);
                return false;
            }
            if(occupied) {
                if(g_fixed_spawn_occupied_since==Clock::time_point{})g_fixed_spawn_occupied_since=Clock::now();
                else if(std::chrono::duration<double>(Clock::now()-g_fixed_spawn_occupied_since).count()>=kSpawnClearSeconds) {
                    ClearFixedSpawnOccupants(spawn);g_fixed_spawn_occupied_since=Clock::now();
                }
            } else g_fixed_spawn_occupied_since={};
            if (Clock::now() >= g_fixed_next_spawn_log) {
                Log(std::string("fixed station spawn waiting: ") + reason);
                g_fixed_next_spawn_log = Clock::now() + std::chrono::seconds(10);
            }
            return false;
        }
        g_fixed_spawn_occupied_since={};g_fixed_spawn_wait_since={};
        Log("fixed station surveyed spawn=" + std::to_string(spawn.x) + "," +
            std::to_string(spawn.y) + "," + std::to_string(spawn.z));
    }
    const Vector3 player_position = ENTITY::GET_ENTITY_COORDS(player, true, false);
    const float heading = g_test_host_replace?g_test_host_heading:
        (rules.Hosted() && SelectedFixedRoute().host_heading?*SelectedFixedRoute().host_heading:
        MISC::GET_HEADING_FROM_VECTOR_2D(player_position.x - spawn.x, player_position.y - spawn.y));
    if(!g_opponent_horse) {
        g_opponent_horse = PED::CREATE_PED(g_horse_model, spawn, heading, false, true, true, true);
        if (g_opponent_horse == 0 || !ENTITY::DOES_ENTITY_EXIST(g_opponent_horse)) {
            g_opponent_horse=0;WaitForOpponentAllocation("horse",spawn);
            return false;
        }
        ENTITY::SET_ENTITY_AS_MISSION_ENTITY(g_opponent_horse, true, true);
        appearance::HidePair(0,g_opponent_horse);
        if (rules.Hosted() && ENTITY::GET_ENTITY_MODEL(g_opponent_horse)!=g_horse_model) {
            Log("fixed horse spawn model mismatch; refusing wrong coat"); return false;
        }
        if (!InitialiseNewOpponentRanks()) return false;
        // Original horse preparation: beat_horse_race.c func_624 / func_704.
        // Random appearance alone does not guarantee tack. Apply the saddle outfit and commit
        // the variation before mounting; touch only this encounter's newly created horse.
        appearance::InitializeHorse(g_opponent_horse);
        Log("opponent horse saddle outfit applied; variation refreshed before mounting");
    } else {
        if(!ENTITY::DOES_ENTITY_EXIST(g_opponent_horse) || ENTITY::IS_ENTITY_DEAD(g_opponent_horse) ||
            ENTITY::GET_ENTITY_MODEL(g_opponent_horse)!=g_horse_model) {
            Log("owned opponent horse lost during rider allocation");return false;
        }
        spawn=ENTITY::GET_ENTITY_COORDS(g_opponent_horse,true,false);
    }
    if (!g_use_default_rider_outfit && g_rider_outfit_requested &&
        PED::_IS_META_PED_OUTFIT_REQUEST_VALID(g_rider_outfit_request) &&
        PED::_HAS_META_PED_OUTFIT_LOADED(g_rider_outfit_request)) {
        // Rockstar creates the racer from this exact metaped outfit request rather than randomizing the base model.
        g_opponent_rider = PED::_CREATE_META_PED_OUTFIT_PED(
            g_rider_outfit_request, spawn, heading, true, true, false, true);
    }
    const bool exact_outfit_created =
        g_opponent_rider != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_rider);
    if (!exact_outfit_created) {
        if (!rules.Hosted() && g_variant->archetype == RivalArchetype::Confident) {
            Log("female racer original outfit unavailable; refusing randomized clothing fallback");
            return false;
        }
        // Vanilla func_142/func_334 fall back to CREATE_PED on the same model when
        // the outfit request or outfit-based creation fails (func_490/func_624).
        Log("creating original rider via same-model outfit fallback");
        g_opponent_rider = PED::CREATE_PED(g_rider_model, spawn, heading, false, true, true, true);
        if (g_opponent_rider != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_rider)) {
            ENTITY::SET_ENTITY_VISIBLE(g_opponent_rider, false);
        }
    }
    if (g_opponent_rider == 0 || !ENTITY::DOES_ENTITY_EXIST(g_opponent_rider)) {
        g_opponent_rider=0;WaitForOpponentAllocation("rider",spawn);
        return false;
    }
    ReleaseRiderOutfitRequest();g_actor_allocation_wait=false;
    Log(std::string("original rider created: ") + (exact_outfit_created ? "exact outfit" : "same-model fallback"));
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(g_opponent_rider, true, true);
    appearance::HidePair(g_opponent_rider,g_opponent_horse);
    appearance::InitializeRider(g_opponent_rider,!exact_outfit_created,
        !rules.Hosted() && g_variant->archetype==RivalArchetype::Novice);
    const bool reviewed_voice=!rules.Hosted() || !FixedVoiceClips().empty();
    DECORATOR::DECOR_SET_BOOL(g_opponent_rider, "HAS_VOICE", reviewed_voice);
    // Only native-registered voices, never exported archive-bank aliases. The
    // approved fixed-host clip can override this default immediately before speech.
    if(reviewed_voice)AUDIO::SET_AMBIENT_VOICE_NAME(g_opponent_rider, g_rival->voice);
    AUDIO::ADD_ENTITY_TO_AUDIO_MIX_GROUP(g_opponent_rider, "horse_race_group", 0.0F);
    AUDIO::ADD_ENTITY_TO_AUDIO_MIX_GROUP(g_opponent_horse, "HORSE_RACE_GROUP", 0.0F);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_opponent_rider, true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(g_opponent_horse, true);
    g_opponent_original_horse_relationship=PED::GET_PED_RELATIONSHIP_GROUP_HASH(g_opponent_horse);
    PED::SET_PED_CAN_BE_TARGETTED(g_opponent_rider, true);
    EnsureRelationshipGroup();
    if (g_relationship_group != 0) {
        PED::SET_PED_RELATIONSHIP_GROUP_HASH(g_opponent_rider, g_relationship_group);
        PED::SET_PED_RELATIONSHIP_GROUP_HASH(g_opponent_horse, g_relationship_group);
    }
    if(!firearm_guard::ProtectPair(g_rival->character,g_opponent_rider,g_opponent_horse)) {
        Log("friendly firearm guard could not verify rider/horse proofs; refusing unprotected host");
        return false;
    }
    Log(characters::ProtectFromFirearms(g_rival->character)?
        "friendly race firearm guard verified for rider and horse; no collision/melee invulnerability":
        "race character attackable; no firearm guard added");
    PED::SET_PED_KEEP_TASK(g_opponent_rider, true);
    if (!SelectDifficultyTier()) return false;

    if (rules.Hosted()) {
        // Mounted idle: no approach task, forced orientation, position freeze,
        // countdown, awareness blip, original male result scene or unsolicited speech.
        TASK::TASK_STAND_STILL(g_opponent_horse, -1);
        TASK::TASK_STAND_STILL(g_opponent_rider, -1);
        Log("fixed horse native displayed speed readback=" + std::to_string(DisplayedAttributeLevel(g_opponent_horse, kSpeedAttribute)) +
            " controller speed=" + std::to_string(g_opponent_speed_level));
        Log("fixed host spawned hidden pending complete local outfit readiness");
        return true;
    }

    const char* scene_dictionary = g_variant->archetype == RivalArchetype::Novice
        ? "script@beat@wilderness@horseRace@novice"
        : (g_variant->archetype == RivalArchetype::Confident
                  ? "script@beat@wilderness@horseRace@confident"
                  : "script@beat@wilderness@horseRace@weird");
    g_result_playlists_requested = false;
    g_result_scene = ANIMSCENE::_CREATE_ANIM_SCENE(scene_dictionary, 64, nullptr, false, true);
    if (g_result_scene != 0 && ANIMSCENE::DOES_ANIM_SCENE_EXIST(g_result_scene)) {
        ANIMSCENE::SET_ANIM_SCENE_ENTITY(g_result_scene, "racer", g_opponent_rider, 0);
        ANIMSCENE::SET_ANIM_SCENE_ENTITY(g_result_scene, "HORSE", g_opponent_horse, 0);
        ANIMSCENE::LOAD_ANIM_SCENE(g_result_scene);
        // Request the playlists only after scene loading has completed, not in this same tick.
    } else {
        Log("warning: original result animscene could not be created");
    }
    Log("original opponent created hidden; approach task waits for shared appearance/mount audit");
    return true;
}

bool ScriptTaskActive(Ped ped, const char* task_name)
{
    const int status = TASK::GET_SCRIPT_TASK_STATUS(ped, MISC::GET_HASH_KEY(task_name), true);
    return status == 0 || status == 1;
}

float OriginalFollowDistance(Ped player)
{
    const Ped mount = PED::GET_MOUNT(player);
    float lateral_blend = 0.0F;
    float forward_blend = 0.0F;
    if (mount == 0 || !PED::GET_PED_CURRENT_MOVE_BLEND_RATIO(mount, &lateral_blend, &forward_blend)) {
        return 15.0F;
    }
    if (forward_blend == 0.0F) {
        return 15.0F;
    }
    if (forward_blend == 1.0F) {
        return 20.0F;
    }
    if (forward_blend > 1.0F && forward_blend <= 1.5F) {
        return 30.0F;
    }
    if (forward_blend > 1.5F && forward_blend <= 2.0F) {
        return 35.0F;
    }
    return 45.0F;
}

bool PlayerIsRidingAwayFromOpponent(Ped player)
{
    const Ped mount = PED::GET_MOUNT(player);
    float lateral_blend = 0.0F;
    float forward_blend = 0.0F;
    return mount != 0 && PED::GET_PED_CURRENT_MOVE_BLEND_RATIO(mount, &lateral_blend, &forward_blend) &&
        !PED::IS_PED_FACING_PED(player, g_opponent_rider, 80.0F) && forward_blend >= 1.0F;
}

void MaintainOriginalApproachTask(Ped player, bool force_follow = false)
{
    const float distance = std::sqrt(DistanceSquared(
        ENTITY::GET_ENTITY_COORDS(g_opponent_rider, true, false),
        ENTITY::GET_ENTITY_COORDS(player, true, false)));
    if (distance < 6.0F) {
        if (!ScriptTaskActive(g_opponent_rider, "SCRIPT_TASK_TURN_PED_TO_FACE_ENTITY")) {
            TASK::TASK_TURN_PED_TO_FACE_ENTITY(g_opponent_rider, player, -1, -1.0F, -1.0F, -1.0F);
        }
    } else if ((force_follow || distance <= OriginalFollowDistance(player)) &&
               !ScriptTaskActive(g_opponent_rider, "SCRIPT_TASK_FOLLOW_AND_CONVERSE_WITH_PED")) {
        TASK::TASK_FOLLOW_AND_CONVERSE_WITH_PED(
            g_opponent_rider, player, 0, 0, -1.0F, -1.0F, 8, 0, 0, 3.0F, 4.0F);
        g_follow_approach_started = true;
        g_follow_approach_started_at = Clock::now();
        Log("started original follow-and-converse approach task");
    }
}

void BeginApproach()
{
    PrepareRaceMusic();
    if (CurrentRules().Hosted()) {
        // Inspect must show the real race instance before either paid/free choice.
        // No model defaults or save files are changed; all exit paths own rollback.
        if (!EnsurePlannedOpponentRanks("fixed-offer-preview")) {
            AbortRace("fixed host preview ranks failed validation/readback",true);return;
        }
        BeginMarySession();
        if (!CreateOfferPrompts()) { AbortRace("fixed host interaction unavailable", true); return; }
        SetState(State::Offering);
        return;
    }
    // Issue the riding task only after the shared pipeline confirms mounting.
    // Queuing it on an unmounted hidden ped can accidentally create a foot task.
    const Vector3 player_position=ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(),true,false);
    Vector3 player_road_node{};
    int approach_sequence=0;
    TASK::OPEN_SEQUENCE_TASK(&approach_sequence);
    if(PATHFIND::GET_CLOSEST_VEHICLE_NODE(player_position,&player_road_node,0,3.0F,0.0F))
        MoveInTrafficToDestination(0,player_road_node,5.0F,1.25F,8704);
    MoveInTraffic(0,1.25F,8704);
    TASK::CLOSE_SEQUENCE_TASK(approach_sequence);
    TASK::TASK_PERFORM_SEQUENCE(g_opponent_rider,approach_sequence);
    TASK::CLEAR_SEQUENCE_TASK(&approach_sequence);
    Log("original mounted approach started after shared appearance audit");
    if (g_variant->id == 2) {
        AUDIO::START_AUDIO_SCENE("Horse_Race_Beat_Train_scene");
    }
    AUDIO::START_AUDIO_SCENE("horse_race_scene");
    g_offer_phase = 0;
    g_offer_phase_started = Clock::now();
    SetState(State::Approaching);
}

void BeginOffering()
{
    if (CurrentRules().Hosted()) { BeginApproach(); return; }
    const Ped player = PLAYER::PLAYER_PED_ID();
    MaintainOriginalApproachTask(player, true);
    TASK::TASK_LOOK_AT_ENTITY(g_opponent_rider, player, 7500, 0, 51, 0);
    PlaySpeech(g_opponent_rider, player, g_variant->goal_speech, nullptr);
    g_offer_phase = 1;
    g_waiting_line_index = 0;
    g_offer_phase_started = Clock::now();
    SetState(State::Offering);
}

bool OfferLineFinished(double timeout_seconds = 5.0)
{
    const double elapsed = std::chrono::duration<double>(Clock::now() - g_offer_phase_started).count();
    return (elapsed >= 0.65 && !SpeechPlaying(g_opponent_rider)) || elapsed >= timeout_seconds;
}
