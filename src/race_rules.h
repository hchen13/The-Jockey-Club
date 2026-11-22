#pragma once
// The rules of the race on offer, as data. Every behaviour branch in the
// runtime asks THIS, never which station the player walked up to: a station
// is where a race is held; the route file at that station says what the race
// is. Asking the station instead is what shipped a Blackwater-only predicate answering
// "no field here" at New Austin, a flag called circuit_open_speed doing the
// universal town-speed job, and a circuit clearance rule on a point-to-point
// race.
//
// Pure: no natives, no globals, no runtime state. Derived once from a parsed
// route_config::Route - or from nothing, for an original encounter - cheap to
// compare, printable, and testable from the shipped .ini files alone.
//
// Deliberately NOT here:
//   - the town speed-volume exemption: universal while racing
//     (MotionContext::town_speed_exempt = racing && present), not a property
//     of any race shape;
//   - the drawn lap count X, the frozen plan, the roster actually spawned:
//     those belong to the race INSTANCE (lap::Session, g_extra_rivals), drawn
//     per offer inside the bounds recorded here;
//   - host identity, voice, model, per-station memory: who hosts and what the
//     station remembers are hosts::Profile and station state, not rules.
#include "race_route_config.h"
#include <optional>
#include <string>

namespace horse_races::rules {
enum class Geometry {
    Native,      // Original encounter: Rockstar waypoint recording, no Route at all.
    OpenPoints,  // Mode=Destination (or no Mode): two surveyed points, free road choice, optional [Course] line.
    OpenTrip,    // Mode=SingleTrip: one recorded line from the grid to a distinct finish, corridor-judged, never rings.
    ClosedLoop,  // Mode=SingleLap / Circuit / RecordedLaps: recorded ring(s) judged by the lap engine.
};
inline const char* Name(Geometry g) {
    switch(g) {
    case Geometry::Native:return "Native";
    case Geometry::OpenPoints:return "OpenPoints";
    case Geometry::OpenTrip:return "OpenTrip";
    case Geometry::ClosedLoop:return "ClosedLoop";
    }
    return "?";
}

struct RaceRules {
    // --- shape --------------------------------------------------------------
    Geometry geometry=Geometry::Native; // [Route] Mode; Native when there is no Route.
    bool uses_lap_engine=false;         // Route::circuit.has_value(): judged by lap::Session (ordered gates, strict corridor, section-jump foul).
    bool independent_laps=false;        // Mode=RecordedLaps: every lap is the whole recorded line to the finish, not a shared ring with an entry and exit.
    bool has_recorded_line=false;       // Route::circuit or a [Course] section: the mod owns a centreline for GPS, traffic goals, progress and abandonment.
    int laps_min=1,laps_max=1;          // [Circuit] ExtraLapsMin/Max + 2 (first + X regular + final) for Circuit and RecordedLaps; exactly 1 otherwise. X is drawn in lap::Session::Freeze.
    // --- who races ----------------------------------------------------------
    int rivals_min=1,rivals_max=1;      // Opponents the player faces, host included: 1, or [Route] FieldMin/FieldMax + 1 when the route declares a field.
    std::optional<std::string> breed;   // [Route] Breed: every entrant's mount must be this field_race::Breed id; nullopt = any validated horse.
    float host_level_bonus=0;           // [Route] HostSpeedLevelBonus: fraction of a speed level the field host earns at full flight on a straight; 0 = none.
    // --- money --------------------------------------------------------------
    bool wagered=false;                 // Any Route: entry priced from the roster's displayed speeds and settled through wager::Ledger; Native pays a tier reward and stakes nothing.
    double reward_multiplier=1;         // [Route] RewardMultiplier; 0 resolves from the frozen distance.
    double stake_multiplier=1;          // [Route] StakeMultiplier; 0 resolves from the frozen distance.
    // --- start and street ---------------------------------------------------
    bool blackout_clearance=false;      // Any Route: the race starts under an owned blackout - fade, equal-progress grid, countdown. With uses_lap_engine the blackout also isolates the recorded course.
    bool continuous_clearance=false;    // [Route] ContinuousClearance=1: the course stays policed for the whole race. Default 0. Meaningless without uses_lap_engine.
    bool spectators=false;              // [Circuit] Spectators: the authored Valentine audience scene.
    bool allow_wolves=false;            // [Circuit] AllowWolves (SingleLap): native wolves are the one wildlife exception to the closure.
    bool road_start=false;              // [Circuit] RoadStart (SingleLap): the blackout grid is moved onto the opening road.
    bool random_direction=false;        // [Route] RandomDirection (Destination): direction rolled once per offer cycle.
    // --- judging ------------------------------------------------------------
    bool corridor_dq=false;             // uses_lap_engine: continuous outside 2 s is one offense; player limit 1, each NPC limit 2.
    float abandon_envelope_m=0;         // Metres the player may stray before the abandonment clock runs: 100 from a recorded line, 800 from a bare A-B; 0 = not applicable.

