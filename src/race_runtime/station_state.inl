// Private station state implementation; included by race_system.cpp.
bool g_route_requested = false;
std::array<bool, 11> g_site_waiting_for_leave{};
std::map<int,Clock::time_point> g_model_retry_after;
bool ModelRetryAllowed(int site)
{
    const auto found=g_model_retry_after.find(site);
    return found==g_model_retry_after.end() || Clock::now()>=found->second;
}
Clock::time_point g_next_site_scan{};
float g_activation_min_radius = kOriginalActivationMinRadius;
float g_activation_max_radius = kOriginalActivationMaxRadius;
float g_offer_abandon_radius = kOriginalEncounterCleanupRadius;
float g_release_radius = 120.0F;
float g_finish_radius = 18.0F;
float g_max_race_seconds = 1'200.0F;

State g_state = State::Disabled;
Clock::time_point g_state_entered{};
Clock::time_point g_last_route_request{};
Clock::time_point g_next_progress_log{};
int g_route_points = 0;
Vector3 g_route_start{};
Vector3 g_route_finish{};
int g_navigation_route_points = 0;

Hash g_rider_model = 0;
Hash g_horse_model = 0;
bool g_opponent_models_requested = false;
Clock::time_point g_actor_allocation_started{},g_actor_allocation_next{},g_actor_allocation_log{};
unsigned g_actor_allocation_attempts=0;
bool g_actor_allocation_wait=false;
int g_rider_outfit_request = -1;
bool g_rider_outfit_requested = false;
bool g_use_default_rider_outfit = false;
Clock::time_point g_rider_outfit_requested_at{};
Hash g_relationship_group = 0;
Ped g_opponent_rider = 0;
Ped g_opponent_horse = 0;
Ped g_player_mount = 0;
std::array<Blip, 11> g_start_blips{};
bool g_race_blip_texture_requested = false;
bool g_fixed_online_icon_enabled = true;
bool g_fixed_icon_texture_requested = false;
bool g_fixed_icon_failed = false;
bool g_fixed_icon_failure_reported = false;
Clock::time_point g_fixed_icon_retry_at{};
Clock::time_point g_fixed_icon_requested_at{};
Clock::time_point g_fixed_icon_valid_since{};
Clock::time_point g_fixed_icon_settings_poll{};
Blip g_finish_blip = 0;
Blip g_opponent_blip = 0;
bool g_opponent_colour_ready = false;
Volume g_finish_volume = 0;
Prompt g_accept_prompt = 0;
Prompt g_decline_prompt = 0;
Prompt g_inspect_prompt = 0; // Non-owning alias for the current host's info prompt.
horse_inspection::Card g_horse_info_card;
inspection::Interactions g_race_inspection;
int g_prompt_group = 0;
bool g_owns_mission_flag = false;
bool g_f10_was_down = false;
bool g_page_left_was_down=false,g_page_right_was_down=false;
bool g_showcase_enabled = true;
int g_player_progress_max = 0;
int g_opponent_progress_max = 0;
bool g_reissued_route_task = false;
bool g_route_delay_reported = false;
int g_offer_phase = 0;
bool g_departure_task_started = false;
Clock::time_point g_offer_phase_started{};
Clock::time_point g_route_task_reissued_at{};
struct OpponentDamageWatch {
    race_horses::Identity identity{};
    int health = 0;
    Clock::time_point player_attack_at{};
};
std::array<OpponentDamageWatch, 2> g_opponent_damage{};
float g_initial_player_route_deviation = 0.0F;
bool g_follow_approach_started = false;
Clock::time_point g_follow_approach_started_at{};
AnimScene g_result_scene = 0;
ScrHandle g_race_shocking_event = 0;
ResultPhase g_result_phase = ResultPhase::Arrival;
bool g_player_won = false;
bool g_result_playlists_requested = false;
Clock::time_point g_result_started_at{};
Clock::time_point g_result_next_log{};
bool g_result_scripted_line_attempted = false;
Clock::time_point g_result_scripted_line_at{};
bool g_result_interaction_registered = false;
bool g_result_interaction_positive = false;
bool g_result_response_started = false;
int g_difficulty_tier = 0;
int g_opponent_base_speed_level = 5;
int g_opponent_base_accel_level = 5;
int g_opponent_speed_level = 5;
int g_opponent_accel_level = 5;
int g_reward_cents = 1000;
bool g_reward_granted = false;
bool g_victory_toast_queued = false;
Clock::time_point g_race_started_at{};
Clock::time_point g_next_banter_at{};
Clock::time_point g_lead_candidate_since{};
int g_stable_leader = 0;
int g_lead_candidate = 0;
int g_ahead_taunt_index = 0;
int g_opponent_pass_index = 0;
int g_player_pass_index = 0;
int g_waiting_line_index = 0;

