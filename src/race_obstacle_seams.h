#pragma once
#include "race_native_corridor.h"
#include <locale>

namespace horse_races::obstacle_seams {
struct Certificate {
    bool loaded=false;
    std::size_t from_row=0;
    std::vector<obstacle_map::Passage> strips;
};
inline std::string Key(const obstacle_map::Map& from,const obstacle_map::Map& to) {
    return from.key+"--"+to.key;
}
inline std::size_t LastRow(const obstacle_map::Map& map,const course::Path& path) {
    auto row=map.rows.size();
    while(row&&map.Along(row-1)>=path.Length()-.0001F)--row;
    return row?row-1:map.rows.size();
}
// The file proves exactly these two original section frames, including their
// independent normals. It is never an observation of the successor's row-zero
// incoming connection in its standalone recorded profile.
inline bool Parse(const std::string& bytes,const obstacle_map::Map& from,const course::Path& from_path,
    const obstacle_map::Map& to,const course::Path& to_path,Certificate& result) {
    result={};if(bytes.size()>32768)return false;
    const auto bound=[](const obstacle_map::Map& map,const course::Path& path) {
        return !map.key.empty()&&map.geometry_hash==obstacle_map::Map::GeometryHash(path)&&
            map.length==path.Length()&&map.row_metres.empty()&&map.rows.size()==obstacle_map::Map::Rows(path.Length());
    };
    if(!bound(from,from_path)||!bound(to,to_path))return false;
    const auto last=LastRow(from,from_path);if(last>=from.rows.size()||to.rows.empty())return false;
    std::istringstream input(bytes);input.imbue(std::locale::classic());
    std::array<std::string,5> lines;
    for(auto& line:lines)if(!std::getline(input,line))return false;
    input>>std::ws;if(!input.eof())return false;
    const auto stream=[](const std::string& line) {std::istringstream in(line);in.imbue(std::locale::classic());return in;};
    const auto word=[](std::istream& in,const char* expected) {std::string value;return bool(in>>value)&&value==expected;};
    const auto ended=[](std::istream& in) {in>>std::ws;return in.eof();};
    auto header=stream(lines[0]);std::string a,b;int version=0;
    if(!word(header,"seam")||!(header>>a>>b)||a!=from.key||b!=to.key||
        !word(header,"version")||!(header>>version)||version!=1||!ended(header))return false;
    const auto endpoint=[&](const std::string& line,const char* side,const obstacle_map::Map& map,
        const course::Path& path,std::size_t expected_row) {
        auto in=stream(line);std::string hash;std::size_t row=0;float along=0,length=0;obstacle_map::Section frame;
        if(!word(in,side)||!word(in,"geometry")||!(in>>hash)||hash!=map.geometry_hash||
            !word(in,"row")||!(in>>row)||row!=expected_row||!word(in,"along")||!(in>>along)||
            !std::isfinite(along)||along!=map.Along(row)||!word(in,"length")||!(in>>length)||
            !std::isfinite(length)||length!=path.Length()||!word(in,"frame")||
            !(in>>frame.centre.x>>frame.centre.y>>frame.centre.z>>frame.nx>>frame.ny)||!ended(in))return false;
        const auto expected=map.SectionFor(path,row);
        return std::isfinite(frame.centre.x)&&std::isfinite(frame.centre.y)&&std::isfinite(frame.centre.z)&&
            std::isfinite(frame.nx)&&std::isfinite(frame.ny)&&frame.centre.x==expected.centre.x&&
            frame.centre.y==expected.centre.y&&frame.centre.z==expected.centre.z&&frame.nx==expected.nx&&frame.ny==expected.ny;
    };
    if(!endpoint(lines[1],"from",from,from_path,last)||!endpoint(lines[2],"to",to,to_path,0))return false;
    auto marker=stream(lines[3]);std::string source;float radius=0;
    if(!word(marker,"native")||!word(marker,"source")||!(marker>>source)||source!=native_corridor::kSource||
        !word(marker,"body_radius")||!(marker>>radius)||radius!=native_corridor::kBodyRadius||!ended(marker))return false;
    auto payload=stream(lines[4]);std::size_t count=0;
    if(!word(payload,"native_strips")||!(payload>>count)||count>64)return false;
    Certificate candidate;candidate.from_row=last;
    const auto entry=from.SectionFor(from_path,last),exit=to.SectionFor(to_path,0);
    const float normal=strip_geometry::MinimumNormalLength(entry,exit);
    for(std::size_t i=0;i<count;++i) {
        obstacle_map::Passage strip;
        if(!(payload>>strip.from.lo>>strip.from.hi>>strip.to.lo>>strip.to.hi>>strip.min_normal))return false;
        const auto valid=[](obstacle_map::Interval span) {
            return std::isfinite(span.lo)&&std::isfinite(span.hi)&&span.lo<span.hi&&span.lo>=-20&&span.hi<=20;
        };
        if(!valid(strip.from)||!valid(strip.to)||!std::isfinite(strip.min_normal)||strip.min_normal<.01F||
            strip.min_normal>1.001F||!std::isfinite(normal)||strip.min_normal>normal+1e-5F)return false;
        const auto prepared=strip_geometry::Prepare({entry,exit,strip.from,strip.to});
        // Permit only serialization-scale inward fold correction. A partly
        // inverted claimed corridor cannot be promoted to a different proof.
        if(!prepared.regular||std::abs(prepared.patch.entry.lo-strip.from.lo)>1e-4F||
            std::abs(prepared.patch.entry.hi-strip.from.hi)>1e-4F||
            std::abs(prepared.patch.exit.lo-strip.to.lo)>1e-4F||
            std::abs(prepared.patch.exit.hi-strip.to.hi)>1e-4F)return false;
        strip.from=prepared.patch.entry;strip.to=prepared.patch.exit;strip.min_normal=std::min(strip.min_normal,normal);
        candidate.strips.push_back(strip);
    }
    if(!ended(payload))return false;
    candidate.loaded=true;result=std::move(candidate);return true;
}
inline Certificate Load(const std::filesystem::path& native_folder,const obstacle_map::Map& from,
    const course::Path& from_path,const obstacle_map::Map& to,const course::Path& to_path) {
    Certificate out;
    const auto safe=[](const std::string& key) {return !key.empty()&&key.size()<=100&&
        std::all_of(key.begin(),key.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||
            (c>='0'&&c<='9')||c=='_'||c=='-';});};
    if(!from.native_loaded||!to.native_loaded||!safe(from.key)||!safe(to.key))return out;
    const auto file=native_folder/"seams"/(Key(from,to)+".txt");
    std::error_code error;const auto size=std::filesystem::file_size(file,error);
    if(error||size>32768)return out;
    std::ifstream input(file,std::ios::binary);if(!input)return out;
    std::string bytes(32769,'\0');input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    if(!input.bad()&&bytes.size()<=32768)Parse(bytes,from,from_path,to,to_path,out);
    return out;
}
} // namespace horse_races::obstacle_seams
