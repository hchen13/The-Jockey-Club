// Private guidance implementation; included by race_system.cpp.
bool IsCampStation(const Station& station){return std::string_view(station.key).starts_with("arthur-camp");}
bool StationHostAvailable(const Station& station)
{
    if(!station.route)return false;
    if(!progression::runtime::Open(station.key))return false;
    // A camp station's host is Arthur in his camp: there is no pickup to
    // rearm and nobody rides off. It is on the map exactly while the camp
    // says its marker belongs there.
    if(IsCampStation(station))return station.camp_marker.has_value();
    if(!EraAllows(*station.route))return false;
    if(g_variant==&station.variant && g_state!=State::Scanning && g_state!=State::Ready && g_state!=State::Disabled)return true;
    return ModelRetryAllowed(station.variant.id) && !FixedDirectionRetirementPending(&station.variant) &&
        !g_fixed_direction_renew[StationSlot(station)] && !station.waiting_for_leave;
}
// Markers of stations whose host cannot come go dark until one can.
void SyncStationBlips()
{
    if(!g_fixed_race_enabled)return;
    for(auto& station:g_stations) {
        if(station.start_blip && !StationHostAvailable(station)) {
            RemoveBlip(station.start_blip);station.icon_sprite=0;
            Log(std::string("station marker hidden: ")+station.key+" has no host to meet until the pickup rearms");
        }
    }
}
void AddEncounterStartBlips()
{
    const bool loading=DLC::GET_IS_LOADING_SCREEN_ACTIVE();
    if(loading){native_candidates::Reset();native_progress::g_state={};}
    if(loading || !g_show_start_markers || !g_show_random_race_world_map_markers || g_state==State::Disabled ||
        g_state==State::Racing) {
        for(Blip& blip:g_start_blips)RemoveBlip(blip);
        return;
    }
    const bool native_wait=native_progress::HoldsOurEncounters();
    const auto native_points=native_wait && native_progress::g_state.status==native_reward::Status::NotAwarded?
        native_candidates::VisiblePoints():std::array<bool,11>{};
    // Readiness limits creation, never removal after a point reroll or stale handle.
    bool can_create=Clock::now()>=g_encounter_marker_retry_at && g_race_blip_texture_requested &&
        TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(kRaceBlipTextureDict);
    for(const auto& variant:kOriginalRaceVariants) {
        Blip& blip=g_start_blips[static_cast<std::size_t>(variant.id)];
        const bool visible=native_wait?native_points[variant.id]:EncounterPointLive(variant);
        if(!visible){RemoveBlip(blip);continue;}
        if(blip && !MAP::DOES_BLIP_EXIST(blip))blip=0;
        if(blip || !can_create)continue;
        blip=MAP::BLIP_ADD_FOR_COORDS(kEncounterCompanionBlipStyle,variant.encounter_anchor);
        if(!blip)continue;
        // Range and visibility are separate: keep remote map pins, never radar.
        if(!MAP::BLIP_ADD_MODIFIER(blip,kEncounterAlwaysRangeModifier) ||
            !MAP::BLIP_ADD_MODIFIER(blip,kEncounterMapOnlyModifier)) {
            RemoveBlip(blip);g_encounter_marker_retry_at=Clock::now()+std::chrono::seconds(10);
            Log("encounter map-only modifier failed; launcher removed, retry in 10 game seconds");
            can_create=false;continue;
        }
        MAP::SET_BLIP_SPRITE(blip,kEncounterCompanionBlipSprite,true);
        SetBlipDisplayName(blip, i18n::Text("Horse race", "遭遇赛马"));
    }
}
void AddStartBlip()
{
    if ((!g_show_start_markers && !g_fixed_race_enabled) || g_state == State::Disabled) {
        return;
    }
    if (!g_race_blip_texture_requested) {
        map_textures::Acquire(map_textures::Dictionary::Story,map_textures::Owner::RaceMarkers);
        g_race_blip_texture_requested = true;
        Log("race marker SP texture requested; no launchers until loaded");
    }
    // Non-blocking: the scheduler continues while UI assets stream. Retain our
    // texture reference for the entire marker lifetime, not just the first frame.
    if (!TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(kRaceBlipTextureDict)) return;
    // A configured station has a marker; an unconfigured one is not on the map,
    // and neither is one whose host cannot come (StationHostAvailable).
    if (g_fixed_race_enabled) for (auto& station : g_stations) {
        if (!station.route || station.start_blip || !StationHostAvailable(station)) continue;
        station.start_blip = MAP::BLIP_ADD_FOR_COORDS(kRaceActivityBlipStyle,
            station.camp_marker?*station.camp_marker:station.variant.encounter_anchor);
        station.icon_sprite = 0;
        if(station.start_blip && station.camp_marker)SetBlipDisplayName(station.start_blip,FixedRouteTitle(&station.variant).c_str());
    }
    UpdateFixedRaceIcon();
    AddEncounterStartBlips();
}

