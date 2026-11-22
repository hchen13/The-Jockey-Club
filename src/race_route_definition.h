#pragma once

#include "race_destination.h"
#include "race_course.h"
#include "race_segmented_circuit.h"
#include "race_host_profiles.h"
#include "race_wager.h"
#include "race_distance_pricing.h"
#include "race_breeds.h"
#include "race_host_boost.h"
#include <map>
#include <sstream>
#include <string>

namespace horse_races::route_config {
struct Point {
    std::string id, name;
    Vector3 position{};
    std::string name_zh;
};
struct Route {
    std::string title;
    Point start, finish;
    std::string title_zh;
    double reward_multiplier = 1; // 0 = Distance; positive = authored fixed coefficient.
    double stake_multiplier = 1;
    std::vector<Vector3> course_points;
    std::optional<lap::Definition> circuit;
    std::string host; // Required: the profile key of who hosts this race. Live-reloadable.
    // Optional Era=Arthur|John: the station is there only while the player is
    // that protagonist. Empty is both. John's stakes race is Era=Arthur: once
    // the player is John the same race is Arthur's, at his camp.
    std::string era;
    bool random_direction = false; // Destination races only; resolved once per offer cycle.
    // ContinuousClearance=1. Police the whole course for the whole race: a
    // nonparticipant that spawns on it is removed, one approaching it is turned
    // away. Off by default; every fixed-station race still gets the blackout
    // clearance that guarantees an unobstructed start.
    bool continuous_clearance = false;
    std::optional<Point> host_point; // Off-road offer pose, independent of the actual grid.
    std::optional<float> host_heading,grid_heading;
    // A field race: the host plus this many extra rivals, all on one breed.
    // Absent means the ordinary one-against-one race. This is what decides a
    // multi-rider start, not which station the player happens to be standing at.
    // host_top_kmh 0 means the host races on the same ceiling as everyone
    // else; above it, the host is deliberately given more top end.
    // host_level_bonus is how much faster the host is AT THE RANK SHE DREW:
    // 0.5 means her 7 beats our 7 and loses to our 8. A fixed target km/h was
    // wrong - it made her unbeatable at low ranks and slower than the player at
    // high ones, because it ignored the rank entirely. Acceleration is not here
    // on purpose: it belongs to the difficulty settings, like every other rival.
    struct Field {std::string breed;int minimum=0,maximum=0;float host_level_bonus=0;};
    std::optional<Field> field;
    // The host's earned edge, in fractions of a speed level, and how long one
    // burst may last before he has to earn it again. Zero seconds is Sadie's:
    // it holds for as long as the gait does. Outside a field race the edge is
    // the host's all the same - Arthur bursts in his one-on-ones too.
    float host_level_bonus=0;
    float host_boost_seconds=0;
    // Optional field-only speed band, player-visible 1..10, with natural
    // acceleration. Thoroughbred fields instead use the host's ability band.
    int field_speed_min=0,field_speed_max=0;
    // How many abreast the starting grid stands: 0 keeps the shipped three.
    int grid_width=0;
    // Breed= on its own: one rival on the host's own mount of that breed, and
    // the player must bring one too. A field race is Breed with FieldMin and
    // FieldMax; this is the same gate without the grid.
    std::string breed;
    // Surveyed road reference for a free-route destination race. This prices
    // the offer only; it does not turn the ride into a judged fixed corridor.
    float pricing_distance_metres=0;
};

inline bool SamePoint(const Point& a, const Point& b)
{
    return a.id == b.id && a.name == b.name && a.name_zh == b.name_zh && a.position.x == b.position.x &&
        a.position.y == b.position.y && a.position.z == b.position.z;
}
inline bool SameRoute(const Route& a, const Route& b)
{
    return a.title == b.title && a.title_zh == b.title_zh && SamePoint(a.start, b.start) && SamePoint(a.finish, b.finish) &&
        a.reward_multiplier == b.reward_multiplier && a.stake_multiplier == b.stake_multiplier &&
        a.pricing_distance_metres == b.pricing_distance_metres &&
        a.host==b.host && a.era==b.era && a.random_direction==b.random_direction && a.continuous_clearance==b.continuous_clearance &&
        a.host_heading==b.host_heading && a.grid_heading==b.grid_heading &&
        a.host_point.has_value()==b.host_point.has_value() && (!a.host_point || SamePoint(*a.host_point,*b.host_point)) &&
        a.field.has_value()==b.field.has_value() && (!a.field || (a.field->breed==b.field->breed &&
            a.field->minimum==b.field->minimum && a.field->maximum==b.field->maximum &&
            a.field->host_level_bonus==b.field->host_level_bonus)) &&
        a.circuit==b.circuit && a.course_points.size()==b.course_points.size() && std::equal(a.course_points.begin(),a.course_points.end(),
            b.course_points.begin(),[](Vector3 p,Vector3 q){return p.x==q.x && p.y==q.y && p.z==q.z;});
}
// Preserve the canonical INI revision; AI, GPS, labels and finish checks consume
// this same resolved copy. Reversing a surveyed course also reverses every node.
inline Route Directed(const Route& definition, bool reverse)
{
    Route result=definition;
    if(reverse && !result.circuit) {
        std::swap(result.start,result.finish);
        std::reverse(result.course_points.begin(),result.course_points.end());
    }
    return result;
}
} // namespace horse_races::route_config
