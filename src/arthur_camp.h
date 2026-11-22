#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Arthur's camp at Horseshoe Overlook, for the epilogue: Dutch's old tent
// pitched where the chapter 2 camp stood, a fire in front of it, and Arthur
// - cooking at the fire, resting in a chair, or lying on the bed in the tent
// - with every horse of his stable grazing on the meadow beside it. This
// header is the layout and the decisions; it knows no game natives, so the
// ini file, the transforms, the presence rule and the activity roster are all
// testable offline. The natives that build and tend the scene live in
// arthur_camp.cpp.
//
// The layout is one captured pose (tent centre + heading) and one captured
// point (the meadow's centre); everything else is placed relative to the tent
// in metres - forward is out of the tent's open front, right is the tent's
// right hand, up is up - so re-capturing the tent moves the whole camp with
// it, and a prop that lands badly is a number in ArthurCamp.ini, not a
// rebuild. The tent itself may instead come from the game's own map file
// for the chapter 2 camp ([Ipl] Request=), which is the camp exactly as the
// story had it.
namespace horse_races::arthur_camp {
struct Point { float x=0,y=0,z=0; bool operator==(const Point&) const=default; };
struct Pose { Point position; float heading=0; bool operator==(const Pose&) const=default; };
// A prop of the camp: a model at a local offset and yaw. `ground` drops it
// onto the ground under its spot; a prop that sits on another (the
// phonograph on the table) keeps its stated height instead.
struct Prop {
    std::string key,model;
    float forward=0,right=0,up=0,yaw=0;
    bool ground=true;
    // World pitch and roll in degrees (the game's rotation order 2), for a
    // piece that leans or hangs: the map pinned to the wagon's side, the
    // photographs propped against it, a basin hung upside down. Zero for
    // anything that simply stands; placed flat, those three lay on the ground.
    float pitch=0,roll=0;
    bool operator==(const Prop&) const=default;
};
// A map entity hidden while the camp is up: every instance of the model
// within `radius` of a world position (ENTITY::CREATE_MODEL_HIDE). The
// chapter 2 arrangement map file that lays Dutch's tent floor also raises
// John's tent frame beside Arthur's wagon; [Hide] takes such pieces away.
struct Hide {
    std::string key,model;
    Point position;
    float radius=1;
    bool operator==(const Hide&) const=default;
};
// What John can do at Arthur's wagon, each tied to a prop by its key:
// Locker= the chest the game's own gun locker opens at.
// The camp's outline and the circles inside it that are kept. Everything the
// map puts down inside the outline but outside every circle is put out of
// sight while the camp stands: it is the rule "the two circles that were
// walked round are the camp, and nothing else is wanted", by place rather
// than by model, so no list of models can take a tent's floor with it.
struct Outside {
    Point camp;                       // Centre of the camp.
    float radius=0;                   // Nothing beyond this is touched. 0: off.
    std::vector<Point> circles;       // x,y and radius (in z) of each circle kept.
    bool operator==(const Outside&) const=default;
};
struct Interactions {
    std::string locker;      // Locker=  : the game's gun locker (its own weapon_locker script).
    bool operator==(const Interactions&) const=default;
};
// One of Arthur's activities: a scenario type started at a pose. The pose is
// an offset from a prop (the fire, the chair, the bed) in that prop's own
// frame, so moving the prop moves the activity with it. `anim` names one
// conditional animation of the scenario type (empty: the game's choice).
// The roster is free: [Activities] adds one under any name, or removes one
// with "name=none"; he drifts between whatever is left, never repeating.
struct Scene {
    std::string name,scenario,anchor,anim;
    float forward=0,right=0,up=0,yaw=0;
    bool sitting=false;
    bool operator==(const Scene&) const=default;
};
inline constexpr std::size_t kMaxActivities=24;
struct Layout {
    // [Mary]: the companion who lives here with him. Her activities are
    // [MaryActivities], in the [Activities] grammar, on seats he does not use.
    bool companion=true;
    std::string companion_model="CS_MARYLINTON";
    std::vector<Scene> companion_scenes;
    bool enabled=true;
    bool only_for_john=true;           // The epilogue's Arthur: never a twin of a playing Arthur.
    bool map_marker=true;
    bool player_seats=true;            // Seats get scenario points the player may sit on.
    Pose tent;                          // Captured: centre of the tent, heading out of its open front.
    Point horses;                       // Captured: the meadow's centre.
    float horses_radius=8;              // Metres around it the horses graze.
    float spawn_radius=220,despawn_radius=320;
    double activity_min_seconds=180,activity_max_seconds=360;
    std::vector<Prop> props;
    std::vector<Scene> scenes;         // Never empty once parsed.
    std::vector<std::string> horse_models;
    // The gang's horses at Horseshoe Overlook graze at ease on the meadow,
    // drifting a step or two, on the camp's own grazing scenario.
    std::string horse_scenario="WORLD_ANIMAL_HORSE_GRAZING_CAMP";
    // Where each horse stands, recorded from the chapter 2 camp (world
    // coordinates and heading, [Horses] Spot1..Spot12): the first horse on
    // the first spot and so on. A horse with no spot of its own is left out.
    std::vector<Pose> horse_spots;
    std::vector<int> ipls;               // Map files (IPL hashes) to load while the camp is up.
    std::vector<Hide> hides;             // [Hide] key=model,x,y,z[,radius]: map entities hidden while the camp is up.
    Outside outside;                     // [HideOutside] Camp=x,y,r and Circle1..N=x,y,r.
    Interactions interactions;           // [Interactions] Locker=<prop key>.
    bool operator==(const Layout&) const=default;
};
// A layout is usable once both captures are in: an all-zero tent or meadow
// is the template's TODO, not a place.
inline bool Captured(const Layout& l) {
    const auto real=[](const Point& p){return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
        (p.x!=0 || p.y!=0) && std::abs(p.x)<20000 && std::abs(p.y)<20000 && std::abs(p.z)<5000;};
    return real(l.tent.position) && real(l.horses) && std::isfinite(l.tent.heading);
}
// ---- frames ------------------------------------------------------------------
// The game's heading: 0 faces north (+y), growing counter-clockwise, so a
// heading's forward is (-sin, cos) and its right hand (cos, sin).
inline constexpr float kDegrees=57.29577951F;
inline Point Forward(float heading){const float r=heading/kDegrees;return {-std::sin(r),std::cos(r),0};}
inline Point Right(float heading){const float r=heading/kDegrees;return {std::cos(r),std::sin(r),0};}
inline float Wrap(float heading){heading=std::fmod(heading,360.0F);if(heading<0)heading+=360;return heading;}
inline Pose Offset(const Pose& from,float forward,float right,float up,float yaw) {
    const Point f=Forward(from.heading),r=Right(from.heading);
    return {{from.position.x+f.x*forward+r.x*right,from.position.y+f.y*forward+r.y*right,from.position.z+up},
        Wrap(from.heading+yaw)};
}
inline Pose PropPose(const Layout& l,const Prop& p){return Offset(l.tent,p.forward,p.right,p.up,p.yaw);}
inline const Prop* FindProp(const Layout& l,std::string_view key) {
    const auto it=std::find_if(l.props.begin(),l.props.end(),[&](const Prop& p){return p.key==key;});
    return it==l.props.end()?nullptr:&*it;
}
inline Prop* FindProp(Layout& l,std::string_view key) {
    const auto it=std::find_if(l.props.begin(),l.props.end(),[&](const Prop& p){return p.key==key;});
    return it==l.props.end()?nullptr:&*it;
}
// Where an activity happens: its offset from its anchor prop, or from the
// tent when it names none (or names a prop that is not there).
inline Pose ScenePose(const Layout& l,const Scene& s) {
    const Prop* anchor=s.anchor.empty()?nullptr:FindProp(l,s.anchor);
    const Pose base=anchor?PropPose(l,*anchor):l.tent;
    return Offset(base,s.forward,s.right,s.up,s.yaw);
}
inline std::string Lower(std::string_view s) {
    std::string out(s);for(auto& c:out)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return out;
}
// ---- seats ---------------------------------------------------------------------
// The scenario type a seat prop offers, so the player (and Arthur) can sit
// on it: a log or bench at the fire, a crate or stool, a chair.
// Inside the camp, outside every circle: the pieces the map put down that
// this camp does not want.
inline bool IsOutside(const Outside& o,float x,float y) {
    if(o.radius<=0)return false;
    if(std::hypot(x-o.camp.x,y-o.camp.y)>o.radius)return false;
    for(const auto& c:o.circles)if(std::hypot(x-c.x,y-c.y)<=c.z)return false;
    return true;
}
inline const char* SeatScenario(std::string_view model) {
    const std::string lower=Lower(model);
    // Arthur's own bed: the player's sleep scenario the epilogue's property
    // script watches for (PROP_PLAYER_SLEEP_BED), so John may lie down in it.
    if(lower.find("bedarthur")!=std::string::npos)return "PROP_PLAYER_SLEEP_BED";
    if(lower.find("bench")!=std::string::npos || lower.find("_log")!=std::string::npos)return "PROP_CAMP_FIRE_SEAT_BENCH";
    if(lower.find("crateseat")!=std::string::npos || lower.find("chair_crate")!=std::string::npos || lower.find("stool")!=std::string::npos)return "PROP_HUMAN_CAMP_FIRE_SEAT_BOX";
    if(lower.find("chair")!=std::string::npos)return "PROP_HUMAN_SEAT_CHAIR";
    return nullptr;
}
// Where on the prop the seat's point sits and which way it faces. A point at
// the prop's origin facing 0 seated everyone on the ground, backwards -
// Arthur on 2026-09-16, and on 2026-09-20 the passers-by who walk in and use
// the player's seat points. The values are the ones Arthur's own activities
// proved on the fire chair, the crate and the cot.
struct SeatOffset {float up=0,yaw=0;};
// `measured_top` is the top of the prop, in metres above its origin, which is what a box or a bench
// is sat on: Mary's hands were inside the crate at the 0.42 typed here (the user, 2026-09-22, twice).
// A chair is never measured, because the top of a chair is its back, not its seat.
inline SeatOffset SeatPointOffset(std::string_view scenario,float measured_top=0) {
    if(measured_top>0.2F && measured_top<0.9F &&
       (scenario=="PROP_HUMAN_CAMP_FIRE_SEAT_BOX" || scenario=="PROP_CAMP_FIRE_SEAT_BENCH"))
        return {measured_top,180};
    if(scenario=="PROP_HUMAN_SEAT_CHAIR")return {.45F,180};
    if(scenario=="PROP_HUMAN_CAMP_FIRE_SEAT_BOX")return {.42F,180};
    if(scenario=="PROP_CAMP_FIRE_SEAT_BENCH")return {.42F,180};
    if(scenario=="PROP_PLAYER_SLEEP_BED")return {.55F,180};
    return {};
}
// ---- presence ----------------------------------------------------------------
// The camp is built out of the player's sight when they come within
// spawn_radius, and it stays: a mission, an online session or a playing
// Arthur strike it, and nothing else does - a rider leaves, a camp does not.
struct Conditions {
    bool eligible=false;   // The player is John (when required), no mission, no session, not paused.
    float distance=0;      // Player to tent, metres.
    bool out_of_view=true; // The tent is off camera, or so far that it is a speck.
};
enum class Presence { Absent, Present };
enum class Action { None, Spawn, Despawn };
inline Action Decide(Presence presence,const Conditions& c,const Layout& l) {
    if(!l.enabled)return presence==Presence::Present?Action::Despawn:Action::None;
    if(presence==Presence::Present) {
        if(!c.eligible)return Action::Despawn;
        return Action::None;
    }
    return (c.eligible && c.distance<=l.spawn_radius && c.out_of_view)?Action::Spawn:Action::None;
}
// ---- the roster of activities ------------------------------------------------
// Which one next: never the one just done; `roll` in [0,1) picks among the
// rest. How long: a draw between the two bounds, `unit` in [0,1).
inline std::size_t Next(std::size_t current,std::size_t count,double roll) {
    if(count<=1)return 0;
    const std::size_t n=current<count?count-1:count;
    auto pick=static_cast<std::size_t>(std::clamp(roll,0.0,0.999999)*static_cast<double>(n));
    pick=std::min(pick,n-1);
    return current<count && pick>=current?pick+1:pick;
}
inline std::size_t First(std::size_t count,double roll) {
    if(count==0)return 0;
    const auto pick=static_cast<std::size_t>(std::clamp(roll,0.0,0.999999)*static_cast<double>(count));
    return std::min(pick,count-1);
}
inline const Scene* FindScene(const Layout& l,const std::string& name) {
    for(const auto& s:l.scenes)if(s.name==name)return &s;
    return nullptr;
}
inline double Duration(const Layout& l,double unit) {
    const double lo=std::min(l.activity_min_seconds,l.activity_max_seconds),hi=std::max(l.activity_min_seconds,l.activity_max_seconds);
    return lo+(hi-lo)*std::clamp(unit,0.0,1.0);
}
// ---- ArthurCamp.ini ----------------------------------------------------------
// Sections: [Camp] the captures and the radii; [Props] key=model,forward,
// right,up,yaw[,float]; [Activities] name=scenario,anchor,forward,right,up,
// yaw,sitting[,anim] (or name=none); [Horses] Models=; [Ipl] Request= map
// file hashes. Nothing is in the camp that the file does not put there, and
// a bad line rejects the whole file so a typo never half-applies.
namespace detail {
inline std::string Trim(std::string s) {
    const auto b=s.find_first_not_of(" \t\r\n");if(b==std::string::npos)return "";
    const auto e=s.find_last_not_of(" \t\r\n");return s.substr(b,e-b+1);
}
inline std::vector<std::string> Split(const std::string& s,char sep=',') {
    std::vector<std::string> out;std::string cur;
    for(char c:s){if(c==sep){out.push_back(Trim(cur));cur.clear();}else cur+=c;}
    out.push_back(Trim(cur));return out;
}
inline bool Number(const std::string& s,float& out) {
    if(s.empty() || s.size()>32)return false;
    char* end=nullptr;const double v=std::strtod(s.c_str(),&end);
    if(!end || *end!='\0' || !std::isfinite(v))return false;
    out=static_cast<float>(v);return true;
}
// An IPL hash as the scripts print it: signed decimal, or 0x hex.
inline bool HashValue(const std::string& s,int& out) {
    if(s.empty() || s.size()>12)return false;
    char* end=nullptr;
    const bool hex=s.size()>2 && s[0]=='0' && (s[1]=='x' || s[1]=='X');
    const long long v=hex?std::strtoll(s.c_str()+2,&end,16):std::strtoll(s.c_str(),&end,10);
    if(!end || *end!='\0' || v<-2147483648LL || v>4294967295LL || v==0)return false;
    out=static_cast<int>(static_cast<unsigned>(v&0xffffffffLL));return true;
}
inline bool Flag(const std::string& s,bool& out) {
    if(s=="1" || s=="true" || s=="float")out=true;else if(s=="0" || s=="false" || s=="ground")out=false;else return false;
    return true;
}
inline bool ValidModel(const std::string& s) {
    if(s.empty() || s.size()>64)return false;
    return std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isalnum(c) || c=='_';});
}
// The file's own order is the camp's order. [Activities] is a list and not
// a set: the roster the player picks from in F11 has to read the way the
// file reads, and a prop has to be created in the order it was written. A
// key repeated inside one section rejects the file, so every lookup here is
// a walk over a handful of lines and none of it is hot.
struct Rows {
    std::vector<std::pair<std::string,std::string>> lines;
    auto begin() const {return lines.begin();}
    auto end() const {return lines.end();}
    auto find(std::string_view key) const {
        return std::find_if(lines.begin(),lines.end(),[&](const auto& row){return row.first==key;});
    }
    bool Add(const std::string& key,const std::string& value) {
        if(find(key)!=lines.end())return false;
        lines.push_back({key,value});return true;
    }
};
struct Sections {
    std::vector<std::pair<std::string,Rows>> named;
    auto end() const {return named.end();}
    auto find(std::string_view name) const {
        return std::find_if(named.begin(),named.end(),[&](const auto& section){return section.first==name;});
    }
    Rows& operator[](const std::string& name) {
        for(auto& section:named)if(section.first==name)return section.second;
        named.push_back({name,{}});return named.back().second;
    }
};
}
inline std::optional<Layout> Parse(const std::string& text,std::string& error) {
    Layout l;
    detail::Sections sections;
    std::string section;
    for(std::size_t start=0;start<=text.size();) {
        auto end=text.find('\n',start);if(end==std::string::npos)end=text.size();
        std::string line=detail::Trim(text.substr(start,end-start));start=end+1;
        if(line.empty() || line[0]==';' || line[0]=='#')continue;
        if(line.front()=='[') {
            if(line.back()!=']'){error="malformed section: "+line;return std::nullopt;}
            section=line.substr(1,line.size()-2);continue;
        }
        const auto eq=line.find('=');
        if(eq==std::string::npos){error="not key=value: "+line;return std::nullopt;}
        const std::string key=detail::Trim(line.substr(0,eq)),value=detail::Trim(line.substr(eq+1));
        if(key.empty()){error="empty key: "+line;return std::nullopt;}
        if(!sections[section].Add(key,value)){error="duplicate key "+key;return std::nullopt;}
    }
    const auto camp=sections.find("Camp");
    if(camp==sections.end()){error="missing [Camp]";return std::nullopt;}
    const auto number=[&](const detail::Rows& s,const char* key,float& out,float lo,float hi)->bool{
        const auto it=s.find(key);if(it==s.end())return true;
        float v=0;if(!detail::Number(it->second,v) || v<lo || v>hi){error=std::string("bad ")+key;return false;}
        out=v;return true;
    };
    const auto flag=[&](const detail::Rows& s,const char* key,bool& out)->bool{
        const auto it=s.find(key);if(it==s.end())return true;
        if(!detail::Flag(it->second,out)){error=std::string("bad ")+key;return false;}
        return true;
    };
    float min_seconds=static_cast<float>(l.activity_min_seconds),max_seconds=static_cast<float>(l.activity_max_seconds);
    if(!flag(camp->second,"Enabled",l.enabled) || !flag(camp->second,"OnlyForJohn",l.only_for_john) ||
        !flag(camp->second,"MapMarker",l.map_marker) || !flag(camp->second,"PlayerSeats",l.player_seats) ||
        !number(camp->second,"TentX",l.tent.position.x,-20000,20000) || !number(camp->second,"TentY",l.tent.position.y,-20000,20000) ||
        !number(camp->second,"TentZ",l.tent.position.z,-5000,5000) || !number(camp->second,"TentHeading",l.tent.heading,-360,720) ||
        !number(camp->second,"HorsesX",l.horses.x,-20000,20000) || !number(camp->second,"HorsesY",l.horses.y,-20000,20000) ||
        !number(camp->second,"HorsesZ",l.horses.z,-5000,5000) || !number(camp->second,"HorsesRadius",l.horses_radius,3,30) ||
        !number(camp->second,"SpawnRadius",l.spawn_radius,60,600) || !number(camp->second,"DespawnRadius",l.despawn_radius,80,900) ||
        !number(camp->second,"ActivityMinSeconds",min_seconds,10,3600) || !number(camp->second,"ActivityMaxSeconds",max_seconds,10,3600))
        return std::nullopt;
    l.tent.heading=Wrap(l.tent.heading);
    l.activity_min_seconds=min_seconds;l.activity_max_seconds=max_seconds;
    if(l.despawn_radius<=l.spawn_radius){error="DespawnRadius must exceed SpawnRadius";return std::nullopt;}
    if(const auto props=sections.find("Props");props!=sections.end()) {
        for(const auto& [key,value]:props->second) {
            const auto f=detail::Split(value);
            Prop p;p.key=key;
            // "key=none" alone removes a default prop, as the template says; the
            // five numbers are not asked of a line that places nothing.
            if(f.size()==1 && (f[0]=="none" || f[0]=="off")){std::erase_if(l.props,[&](const Prop& q){return q.key==key;});continue;}
            // key=model,forward,right,up,yaw[,float][,pitch,roll]: the two angles
            // only behind the flag, so a line reads one way.
            if(f.size()<5 || (f.size()>6 && f.size()!=8) || !detail::ValidModel(f[0]) || !detail::Number(f[1],p.forward) || !detail::Number(f[2],p.right) ||
                !detail::Number(f[3],p.up) || !detail::Number(f[4],p.yaw)){error="bad prop "+key;return std::nullopt;}
            p.model=f[0];
            if(f.size()>=6){bool floating=false;if(!detail::Flag(f[5],floating)){error="bad prop "+key;return std::nullopt;}p.ground=!floating;}
            if(f.size()==8 && (!detail::Number(f[6],p.pitch) || !detail::Number(f[7],p.roll))){error="bad prop "+key;return std::nullopt;}
            if(f[0]=="none" || f[0]=="off"){std::erase_if(l.props,[&](const Prop& q){return q.key==key;});continue;}
            if(auto* existing=FindProp(l,key))*existing=p;else l.props.push_back(p);
        }
    }
    if(const auto mary=sections.find("Mary");mary!=sections.end()) {
        if(const auto it=mary->second.find("Enabled");it!=mary->second.end() && !detail::Flag(it->second,l.companion)){error="bad [Mary] Enabled";return std::nullopt;}
        if(const auto it=mary->second.find("Model");it!=mary->second.end()){if(!detail::ValidModel(it->second)){error="bad [Mary] Model";return std::nullopt;}l.companion_model=it->second;}
    }
    if(const auto acts=sections.find("MaryActivities");acts!=sections.end()) {
        for(const auto& [key,value]:acts->second) {
            if(!detail::ValidModel(key)){error="bad Mary activity name "+key;return std::nullopt;}
            const auto f=detail::Split(value);
            Scene s;s.name=key;
            if(f.size()<7 || f.size()>8 || !detail::ValidModel(f[0]) || !detail::Number(f[2],s.forward) || !detail::Number(f[3],s.right) ||
                !detail::Number(f[4],s.up) || !detail::Number(f[5],s.yaw) || !detail::Flag(f[6],s.sitting)){error="bad Mary activity "+key;return std::nullopt;}
            s.scenario=f[0];s.anchor=f[1];
            if(!s.anchor.empty() && s.anchor!="tent" && !FindProp(l,s.anchor)){error="Mary's activity "+key+" anchors to a prop that is not there: "+s.anchor;return std::nullopt;}
            if(s.anchor=="tent")s.anchor.clear();
            if(f.size()==8){if(!f[7].empty() && !detail::ValidModel(f[7])){error="bad Mary activity "+key;return std::nullopt;}s.anim=f[7];}
            l.companion_scenes.push_back(s);
        }
        if(l.companion_scenes.size()>kMaxActivities){error="at most 24 activities for Mary";return std::nullopt;}
    }
    if(const auto acts=sections.find("Activities");acts!=sections.end()) {
        for(const auto& [key,value]:acts->second) {
            if(!detail::ValidModel(key)){error="bad activity name "+key;return std::nullopt;}
            const auto f=detail::Split(value);
            if(f.size()==1 && (f[0]=="none" || f[0]=="off")){std::erase_if(l.scenes,[&](const Scene& q){return q.name==key;});continue;}
            Scene s;s.name=key;
            if(f.size()<7 || f.size()>8 || !detail::ValidModel(f[0]) || !detail::Number(f[2],s.forward) || !detail::Number(f[3],s.right) ||
                !detail::Number(f[4],s.up) || !detail::Number(f[5],s.yaw) || !detail::Flag(f[6],s.sitting)){error="bad activity "+key;return std::nullopt;}
            s.scenario=f[0];s.anchor=f[1];
            if(!s.anchor.empty() && s.anchor!="tent" && !FindProp(l,s.anchor)){error="activity "+key+" anchors to a prop that is not there: "+s.anchor;return std::nullopt;}
            if(s.anchor=="tent")s.anchor.clear();
            if(f.size()==8){if(!f[7].empty() && !detail::ValidModel(f[7])){error="bad activity "+key;return std::nullopt;}s.anim=f[7];}
            bool replaced=false;
            for(auto& have:l.scenes)if(have.name==key){have=s;replaced=true;break;}
            if(!replaced)l.scenes.push_back(s);
        }
    }
    if(l.scenes.empty()){error="no activity left";return std::nullopt;}
    if(l.scenes.size()>kMaxActivities){error="at most 24 activities";return std::nullopt;}
    for(const auto& s:l.scenes)if(!s.anchor.empty() && !FindProp(l,s.anchor)){error="an activity's anchor prop was removed: "+s.anchor;return std::nullopt;}
    if(const auto horses=sections.find("Horses");horses!=sections.end()) {
        if(const auto it=horses->second.find("Models");it!=horses->second.end()) {
            l.horse_models=detail::Split(it->second);
            std::erase_if(l.horse_models,[](const std::string& m){return m.empty();});
            for(const auto& m:l.horse_models)if(!detail::ValidModel(m)){error="bad horse model: "+m;return std::nullopt;}
            if(l.horse_models.size()>12){error="at most 12 horses";return std::nullopt;}
        }
        if(const auto it=horses->second.find("Scenario");it!=horses->second.end()) {
            if(!detail::ValidModel(it->second)){error="bad horse scenario: "+it->second;return std::nullopt;}
            l.horse_scenario=it->second;
        }
        l.horse_spots.clear();
        for(int n=1;n<=12;++n) {
            const auto it=horses->second.find("Spot"+std::to_string(n));
            if(it==horses->second.end())break;
            const auto f=detail::Split(it->second);
            Pose p;
            if(f.size()!=4 || !detail::Number(f[0],p.position.x) || !detail::Number(f[1],p.position.y) ||
                !detail::Number(f[2],p.position.z) || !detail::Number(f[3],p.heading)){error="bad horse Spot"+std::to_string(n);return std::nullopt;}
            p.heading=Wrap(p.heading);
            l.horse_spots.push_back(p);
        }
    }
    if(const auto ipl=sections.find("Ipl");ipl!=sections.end()) {
        if(const auto it=ipl->second.find("Request");it!=ipl->second.end()) {
            for(const auto& token:detail::Split(it->second)) {
                if(token.empty())continue;
                int hash=0;if(!detail::HashValue(token,hash)){error="bad IPL hash: "+token;return std::nullopt;}
                if(std::find(l.ipls.begin(),l.ipls.end(),hash)==l.ipls.end())l.ipls.push_back(hash);
            }
            if(l.ipls.size()>16){error="at most 16 IPLs";return std::nullopt;}
        }
    }
    if(const auto hide=sections.find("Hide");hide!=sections.end()) {
        for(const auto& [key,value]:hide->second) {
            const auto f=detail::Split(value);
            if(f.empty() || (f.size()==1 && (f[0]=="none" || f[0]=="off"))){std::erase_if(l.hides,[&](const Hide& q){return q.key==key;});continue;}
            Hide h;h.key=key;
            if(f.size()<4 || f.size()>5 || !detail::ValidModel(f[0]) || !detail::Number(f[1],h.position.x) || !detail::Number(f[2],h.position.y) ||
                !detail::Number(f[3],h.position.z) || (f.size()==5 && (!detail::Number(f[4],h.radius) || h.radius<=0 || h.radius>50))){error="bad hide "+key;return std::nullopt;}
            h.model=f[0];
            if(const auto it=std::find_if(l.hides.begin(),l.hides.end(),[&](const Hide& q){return q.key==key;});it!=l.hides.end())*it=h;else l.hides.push_back(h);
        }
        if(l.hides.size()>2048){error="at most 2048 hides";return std::nullopt;}
    }
    if(const auto out=sections.find("HideOutside");out!=sections.end()) {
        for(const auto& [key,value]:out->second) {
            const auto f=detail::Split(value);
            if(f.size()==1 && (f[0]=="none" || f[0]=="off")){if(key=="Camp")l.outside=Outside{};continue;}
            Point p;
            if(f.size()!=3 || !detail::Number(f[0],p.x) || !detail::Number(f[1],p.y) || !detail::Number(f[2],p.z) || p.z<=0)
                {error="bad "+key+" in [HideOutside]";return std::nullopt;}
            if(key=="Camp"){l.outside.camp={p.x,p.y,0};l.outside.radius=p.z;continue;}
            if(key.rfind("Circle",0)!=0){error="unknown key in [HideOutside]: "+key;return std::nullopt;}
            l.outside.circles.push_back(p);
        }
        if(l.outside.circles.size()>16){error="at most 16 circles";return std::nullopt;}
    }
    if(const auto inter=sections.find("Interactions");inter!=sections.end()) {
        for(const auto& [key,value]:inter->second) {
            std::string* slot=key=="Locker"?&l.interactions.locker:nullptr;
            if(!slot){error="unknown interaction "+key;return std::nullopt;}
            const auto f=detail::Split(value);
            if(f.size()>1){error="bad interaction "+key;return std::nullopt;}
            *slot=(f.empty() || f[0]=="none" || f[0]=="off")?std::string():f[0];
            if(!slot->empty() && !FindProp(l,*slot)){error="interaction "+key+" names no prop: "+*slot;return std::nullopt;}
        }
    }
    return l;
}
} // namespace horse_races::arthur_camp
