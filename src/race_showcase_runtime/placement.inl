// Private implementation of race_showcase.cpp; included inside its original namespace.
// Bounded display placement and rider and horse creation.
// Prefer the requested row, then move this pair locally. A bad sixth slot cannot veto the first five.
// At most four probes per frame. Keep real obstacle checks and minimum separation from displayed pairs.
bool PlanEntry()
{
    constexpr std::array<std::array<float, 2>, 15> offsets{{
        {0,0}, {0,4}, {0,-4}, {0,8}, {1.5F,0}, {-1.5F,0}, {1.5F,4}, {-1.5F,4},
        {1.5F,-4}, {-1.5F,-4}, {0,12}, {3,8}, {-3,8}, {3,12}, {-3,12}}};
    Entry& entry = g_entries[g_cursor];
    for (int probe = 0; probe < 4; ++probe) {
        const auto offset = offsets[g_candidate++ % offsets.size()];
        const float forward_distance = 11.0F + offset[1]; // One page only: identical nearby slots, no 34-pair load.
        const float lateral = (static_cast<float>(g_site.all_rider_presets ? g_cursor % 6 : g_cursor) - 2.5F) * kSpacing + offset[0];
        Vector3 point{g_origin.x + g_forward.x * forward_distance + g_forward.y * lateral,
            g_origin.y + g_forward.y * forward_distance - g_forward.x * lateral, g_origin.z};
        STREAMING::REQUEST_COLLISION_AT_COORD(point);
        Vector3 normal{};
        float ground = 0.0F;
        const bool ground_found = MISC::GET_GROUND_Z_AND_NORMAL_FOR_3D_COORD(
            point.x, point.y, point.z + 20.0F, &ground, &normal);
        const char* failure = nullptr;
        if (!ground_found) failure = "ground query not ready";
        else if (!std::isfinite(ground) || !std::isfinite(normal.x) || !std::isfinite(normal.y) ||
            !std::isfinite(normal.z)) failure = "invalid ground data";
        else if (normal.z < g_min_normal_z) failure = "ground normal / slope rejected";
        else if (std::abs(ground - g_origin.z) > g_max_ground_delta) failure = "ground height jump rejected";
        point.z = ground + 0.05F;
        float water = 0.0F;
        if (!failure && WATER::GET_WATER_HEIGHT(point, &water) && water > ground + 0.15F)
            failure = "ground below water";
        if (!failure && DistanceSquared(ENTITY::GET_ENTITY_COORDS(g_player, true, false), point) < 16.0F)
            failure = "player within 4m of slot";
        for (const Entry& previous : g_entries) {
            if (failure) break;
            if (!previous.shown) continue;
            const float dx = previous.position.x - point.x, dy = previous.position.y - point.y;
            if (dx*dx + dy*dy < kSpacing*kSpacing - 0.01F) failure = "too close to displayed pair";
        }
        if (!failure && PositionOccupied(point)) failure = "space occupied";
        if (failure) {
            g_placement_failure = failure;
            if (Clock::now() >= g_next_placement_log) {
                std::ostringstream detail;
                detail << "placement rejected: site=" << g_site.id << " slot=" << g_cursor + 1 <<
                    " candidate=" << g_candidate << " reason=" << failure << " ground=" << ground <<
                    " normal_z=" << normal.z << " height_delta=" << ground - g_origin.z;
                Log(detail.str());
                g_next_placement_log = Clock::now() + std::chrono::seconds(1);
            }
            continue;
        }
        entry.position = point;
        entry.position_valid=true;
        LogEntry(entry, "placement accepted candidate=" + std::to_string(g_candidate));
        g_placement_failure.clear();
        return true;
    }
    return false;
}

void SecureDisplayPed(Ped ped)
{
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped, true, true);
    ENTITY::SET_ENTITY_VISIBLE(ped, false);
    ENTITY::SET_ENTITY_INVINCIBLE(ped, true);
    PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, true);
    PED::SET_PED_CAN_BE_TARGETTED(ped, false);
    PED::_SET_PED_CAN_BE_LASSOED(ped, false);
    PED::SET_PED_CAN_RAGDOLL(ped, false);
}

bool CreateEntry(Entry& entry)
{
    // Streaming can take seconds: traffic/player positions may have changed since planning.
    const float player_distance = std::sqrt(DistanceSquared(ENTITY::GET_ENTITY_COORDS(g_player, true, false), entry.position));
    const bool occupied = PositionOccupied(entry.position);
    if (player_distance < 4.0F || occupied) {
        LogEntry(entry, "pre-create blocked: player_distance=" + std::to_string(player_distance) +
            " occupied=" + std::to_string(occupied));
        return false;
    }
    Vector3 horse_spawn = entry.position;
    horse_spawn.z += g_spawn_lift;
    entry.horse = PED::CREATE_PED(entry.horse_model, horse_spawn, g_heading, false, true, true, true);
    LogEntry(entry, "CREATE_PED horse returned=" + std::to_string(entry.horse));
    if (!Exists(entry.horse)) return false;
    SecureDisplayPed(entry.horse);
    // Leave gravity/collision active until the horse actually stands on the ground.
    // Freezing a newly created ped at an estimated Z can lock it into the air or terrain.
    TASK::TASK_STAND_STILL(entry.horse, -1);
    // Same stock NPC saddle preparation as the original race. No shop items, inventory,
    // player tack or hand-picked premium/Trapper gear. The native draw may repeat.
    appearance::InitializeHorse(entry.horse);
    if (entry.donor_model != 0) {
        // Spawn the WHOLE model: its body, masks, clothes and accessories belong
        // together. No hidden garment donor and no cross-model component writes.
        entry.rider = PED::CREATE_PED(entry.donor_model, entry.position, g_heading, false, true, true, true);
        if (!Exists(entry.rider)) return false;
        SecureDisplayPed(entry.rider);
    } else {
        entry.rider = PED::_CREATE_META_PED_OUTFIT_PED(
            g_outfit_request, entry.position, g_heading, true, true, false, true);
        if (!Exists(entry.rider)) return false;
        SecureDisplayPed(entry.rider);
    }
    ENTITY::SET_ENTITY_COLLISION(entry.rider, false, false);
    ENTITY::FREEZE_ENTITY_POSITION(entry.rider, true);
    ReleaseOutfit(); // Creation request lifetime matches the production creator.
    appearance::InitializeRider(entry.rider,entry.donor_model!=0,g_site.remove_original_hat && entry.donor_model==0);
    if(!firearm_guard::ProtectPair(g_site.character,entry.rider,entry.horse)) {
        LogEntry(entry,"friendly firearm protection could not be verified");return false;
    }
    return true;
}
