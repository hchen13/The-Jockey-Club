#pragma once
#include "race_course.h"
#include "game_natives.h"
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace horse_races::course {
// beat_voice.c func_195: a script array header followed by struct<8> nodes.
// Only xyz are initialised by the original script; the remaining five slots
// are zero. This is not a packed C++ Vector3[] (the SDK vector uses 8-byte slots).
// How many points the engine's own waypoint recording holds. A path longer than this cannot be
// registered at all: on 2026-09-23 a 1168-node course was refused here and the race was cancelled at
// the flag, after the countdown and the music had already started. Courses are thinned to fit.
inline constexpr std::size_t kNativeWaypointNodes=1024;
inline bool UseSadieRemainingGps(std::string_view station,bool hosted,bool lap_engine,bool recorded) {
    return station=="adler-strawberry" && hosted && !lap_engine && recorded;
}
struct NativeGps {
    std::string name;
    std::vector<std::uint64_t> storage;
    bool owned=false, ready=false, drawing=false;
    std::uint32_t last_health_check=0;
    unsigned recoveries=0;
    std::size_t remaining_segment=0;
    float remaining_along=0;
    bool remaining_sampled=false;
    Vector3 remaining_previous{};
    unsigned repairs=0;   // Times the recording was made again because it no longer answered for its path.
    void Release() {
        if(owned)TASK::REMOVE_WAYPOINT_RECORDING(name.c_str());
        remaining_segment=0;remaining_along=0;remaining_sampled=false;
        owned=ready=drawing=false;last_health_check=0;recoveries=0;name.clear();storage.clear();
    }
    bool Register(const Path& path,const std::string& unique_name) {
        Release();name=unique_name;
        const auto count=path.points.size();if(count<2 || count>kNativeWaypointNodes)return false;
        // Never replace another script/mod's resource, even on an unlikely name collision.
        int existing=0;if(TASK::WAYPOINT_RECORDING_GET_NUM_POINTS(name.c_str(),&existing))return false;
        storage.assign(1+8*count,0);storage[0]=count;
        for(std::size_t i=0;i<count;++i) {
            storage[1+i*8]=std::bit_cast<std::uint32_t>(path.points[i].x);
            storage[2+i*8]=std::bit_cast<std::uint32_t>(path.points[i].y);
            storage[3+i*8]=std::bit_cast<std::uint32_t>(path.points[i].z);
        }
        owned=TASK::_CREATE_WAYPOINT_PATH(name.c_str(),reinterpret_cast<Any*>(storage.data()),static_cast<int>(count),0);
        if(!owned)return false;
        int actual=0;
        if(!TASK::WAYPOINT_RECORDING_GET_NUM_POINTS(name.c_str(),&actual) || actual!=static_cast<int>(count)) {
            Release();return false;
        }
        // Verify every node before handing the resource to the GPS consumer.
        // Creation success alone is not evidence of recording compatibility.
        for(std::size_t i=0;i<count;++i) {
            Vector3 p{};
            if(!TASK::WAYPOINT_RECORDING_GET_COORD(name.c_str(),static_cast<int>(i),&p) ||
                !destination::Finite(p) || destination::Distance(p,path.points[i])>.05F ||
                std::abs(p.z-path.points[i].z)>.05F) {Release();return false;}
        }
        ready=true;return true;
    }
    // The recording read back against the path it was made from: the count,
    // and eight nodes spread along it.
    bool Matches(const Path& path) const {
        if(!ready || path.points.size()<2)return false;
        int n=0;if(!TASK::WAYPOINT_RECORDING_GET_NUM_POINTS(name.c_str(),&n) || n!=static_cast<int>(path.points.size()))return false;
        const std::size_t count=path.points.size();
        for(std::size_t k=0;k<8;++k) {
            const std::size_t i=count<=8?k:(k*(count-1))/7;
            if(i>=count)break;
            Vector3 p{};
            if(!TASK::WAYPOINT_RECORDING_GET_COORD(name.c_str(),static_cast<int>(i),&p) || !destination::Finite(p) ||
                destination::Distance(p,path.points[i])>.05F || std::abs(p.z-path.points[i].z)>.05F)return false;
        }
        return true;
    }
    // Before every upload the recording is read back: the yellow guide has
    // been seen to follow the host's computed line once the survey grew and
    // the followers' chunks were made again (2026-09-18) - a recording made
    // later, under another name, answering for this one. One that no longer
    // says what it was given is left where it is (removing it by name might
    // remove whatever answers to the name now) and made again, fresh name.
    bool Repair(const Path& path) {
        if(Matches(path))return true;
        const std::string fresh=name+"_r"+std::to_string(++repairs);
        owned=false;   // Not removed: the name may answer for another recording.
        return Register(path,fresh);
    }
    bool Draw(const Path& path,const Progress& player,Hash color,bool force=false,bool recorded_only=false,bool remaining_only=false) {
        if(!ready || path.points.size()<2)return false;
        // The engine can drop this script GPS channel mid-race in either mode
        // (the circuit channel has been seen restored from its recording), so
        // the route-found probe runs for both. A restart begins at the player's
        // live segment below, so an A->B line resumes where the rider is now.
        const auto now=static_cast<std::uint32_t>(MISC::GET_GAME_TIMER());
        // The Sadie A-to-B display consumes only the surveyed prefix. A distant
        // projection, a teleport, or riding off the recorded branch must not
        // promote the cursor and remove a fork the rider still needs to see.
        float accepted_remaining=remaining_along;
        std::size_t start_remaining=remaining_segment;
        if(remaining_only && player.ready) {
            const float step=remaining_sampled?destination::Distance(remaining_previous,player.previous):0;
            const float max_advance=remaining_sampled?std::min(30.0F,2*step+8.0F):35.0F;
            remaining_previous=player.previous;remaining_sampled=true;
            if(!player.continuity_lost && player.current.valid && player.current.distance<=10.0F &&
                player.current.along<=player.furthest && player.current.along>remaining_along &&
                player.current.along-remaining_along<=max_advance) {
                accepted_remaining=player.current.along;
                start_remaining=path.Segment(accepted_remaining);
                if(start_remaining!=remaining_segment)force=true;
            }
        }
        if(drawing && !force && now-last_health_check>=500) {
            last_health_check=now;
            // Verified 1491 getter reads this same script GPS channel (index 1).
            // Restore a missing route, not on every node or ordinary movement.
            if(!PATHFIND::GET_GPS_BLIP_ROUTE_FOUND()){force=true;++recoveries;}
        }
        if(drawing && !force) {
            // The vanilla custom renderer consumes the visible prefix as the
            // rider proceeds. That is suitable for A->B, but a circuit must
            // still show its recorded line when the player reverses to recover
            // a missed gate: re-upload the unchanged input polyline. Live 1491
            // custom-render copies input points -> display points and selects
            // mode 8. It does NOT call the clear/reset used by START, or
            // calculate a road route. An A->B line is not re-uploaded: that
            // would restore its consumed tail every frame, and the vanilla
            // A->B race never re-uploads either (beat_horse_race.c:3370-3371).
            if(recorded_only || remaining_only)MAP::SET_GPS_CUSTOM_ROUTE_RENDER(true,3,30);
            if(remaining_only)remaining_along=accepted_remaining;
            return true;
        }
        if(!Repair(path))return false;
        const float along=player.current.valid?player.current.along:player.furthest;
        const auto next=std::min(path.points.size()-1,path.Segment(along)+1);
        if(remaining_only) {
            remaining_along=accepted_remaining;remaining_segment=start_remaining;
            remaining_previous=player.previous;remaining_sampled=player.ready;
        }
        MAP::_START_GPS_CUSTOM_ROUTE_FROM_WAYPOINT_RECORDING_ROUTE(name.c_str(),
            recorded_only?0:static_cast<int>(remaining_only?remaining_segment:next-1),
            static_cast<int>(path.points.size()),color,false,true);
        // Exact GRY1 arguments: full point count, including a nonzero start.
        // Circuits must not layer the road-network router over the recorded
        // line: it can offer a shortcut back to the nearby lap destination.
        // Always retain the whole current lap, including its already traversed
        // nodes. Qualification/direction remains exclusively in the lap engine.
        MAP::SET_GPS_MULTI_ROUTE_RENDER(!recorded_only && !remaining_only);
        MAP::SET_GPS_CUSTOM_ROUTE_RENDER(true,3,30);
        drawing=true;last_health_check=now;return true;
    }
};
// A contiguous slice of the same ordered line the AI follows and the lap engine
// qualifies. Interior nodes stay exact; only window cuts between nodes may be
// interpolated. The real start and finish must never be trimmed as window cuts.
inline Path Slice(const Path& path,float low,float high,std::size_t& first,std::size_t& last) {
    Path out;
    if(path.points.size()<2)return out;
    const float length=path.Length();
    low=std::clamp(low,0.0F,length);high=std::clamp(std::max(high,low),0.0F,length);
    first=path.Segment(low);
    last=std::min(path.points.size()-1,path.Segment(high)+1);
    if(last<=first)last=std::min(path.points.size()-1,first+1);
    if(last<=first)return out;
    for(std::size_t i=first;i<=last;++i)out.points.push_back(path.points[i]);
    // Cut the window at the requested metres rather than at the nearest
    // recorded node, so the tail behind the rider is the tail that was asked
    // for and not that plus a node spacing. A cut that would land within a
    // metre of the neighbouring node keeps the node instead: the recording
    // native refuses points closer than 5 cm.
    const auto at=[&](float metres){
        const auto i=path.Segment(metres);
        const float span=path.metres[i+1]-path.metres[i];
        const float t=span>0?(metres-path.metres[i])/span:0;
        const auto a=path.points[i],b=path.points[i+1];
        return Vector3{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
    };
    if(low>0 && out.points.size()>=3) {
        if(path.metres[first+1]-low<=1)out.points.erase(out.points.begin());       // the cut IS the next node
        else if(low-path.metres[first]>1)out.points.front()=at(low);
    }
    if(high<length && out.points.size()>=3) {
        if(high-path.metres[last-1]<=1)out.points.pop_back();                      // the cut IS the previous node
        else if(path.metres[last]-high>1)out.points.back()=at(high);
    }
    out.metres.assign(out.points.size(),0);
    for(std::size_t i=1;i<out.points.size();++i)
        out.metres[i]=out.metres[i-1]+destination::Distance(out.points[i-1],out.points[i]);
    return out;
}
// Rolling minimap guidance. A recorded lap that crosses itself cannot be drawn
// whole: every junction then shows both the pass being ridden and a later one,
// and the yellow line stops naming the next turn. Draw a bounded section around
// qualified progress instead, the way the vanilla renderer consumes an A->B
// route. The window never reveals a section the rider has not qualified for.
struct RollingGps {
    NativeGps channel;
    Path section; // The registered slice, kept so an unchanged frame re-renders it.
    std::size_t first=0,last=0;
    float built_along=0;
    bool built=false;
    // Rebuilding registers a fresh recording, so it is paced by distance
    // instead of running once per frame.
    // How far the rider travels between window rebuilds. The tail behind the
    // rider grows by this much before it is trimmed, so it stays close to the
    // route's GuideBehindMetres rather than that plus twenty-five. Each rebuild
    // registers a ~20-node recording, which is cheap.
    static constexpr float kRebuildMetres=8;
    void Release(){channel.Release();section={};first=last=0;built_along=0;built=false;}
    bool Draw(const Path& path,float along,Hash colour,float ahead,float behind,bool force,
              const std::string& base,std::uint64_t& serial) {
        if(path.points.size()<2 || !std::isfinite(along) || !std::isfinite(ahead) || ahead<=0 ||
            !std::isfinite(behind) || behind<0)return false;
        along=std::clamp(along,0.0F,path.Length());
        // Rebuilt by distance travelled, not by which recorded node the window
        // starts at: with the window cut at exact metres, node changes are not
        // what makes it stale.
        const bool paced=!built || std::abs(along-built_along)>=kRebuildMetres;
        if(force || paced) {
            // The geometry is immutable between paced rebuilds. Ordinary
            // frames only re-render the registered section, with no path copy.
            std::size_t want_first=0,want_last=0;
            const auto window=Slice(path,along-behind,along+ahead,want_first,want_last);
            if(window.points.size()<2)return false;
            channel.Release();
            if(!channel.Register(window,base+"_"+std::to_string(++serial))){built=false;return false;}
            section=window;first=want_first;last=want_last;built_along=along;built=true;
            return channel.Draw(section,{},colour,true,true);
        }
        return built && channel.Draw(section,{},colour,false,true);
    }
};
} // namespace horse_races::course