void RemoveStartBlips()
{
    for (auto& station : g_stations) { RemoveBlip(station.start_blip); station.icon_sprite = 0; }
    ReleaseFixedIconTexture();
    g_fixed_icon_failed = false;
    g_fixed_icon_retry_at={};g_fixed_icon_failure_reported=false;
    for (Blip& blip : g_start_blips) {
        RemoveBlip(blip);
    }
    if (g_race_blip_texture_requested) {
        map_textures::Release(map_textures::Dictionary::Story,map_textures::Owner::RaceMarkers);
        g_race_blip_texture_requested = false;
    }
}

void TickRaceMapMarkers()
{
    horse_diagnostics::Scope stage_cost("race: map markers");
    // Map anchors describe activities, not the lifetime of the last opponent.
    // Restore them on the first result/departure frame, including while the
    // finish reaction waits for its listener. Keep texture-readiness guards and
    // retry streaming without ending dialogue, spawning a host or rearming a site.
    SyncStationBlips();
    AddEncounterStartBlips(); // Revoke stale permission, retry only owned map launchers.
    if (g_state == State::Result || g_state == State::Departing) {
        AddStartBlip();
    } else {
        // A station whose host can come again (the retired pair released, the
        // player back in range) gets its marker back the same frame - while
        // idle only: a race in progress keeps the launchers as they are.
        if(g_fixed_race_enabled && (g_state==State::Scanning || g_state==State::Ready) &&
            std::any_of(g_stations.begin(),g_stations.end(),
            [](const Station& s){return s.route && !s.start_blip && StationHostAvailable(s);}))AddStartBlip();
        UpdateFixedRaceIcon();
    }
}

void UpdateCircuitLapMarker()
{
    // A one-lap plan has no lap line and keeps its finish marker; an unfrozen
    // plan has no laps at all. The names are the route's own start and finish
    // points - this marker used to say "Valentine Butcher" on every circuit.
    if(g_laps.plan.Total()<2 || !g_finish_blip || !MAP::DOES_BLIP_EXIST(g_finish_blip))return;
    const bool final=g_laps.plan.Current(GetAutoSurveyState().running?g_laps.opponent:g_laps.player)+1==g_laps.plan.Total();
    MAP::SET_BLIP_COORDS(g_finish_blip,final?g_route_finish:g_laps.plan.profiles[1].points.front());
    const auto& route=SelectedFixedRoute();
    const std::string name=final?i18n::Text("Finish: ","终点：")+FixedFinishName():
        i18n::Text("Lap line: ","计圈点：")+i18n::Label(route.start.name,route.start.name_zh);
    SetBlipDisplayName(g_finish_blip,name.c_str());
}