std::string g_test_host_override; // Applied at an idle boundary; never changes a live opponent.
dialogue_test::Listener g_test_listener=dialogue_test::Listener::Auto;
// Era=: the station is there only for that protagonist. F11's test player
// (TestDialoguePlayer) counts as the player here, so a John save can be shown
// Arthur's stations before the Arthur save is loaded; everything that is the
// player's own - horse, cash, the camp - stays the real character's.
bool EraAllows(const route_config::Route& route) {
    if(route.era.empty())return true;
    const auto actual=static_cast<std::uint32_t>(ENTITY::GET_ENTITY_MODEL(PLAYER::PLAYER_PED_ID()));
    return (dialogue_test::Model(g_test_listener,actual)==0x00B69710U)==(route.era=="John");
}
bool g_test_host_replace=false; // Explicit F11 preview replacement may appear on-screen.
Vector3 g_test_host_position{};
float g_test_host_heading=0;
Clock::time_point g_test_host_poll{};
// The station a selection stands at, or nothing for an original encounter.
// This is the whole of what the runtime may learn from station identity: a
// slot into the station table, never who is there.
int FixedStationIndex(const OriginalRaceVariant* site) {
    for(const auto& station:g_stations)if(site==&station.variant)return static_cast<int>(StationSlot(station));
    return -1;
}
Station* StationOf(const OriginalRaceVariant* site) {
    const int index=FixedStationIndex(site);
    return index<0?nullptr:&g_stations[static_cast<std::size_t>(index)];
}
Station* StationById(int id) {
    for(auto& station:g_stations)if(station.variant.id==id)return &station;
    return nullptr;
}
bool FixedStationConfigured(const OriginalRaceVariant* site) {
    const auto* station=StationOf(site);
    return station && station->route.has_value();
}
// A site with no station of its own - an original encounter - has no route,
// and the empty one names nobody: asking a question of it answers the same
// way a hand-built route does.
const route_config::Route& FixedRouteDefinition(const OriginalRaceVariant* site) {
    static const route_config::Route kNoRoute{};
    const auto* station=StationOf(site);
    return station && station->route?*station->route:kNoRoute;
}
const route_config::Route& ActiveFixedRoute(const OriginalRaceVariant* site) {
    const auto& definition=FixedRouteDefinition(site);const int index=FixedStationIndex(site);
    if(definition.random_direction && index>=0 && g_fixed_directed_routes[index])return *g_fixed_directed_routes[index];
    return definition;
}
const route_config::Route& SelectedFixedRoute() {
    return ActiveFixedRoute(g_variant);
}
// A multi-rider start is a property of the ROUTE, not of which station the
// player walked up to. Any route that declares a field gets the full grid,
// the extra roster and the breed restriction; any route that does not gets
// the ordinary one-against-one race, at the same station if it changes.
const field_race::Breed& CurrentBreed() {
    const auto& field=SelectedFixedRoute().field;
    const auto* breed=field?field_race::FindBreed(field->breed):nullptr;
    // Parsing refuses an unknown breed, so a route with a field always resolves.
    return breed?*breed:field_race::kBreeds.front();
}

// ---- Race rules ------------------------------------------------------------
// A station owns one route file; an original encounter owns none. That is the
// whole of what the runtime may learn from station identity: everything else
// it needs to know about a race is in rules::RaceRules, derived from that file.
const route_config::Route* StationRoute(const OriginalRaceVariant* site) {
    return FixedStationIndex(site)<0?nullptr:&ActiveFixedRoute(site);
}
// Resolved when the selection changes, re-resolved when a route definition is
// (re)assigned, never re-derived per call otherwise. A selected, invited or
// running race can never see a hot edit: the pending-route appliers refuse
// while a station is selected, so the .ini snapshot doctrine holds for rules.
std::uint64_t g_rules_generation=1;
struct ResolvedRules {
    const OriginalRaceVariant* variant=nullptr;  // The selection the cache was built for.
    std::uint64_t generation=0;                  // g_rules_generation at that time.
    rules::RaceRules rules;
} g_rules;
void InvalidateRules(const char* why) {
    ++g_rules_generation;
    Log(std::string("race rules invalidated: ")+why);
}
const rules::RaceRules& CurrentRules() {
    if(g_rules.variant!=g_variant || g_rules.generation!=g_rules_generation) {
        auto resolved=rules::Resolve(StationRoute(g_variant));
        if(!g_rules.variant || !(resolved==g_rules.rules))
            Log("race rules: site="+std::to_string(g_variant->id)+" "+rules::Describe(resolved));
        g_rules={g_variant,g_rules_generation,std::move(resolved)};
    }
    return g_rules.rules;
}
const hosts::Profile& FixedProfile(const OriginalRaceVariant* site=nullptr) {
    if(!site)site=g_variant;
    // A field race binds its host to a named mount through the breed, so a
    // debug host swap would leave the wrong rider on the exclusive horse.
    if(FixedRouteDefinition(site).field) {
        if(const auto* host=hosts::Find(FixedRouteDefinition(site).host))return *host;
    } else if(const auto* override_profile=hosts::Find(g_test_host_override))return *override_profile;
    if(const auto* profile=hosts::Find(FixedRouteDefinition(site).host))return *profile;
    // Every route names its host - the parser refuses one that does not - so
    // this is reached only by a hand-built route, and answers with the first
    // profile rather than with any station's host.
    return hosts::kProfiles.front();
}
// One eligibility rule for player-race force, far pace, voice and presentation.
// Bookmaker exhibition contexts are separate and do not use this restriction.
// Sadie retains her existing route-configured bonus; no new breed restriction.
bool PlayerRaceBurstEligible() {
    return CurrentRules().Hosted() && hosts::BurstDisplayEligible(FixedProfile().id);
}
const char* FixedHostName() {
    const auto& p=FixedProfile();return i18n::Text(p.name,p.chinese);
}
int FixedMinimumSpeed() { return FixedProfile().minimum; } // Player-visible 1..10.
// Only a station waits for the player to leave; an encounter site has its own
// flag in g_site_waiting_for_leave and never reaches this.
bool& FixedWaitingFlag(const OriginalRaceVariant* site) {
    static bool no_station=false;
    auto* station=StationOf(site);
    return station?station->waiting_for_leave:no_station;
}
const char* FixedRiderModel(const OriginalRaceVariant* site) {
    return FixedProfile(site).model;
}
characters::Id FixedRaceCharacter(const OriginalRaceVariant* site) {
    return FixedProfile(site).id;
}

