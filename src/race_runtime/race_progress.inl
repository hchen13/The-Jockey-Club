// Private race progress implementation; included by race_system.cpp.
bool UpdateProgress(Ped entity, int& progress_max)
{
    if (CurrentRules().Hosted()) {
        const Vector3 position = ENTITY::GET_ENTITY_COORDS(entity, true, false);
        if (!destination::Finite(position)) return false;
        if(HasRecordedCourse()) {
            auto& progress=entity==g_opponent_rider?g_course_rival:g_course_player;
            if(!CurrentRules().uses_lap_engine)progress.Update(g_course,position);
            if(progress.current.valid)progress_max=static_cast<int>(progress.current.along);
            return progress.current.valid;
        }
        // Metres of straight-line closing distance for banter only, not a waypoint
        // index or enforced track. Reversing must reduce this value.
        progress_max = static_cast<int>(destination::Distance(g_route_start, g_route_finish) -
            destination::Distance(position, g_route_finish));
        return true;
    }
    int progress = 0;
    if (!TASK::WAYPOINT_RECORDING_GET_CLOSEST_WAYPOINT(
            g_shortcut_route_name.c_str(), ENTITY::GET_ENTITY_COORDS(entity, true, false), &progress)) {
        return false;
    }
    progress_max = std::max(progress_max, progress);
    return true;
}

bool Finished(Ped entity, int progress_max)
{
    if (CurrentRules().Hosted()) {
        const Vector3 position = ENTITY::GET_ENTITY_COORDS(entity, true, false);
        if(CurrentRules().uses_lap_engine)return (entity==g_opponent_rider?g_laps.opponent:g_laps.player).Finished();
        if(HasRecordedCourse() && !(entity==g_opponent_rider?g_course_rival:g_course_player).MayFinish(g_course))return false;
        return destination::ArrivalFraction(position, position, g_route_finish).has_value();
    }
    if (progress_max < g_route_points - 3) {
        return false;
    }
    if (g_finish_volume != 0 && VOLUME::DOES_VOLUME_EXIST(g_finish_volume)) {
        return ENTITY::IS_ENTITY_IN_VOLUME(entity, g_finish_volume, true, 0);
    }
    return DistanceSquared(ENTITY::GET_ENTITY_COORDS(entity, true, false), g_route_finish) <=
        g_finish_radius * g_finish_radius;
}

struct OpponentIncident {
    bool rider_exists = false, horse_exists = false, identity_changed = false;
    bool rider_dead = false, horse_dead = false, mounted = false;
    bool player_caused = false, lassoed = false;
    Entity rider_source = 0, horse_source = 0;
    Hash rider_cause = 0, horse_cause = 0;
    bool Out() const { return rider_dead || horse_dead || !mounted; }
    std::string Detail() const
    {
        std::ostringstream s;
        s << "rider=" << g_opponent_rider << " exists=" << rider_exists << " dead=" << rider_dead
          << " source=" << rider_source << " cause=" << rider_cause
          << "; horse=" << g_opponent_horse << " exists=" << horse_exists << " dead=" << horse_dead
          << " source=" << horse_source << " cause=" << horse_cause
          << "; mounted=" << mounted << " player_caused=" << player_caused << " lassoed=" << lassoed
          << " identity_changed=" << identity_changed;
        return s.str();
    }
};