// The A->B recorded line lives on the vanilla custom GPS channel, which the
// engine can drop mid-race. The channel's own 500 ms route-found probe restarts
// it from the player's live segment; each restart is reported with where the
// player was, so a recording can tell a restored channel from any other blink.
bool UsesSadieRemainingGps()
{
    const auto* station=StationOf(g_variant);
    return station && course::UseSadieRemainingGps(station->key,CurrentRules().Hosted(),
        CurrentRules().uses_lap_engine,HasRecordedCourse());
}
bool DrawCourseGps()
{
    const auto& viewed=GetAutoSurveyState().running?g_course_rival:g_course_player;
    const auto recovered=g_course_gps.recoveries;
    const bool drawn=g_course_gps.Draw(g_course,viewed,MISC::GET_HASH_KEY("COLOR_YELLOW"),false,false,UsesSadieRemainingGps());
    if(g_course_gps.recoveries!=recovered)Log("shared course GPS missing channel restored from the viewed rider's live segment"
        " along_m="+std::to_string(viewed.current.along)+" deviation_m="+std::to_string(viewed.current.distance)+
        " recovery="+std::to_string(g_course_gps.recoveries)+"; progress and adjudication unchanged");
    return drawn;
}
// The minimap guidance: both GPS channels and the finish marker. It goes with
// the rest of the navigation, except after a lost open race on a recorded line:
// the player still has to ride to the finish for the result scene and keeps the
// line and the marker as guidance until they get there or the host departs.
// (Vanilla clears both the instant the rival wins; the mod chooses otherwise.)
bool g_guidance_retained=false;
void StopGuidance()
{
    MAP::SET_GPS_MULTI_ROUTE_RENDER(false);
    MAP::SET_GPS_CUSTOM_ROUTE_RENDER(false,3,30);
    MAP::CLEAR_GPS_CUSTOM_ROUTE();
    MAP::CLEAR_GPS_MULTI_ROUTE();
    g_course_gps.Release();
    RemoveBlip(g_finish_blip);
    g_guidance_retained=false;
}
// The finish area as the finish itself adjudicates it: inside the finish
// cylinder, not a made-up radius around the marker.
bool PlayerAtFinish()
{
    const Vector3 position=ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(),true,false);
    return destination::ArrivalFraction(position,position,g_route_finish).has_value();
}
// During the result of a race that kept its guidance: the player's course
// progress keeps moving (the racing tick no longer runs), the line keeps its
// channel probe, and both are released once the player reaches the finish.
void TickRetainedGuidance()
{
    if(!g_guidance_retained)return;
    if(!g_course_gps.ready || !HasRecordedCourse()){StopGuidance();return;}
    const Vector3 position=ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(),true,false);
    if(destination::Finite(position))g_course_player.Update(g_course,position);
    if(PlayerAtFinish()) {
        Log("recorded-course guidance released: player reached the finish area at along_m="+
            std::to_string(g_course_player.current.along));
        StopGuidance();return;
    }
    DrawCourseGps();
}
void StopNavigation(bool keep_guidance=false)
{
    g_competitive_follower.ReleaseSpeedTrials();
    if(g_competitive_follower.native_vector.Observe().state)
        Log("native vector race stop:"+g_competitive_follower.native_vector.Detail());
    if(g_competitive_follower.move_rate.observation.state!=0){
        const auto& rate=g_competitive_follower.move_rate.observation;
        Log("native move rate race stop: state="+std::to_string(rate.state)+
            " readback="+(rate.after?std::to_string(*rate.after):"unavailable"));
    }
    if(g_competitive_follower.speed_table.Status()!=horse_motion::native_speed_table::State::Idle)
        Log(std::string("private speed table race stop: ")+
            horse_motion::native_speed_table::Name(g_competitive_follower.speed_table.Status())+
            " restore_mask="+std::to_string(g_competitive_follower.speed_table.RestoreMask()));
    g_circuit_start.Close();
    RetireExtraRivals();
    g_circuit_scene.Finish(CircuitNow());
    g_laps.Stop();
    g_lap_guide.Release();g_guidance_early=false;g_accept_line_queued=false;
    g_starter.Retire();g_starter.Release();
    g_lap_guide_current=-1;
    if(!keep_guidance)StopGuidance();
}

void ReleaseMissionFlag()
{
    // Restore before reenabling save/ambient mission behaviour, not after dialogue.
    const bool extras_safe=RestoreExtraRanks();
    if (!RestoreOpponentRanks("mission ownership released") || !extras_safe) return;
    if (g_owns_mission_flag) {
        if (MISC::GET_MISSION_FLAG()) {
            MISC::SET_MISSION_FLAG(false);
        }
        g_owns_mission_flag = false;
        Log("released mission flag owned by race prototype");
    }
}

void ReleaseResultScene()
{
    if (g_result_scene != 0 && ANIMSCENE::DOES_ANIM_SCENE_EXIST(g_result_scene)) {
        if (ANIMSCENE::IS_ANIM_SCENE_RUNNING(g_result_scene, false)) {
            ANIMSCENE::ABORT_ANIM_SCENE(g_result_scene, true);
        }
        if (g_opponent_rider != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_rider)) {
            ANIMSCENE::REMOVE_ANIM_SCENE_ENTITY(g_result_scene, "racer", g_opponent_rider);
        }
        if (g_opponent_horse != 0 && ENTITY::DOES_ENTITY_EXIST(g_opponent_horse)) {
            ANIMSCENE::REMOVE_ANIM_SCENE_ENTITY(g_result_scene, "HORSE", g_opponent_horse);
        }
        ANIMSCENE::_DELETE_ANIM_SCENE(g_result_scene);
    }
    g_result_scene = 0;
    g_result_playlists_requested = false;
}