    // Derived predicates: the vocabulary the call sites migrate to.
    bool Hosted() const {return geometry!=Geometry::Native;}     // A fixed-station race: host, prompts, practice, wager, station pickup.
    bool Field() const {return rivals_max>1;}                     // Multi-rider grid, extra roster, breed gate, roster pricing.
    bool Closed() const {return geometry==Geometry::ClosedLoop;}
    bool SingleLap() const {return Closed() && laps_max==1;}
    bool SingleTrip() const {return geometry==Geometry::OpenTrip;}
    bool FixedLaps() const {return laps_min==laps_max;}
    bool operator==(const RaceRules&) const=default;
};

// An original encounter: no Route, everything native.
inline RaceRules Native() {return {};}

// The pure derivation. Reads only scalars and flags of the parsed Route; never
// its node lists, so it is cheap enough to call whenever a cache is stale.
inline RaceRules Derive(const route_config::Route& route) {
    RaceRules r;
    const auto& circuit=route.circuit;
    r.geometry=!circuit?Geometry::OpenPoints:(circuit->single_trip?Geometry::OpenTrip:Geometry::ClosedLoop);
    r.uses_lap_engine=circuit.has_value();
    r.independent_laps=circuit && circuit->independent_laps;
    r.has_recorded_line=circuit.has_value() || !route.course_points.empty();
    if(circuit && !circuit->single_loop && !circuit->single_trip) {
        r.laps_min=circuit->extra_min+2;r.laps_max=circuit->extra_max+2; // lap::Plan::Total() == selected+2.
    }
    if(route.field) {
        r.rivals_min=route.field->minimum+1;r.rivals_max=route.field->maximum+1;
        r.breed=route.field->breed;
    } else if(!route.breed.empty()) {
        // One rival, both on that breed: the gate without the grid.
        r.breed=route.breed;
    }
    // The host's burst is the host's, with or without a field behind him
    // (2026-09-18: Arthur's one-on-one carries his).
    r.host_level_bonus=route.host_level_bonus;
    r.wagered=true;
    r.reward_multiplier=route.reward_multiplier;r.stake_multiplier=route.stake_multiplier;
    r.blackout_clearance=true;
    r.continuous_clearance=route.continuous_clearance;
    r.spectators=circuit && circuit->spectators;
    r.allow_wolves=circuit && circuit->allow_wolves;
    r.road_start=circuit && circuit->road_start;
    r.random_direction=route.random_direction;
    r.corridor_dq=circuit.has_value();
    r.abandon_envelope_m=circuit?0.0F:(route.course_points.empty()?800.0F:100.0F);
    return r;
}
// The single entry point for the runtime: a station hands over its route, an
// original encounter hands over nothing.
inline RaceRules Resolve(const route_config::Route* route) {return route?Derive(*route):Native();}

// One line for the log and for test failures.
inline std::string Describe(const RaceRules& r) {
    auto flag=[](const char* name,bool value){return std::string(" ")+name+"="+(value?"1":"0");};
    auto price=[](double value){return value==distance_pricing::kAutomatic?std::string("distance"):std::to_string(value);};
    return std::string("geometry=")+Name(r.geometry)+
        " laps="+std::to_string(r.laps_min)+".."+std::to_string(r.laps_max)+
        " rivals="+std::to_string(r.rivals_min)+".."+std::to_string(r.rivals_max)+
        " breed="+(r.breed?*r.breed:std::string("-"))+
        " host_level_bonus="+std::to_string(r.host_level_bonus)+
        " reward_x"+price(r.reward_multiplier)+" stake_x"+price(r.stake_multiplier)+
        " abandon_m="+std::to_string(r.abandon_envelope_m)+
        flag("lap_engine",r.uses_lap_engine)+flag("independent_laps",r.independent_laps)+
        flag("recorded_line",r.has_recorded_line)+flag("wagered",r.wagered)+
        flag("blackout",r.blackout_clearance)+flag("continuous",r.continuous_clearance)+
        flag("spectators",r.spectators)+flag("wolves",r.allow_wolves)+flag("road_start",r.road_start)+
        flag("random_dir",r.random_direction)+flag("corridor_dq",r.corridor_dq);
}
} // namespace horse_races::rules
