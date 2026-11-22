// Private implementation of race_showcase.cpp; included inside its original namespace.
// Display session safety, model ownership and entry cleanup.
void Log(const std::string& message)
{
    horse_races::log_sink::Shared().Write("[RaceShowcase] " + message);
}

void Notify(const std::string& message)
{
    UILOG::_UILOG_SET_CACHED_OBJECTIVE(i18n::Literal(message));
    UILOG::_UILOG_PRINT_CACHED_OBJECTIVE();
    UILOG::_UILOG_CLEAR_HAS_DISPLAYED_CACHED_OBJECTIVE();
    UILOG::_UILOG_CLEAR_CACHED_OBJECTIVE();
}

float DistanceSquared(Vector3 a, Vector3 b)
{
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x*x + y*y + z*z;
}

bool Exists(Ped ped) { return ped != 0 && ENTITY::DOES_ENTITY_EXIST(ped); }
void SetPhase(Phase phase) { g_phase = phase; g_phase_started = Clock::now(); }

bool SafeSession()
{
    return !NETWORK::NETWORK_IS_SESSION_ACTIVE() && !MISC::GET_MISSION_FLAG() &&
        g_player == PLAYER::PLAYER_PED_ID() && Exists(g_player) && !ENTITY::IS_ENTITY_DEAD(g_player);
}

void ReleaseOutfit()
{
    if (g_outfit_requested && PED::_IS_META_PED_OUTFIT_REQUEST_VALID(g_outfit_request)) {
        PED::_RELEASE_META_PED_OUTFIT_REQUEST(g_outfit_request);
    }
    g_outfit_requested = false;
    g_outfit_request = 0;
}

void ReleaseAssets()
{
    ReleaseOutfit();
    for (Hash model : g_requested_models) STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
    g_requested_models.clear();
}

void RequestModel(Hash model)
{
    if (std::find(g_requested_models.begin(), g_requested_models.end(), model) == g_requested_models.end()) {
        STREAMING::REQUEST_MODEL(model, false);
        g_requested_models.push_back(model);
    }
}

bool ValidModel(Hash model)
{
    return model != 0 && STREAMING::IS_MODEL_IN_CDIMAGE(model) && STREAMING::IS_MODEL_VALID(model);
}

float PlacementSetting(const char* key, const char* fallback, float minimum, float maximum)
{
    char value[64]{};
    const std::string path = g_paths.MainConfig().string();
    GetPrivateProfileStringA("RacePrototype", key, fallback, value, sizeof(value), path.c_str());
    try {
        const float parsed = std::stof(value);
        if (std::isfinite(parsed)) return std::clamp(parsed, minimum, maximum);
    } catch (...) { }
    return std::stof(fallback);
}

bool PositionOccupied(Vector3 position)
{
    // Match RDR2's own occupied-position checks (e.g. spd_sheriffoftumbleweed.c).
    // p4 is undocumented and must not be guessed as an "all obstacles" flag. Enabling
    // it made the original display probe overbroad; retain the original p5/p6 checks.
    return MISC::IS_POSITION_OCCUPIED(position.x, position.y, position.z + 1.0F, 1.8F,
        false, true, true, false, false, 0, false);
}

void LogEntry(const Entry& entry, const std::string& event)
{
    std::ostringstream out;
    out << "site=" << g_site.id << " slot=" << (&entry - g_entries.data()) + 1 << " " << event <<
        " position=" << entry.position.x << ',' << entry.position.y << ',' << entry.position.z <<
        " horse=" << entry.horse_name << " horse_handle=" << entry.horse << " rider_handle=" << entry.rider <<
        " donor=" << (entry.donor_name ? entry.donor_name : "none");
    Log(out.str());
}

void DeleteEntry(Entry& entry)
{
    firearm_guard::Release(entry.rider);
    firearm_guard::Release(entry.horse);
    if (entry.blip != 0 && MAP::DOES_BLIP_EXIST(entry.blip)) MAP::REMOVE_BLIP(&entry.blip);
    if (Exists(entry.rider)) PED::DELETE_PED(&entry.rider);
    if (Exists(entry.horse)) PED::DELETE_PED(&entry.horse);
    entry.blip = 0; entry.rider = entry.horse = 0;
    entry.shown = entry.grounded = entry.clothing_verified = false;
    entry.clothing_changes = 0;
    entry.clothing_status = "original";
    entry.grounded_since = {};
    entry.appearance.Reset();
}
