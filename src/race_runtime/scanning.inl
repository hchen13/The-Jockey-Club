// Private scanning implementation; included by race_system.cpp.
void TickReady()
{
    // The retirement bay is full: nothing new is spawned until a departed pair
    // is gone. One gate, before the only path that spawns - scanning and the
    // states behind it went on selecting and then failed with three different
    // stories (a silent freeze, another silent freeze, and an abort whose
    // reason said "waiting").
    if (g_retired_opponents.size()>=kRetiredOpponentLimit) return;
    if (!PlayerInSelectedSpawnBand()) {
        AbortRace("player left selected site before opponent streaming", false);
        return;
    }
    AddStartBlip();
    if (CurrentRules().Hosted()) {
        if (PlayerInSelectedSpawnBand()) BeginModelLoading();
        else AbortRace("left fixed station before opponent loading", false);
        return;
    }
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (player == 0 || !ENTITY::DOES_ENTITY_EXIST(player)) {
        return;
    }
    const float distance_squared =
        DistanceSquared(ENTITY::GET_ENTITY_COORDS(player, true, false), g_variant->encounter_anchor);
    if (distance_squared >= g_activation_min_radius * g_activation_min_radius &&
        distance_squared <= g_activation_max_radius * g_activation_max_radius && PED::IS_PED_ON_MOUNT(player) &&
        !MISC::GET_MISSION_FLAG()) {
        BeginModelLoading();
    }
}

OriginalRaceVariant* NearestAvailableFixedStation(Vector3 position,float& nearest)
{
    OriginalRaceVariant* selected=nullptr;
    for(auto& station:g_stations) {
        auto* candidate=&station.variant;
        if(candidate==g_variant)continue;
        if(!progression::runtime::Open(station.key))continue;
        if(!ModelRetryAllowed(candidate->id))continue;
        if(!station.route || g_fixed_direction_renew[StationSlot(station)])continue;
        if(FixedDirectionRetirementPending(candidate))continue;
        // The camp's races are chosen at the camp, on Arthur or on one of his
        // horses (BeginCampRace), never picked up by riding near their start:
        // the one-on-one starts at the camp itself, and this pickup put a
        // second Arthur on a horse at the camp's edge and took the horses'
        // prompts for its own (2026-09-18).
        if(IsCampStation(station) || !EraAllows(*station.route))continue;
        const float distance_squared=DistanceSquared(position,candidate->encounter_anchor);
        bool& waiting=station.waiting_for_leave;
        if(distance_squared>=50.0F*50.0F)waiting=false;
        // 50m rearms a completed pickup; fresh arrivals start invisible model/
        // outfit preparation even at the map pin. Actual player/mount overlap
        // is checked at allocation and reveal, never used to hide the pickup.
        if(waiting || distance_squared>nearest)continue;
        nearest=distance_squared;selected=candidate;
    }
    return selected;
}

bool TryHandoffFixedHost(Vector3 position)
{
    // A distant replay host must not monopolize nearby newly unlocked posts.
    // Only an idle offer can yield; acceptance, practice, staging and racing
    // retain their session. Existing retirement keeps visible actors in world.
    if(g_state!=State::Offering || !CurrentRules().Hosted() || !g_fixed_race_enabled ||
        showcase::Active() || g_horse_info_card.Active() || !destination::Finite(position) ||
        g_retired_opponents.size()>=kRetiredOpponentLimit)return false;
    const float current_distance=std::sqrt(DistanceSquared(position,g_variant->encounter_anchor));
    if(current_distance<=30.0F)return false;
    float nearest=75.0F*75.0F;
    const auto* next=NearestAvailableFixedStation(position,nearest);
    if(!next || current_distance<std::sqrt(nearest)+25.0F)return false;
    Log("idle fixed host yields to nearby station="+std::string(StationOf(next)->key)+
        "; previous="+std::string(StationOf(g_variant)->key));
    ReturnToReady();
    return true;
}

bool TrySelectFixedRace(Vector3 position)
{
    if (!g_fixed_race_enabled || CurrentRules().Hosted() || (g_state != State::Scanning && g_state != State::Ready) ||
        showcase::Active() || !destination::Finite(position)) return false;
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (player == 0 || !ENTITY::DOES_ENTITY_EXIST(player) ||
        ENTITY::IS_ENTITY_DEAD(player) || MISC::GET_MISSION_FLAG() || NETWORK::NETWORK_IS_SESSION_ACTIVE()) return false;
    float nearest=kFixedHostPreloadRadius*kFixedHostPreloadRadius;
    auto* selected=NearestAvailableFixedStation(position,nearest);
    if(!selected)return false;
    g_fixed_return_variant = g_variant;
    ReleaseRoute();
    g_variant = selected;
    g_rival = &FixedRivalDefinition();
    g_route_name = g_variant->full_route;
    g_shortcut_route_name = g_variant->shortcut_route;
    Log("selected fixed station prototype: " + SelectedFixedRoute().start.name + " -> " +
        SelectedFixedRoute().finish.name + "; host="+g_rival->rider_model+
        "; preload_distance_m="+std::to_string(std::sqrt(nearest))+"; route revision locked until encounter release");
    SetState(State::LoadingRoute);
    return true;
}

