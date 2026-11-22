#pragma once
#include "race_route_overlay.h"

namespace horse_races::route_config {
// Strict, all-or-nothing parsing: never mix a half-written point with old fields.
// UTF-8 labels are preserved; point IDs/keys are ASCII and case-sensitive.
inline std::optional<Route> Parse(std::string bytes, std::string& error)
{
    error.clear();
    // Takes a std::string so a reason can name the bound it checked rather than repeat the number.
    const auto reject = [&](std::string reason) -> std::optional<Route> { error = std::move(reason); return {}; };
    if (bytes.empty() || bytes.size() > 65536 || bytes.find('\0') != std::string::npos) return reject("empty/oversized/NUL file");
    if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0, 3);
    using Fields = std::map<std::string, std::string>;
    std::map<std::string, Fields> sections;
    std::string section, line;
    std::istringstream input(bytes);
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[') {
            if (line.back() != ']') return reject("incomplete section");
            section = line.substr(1, line.size() - 2);
            if (section != "Route" && section!="Course" && section!="Circuit" && section!="Circuit.Entry" &&
                section!="Circuit.Shared" && section!="Circuit.Return" && section!="Circuit.Finish" && section!="Circuit.Loop" &&
                section!="Circuit.Trip" &&
                !(section.starts_with("Point.") && ValidId(section.substr(6))))
                return reject("unknown section or invalid point ID");
            if (sections.contains(section) || sections.size() >= 65) return reject("duplicate/too many sections");
            sections.emplace(section, Fields{});
            continue;
        }
        const auto equals = line.find('=');
        if (section.empty() || equals == std::string::npos) return reject("key without section/value");
        const std::string key = Trim(line.substr(0, equals)), value = Trim(line.substr(equals + 1));
        if (value.empty() || !sections.at(section).emplace(key, value).second) return reject("empty/duplicate key");
    }
    const auto active = sections.find("Route");
    if (active == sections.end()) return reject("missing Route section");
    const auto& route = active->second;
    if (route.size() != 3 + route.count("TitleZh") + route.count("RewardMultiplier") + route.count("StakeMultiplier") + route.count("Mode") + route.count("Host") + route.count("Era") + route.count("RandomDirection") + route.count("HostPoint") + route.count("HostHeading") + route.count("GridHeading") + route.count("Breed") + route.count("FieldMin") + route.count("FieldMax") + route.count("ContinuousClearance")
        + route.count("HostBoostSeconds") + route.count("PricingDistanceMetres")
        + route.count("FieldSpeedMin") + route.count("FieldSpeedMax") + route.count("GridWidth")
        + route.count("HostSpeedLevelBonus") ||
        !route.contains("Title") || !route.contains("StartPoint") || !route.contains("FinishPoint") ||
        (route.contains("TitleZh") && !ValidLabel(route.at("TitleZh"))) ||
        !ValidLabel(route.at("Title")) || !ValidId(route.at("StartPoint")) || !ValidId(route.at("FinishPoint")))
        return reject("Route requires Title, StartPoint, FinishPoint, Host; optional TitleZh/RewardMultiplier/StakeMultiplier/Mode/RandomDirection/ContinuousClearance");
    const bool single_loop=route.contains("Mode") && route.at("Mode")=="SingleLap";
    const bool independent_laps=route.contains("Mode") && route.at("Mode")=="RecordedLaps";
    // A single trip is a point-to-point run judged with the circuit machinery -
    // same corridor, ordered gates, section-jump foul - but it never rings, so
    // its endpoints are separated like a destination race, not identical.
    const bool single_trip=route.contains("Mode") && route.at("Mode")=="SingleTrip";
    const bool is_circuit=single_loop || independent_laps || single_trip ||
        (route.contains("Mode") && route.at("Mode")=="Circuit");
    const bool open_ended=independent_laps || single_trip;
    if(route.contains("RandomDirection") && route.at("RandomDirection")!="0" && route.at("RandomDirection")!="1")
        return reject("RandomDirection must be 0 or 1");
    const bool random_direction=route.contains("RandomDirection") && route.at("RandomDirection")=="1";
    if(route.contains("ContinuousClearance") && route.at("ContinuousClearance")!="0" && route.at("ContinuousClearance")!="1")
        return reject("ContinuousClearance must be 0 or 1");
    const bool continuous_clearance=route.contains("ContinuousClearance") && route.at("ContinuousClearance")=="1";
    if(is_circuit && random_direction)return reject("RandomDirection is only supported for destination races");
    // Who hosts a race is route data. A station has no host of its own to fall
    // back on, so a route that does not say is not a route.
    if(!route.contains("Host"))return reject("Route requires Host: the profile key of who hosts this race");
    if(!hosts::Find(route.at("Host")))return reject("unknown fixed Host profile");
    if(route.contains("Era") && route.at("Era")!="Arthur" && route.at("Era")!="John")return reject("Era must be Arthur or John");
    if(route.contains("Mode") && !is_circuit && route.at("Mode")!="Destination")return reject("unknown route Mode");
    if(!is_circuit && (sections.contains("Circuit") || sections.contains("Circuit.Entry") || sections.contains("Circuit.Shared") ||
        sections.contains("Circuit.Return") || sections.contains("Circuit.Finish") || sections.contains("Circuit.Loop") ||
        sections.contains("Circuit.Trip")))
        return reject("Circuit sections require Mode=Circuit, SingleLap, RecordedLaps or SingleTrip");
    if(!single_trip && sections.contains("Circuit.Trip"))return reject("Circuit.Trip requires Mode=SingleTrip");
    if(is_circuit && sections.contains("Course"))return reject("Circuit and Course cannot be mixed");
    double reward_multiplier=1, stake_multiplier=1;
    if ((route.contains("RewardMultiplier") && !Multiplier(route.at("RewardMultiplier"),reward_multiplier)) ||
        (route.contains("StakeMultiplier") && !Multiplier(route.at("StakeMultiplier"),stake_multiplier)))
        return reject("reward/stake multipliers must be Distance or 1..100 in half-unit steps (for example 2.5)");
    std::map<std::string, Point> points;
    for (const auto& [name, fields] : sections) {
        if (!name.starts_with("Point.")) continue;
        Point point; point.id = name.substr(6);
        if (fields.size() != 4 + fields.count("NameZh") || !fields.contains("Name") || !fields.contains("X") || !fields.contains("Y") ||
            (fields.contains("NameZh") && !ValidLabel(fields.at("NameZh"))) ||
            !fields.contains("Z") || !ValidLabel(fields.at("Name"))) return reject("point requires Name, X, Y, Z; optional NameZh");
        point.name = fields.at("Name");
        if (fields.contains("NameZh")) point.name_zh = fields.at("NameZh");
        if (!Number(fields.at("X"), point.position.x) || !Number(fields.at("Y"), point.position.y) ||
            !Number(fields.at("Z"), point.position.z) || std::abs(point.position.x) > 20000.0F ||
            std::abs(point.position.y) > 20000.0F || point.position.z < -500.0F || point.position.z > 3000.0F)
            return reject("invalid/nonfinite/out-of-bounds coordinate");
        points.emplace(point.id, point);
    }
    const auto start = points.find(route.at("StartPoint")), finish = points.find(route.at("FinishPoint"));
    if (start == points.end() || finish == points.end()) return reject("selected point does not exist");
    const float separation = destination::Distance(start->second.position, finish->second.position);
    // A ring comes back to where it started; a destination or a single trip is
    // a journey and must be one. Recorded laps are neither: the lap is the
    // distance, and the finish may be a few metres past the start line, which
    // is how a real circuit's finish usually sits (2026-09-16).
    const float minimum_separation=independent_laps?5.0F:50.0F;
    if (is_circuit && !open_ended ? !SamePoint(start->second,finish->second) :
        (separation < minimum_separation || separation > 30000.0F))
        return reject("endpoints too close or too far; a ringing Circuit must use the same start/finish point");
    Route result{route.at("Title"),start->second,finish->second,route.contains("TitleZh")?route.at("TitleZh"):"",
        reward_multiplier,stake_multiplier};
    if(route.contains("PricingDistanceMetres")) {
        if(is_circuit || sections.contains("Course") ||
            !Number(route.at("PricingDistanceMetres"),result.pricing_distance_metres) ||
            result.pricing_distance_metres<separation || result.pricing_distance_metres>100000)
            return reject("PricingDistanceMetres requires an unrecorded destination and a surveyed length between endpoint separation and 100000m");
    }
    result.host=route.at("Host");
    if(route.contains("Era"))result.era=route.at("Era");
    result.random_direction=random_direction;result.continuous_clearance=continuous_clearance;
    if(route.contains("HostPoint")) {
        const auto host=points.find(route.at("HostPoint"));
        if(host==points.end() || destination::Distance(host->second.position,result.start.position)>50)
            return reject("HostPoint must exist within 50m of grid");
        result.host_point=host->second;
    }
    for(auto [key,target]:{std::pair{"HostHeading",&result.host_heading},{"GridHeading",&result.grid_heading}}) {
        if(!route.contains(key))continue;
        float angle=0;if(!Number(route.at(key),angle) || angle<0 || angle>=360)return reject("heading must be 0..<360");
        *target=angle;
    }
    if(route.contains("HostSpeedLevelBonus")) {
        if(!Number(route.at("HostSpeedLevelBonus"),result.host_level_bonus) ||
            !(result.host_level_bonus>0) || result.host_level_bonus>host_boost::kMaxLevelBonus)
            return reject("HostSpeedLevelBonus must be above 0 and at most two levels");
    }
    if(route.contains("HostBoostSeconds")) {
        if(!Number(route.at("HostBoostSeconds"),result.host_boost_seconds) ||
            result.host_boost_seconds<0 || result.host_boost_seconds>120)
            return reject("HostBoostSeconds must be 0 (no limit) to 120");
        if(!(result.host_level_bonus>0))return reject("HostBoostSeconds needs HostSpeedLevelBonus");
    }
    for(auto [key,target]:{std::pair{"FieldSpeedMin",&result.field_speed_min},{"FieldSpeedMax",&result.field_speed_max}}) {
        if(!route.contains(key))continue;
        float level=0;
        if(!Number(route.at(key),level) || level<1 || level>10 || level!=static_cast<int>(level))
            return reject("a field level must be a whole 1..10");
        *target=static_cast<int>(level);
    }
    if((result.field_speed_min>0)!=(result.field_speed_max>0) ||
        (result.field_speed_min>0 && result.field_speed_min>result.field_speed_max))
        return reject("FieldSpeedMin and FieldSpeedMax go together, lowest first");
    if(route.contains("GridWidth")) {
        float width=0;
        if(!Number(route.at("GridWidth"),width) || (width!=2 && width!=3))return reject("GridWidth must be 2 or 3");
        result.grid_width=static_cast<int>(width);
    }
    // Breed alone limits a one-on-one race to that breed. With FieldMin and
    // FieldMax it is a field race instead: a size with no breed is an
    // authoring mistake, never a default.
    const std::size_t field_keys=route.count("Breed")+route.count("FieldMin")+route.count("FieldMax");
    if(field_keys==1 && route.count("Breed")) {
        if(!field_race::FindBreed(route.at("Breed")))return reject("unknown Breed");
        result.breed=route.at("Breed");
    } else if(field_keys) {
        if(field_keys!=3)return reject("a field race needs Breed, FieldMin and FieldMax together");
        if(!field_race::FindBreed(route.at("Breed")))return reject("unknown Breed");
        Route::Field f{route.at("Breed"),0,0};
        auto count=[](const std::string& s,int& n) {
            if(s.size()!=1 || s[0]<'0' || s[0]>'9')return false;
            n=s[0]-'0';return true;
        };
        if(!count(route.at("FieldMin"),f.minimum) || !count(route.at("FieldMax"),f.maximum) ||
            f.minimum<1 || f.maximum<f.minimum ||
            // Host plus rivals plus the player must still fit one starting grid.
            f.maximum>field_race::kMaxFieldRivals)
            return reject("FieldMin/FieldMax must be 1..7 rivals and fit the grid");
        if(!result.grid_heading)return reject("a field race needs a surveyed GridHeading");
        f.host_level_bonus=result.host_level_bonus;
        result.field=f;
    }
    auto read_nodes=[&](const Fields& nodes,std::vector<Vector3>& out) {
        if(nodes.size()<2 || nodes.size()>course::kMaxNodes)return false;
        for(std::size_t i=0;i<nodes.size();++i) {
            const auto node=nodes.find("Node"+std::to_string(i));if(node==nodes.end())return false;
            std::string xyz=node->second;if(std::count(xyz.begin(),xyz.end(),',')!=2)return false;
            std::replace(xyz.begin(),xyz.end(),',',' ');std::istringstream values(xyz);Vector3 p{};std::string extra;
            if(!(values>>p.x>>p.y>>p.z) || values>>extra || !destination::Finite(p) || std::abs(p.x)>20000 ||
                std::abs(p.y)>20000 || p.z< -500 || p.z>3000)return false;
            out.push_back(p);
        }
        return true;
    };
    if(is_circuit) {
        if(!sections.contains("Circuit"))return reject("missing Circuit policy");
        const auto& policy=sections.at("Circuit");lap::Definition definition;
        auto integer=[](const std::string& s,int& n){
            if(s.empty() || s.size()>4 || s.find_first_not_of("0123456789")!=std::string::npos)return false;
            n=std::stoi(s);return true;
        };
        // Optional in every circuit mode: bound the drawn yellow line instead of
        // showing a whole self-crossing lap. Absent keys keep the shipped look.
        const std::size_t guide_fields=policy.count("GuideAheadMetres")+policy.count("GuideBehindMetres");
        if(policy.count("GuideBehindMetres") && !policy.count("GuideAheadMetres"))
            return reject("GuideBehindMetres requires GuideAheadMetres");
        if((policy.count("GuideAheadMetres") && !Number(policy.at("GuideAheadMetres"),definition.guide_ahead)) ||
            (policy.count("GuideBehindMetres") && !Number(policy.at("GuideBehindMetres"),definition.guide_behind)))
            return reject("invalid guidance window");
        if(single_trip) {
            // One recorded line, open at both ends. No lap count, no gate list:
            // the plan spaces its own gates along the trip, so the policy here
            // is only the corridor and the optional guidance window.
            definition.single_trip=true;definition.spectators=false;
            definition.extra_min=definition.extra_max=0;
            if(policy.size()!=2+guide_fields || !policy.contains("TrackHalfWidth") || !policy.contains("HeightTolerance") ||
                !Number(policy.at("TrackHalfWidth"),definition.strict_half_width) ||
                !Number(policy.at("HeightTolerance"),definition.strict_height))
                return reject("SingleTrip requires TrackHalfWidth and HeightTolerance only");
            if(sections.contains("Circuit.Entry") || sections.contains("Circuit.Shared") ||
                sections.contains("Circuit.Return") || sections.contains("Circuit.Finish") ||
                sections.contains("Circuit.Loop") || !sections.contains("Circuit.Trip") ||
                !read_nodes(sections.at("Circuit.Trip"),definition.shared))
                return reject("SingleTrip requires only Circuit.Trip geometry");
            lap::Plan checked;
            if(!checked.Build(definition,0) ||
                destination::Distance(definition.shared.front(),result.start.position)>.1F ||
                std::abs(definition.shared.front().z-result.start.position.z)>.25F ||
                destination::Distance(definition.shared.back(),result.finish.position)>.1F ||
                std::abs(definition.shared.back().z-result.finish.position.z)>.25F)
                return reject("invalid SingleTrip geometry/grid/finish/corridor");
            result.circuit=std::move(definition);
        } else if(independent_laps) {
            definition.independent_laps=true;definition.spectators=false;
            const auto audience_fields=policy.count("Spectators");
            if(audience_fields){
                if(policy.at("Spectators")!="0" && policy.at("Spectators")!="1")
                    return reject("RecordedLaps Spectators must be 0 or 1");
                definition.spectators=policy.at("Spectators")=="1";
            }
            if(policy.size()!=4+guide_fields+audience_fields || !policy.contains("ExtraLapsMin") || !policy.contains("ExtraLapsMax") ||
                !policy.contains("TrackHalfWidth") || !policy.contains("HeightTolerance") ||
                !integer(policy.at("ExtraLapsMin"),definition.extra_min) || !integer(policy.at("ExtraLapsMax"),definition.extra_max) ||
                !Number(policy.at("TrackHalfWidth"),definition.strict_half_width) || !Number(policy.at("HeightTolerance"),definition.strict_height))
                return reject("RecordedLaps requires ExtraLapsMin/Max, TrackHalfWidth, HeightTolerance");
            if(sections.contains("Circuit.Return") || sections.contains("Circuit.Loop"))return reject("RecordedLaps uses full Entry/Shared/Finish only");
            for(auto [key,nodes]:{std::pair{"Circuit.Entry",&definition.entry},{"Circuit.Shared",&definition.shared},{"Circuit.Finish",&definition.finish}})
                if(!sections.contains(key) || !read_nodes(sections.at(key),*nodes))return reject("missing recorded lap shape");
            lap::Plan checked;
            if(!checked.Build(definition,definition.extra_max) ||
                destination::Distance(definition.entry.front(),result.start.position)>.1F ||
                std::abs(definition.entry.front().z-result.start.position.z)>.25F ||
                destination::Distance(definition.finish.back(),result.finish.position)>.1F ||
                std::abs(definition.finish.back().z-result.finish.position.z)>.25F)
                return reject("invalid recorded lap seam/grid/finish/policy");
            result.circuit=std::move(definition);
        } else if(single_loop) {
            definition.single_loop=true;definition.extra_min=definition.extra_max=0;
            const std::size_t grid_fields=policy.count("RoadStart");
            if(grid_fields && policy.at("RoadStart")!="0" && policy.at("RoadStart")!="1")
                return reject("RoadStart must be 0 or 1");
            definition.road_start=grid_fields && policy.at("RoadStart")=="1";
            if(!policy.contains("TrackHalfWidth") || !policy.contains("HeightTolerance") ||
                !policy.contains("Spectators") || !policy.contains("AllowWolves") ||
                policy.size()<7+grid_fields+guide_fields || policy.size()>68+grid_fields+guide_fields ||
                !Number(policy.at("TrackHalfWidth"),definition.strict_half_width) ||
                !Number(policy.at("HeightTolerance"),definition.strict_height) ||
                (policy.at("Spectators")!="0" && policy.at("Spectators")!="1") ||
                (policy.at("AllowWolves")!="0" && policy.at("AllowWolves")!="1"))
                return reject("SingleLap requires TrackHalfWidth, HeightTolerance, Spectators, AllowWolves and 3..64 gates");
            definition.spectators=policy.at("Spectators")=="1";
            definition.allow_wolves=policy.at("AllowWolves")=="1";
            // The authored Valentine audience placement is not a generic trail
            // crowd generator. An unsupported request must fail explicitly.
            if(definition.spectators)return reject("SingleLap currently requires Spectators=0");
            for(std::size_t i=0;i<policy.size()-4-grid_fields;++i) {
                const auto gate=policy.find("Gate"+std::to_string(i));int index=0;
                if(gate==policy.end() || !integer(gate->second,index))return reject("invalid SingleLap gate index");
                definition.required.push_back(static_cast<std::size_t>(index));
            }
            if(sections.contains("Circuit.Entry") || sections.contains("Circuit.Shared") ||
                sections.contains("Circuit.Return") || sections.contains("Circuit.Finish") ||
                !sections.contains("Circuit.Loop") || !read_nodes(sections.at("Circuit.Loop"),definition.shared))
                return reject("SingleLap requires only Circuit.Loop geometry");
            lap::Plan checked;
            if(!checked.Build(definition,0) || destination::Distance(definition.shared.front(),result.start.position)>.1F ||
                std::abs(definition.shared.front().z-result.start.position.z)>.25F ||
                destination::Distance(definition.shared[0],definition.shared[1])<4)
                return reject("invalid SingleLap geometry/start/direction/corridor/gates");
            result.circuit=std::move(definition);
        } else {
        if(sections.contains("Circuit.Loop"))return reject("Circuit.Loop requires Mode=SingleLap");
        const std::size_t corridor_fields=policy.count("TrackHalfWidth")+policy.count("HeightTolerance");
        if((policy.contains("TrackHalfWidth") && !Number(policy.at("TrackHalfWidth"),definition.strict_half_width)) ||
            (policy.contains("HeightTolerance") && !Number(policy.at("HeightTolerance"),definition.strict_height)))
            return reject("invalid Circuit corridor tolerance");
        if(!policy.contains("ExtraLapsMin") || !policy.contains("ExtraLapsMax") ||
            policy.size()<5+corridor_fields+guide_fields || policy.size()>6+corridor_fields+guide_fields ||
            !integer(policy.at("ExtraLapsMin"),definition.extra_min) || !integer(policy.at("ExtraLapsMax"),definition.extra_max))
            return reject("Circuit requires ExtraLapsMin/Max and 3..4 ordered Gate indices");
        // The size check above already allows for the guide window; the gate
        // count must exclude it too, or a Circuit with a window looks for gates
        // that were never written.
        for(std::size_t i=0;i<policy.size()-2-corridor_fields-guide_fields;++i) {
            const auto gate=policy.find("Gate"+std::to_string(i));int index=0;
            if(gate==policy.end() || !integer(gate->second,index))return reject("invalid Circuit gate index");
            definition.required.push_back(static_cast<std::size_t>(index));
        }
        for(auto pair:{std::pair{"Circuit.Entry",&definition.entry},{"Circuit.Shared",&definition.shared},
            {"Circuit.Return",&definition.returning},{"Circuit.Finish",&definition.finish}}) {
            if(!sections.contains(pair.first) || !read_nodes(sections.at(pair.first),*pair.second))
                return reject("missing/invalid Circuit section nodes");
        }
        lap::Plan checked;
        if(!checked.Build(definition,definition.extra_max) ||
            destination::Distance(definition.entry.front(),result.start.position)>.1F ||
            std::abs(definition.entry.front().z-result.start.position.z)>.25F)
            return reject("invalid Circuit policy/geometry/seams/gates/start point");
        result.circuit=std::move(definition);
        }
    }
    if(const auto found=sections.find("Course");found!=sections.end()) {
        const auto& nodes=found->second;
        if(nodes.size()<2 || nodes.size()>course::kMaxNodes)
            return reject("Course requires 2.."+std::to_string(course::kMaxNodes)+" consecutive nodes");
        for(std::size_t i=0;i<nodes.size();++i) {
            const auto node=nodes.find("Node"+std::to_string(i));
            if(node==nodes.end())return reject("Course nodes must be consecutive Node0..NodeN");
            std::string xyz=node->second;
            if(std::count(xyz.begin(),xyz.end(),',')!=2)return reject("Course node requires X,Y,Z");
            std::replace(xyz.begin(),xyz.end(),',',' ');
            std::istringstream values(xyz);Vector3 p{};std::string extra;
            if(!(values>>p.x>>p.y>>p.z) || values>>extra || !destination::Finite(p) ||
                std::abs(p.x)>20000 || std::abs(p.y)>20000 || p.z< -500 || p.z>3000)
                return reject("invalid Course coordinate");
            result.course_points.push_back(p);
        }
        course::Path checked;
        if(!checked.Build(result.course_points))return reject("Course has invalid lengths or duplicate/oversized segments");
        if(destination::Distance(result.course_points.front(),result.start.position)>30 ||
            destination::Distance(result.course_points.back(),result.finish.position)>15)
            return reject("Course endpoints do not match the selected stations");
        if(random_direction && destination::Distance(result.course_points.front(),result.start.position)>15)
            return reject("RandomDirection requires both Course endpoints within the 15m finish tolerance");
    }
    return result;
}
} // namespace horse_races::route_config