wager::Ledger g_fixed_wager;
std::uint64_t g_race_cash_timeline = 0, g_cash_timeline = 0;
int g_fixed_stake_cents = 1000;
bool g_fixed_practice = true;
std::string g_fixed_selected_horse_name;
// The camp's own horses: the player locks on to one and races Arthur on it.
// The name is spent by the next plan, so it never leaks into another race.
std::string g_asked_host_horse;
// One-shot appearance of the exact model selected at the camp; consumed only
// by the newly created opponent, never retained as a horse/entity handle.
std::optional<float> g_camp_horse_dirt;
Hash g_camp_horse_dirt_model=0;
// The camp offered the race on its own Arthur, or on one of his horses, and
// the player chose. The pipeline runs as it always does - route, host, grid -
// and takes the choice as read the moment everything is ready, so there is no
// second prompt to press at a host the player is already standing in front of.
bool g_camp_choice=false,g_camp_practice=false;
Clock::time_point g_camp_choice_by{};
// The blackout the camp starts on the press, and whether the player has been
// set down at the start line under it yet.
bool g_camp_fade=false,g_camp_moved=false;
// The player's own horse, when they chose on foot: it travels with them and
// they are put on it under the blackout. Zero when they were already mounted.
Ped g_camp_horse=0;
Clock::time_point g_camp_moved_at{};
bool g_camp_mount_tasked=false;
// The camp's own use of the horse info card, kept open by the camp rather
// than by a race offer.
bool g_camp_inspecting=false;
// How far along the line they are set down: clear of the deepest row of an
// eight-horse grid, past the five metres the host's own staging point keeps
// clear of the player, and inside the thirty the circuit staging measures
// from the start (37 m aborted it on 2026-09-17).
constexpr float kCampParkAhead=20.0F;
std::vector<fixed_horses::Choice> g_horse_catalog;
Ped g_catalog_player=0, g_race_player=0;
bool g_catalog_ready=false, g_horse_plan_ready=false;
Clock::time_point g_catalog_retry{};
horse_attributes::Ranks g_planned_natural{}, g_planned_target{};
race_horses::Lease g_rank_lease;
std::vector<field_race::Entrant> g_extra_rivals;
std::vector<Hash> g_extra_requested_models;
std::optional<circuit_start::Grid> g_multi_grid;
bool g_extra_grid_ready=false,g_main_rival_out=false;
std::string g_extra_error;
// A field that cannot be created because the game's ped pool is full (see
// RelievePedPool): when the first refusal came, and what has been freed since.
struct ExtraPoolRelief {Clock::time_point since{};int freed=0,refusals=0;} g_extra_pool_relief;
bool PrepareExtraPlan(const opponent_difficulty::Rule& rule);
bool ExtraHorseClaimed(const field_race::Entrant& e);
bool RestoreExtraRanks();
void RetireExtraRivals();
void TickExtraRetirement();
bool PrepareExtraActors();
std::vector<Ped> GridRiders();
bool StartExtraRacers();
void TickExtraDrivers();
circuit::Outcome UpdateMultiProgress(Vector3 player,Vector3 main,double now);
bool g_restore_requested=false;
bool g_deferred_horse_release=false;
bool g_opponent_horse_claimed=false;
Clock::time_point g_restore_log_at{};
Clock::time_point g_cash_retry{}, g_language_poll{};
bool g_fixed_result_waiting = false;
marybeth_dialogue::Session g_mary;
// Station memory, indexed like FixedStationIndex: the host horse last drawn
// there and the dialogue meeting state. Slot 0 is the default station's; an
// original encounter has no station of its own and reads that slot, as it always has.
std::array<Hash,kStationCount> g_fixed_previous_horses{};
std::array<marybeth_dialogue::Meeting,kStationCount> g_fixed_meetings{};
