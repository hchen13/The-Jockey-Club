#pragma once
#include "game_natives.h"
#include "runtime_paths.h"
#include "kernel/files.h"
#include <main.h>
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <sstream>
#include <vector>

// The route recorder behind F11 > Race debug > Route recording: the player's
// ride written as a course capture, one point every metre of travel whatever
// the speed - the earlier capture sampled ten times a second, so a stretch
// ridden fast had a node every two metres and a bend taken at a gallop came
// out as a corner (2026-09-18). The file is the same shape the external
// recorder wrote (tools/record_player_course.py: a header, "point" rows with
// t_seconds and xyz, a "summary" footer), so tools/build_arthur_duel.py and
// the splice tool read it unchanged. It lands in <game>\TheJockeyClub\courses.
//
// While it runs the world is kept clear: no ambient spawns of any kind (the
// density multipliers held at zero, per-frame settings that end with the
// recording), and every second every ped, animal and vehicle within
// kCullMetres that no script owns is deleted - the ride through the swamp
// was thrown by an alligator on 2026-09-17 and the capture had to be cut and
// bridged; a wagon or a lawman in the road bends the line as surely. The
// player, their horses and anything a script holds are left alone. A jump
// of more than kJumpMetres between frames is a teleport, and ends the
// recording with its footer saying so rather than a line across the map.
namespace horse_races::route_recorder {
inline constexpr float kStepMetres=1.0F;
inline constexpr float kJumpMetres=60.0F;
inline constexpr float kCullMetres=300.0F;
inline constexpr double kCullEverySeconds=1.0;
inline constexpr std::uint64_t kCaptureBytes=16ull*1024*1024,kCaptureTotalBytes=256ull*1024*1024;
inline constexpr unsigned kCaptureFiles=128;
using Logger=std::function<void(const std::string&)>;
struct Recorder {
    bool active=false;
    std::string path;
    std::ofstream file;
    int samples=0,culled=0,vehicles_culled=0;
    double started=0,next_cull=0;
    float travel=0;
    std::uint64_t bytes=0;
    Vector3 first{},last{};