OpponentIncident ReadOpponentIncident(Ped player)
{
    OpponentIncident incident;
    incident.rider_exists = g_opponent_rider && ENTITY::DOES_ENTITY_EXIST(g_opponent_rider);
    incident.horse_exists = g_opponent_horse && ENTITY::DOES_ENTITY_EXIST(g_opponent_horse);
    incident.rider_dead = incident.rider_exists && ENTITY::IS_ENTITY_DEAD(g_opponent_rider);
    incident.horse_dead = incident.horse_exists && ENTITY::IS_ENTITY_DEAD(g_opponent_horse);
    incident.mounted = incident.rider_exists && PED::IS_PED_ON_MOUNT(g_opponent_rider) &&
        PED::GET_MOUNT(g_opponent_rider) == g_opponent_horse;
    const Entity lasso_target = PED::_GET_LASSO_TARGET(player);
    incident.lassoed = (g_opponent_rider && lasso_target == g_opponent_rider) ||
        (g_opponent_horse && lasso_target == g_opponent_horse) ||
        (incident.rider_exists && (PED::IS_PED_LASSOED(g_opponent_rider) || PED::IS_PED_HOGTIED(g_opponent_rider) ||
            PED::IS_PED_BEING_HOGTIED(g_opponent_rider))) ||
        (incident.horse_exists && (PED::IS_PED_LASSOED(g_opponent_horse) || PED::IS_PED_HOGTIED(g_opponent_horse) ||
            PED::IS_PED_BEING_HOGTIED(g_opponent_horse)));
    const auto now = Clock::now();
    for (std::size_t i = 0; i < g_opponent_damage.size(); ++i) {
        const Ped ped = i == 0 ? g_opponent_rider : g_opponent_horse;
        if (!(i == 0 ? incident.rider_exists : incident.horse_exists)) continue;
        auto& watch = g_opponent_damage[i];
        const auto identity = HorseIdentity(ped); // Same identity triple also works for the rider.
        if (watch.identity.Valid() && watch.identity != identity) {
            incident.identity_changed = true;
            continue; // Never pay for a recycled entity handle.
        }
        const int health = ENTITY::GET_ENTITY_HEALTH(ped);
        const bool damaged = watch.identity.Valid() && health < watch.health;
        watch.identity = identity;
        watch.health = health;
        const bool dead = i == 0 ? incident.rider_dead : incident.horse_dead;
        if (dead) {
            const Entity source = PED::GET_PED_SOURCE_OF_DEATH(ped);
            const Hash cause = PED::GET_PED_CAUSE_OF_DEATH(ped);
            (i == 0 ? incident.rider_source : incident.horse_source) = source;
            (i == 0 ? incident.rider_cause : incident.horse_cause) = cause;
            // An explicit engine killer is authoritative; an unrelated wagon
            // or terrain death must not become sabotage merely because a sticky
            // HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY flag was set earlier in the race.
            if (source == player) incident.player_caused = true;
        }
        if (damaged && ENTITY::HAS_ENTITY_BEEN_DAMAGED_BY_ENTITY(ped, player, true, true)) {
            Hash weapon = 0;
            const bool shooting = PED::IS_PED_SHOOTING(player) &&
                WEAPON::GET_CURRENT_PED_WEAPON(player, &weapon, true, 0, false) &&
                WEAPON::_HAS_ENTITY_BEEN_DAMAGED_BY_WEAPON_RECENTLY(ped, weapon, 1000);
            const bool melee = PED::IS_PED_IN_MELEE_COMBAT(player) && PED::GET_MELEE_TARGET_FOR_PED(player) == ped;
            if (shooting || melee) watch.player_attack_at = now;
        }
        // Covers a shot/punch that throws the rider before a death source is
        // available. Evidence is current-race, damage-backed and short-lived.
        if (watch.player_attack_at != Clock::time_point{} && now - watch.player_attack_at <= std::chrono::seconds(3))
            incident.player_caused = true;
    }
    return incident;
}

bool PlayerAbandonedRoute(Ped player)
{
    if(CurrentRules().corridor_dq){g_fixed_abandon_since={};return false;} // Timed per-rider boundary offenses, not destination abandonment.
    if (CurrentRules().Hosted()) {
        const Vector3 position = ENTITY::GET_ENTITY_COORDS(player, true, false);
        float deviation=0;
        const bool measured=HasRecordedCourse()?TryGetRouteDeviation(player,deviation):destination::Finite(position);
        if(!HasRecordedCourse() && measured)deviation=destination::DistanceFromSegment(position,g_route_start,g_route_finish);
        const bool outside_envelope=measured && deviation>(HasRecordedCourse()?100.0F:800.0F);
        // beat_horse_race func_232 does NOT disqualify a rider simply for taking
        // a shortcut: route-distance failure also requires NOT heading towards
        // the finish. Use the identical native and its original 80f parameter.
        const bool towards_finish=outside_envelope && PED::IS_PED_HEADING_TOWARDS_POSITION(player,g_route_finish,kFinishHeadingTolerance);
        const bool leaving=outside_envelope && !towards_finish;
        if (!leaving) {
            if(g_fixed_abandon_since!=Clock::time_point{})Log(std::string(towards_finish?
                "fixed rider heading to finish":"fixed course rejoined")+
                "; abandonment timer cleared; deviation_m="+std::to_string(deviation));
            g_fixed_abandon_since = {};
        }
        else if (g_fixed_abandon_since == Clock::time_point{}) g_fixed_abandon_since = Clock::now();
        const bool abandoned=leaving && Clock::now()-g_fixed_abandon_since>=std::chrono::seconds(15);
        if(abandoned)Log("fixed course abandonment confirmed: deviation_m="+std::to_string(deviation)+
            (HasRecordedCourse()?" geometry=whole-course":" geometry=destination-envelope")+
            " xyz="+std::to_string(position.x)+","+std::to_string(position.y)+","+std::to_string(position.z));
        return abandoned;
    }
    float current_deviation = 0.0F;
    if (!TryGetRouteDeviation(player, current_deviation)) {
        return false;
    }
    return current_deviation >= g_initial_player_route_deviation + kRouteDeviationAllowance &&
        !PED::IS_PED_HEADING_TOWARDS_POSITION(player, g_route_finish, kFinishHeadingTolerance);
}

bool RaceSpeechAvailable()
{
    return !SpeechPlaying(g_opponent_rider) && !SpeechPlaying(PLAYER::PLAYER_PED_ID());
}

