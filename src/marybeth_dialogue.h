#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace marybeth_dialogue {
enum class Event {
    Greeting, GreetingAgain, NewHorse, HealthyHorse, Confirm, Start, Decline,
    Ahead, Passed, WrongWay, Collision, PlayerWon, PlayerLost, FellOff, ShotHorse,
    TalkWin, Talk, Goodbye, Evening, TalkLost, Count
};
enum class Phase { Offering, Racing, Result, Interrupted, Departing };
struct Clip {
    const char* id;
    Event event;
    unsigned players; // John=1, Arthur=2. These are listener filters, not speakers.
    const char* context;
    int variation; // Native explicit 1-based variant. NEVER 0/random.
    const char* native_voice=nullptr; // Optional per-clip registered voice, NEVER an AWC bank alias.
};
inline unsigned PlayerMask(std::uint32_t model)
{
    return model==0x00B69710 ? 1U : (model==0x0D7114C9 ? 2U : 0U);
}
inline bool Allowed(Event event, Phase phase, bool fixed, bool won, bool reference, int hour)
{
    switch(event) {
    case Event::Greeting: case Event::GreetingAgain: case Event::NewHorse: case Event::HealthyHorse:
        return phase==Phase::Offering;
    case Event::Confirm: case Event::Start: case Event::Ahead: case Event::Passed: case Event::Collision:
        return phase==Phase::Racing;
    case Event::WrongWay: return phase==Phase::Racing && reference;
    case Event::Decline: return !fixed && phase==Phase::Departing;
    case Event::PlayerWon: case Event::TalkWin: return phase==Phase::Result && won;
    case Event::PlayerLost: case Event::TalkLost: return phase==Phase::Result && !won;
    case Event::Talk: return phase==Phase::Result;
    case Event::FellOff: case Event::ShotHorse: return phase==Phase::Interrupted;
    case Event::Goodbye: return phase==Phase::Departing;
    case Event::Evening: return phase==Phase::Departing && (hour>=18 || hour<5);
    case Event::Count: return false;
    }
    return false;
}
struct Identity {
    int handle=0;
    std::uint32_t model=0;
    std::uintptr_t address=0;
    bool operator==(const Identity&) const = default;
    bool Valid() const { return handle && model && address; }
};
struct Meeting {
    Identity player{}, mount{};
    bool Observe(Identity who, Identity horse) {
        const bool changed=who.Valid() && who==player && mount.Valid() && horse.Valid() &&
            horse!=mount;
        if (who!=player) mount={};
        player=who;
        // Dismounting is not a new meeting with a different horse.
        if (horse.Valid()) mount=horse;
        return changed;
    }
};
struct Pending { Event event; double expires; int clip=-1, attempts=0; };
struct Session {
    std::uint64_t id=0;
    Identity rider{}, player{};
    Phase phase=Phase::Offering;
    bool won=false, result_attempted=false, talk_used=false, farewell_attempted=false;
    bool result_queued=false, result_failed=false, result_approaching=false;
    double result_ready_since=-1, result_approach_at=0, result_next_approach=0, result_next_log=0;
    double result_settled_since=-1,result_next_departure_try=0;
    bool result_departing=false,result_listener_seen=false;
    bool greeting_done=false, new_horse=false;
    double next_speech=0, last_speech=-1000, next_banter=0, lead_since=0;
    int lead_candidate=0, stable_leader=0;
    double last_lead_evidence=-1000, next_pass=0, pass_expires=0;
    int pass_direction=0;
    bool touching=false;
    std::optional<Pending> pending;
    std::array<int,static_cast<std::size_t>(Event::Count)> last_variant{};

    bool Owns(std::uint64_t current_id,Identity current_rider,Identity current_player) const {
        return id && id==current_id && rider.Valid() && player.Valid() &&
            rider==current_rider && player==current_player;
    }
    void Transition(Phase value) {
        phase=value;
        pending.reset(); // No greeting/start/banter can survive into another phase.
        pass_direction=0; pass_expires=0;
        next_speech=0;
    }
    void Queue(Event event,double now,double ttl) { pending=Pending{event,now+ttl}; }
    // Hysteresis rejects adjacent-lane jitter. 0 means no reliable lead.
    // Callers must supply course evidence; raw distance-to-finish isn't a route.
    // Return +1 for player passing, -1 for rival passing, 0 for no new pass.
    int ObserveLead(int candidate,double now) {
        if (now-last_lead_evidence>3.0) { stable_leader=0; lead_candidate=0; }
        // 2 means valid nearby parallel travel, but currently side by side.
        // Slow overtakes must not lose the old leader in that dead band.
        if (candidate==2) { last_lead_evidence=now; lead_candidate=0; return 0; }
        if (!candidate) { lead_candidate=0; return 0; }
        last_lead_evidence=now;
        if (candidate!=lead_candidate) { lead_candidate=candidate;lead_since=now;return 0; }
        if (candidate==stable_leader || now-lead_since<0.4) return 0;
        const int passed=stable_leader && stable_leader!=candidate ? candidate:0;
        stable_leader=candidate;
        return passed;
    }
};
} // namespace marybeth_dialogue
