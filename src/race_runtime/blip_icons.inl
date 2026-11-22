// Private blip icons implementation; included by race_system.cpp.
void StartFixedResultDepartureMotion();
void MaintainOriginalApproachTask(Ped player, bool force_follow);
bool StartOriginalOpponentRoute(bool refresh_line=false);
void AdvanceRecordedRival();
#include "../marybeth_race_runtime.inl"

void OverrideWaypointSpeed(Ped ped, float speed, bool absolute=false,
    float transition_rate=native_far_speed::kWaypointTransitionRate)
{
    if(!race_speed_policy::Enabled())return;
    // The SDK types the transition RATE as Any. Nonpositive sets the native
    // request immediately; ordinary gait changes retain the existing .4f rate.
    invoke<Void>(0x7D7D2B47FA788E85, ped, speed, 1, transition_rate, absolute?1:0);
}

void MoveInTraffic(Ped ped, float speed, int flags)
{
    // Preserve the float speed; the bundled SDK exposes this native's parameters as integer-sized Any values.
    invoke<Void>(0x8AA1593AEC087A29, ped, speed, flags, 0);
}

void MoveInTrafficToDestination(Ped ped, const Vector3& destination, float stopping_range, float speed, int flags)
{
    // Exact argument layout used by beat_horse_race.c; the SDK declares the float slots as Any.
    invoke<Void>(0xDCA3A13F7A45338B, ped, destination.x, destination.y, destination.z, stopping_range, speed, flags, 0);
}

void RemoveBlip(Blip& blip)
{
    if (blip != 0 && MAP::DOES_BLIP_EXIST(blip)) {
        MAP::REMOVE_BLIP(&blip);
    }
    blip = 0;
}

bool SetBlipDisplayName(Blip blip, const char* text)
{
    // This native takes a player/VAR_STRING, not a raw literal. Also used by the
    // installed bounty mod's label helper (RVA 0x3B460). Set names after styling.
    if (const char* display_name = MISC::VAR_STRING(10, "LITERAL_STRING", text)) {
        MAP::_SET_BLIP_NAME(blip, display_name);
        return true;
    }
    return false;
}

void AddOpponentBlip()
{
    if (g_opponent_rider == 0 || !ENTITY::DOES_ENTITY_EXIST(g_opponent_rider) ||
        ENTITY::IS_ENTITY_DEAD(g_opponent_rider)) {
        return;
    }
    if (g_opponent_blip != 0 && MAP::DOES_BLIP_EXIST(g_opponent_blip)) return;
    // Unlike the original generic helper, never remove another script's entity blip.
    g_opponent_blip = MAP::BLIP_ADD_FOR_ENTITY(kOriginalRivalBlipStyle, g_opponent_rider);
    g_opponent_colour_ready = false;
    if (g_opponent_blip != 0) {
        MAP::BLIP_ADD_MODIFIER(g_opponent_blip, kOriginalRivalBlipAlert);
        g_opponent_colour_ready=bookmaker_blips::ApplyColour(g_opponent_blip,
            bookmaker_blips::Desired(CurrentRules().breed.has_value(),g_host_burst_shown));
        if (CurrentRules().Hosted()) SetBlipDisplayName(g_opponent_blip, FixedHostName());
        else MAP::SET_BLIP_NAME_FROM_TEXT_FILE(g_opponent_blip, "RE_INTER_STRANGER");
        Log("original rival blip attached to rider=" + std::to_string(g_opponent_rider));
    }
}

void ReleaseFixedIconTexture()
{
    // Call only after removing the fixed marker or replacing its MP sprite.
    if (g_fixed_icon_texture_requested) {
        map_textures::Release(map_textures::Dictionary::Multiplayer,map_textures::Owner::RaceMarkers);
        g_fixed_icon_texture_requested = false;
    }
    g_fixed_icon_requested_at = g_fixed_icon_valid_since = {};
}