    static std::string Stamp() {
        SYSTEMTIME t{};GetLocalTime(&t);char b[40];
        std::snprintf(b,sizeof b,"%04u%02u%02u-%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
        return b;
    }
    static std::string Xyz(const Vector3& p) {
        char b[96];std::snprintf(b,sizeof b,"[%.6f, %.6f, %.6f]",static_cast<double>(p.x),static_cast<double>(p.y),static_cast<double>(p.z));
        return b;
    }
    bool Start(const std::string& directory,double now,const Vector3& at,const Logger& log) {
        if(active)return true;
        const auto courses=runtime_paths::Resolve(directory).Courses();
        if(!files::CanCreateDirectory(courses)){if(log)log("route recording refused: unsafe output directory");return false;}
        runtime_paths::EnsureDirectory(courses);
        if(!files::AdmitGenerated(courses,[](const std::string& name){return name.starts_with("route-")&&name.ends_with(".jsonl");},
            1,kCaptureBytes,kCaptureFiles,kCaptureTotalBytes)) {
            if(log)log("route recording storage is full or unsafe; existing course captures preserved");return false;
        }
        const std::string dir=courses.string();
        path=dir+"\\route-"+Stamp()+"-"+std::to_string(GetTickCount64())+".jsonl";
        std::ostringstream header;
        header<<"{\"type\": \"header\", \"schema\": \"HorseSpeedLab.player-course.v1\", \"source\": \"in-game F11 route recorder; one point per metre of travel\", "
            "\"intended_start\": "<<Xyz(at)<<", \"rate_hz\": 0, \"step_metres\": "<<kStepMetres<<"}\n";
        if(!files::WriteNew(path,header.str())){if(log)log("route recording could not create "+path);path.clear();return false;}
        file.clear();file.open(path,std::ios::out|std::ios::app|std::ios::binary);
        if(!file){if(log)log("route recording could not open "+path);path.clear();return false;}
        bytes=header.str().size();
        active=true;samples=0;culled=0;vehicles_culled=0;travel=0;started=now;next_cull=now;first=last=at;
        if(!Emit(now,at,0)){Stop(now,"write_failure",log);return false;}
        if(log)log("route recording started: "+path);
        return true;
    }
    bool Emit(double now,const Vector3& at,float speed) {
        char b[220];
        std::snprintf(b,sizeof b,"{\"type\": \"point\", \"sample\": %d, \"t_seconds\": %.3f, \"xyz\": ",samples,now-started);
        std::ostringstream row;row<<b<<Xyz(at)<<", \"speed_mps\": "<<speed<<"}\n";
        const auto text=row.str();
        if(bytes>kCaptureBytes||text.size()+2048>kCaptureBytes-bytes)return false;
        file<<text;bytes+=text.size();
        ++samples;last=at;
        if(samples%50==0)file.flush();
        return file.good();
    }
    void Stop(double now,const char* reason,const Logger& log) {
        if(!active)return;
        char b[160];std::snprintf(b,sizeof b,"{\"type\": \"summary\", \"stop_reason\": \"%s\", \"samples\": %d, \"observed_horizontal_m\": %.1f, ",reason,samples,static_cast<double>(travel));
        std::ostringstream footer;footer<<b<<"\"first_sample\": "<<Xyz(first)<<", \"last_sample\": "<<Xyz(last)<<", \"seconds\": "<<(now-started)
            <<", \"peds_culled\": "<<culled<<", \"vehicles_culled\": "<<vehicles_culled<<"}\n";
        const auto text=footer.str();if(bytes<=kCaptureBytes&&text.size()<=kCaptureBytes-bytes){file<<text;bytes+=text.size();}
        file.close();active=false;
        if(log)log("route recording stopped ("+std::string(reason)+"): "+std::to_string(samples)+" points, "+
            std::to_string(static_cast<int>(travel))+" m, "+std::to_string(culled)+" peds and "+std::to_string(vehicles_culled)+" vehicles culled: "+path);
    }
    // Every frame while recording: the point rule, no spawns, the cull.
    void Tick(double now,const Vector3& at,float speed,Ped player,const Logger& log) {
        if(!active)return;
        if(now-started>=3600){Stop(now,"time_limit",log);return;}
        PED::_SET_AMBIENT_PED_DENSITY_MULTIPLIER_THIS_FRAME(0.0F);
        PED::_SET_AMBIENT_HUMAN_DENSITY_MULTIPLIER_THIS_FRAME(0.0F);
        PED::SET_SCENARIO_PED_DENSITY_MULTIPLIER_THIS_FRAME(0.0F);
        PED::_SET_AMBIENT_ANIMAL_DENSITY_MULTIPLIER_THIS_FRAME(0.0F);
        PED::_SET_SCENARIO_ANIMAL_DENSITY_MULTIPLIER_THIS_FRAME(0.0F);
        VEHICLE::SET_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME(0.0F);
        VEHICLE::SET_RANDOM_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME(0.0F);
        VEHICLE::SET_PARKED_VEHICLE_DENSITY_MULTIPLIER_THIS_FRAME(0.0F);
        const float step=std::hypot(at.x-last.x,at.y-last.y);
        if(!std::isfinite(step)){Stop(now,"capture_invalid: position",log);return;}
        if(step>kJumpMetres){Stop(now,"discontinuity",log);return;}
        if(step>=kStepMetres){travel+=step;if(!Emit(now,at,speed)){Stop(now,"storage_limit_or_write_failure",log);return;}}
        if(now>=next_cull){next_cull=now+kCullEverySeconds;Cull(at,player,log);}
    }
    // The player's: themselves, the horse under them, the saddle horse and
    // the temporary one, and whatever they are riding in.
    static bool Theirs(Entity e,Ped player) {
        const auto id=PLAYER::PLAYER_ID();
        return e==player || e==PED::GET_MOUNT(player) || e==PLAYER::_GET_SADDLE_HORSE_FOR_PLAYER(id) ||
            e==PLAYER::_GET_TEMP_PLAYER_HORSE(id) || e==PED::_GET_LAST_MOUNT(player) || e==PED::GET_VEHICLE_PED_IS_IN(player,false);
    }
    void Cull(const Vector3& at,Ped player,const Logger& log) {
        std::vector<int> handles(2048,0);
        int removed=0,vehicles=0;
        const auto within=[&](Entity e){const Vector3 p=ENTITY::GET_ENTITY_COORDS(e,true,false);return std::hypot(p.x-at.x,p.y-at.y)<=kCullMetres;};   // `near` is a Windows macro.
        // Vehicles first, riders and passengers with them.
        int count=std::min(worldGetAllVehicles(handles.data(),static_cast<int>(handles.size())),static_cast<int>(handles.size()));
        for(int i=0;i<count;++i) {
            const Vehicle v=handles[i];
            if(!v || !ENTITY::DOES_ENTITY_EXIST(v) || ENTITY::IS_ENTITY_A_MISSION_ENTITY(v) || Theirs(v,player) || !within(v))continue;
            if(VEHICLE::IS_THIS_MODEL_A_TRAIN(ENTITY::GET_ENTITY_MODEL(v)))continue;   // Trains are the track's; a carriage alone keeps rolling.
            bool occupied_by_player=false;
            const int seats=VEHICLE::GET_VEHICLE_MODEL_NUMBER_OF_SEATS(ENTITY::GET_ENTITY_MODEL(v));
            for(int seat=-1;seat<seats && !occupied_by_player;++seat){const Ped o=VEHICLE::GET_PED_IN_VEHICLE_SEAT(v,seat);if(o && (o==player || PED::IS_PED_A_PLAYER(o)))occupied_by_player=true;}
            if(occupied_by_player)continue;
            for(int seat=-1;seat<seats;++seat) {
                const Ped o=VEHICLE::GET_PED_IN_VEHICLE_SEAT(v,seat);
                if(!o || !ENTITY::DOES_ENTITY_EXIST(o) || ENTITY::IS_ENTITY_A_MISSION_ENTITY(o))continue;
                ENTITY::SET_ENTITY_AS_MISSION_ENTITY(o,true,true);Ped h=o;PED::DELETE_PED(&h);
                if(!ENTITY::DOES_ENTITY_EXIST(o))++removed;
            }
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(v,true,true);Vehicle h=v;VEHICLE::DELETE_VEHICLE(&h);
            if(!ENTITY::DOES_ENTITY_EXIST(v))++vehicles;
        }
        count=std::min(worldGetAllPeds(handles.data(),static_cast<int>(handles.size())),static_cast<int>(handles.size()));
        for(int i=0;i<count;++i) {
            const Ped ped=handles[i];
            if(!ped || !ENTITY::DOES_ENTITY_EXIST(ped) || PED::IS_PED_A_PLAYER(ped) || Theirs(ped,player))continue;
            if(ENTITY::IS_ENTITY_A_MISSION_ENTITY(ped) || !within(ped))continue;   // Another script's is left to it.
            ENTITY::SET_ENTITY_AS_MISSION_ENTITY(ped,true,true);
            Ped handle=ped;PED::DELETE_PED(&handle);
            if(!ENTITY::DOES_ENTITY_EXIST(ped))++removed;
        }
        if(removed || vehicles) {
            culled+=removed;vehicles_culled+=vehicles;
            if(log)log("route recording: "+std::to_string(removed)+" peds and "+std::to_string(vehicles)+" vehicles within "+
                std::to_string(static_cast<int>(kCullMetres))+" m deleted");
        }
    }
};
} // namespace horse_races::route_recorder
