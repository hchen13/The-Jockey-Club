#pragma once
#include <array>
#include <cstddef>
#include <span>
#include "race_character_policy.h"

namespace horse_races::bookmaking {
struct VenuePose {float x,y,z,heading;};
struct Venue {
    const char *key,*title,*chinese,*route_file,*clerk_model,*clerk_name;
    VenuePose clerk,viewer;
    std::size_t entrants;
    int columns;
    int fixed_extra_laps;
    bool shared_attributes,crowd,greeting;
    float grid_forward_metres=0;
    characters::Id clerk_host=characters::Id::OtherRival;
    std::span<const int> clerk_outfits{};
};
inline constexpr std::array<int,4> kRanchBookieOutfits{6,12,17,19};
inline constexpr std::array<Venue,4> kVenues{{
    {"valentine-circuit","VALENTINE HANDICAP","瓦伦丁让磅赛","CharlesRaceRoutes.ini","CS_CLAY","Clay Davies",
        {-250.9171753F,781.9220581F,118.3924866F,101.64257895F},
        {-330.6638184F,778.1528320F,121.5237961F,30.9958040F},10,5,2,true,true,true,5.0F},
    {"strawberry-wfa","STRAWBERRY WFA","草莓镇平磅赛","StrawberryWfaRoutes.ini","A_M_M_RANCHER_01","Bookmaker",
        {-1773.691406F,-548.159058F,157.410675F,164.545639F},
        {-1755.745239F,-567.311462F,157.831757F,79.367241F},7,2,-1,false,false,false},
    {"rhodes-handicap","RHODES HANDICAP","罗兹镇让磅赛","RhodesHandicapRoutes.ini","CS_CLAY","Clay Davies",
        {1322.086914F,-1346.935425F,77.922546F,228.819611F},
        {1230.731812F,-1279.179443F,79.192116F,326.002747F},6,3,-1,true,true,true},
    {"macfarlane-wfa","MACFARLANE'S RANCH WFA","麦克法伦牧场平磅赛","MacfarlaneWfaRoutes.ini","CS_MRSADLER","Sadie Adler",
        {-2404.537109F,-2351.468018F,61.187817F,331.218475F},
        {-2430.762207F,-2363.416260F,61.175507F,219.695541F},4,2,-1,false,false,false,0,
        characters::Id::Sadie,kRanchBookieOutfits}
}};
template<class Circuit>
void ApplyLapPolicy(const Venue& venue,Circuit& circuit) {
    if(venue.fixed_extra_laps>=0)
        circuit.extra_min=circuit.extra_max=venue.fixed_extra_laps;
}
} // namespace horse_races::bookmaking
