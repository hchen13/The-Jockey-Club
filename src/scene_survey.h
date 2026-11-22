#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// A scene survey: what stands around the player, written to a file so a
// camp the game built can be copied. Started from the F11 menu's debug rows;
// the natives live in scene_survey.cpp, this header is the record and its
// text form, testable offline.
//
// The ordinary F11 survey only reads existing pool entities, so it cannot
// change map streaming while diagnosing a broken camp. Explicit detailed
// mode can also query/pin map entities (tent canvas, ground-plan beds) by
// model from several points around the player.
namespace horse_races::scene_survey {
struct Record {
    std::string kind;        // player, pinned, object, ped, vehicle, mapobject (a map entity found by model)
    std::uint32_t model=0;
    float x=0,y=0,z=0,heading=0,pitch=0,roll=0,yaw=0,distance=0;
    int handle=0;
    std::string extra;
    bool operator==(const Record&) const=default;
};
inline bool Same(const Record& a,const Record& b) {
    return a.model==b.model && std::abs(a.x-b.x)<.01F && std::abs(a.y-b.y)<.01F && std::abs(a.z-b.z)<.01F;
}
// One instance once, however many query points found it.
inline bool Add(std::vector<Record>& records,const Record& r) {
    for(const auto& have:records)if(have.kind==r.kind && Same(have,r))return false;
    records.push_back(r);return true;
}
inline std::string Hex(std::uint32_t v){char b[16];std::snprintf(b,sizeof b,"0x%08X",v);return b;}
inline std::string Line(const Record& r) {
    // The pose in a fixed buffer; the extra (a ped's whole flag list ran to
    // 400 characters and was cut at 320 on 2026-09-15) appended whole.
    char b[200];
    std::snprintf(b,sizeof b,"%s %s %u %.4f %.4f %.4f h=%.3f rot=%.3f,%.3f,%.3f d=%.2f handle=%d",
        r.kind.c_str(),Hex(r.model).c_str(),r.model,r.x,r.y,r.z,r.heading,r.pitch,r.roll,r.yaw,r.distance,r.handle);
    std::string line=b;
    if(!r.extra.empty())line+=" "+r.extra;
    return line;
}
// The file: a header, the player, then every record nearest first.
inline std::string Format(const std::string& stamp,const std::vector<Record>& records) {
    std::string out="# The Jockey Club scene survey "+stamp+"\n"
        "# kind model_hex model_dec x y z h=heading rot=pitch,roll,yaw d=distance_to_player handle extra\n";
    std::vector<const Record*> sorted;
    for(const auto& r:records)sorted.push_back(&r);
    std::stable_sort(sorted.begin(),sorted.end(),[](const Record* a,const Record* b){
        if((a->kind=="player")!=(b->kind=="player"))return a->kind=="player";
        return a->distance<b->distance;});
    for(const auto* r:sorted)out+=Line(*r)+"\n";
    return out;
}
// Query points: the player and a ring around them, so a model with several
// instances nearby (two chairs) is found more than once.
struct Point { float x=0,y=0,z=0; };
inline std::vector<Point> QueryPoints(Point player,float ring=8.0F,std::size_t count=8) {
    std::vector<Point> points{player};
    for(std::size_t i=0;i<count;++i) {
        const float a=static_cast<float>(i)*6.2831853F/static_cast<float>(count);
        points.push_back({player.x+ring*std::cos(a),player.y+ring*std::sin(a),player.z});
    }
    return points;
}
} // namespace horse_races::scene_survey