void TickScanning()
{
    if (showcase::Active() || g_state != State::Scanning || Clock::now() < g_next_site_scan) {
        return;
    }
    g_next_site_scan = Clock::now() + std::chrono::milliseconds(250);
    AddStartBlip();
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (player == 0 || !ENTITY::DOES_ENTITY_EXIST(player)) {
        return;
    }
    const Vector3 position = ENTITY::GET_ENTITY_COORDS(player, true, false);
    if (TrySelectFixedRace(position)) return;
    const OriginalRaceVariant* candidate = nullptr;
    float nearest_squared = g_activation_max_radius * g_activation_max_radius;
    for (const auto& variant : kOriginalRaceVariants) {
        if(!ModelRetryAllowed(variant.id))continue;
        if(!EncounterPointLive(variant))continue;
        const float distance_squared = DistanceSquared(position, variant.encounter_anchor);
        bool& waiting = g_site_waiting_for_leave[static_cast<std::size_t>(variant.id)];
        if (waiting) {
            if (distance_squared > g_offer_abandon_radius * g_offer_abandon_radius) {
                waiting = false;
                Log("original site #" + std::to_string(variant.id) + " rearmed after player left its area");
            }
            continue;
        }
        if (distance_squared >= g_activation_min_radius * g_activation_min_radius &&
            distance_squared <= nearest_squared) {
            candidate = &variant;
            nearest_squared = distance_squared;
        }
    }
    if (candidate != nullptr && PlayerCanMeetRival()) {
        SelectOriginalVariant(*candidate);
    }
}

// Returns true when the frame belongs to the display tool, including its closing frame.
bool HandleShowcaseInput(bool toggle_requested, bool end_activity,int page_direction=0)
{
    if (showcase::Active()) {
        const int site = showcase::SiteId();
        if (toggle_requested || end_activity) showcase::Clear(end_activity ? "end activity" : "F11 toggle");
        else if(page_direction)showcase::ChangePage(page_direction);
        else showcase::Tick();
        if (!showcase::Active()) {
            if(auto* station=StationById(site))station->waiting_for_leave=true;
            else if(site>=0 && static_cast<std::size_t>(site)<g_site_waiting_for_leave.size())
                g_site_waiting_for_leave[static_cast<std::size_t>(site)] = true;
            g_next_site_scan = {};
            AddStartBlip();
        }
        return true;
    }
    if (!toggle_requested) return false;
    if (!g_showcase_enabled) {
        ShowObjective(i18n::Text("Race display disabled in INI (ShowcaseEnabled=0).", "配置中已关闭骑手展示（ShowcaseEnabled=0）。"));
        return true;
    }
    if (g_state == State::Accepted || g_state == State::PreparingCircuit || g_state == State::Racing || g_state == State::Result ||
        g_state == State::Declined || g_state == State::Departing) {
        ShowObjective(i18n::Text("Finish or forfeit the current race before opening the outfit display.", "请先完成或退出当前比赛，再开启服装展示。"));
        return false; // Keep the race/result lifecycle advancing.
    }
    const Ped player = PLAYER::PLAYER_PED_ID();
    if (player == 0 || !ENTITY::DOES_ENTITY_EXIST(player)) return true;
    const Vector3 position = ENTITY::GET_ENTITY_COORDS(player, true, false);
    const OriginalRaceVariant* nearest = nullptr;
    float best = 250.0F * 250.0F;
    for (const auto& variant : kOriginalRaceVariants) {
        const float distance = DistanceSquared(position, variant.encounter_anchor);
        if (distance < best) { best = distance; nearest = &variant; }
    }
    const OriginalRaceVariant* fixed_nearest=nullptr;
    if(g_fixed_race_enabled)for(const auto& station:g_stations) {
        if(!station.route)continue;
        const float distance=DistanceSquared(position,station.variant.encounter_anchor);
        if(distance<best){best=distance;fixed_nearest=&station.variant;}
    }
    if(fixed_nearest) {
        const char* model=FixedRiderModel(fixed_nearest);
        if(showcase::Start({fixed_nearest->id,fixed_nearest->encounter_anchor,model,0,"A_C_HORSE_AMERICANSTANDARDBRED_BLACK",
            FixedProfile(fixed_nearest).female,false,true,FixedRaceCharacter(fixed_nearest)})) {
            if(g_state!=State::Scanning && g_state!=State::Ready) AbortRace("F11 fixed host outfit display",false);
            ReleaseRoute(); SetState(State::Scanning);
            Log(std::string("F11 display owns fixed station: all stock presets for ")+model+"; no garment transplantation");
        }
        return true;
    }
    if (nearest == nullptr) {
        ShowObjective(i18n::Text("Outfit display: stop within 250m of a race start marker and face open ground.", "服装展示：请在赛马起点 250 米以内停下，面向空地。"));
        return true;
    }
    const auto& rival = RivalDefinition(nearest->archetype);
    if (showcase::Start({nearest->id, nearest->encounter_anchor, rival.rider_model,
            rival.rider_outfit, rival.horse_model, nearest->archetype == RivalArchetype::Confident,
            nearest->archetype == RivalArchetype::Novice})) {
        // Start only validates/reserves the display: no models or entities yet. Now retire
        // an unaccepted invitation and release its references before streaming shared models.
        if (g_state != State::Scanning && g_state != State::Ready) AbortRace("F11 display requested", false);
        ReleaseRoute();
        SetState(State::Scanning);
        Log("F11 display owns site #" + std::to_string(nearest->id) + "; new invitations paused");
    }
    return true;
}