void UpdateFixedRaceIcon()
{
    if (std::none_of(g_stations.begin(),g_stations.end(),[](const Station& s){return s.start_blip!=0;})) return;
    const auto now = Clock::now();
    if(g_fixed_icon_failed && now>=g_fixed_icon_retry_at)g_fixed_icon_failed=false;
    const auto retry=[&](const std::string& reason){
        g_fixed_icon_failed=true;g_fixed_icon_retry_at=now+std::chrono::seconds(30);
        if(!g_fixed_icon_failure_reported){
            Log("fixed race icon: "+reason+"; SP fallback; retrying after 30s");
            g_fixed_icon_failure_reported=true;
        }
    };
    Hash desired = kHorseRaceBlipSprite;
    if (g_fixed_online_icon_enabled && !g_fixed_icon_failed) {
        if (!g_fixed_icon_texture_requested) {
            if (TXD::DOES_STREAMED_TEXTURE_DICT_EXIST(kFixedRaceTextureDict)) {
                map_textures::Acquire(map_textures::Dictionary::Multiplayer,map_textures::Owner::RaceMarkers);
                g_fixed_icon_texture_requested = true;
                g_fixed_icon_requested_at = now;
                if(!g_fixed_icon_failure_reported)Log("fixed race icon: requested blips_mp; retaining SP horseshoe during streaming");
            } else {
                retry("blips_mp does not exist");
            }
        }
        if (g_fixed_icon_texture_requested) {
            const bool loaded = TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(kFixedRaceTextureDict);
            // NativeDB signature: dictionary hash first, texture hash second. A loaded
            // dictionary alone is not evidence that this particular glyph is available.
            const bool glyph = loaded && GRAPHICS::_IS_TEXTURE_IN_DICT(
                kFixedRaceTextureDictHash, kFixedRaceBlipSprite);
            if (glyph) {
                if (g_fixed_icon_valid_since == Clock::time_point{}) g_fixed_icon_valid_since = now;
                // Cross a render-frame boundary before selecting the MP sprite. This is
                // a streaming guard, not a claim to have inspected the renderer's pixels.
                if (now - g_fixed_icon_valid_since >= std::chrono::milliseconds(100)) {
                    desired = kFixedRaceBlipSprite;
                    if(g_fixed_icon_failure_reported){Log("fixed race icon: MP glyph recovered");g_fixed_icon_failure_reported=false;}
                }
            } else {
                g_fixed_icon_valid_since = {};
            }
            const bool mp_assigned=std::any_of(g_stations.begin(),g_stations.end(),
                [](const Station& s){return s.icon_sprite==kFixedRaceBlipSprite;});
            if ((mp_assigned && !glyph) ||
                (desired != kFixedRaceBlipSprite && now - g_fixed_icon_requested_at >= std::chrono::seconds(10))) {
                retry(std::string("asset check failed loaded=")+(loaded?"1":"0")+" glyph="+(glyph?"1":"0"));
            }
        }
    }
    const auto station_sprite=[&](const Station& station){
        return station.route && station.route->field?desired:kHorseRaceBlipSprite;
    };
    if (std::any_of(g_stations.begin(),g_stations.end(),
            [&](const Station& s){return s.start_blip && s.icon_sprite!=station_sprite(s);} )) {
        // If even the SP dictionary is temporarily unavailable, remove our marker
        // before releasing MP instead of assigning a second unready sprite.
        if ((desired == kHorseRaceBlipSprite || std::any_of(g_stations.begin(),g_stations.end(),
                [&](const Station& s){return s.start_blip && station_sprite(s)==kHorseRaceBlipSprite;})) &&
            !TXD::HAS_STREAMED_TEXTURE_DICT_LOADED(kRaceBlipTextureDict)) {
            for(auto& station:g_stations){RemoveBlip(station.start_blip);station.icon_sprite=0;}
        } else {
            // Styling can reset a blip's name. Every station uses style -> name,
            // only on a sprite transition, and before any MP TXD is released.
            for(auto& station:g_stations) {
                const auto sprite=station_sprite(station);
                if (!station.start_blip || station.icon_sprite==sprite) continue;
                MAP::SET_BLIP_SPRITE(station.start_blip,sprite,true);
                if(SetBlipDisplayName(station.start_blip,FixedRouteTitle(&station.variant).c_str()))station.icon_sprite=sprite;
            }
            Log(desired == kFixedRaceBlipSprite
                ? "fixed race icons: breed MP glyph ready; duels retain SP glyph; texture reference retained"
                : "fixed race icon: SP horseshoe restored before MP release");
        }
    }
    if (!g_fixed_online_icon_enabled || g_fixed_icon_failed) ReleaseFixedIconTexture();
}

void TickFixedIconSetting()
{
    const auto now = Clock::now();
    if (!g_fixed_race_enabled || now < g_fixed_icon_settings_poll) return;
    g_fixed_icon_settings_poll = now + std::chrono::seconds(1);
    const bool enabled = LoadFloat("FixedRaceOnlineIcon", "1", 0.0F, 1.0F, 1.0F) >= 0.5F;
    if (enabled != g_fixed_online_icon_enabled) {
        g_fixed_online_icon_enabled = enabled;
        g_fixed_icon_failed = false;
        g_fixed_icon_retry_at={};g_fixed_icon_failure_reported=false;
        Log(std::string("fixed race icon INI changed: ") + (enabled ? "online guarded attempt" : "SP horseshoe"));
    }
}

bool ScriptTaskActive(Ped ped,const char* task_name);
#include "../fixed_station_runtime.inl"

bool FixedDirectionRetirementPending(const OriginalRaceVariant* site);
// A station's marker means "a host is here or will meet you here". It is
// shown while a host is at the station - invited, racing, at the result,
// departing - and otherwise exactly when TrySelectFixedRace could spawn one:
// a route, models not in a retry hold, no retired pair still riding off
// (no new host comes until it is released), no direction waiting to be
// re-rolled, no pickup waiting for the player to leave. A marker on an
// empty station was the complaint: the last host had ridden off and no new
// one would come while the player stood there.