void UpdateOriginalRivalBanter()
{
    if (g_race_started_at == Clock::time_point{} ||
        std::chrono::duration<double>(Clock::now() - g_race_started_at).count() < 10.0 ||
        DistanceSquared(ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(), true, false), g_route_finish) <=
            85.0F * 85.0F) {
        return;
    }

    const int gap = g_player_progress_max - g_opponent_progress_max;
    const int candidate = gap >= 3 ? 1 : (gap <= -3 ? -1 : 0);
    if (candidate != 0 && candidate != g_lead_candidate) {
        g_lead_candidate = candidate;
        g_lead_candidate_since = Clock::now();
    } else if (candidate == 0) {
        g_lead_candidate = 0;
        g_lead_candidate_since = Clock::time_point{};
    } else if (candidate != g_stable_leader && g_lead_candidate_since != Clock::time_point{} &&
               std::chrono::duration<double>(Clock::now() - g_lead_candidate_since).count() >= 2.0 &&
               RaceSpeechAvailable()) {
        g_stable_leader = candidate;
        const char* line = candidate > 0
            ? g_rival->player_passes[static_cast<std::size_t>(g_player_pass_index++ % 4)]
            : g_rival->opponent_passes[static_cast<std::size_t>(g_opponent_pass_index++ % 4)];
        PlaySpeech(g_opponent_rider, PLAYER::PLAYER_PED_ID(), line, nullptr);
        g_next_banter_at = Clock::now() + std::chrono::seconds(MISC::GET_RANDOM_INT_IN_RANGE(20, 40));
    }

    if (g_stable_leader < 0 && Clock::now() >= g_next_banter_at && RaceSpeechAvailable()) {
        const char* line = g_rival->ahead_taunts[static_cast<std::size_t>(g_ahead_taunt_index++ % 3)];
        PlaySpeech(g_opponent_rider, PLAYER::PLAYER_PED_ID(), line, nullptr);
        g_next_banter_at = Clock::now() + std::chrono::seconds(MISC::GET_RANDOM_INT_IN_RANGE(20, 40));
    }
}

void UpdateHostCatchup(std::optional<float> gap,bool eligible=true) {
    const auto id=HorseIdentity(g_opponent_horse);
    g_pace_command.catchup_fraction=g_host_catchup.Update(g_race_epoch,id.address,id.horse,id.model,gap,
        race_speed_policy::Enabled()&&eligible&&g_state==State::Racing&&id.Valid()&&!g_main_rival_out&&!GetAutoSurveyState().running);
}
float OriginalRivalPlaybackSpeed()
{
    // Competitive mode: no waiting while ahead, random canter windows or
    // braking because the PLAYER (rather than this rider) is near the finish.
    g_pace_command={false,0,race_pace::Effort(g_rider_skill)};
    if(!g_pace_plan.path.points.empty()) {
        const auto& path=g_pace_plan.path;
        g_course_player.Update(path,ENTITY::GET_ENTITY_COORDS(PLAYER::PLAYER_PED_ID(),true,false));
        g_course_rival.Update(path,ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false));
        const int point=std::clamp(TASK::GET_PED_WAYPOINT_PROGRESS(g_opponent_rider),0,static_cast<int>(path.points.size())-1);
        const auto position=ENTITY::GET_ENTITY_COORDS(g_opponent_rider,true,false);
        const auto projected=course::Project(path,position,std::max(0.0F,path.metres[point]-80),path.metres[point]+80);
        if(projected.valid)g_pace_command=g_pace_controller.Update(g_pace_plan,projected.along,
            ENTITY::GET_ENTITY_SPEED(g_opponent_horse),g_rider_skill);
    }
    UpdateHostCatchup(PlayerProgressGap(g_course_rival));
    return g_pace_command.effort;
}

// The trial (2026-09-19, the maintainer's design): "send a rider out along
// the line the current settings give; when he falls, learn, and send the
// next at once, until one reaches the finish". Armed from F11 > Race debug,
// the next race the player starts is ridden by the host alone: at GO the
// player and mount stay parked while native streaming and the gameplay
// camera follow the host. Mouse/controller orbit needs no held button.
// The exclusion, survey and collision learner still observe physical riding.
// A fall learns
// (LearnFromCollision), the host is put back on the line two hundred metres
// before it, healed, and started again on lines rebuilt from what was
// learned; the finish ends the trial with its tally, and so does F11.
struct Trial {
    bool armed=false,active=false;
    int riders=0,incidents=0;
    double started=0;
    bool remounting=false;double remount_asked=0,remount_since=0;float dispatch_along=0;
    trial_spectator::Session spectator;
    bool collision_ready=false;
    std::string summary;
    bool off_course_noted=false;
};
Trial g_trial;
inline constexpr float kTrialBackMetres=200;   // Far enough back to arrive at racing speed, settled on the line, as a race would.
inline constexpr double kTrialRemountSeconds=4;
// The game's heading of `path` at `along`: 0 north, counter-clockwise.
