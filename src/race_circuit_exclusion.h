#pragma once
#include "race_course.h"
#include "game_natives.h"
#include <main.h>
#include <map>
#include <set>
#include <functional>
#include <array>
#include <chrono>

namespace horse_races::circuit_exclusion {
// Permission to occupy the course is independent of ownership/persistence.
// A course occupant that is not on the race roster is temporarily parked off the
// track and put back afterwards. Two kinds are claimed as a mission entity and
// deleted instead, because parking them does not work: one that spawned on the
// course while the race was running, and one that would not stay parked. Never a
// racer, never the player's own animals, never an entity another script holds as
// a mission entity (see Deletable). Native story/PersChar data is untouched.
inline constexpr float kLaneHalfWidth=12.0F,kBufferHalfWidth=30.0F;
inline constexpr float kTrackHalfWidth=kLaneHalfWidth+2;
inline constexpr bool IsNativeWolf(Hash model) {
    return model==0xBBD91DDA || model==0xCB391381 || model==0xCE924A27;
}
struct Identity {
    Entity entity=0;Hash model=0;std::uintptr_t address=0;
    bool Same() const {return entity && ENTITY::DOES_ENTITY_EXIST(entity) &&
        ENTITY::GET_ENTITY_MODEL(entity)==model &&
        reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(entity))==address;}
    static Identity Read(Entity e){return {e,ENTITY::GET_ENTITY_MODEL(e),
        reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(e))};}
};
struct Saved {
    Identity id;
    Vector3 home{},parking{},last_position{};
    float heading=0;
    bool visible=false,collision_disabled=false,frozen=false;
    bool parked=false,home_returned=false,handed_back=false,player_owned=false;
    bool handover_reported=false;
    bool neutralised=false;   // Declined: hidden, frozen and collision-free where it stands.
};
struct Group {
    std::vector<Saved> members;
    bool restoring=false,positions_restored=false;
    // The engine refused to move this group. It is left as it is, never retried,
    // and never allowed to end the race - see Decline().
    bool declined=false;
};
class Exclusion {
    std::vector<Identity> racers_;
    // The native saddle/temp handles can switch when the player mounts a loaner.
    // Keep identity-checked recent mounts so the horse they parked beside the
    // course remains theirs for the whole race, even after that switch.
    std::vector<Identity> player_mounts_;
    std::vector<course::Path> paths_;
    // Distance-to-course is consulted for every bystander and again when a
    // group is checked after parking. A long recorded course can contain
    // thousands of segments; rejecting whole spatial blocks before doing the
    // exact projection keeps that work proportional to nearby track, without
    // weakening the single-snapshot wagon/passenger isolation transaction.
    struct RouteBlock {
        std::size_t path=0,first=0,last=0; // [first,last) segments
        Vector3 low{},high{};
    };
    static constexpr std::size_t kSegmentsPerBlock=32;
    std::vector<RouteBlock> route_blocks_;
    std::vector<Group> parked_;
    Vector3 low_{},high_{};
    Volume query_volume_=0;
    // While the course is prepared the entity query covers the whole course,
    // as it always has: the track is cleared end to end before the flag drops.
    // Once the race runs it covers a window round the live racers instead.
    // The one-on-one's 11 km route has a bounding box that takes in half the
    // map, and an every-frame enumeration of it handed a script GUID to every
    // ped and wagon inside until the game's pool ran dry: three asserts in
    // nine days, in CREATE_ITEMSET, _GET_ENTITIES_IN_VOLUME and
    // _ADD_BOX_VOLUME_TO_VOLUME_AGGREGATE (2026-09-11/14/19), each ending the
    // race with a ScriptHook error. The window moves and grows with the field;
    // what is outside it is not on the track near anyone, and it is dealt with
    // when the field comes within reach. It is the WHOLE enumeration: nothing
    // is merged in from a whole-world pool afterwards, or the window would be
    // a window in name only and every handle in the map would cost a GUID
    // again - which is the assert above (R-CLEAR-08).
    Vector3 window_low_{},window_high_{};
    static constexpr float kWindowMargin=400;   // metres beyond the racers' bounding box, x and y
    static constexpr float kWindowHeight=120;   // metres above and below
    // The windowed query is the only enumerator, so the only thing that can
    // tell a clear course from a query that never ran is the query's own
    // positive control: the race's own actors are known to be inside the
    // window, so a query that cannot find them did not cover the scene. It
    // lives beside the count it vouches for, and both are read once, by the
    // five-second audit line.
    struct WindowQuery {
        int found=0;                            // peds and vehicles the window returned
        int roster_seen=0,roster_expected=0;    // the positive control
        bool Covered() const {return roster_seen==roster_expected;}
    } window_;
    bool active_=false;
    // Trains are never isolated: a carriage frozen and hidden keeps rolling in
    // the train's own hands, and the field rides into a train it cannot see
    // (2026-09-17: a carriage appeared on the crossing at the moment of the
    // collision, not the locomotive, not from afar). Random trains are off for
    // the race, every train is deleted when it begins, and again whenever one
    // is found on the course.
    bool trains_off_=false;
    int last_train_purge_=0;
    bool allow_wolves_=false;
    int declined_groups_=0,residual_reports_=0,removed_groups_=0;
    // Everything seen anywhere inside the course buffer on an earlier tick,
    // by identity so a recycled handle does not inherit a stranger's history.
    // The rule for a policed course tells a SPAWN on the track (removed at
    // once) apart from an APPROACH from outside (turned away first, removed
    // only if that fails); this is the only way the mod can tell the two apart.
    std::map<Entity,Identity> seen_;
    double next_log_=0;
    std::size_t isolated_total_=0;
    std::size_t flag_repairs_=0;
    double audit_max_ms_=0,audit_sum_ms_=0;
    std::size_t audit_frames_=0;
    std::function<void(const std::string&)> log_;
#include "race_circuit_exclusion_scan.inl"
    // Declining is only ever right once the race is RUNNING. Before the flag
    // drops nothing has been invested, and a failure there simply means the
    // attempt is retried or abandoned cleanly - so preparation keeps the hard
    // error it always had.
    bool may_decline_=false;
    double next_sweep_=0;     // When the course may be swept again; see kSweepSeconds.
    // The maintainer's rule for a policed course: a nonparticipant that is ON it
    // is removed, and one that would not leave it is removed. Never a racer,
    // never the player's own animals, never an entity another script holds as a
    // mission entity - those are parked as before, and if even that fails they
    // are hidden, frozen and stripped of collision where they stand.
#include "race_circuit_exclusion_parking.inl"
    bool BlockingHandover(const std::vector<course::Path>& paths) const {
        for(const auto& group:parked_) {
            if(!std::any_of(group.members.begin(),group.members.end(),[](const Saved& s){return s.handed_back;}))continue;
            for(const auto& s:group.members)if(s.id.Same()) {
                const auto p=ENTITY::GET_ENTITY_COORDS(s.id.entity,true,false);
                if(!destination::Finite(p))return true;
                // Include the predictive buffer, not just the centreline.
                // Distant, off-course collision/LOD read-back cannot veto a
                // clean new course. On-course handovers remain fail-closed.
                for(const auto& path:paths) {
                    const auto projection=course::Project(path,p);
                    if(!projection.valid || projection.distance<=kBufferHalfWidth+10)return true;
                }
            }
        }
        return false;
    }
    void RetryAcknowledgedHandovers() {
        for(auto it=parked_.begin();it!=parked_.end();) {
            std::erase_if(it->members,[&](Saved& s){return !s.id.Same() ||
                (s.handed_back && HandBack(s,it->restoring));});
            if(it->members.empty())it=parked_.erase(it);else ++it;
        }
    }
public:
    std::string error;
    void RememberPlayerMount(Ped mount) {
        if(!Exists(mount))return;
        std::erase_if(player_mounts_,[](const Identity& id){return !id.Same();});
        if(std::any_of(player_mounts_.begin(),player_mounts_.end(),[&](const Identity& id){return id.entity==mount;}))return;
        if(player_mounts_.size()==16)player_mounts_.erase(player_mounts_.begin());
        player_mounts_.push_back(Identity::Read(mount));
    }
    bool WildlifeException(Entity e) const {
        return allow_wolves_ && ENTITY::GET_ENTITY_TYPE(e)==1 && IsNativeWolf(ENTITY::GET_ENTITY_MODEL(e));
    }
    float Distance(Vector3 p) const {
        if(!destination::Finite(p))return INFINITY;
        float best=INFINITY;
        for(const auto& block:route_blocks_) {
            if(BlockLowerBound(block,p)>best)continue;
            const auto& points=paths_[block.path].points;
            for(std::size_t i=block.first;i<block.last;++i) {
                const auto& a=points[i];const auto& b=points[i+1];
                const float dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;
                const float t=std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/length2,0.0F,1.0F);
                const float horizontal=std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);
                const float height=std::max(0.0F,std::abs(p.z-a.z-t*(b.z-a.z))-6.0F);
                best=std::min(best,std::hypot(horizontal,height));
            }
        }
        return best;
    }
    bool Racer(Entity e) const {
        return std::any_of(racers_.begin(),racers_.end(),[&](const Identity& id){return id.entity==e && id.Same();});
    }
    // Verified hidden, frozen and collision-free. It occupies space on the map
    // but nothing in the race can hit it, so it is not a residual obstruction.
    bool Neutralised(Entity e) const {
        for(const auto& g:parked_)if(g.declined)for(const auto& s:g.members)
            if(s.id.entity==e && s.id.Same())return s.neutralised;
        return false;
    }
    bool Held(Entity e) const {
        for(const auto& g:parked_)for(const auto& s:g.members)if(s.id.entity==e && s.id.Same())return true;
        return false;
    }
    std::size_t Count() const {std::size_t n=0;for(const auto& g:parked_)n+=g.members.size();return n;}
    // A refusal here cancels a race the player has already accepted, so it says which of its own
    // conditions was not met (the user, 2026-09-22: a cancelled race whose log said only "failed").
    bool AddParticipants(const std::vector<Ped>& actors) {
        if(!active_){error="the course is not being policed yet";return false;}
        if(!CAM::IS_SCREEN_FADED_OUT()){error="the screen is not blacked out";return false;}
        if(racers_.size()+actors.size()>128){error="more racers than the roster can hold";return false;}
        for(Ped p:actors) {
            if(!Exists(p)){error="a racer stopped existing before it could be registered";return false;}
            // Parked means the sweep took it for a bystander: it was on the course, unregistered, when
            // the sweep last looked. Registering one frame after creating it is all it takes.
            if(Held(p)){error="a racer had already been parked as a bystander";return false;}
            if(!Identity::Read(p).Same()){error="a racer's handle was handed to something else";return false;}
        }
        for(Ped p:actors)if(!Racer(p))racers_.push_back(Identity::Read(p));
        return true;
    }
    bool Begin(std::vector<course::Path> paths,const std::vector<Ped>& racers,std::function<void(const std::string&)> log,
        bool allow_wolves=false) {
        if(active_)return false;
        RestorePending(false,false); // Relinquish native/player takeovers before adopting a new roster.
        if(BlockingHandover(paths)) {
            error="native/player handover still pending";return false;
        }
        error.clear();log_=std::move(log);paths_=std::move(paths);racers_.clear();allow_wolves_=allow_wolves;seen_.clear();next_sweep_=0;
        VEHICLE::SET_RANDOM_TRAINS(false);VEHICLE::DELETE_ALL_TRAINS();trains_off_=true;last_train_purge_=MISC::GET_GAME_TIMER();
        Log("trains: random trains off and every train deleted for the race");
        isolated_total_=flag_repairs_=declined_groups_=residual_reports_=removed_groups_=0;next_log_=0;
        if(paths_.empty() || paths_.front().points.empty() || racers.size()<2 || racers.size()>128) {
            error="invalid course/participant roster";return false;
        }
        low_=high_=paths_.front().points.front();
        for(const auto& path:paths_)for(auto p:path.points) {
            low_.x=std::min(low_.x,p.x);low_.y=std::min(low_.y,p.y);
            high_.x=std::max(high_.x,p.x);high_.y=std::max(high_.y,p.y);
            low_.z=std::min(low_.z,p.z);high_.z=std::max(high_.z,p.z);
        }
        BuildRouteBlocks();
        for(const auto e:racers) {
            if(!Exists(e) || Held(e)){error="race roster contains missing/isolated entity";return false;}
            if(!Racer(e))racers_.push_back(Identity::Read(e));
        }
        for(auto& group:parked_) {
            if(std::any_of(group.members.begin(),group.members.end(),[](const Saved& s){return s.handed_back;}))continue;
            group.restoring=false;group.positions_restored=false;
            for(auto& s:group.members)s.home_returned=false;
        }
        audit_max_ms_=audit_sum_ms_=0;audit_frames_=0;
        active_=true;Log("participant-only course; roster="+std::to_string(racers_.size())+
            "; native wolf exception="+std::to_string(allow_wolves_)+
            "; predictive outer buffer; collision preserved; every-frame native audit of a "+
            std::to_string(static_cast<int>(kWindowMargin))+" m window round the racers + SDK pool");return true;
    }
    // How often the course is swept for bystanders. Everything already parked is held in place every
    // frame; the sweep itself - two or three natives off every live entity in the window - runs four
    // times a second. A bystander who steps onto the course is seen within 250 ms, which at racing
    // speed is four metres further along, and is removed just the same. Every frame cost 6.86 ms in
    // the worst frame and 133 natives a frame during a race (measured 2026-09-22).
    static constexpr double kSweepSeconds=.25;
#include "race_circuit_exclusion_sweep.inl"
    void Stop(){
        active_=false;
        if(trains_off_){VEHICLE::SET_RANDOM_TRAINS(true);trains_off_=false;Log("trains: random trains back on");}
        if(query_volume_ && VOLUME::DOES_VOLUME_EXIST(query_volume_))VOLUME::DELETE_VOLUME(query_volume_);
        query_volume_=0;
        Log("isolation stopped; pending entity returns="+std::to_string(Count()));RestorePending(false,false);
    }
    void RestorePending(bool immediate=false,bool allow_normal=true) {
        horse_diagnostics::Scope stage_cost("race: clearance hand-back");
        if(active_)return;
        const auto before=Count();
        for(auto it=parked_.begin();it!=parked_.end();) {
            std::erase_if(it->members,[&](Saved& s){return !s.id.Same() || HandBack(s,it->restoring);});
            const bool handover_pending=std::any_of(it->members.begin(),it->members.end(),[](const Saved& s){return s.handed_back;});
            // The player's own horse must be back as soon as the race ends.
            // Waiting for the player to leave 45 m made a parked main horse
            // disappear for the entire post-race visit (2026-09-24).
            const bool owned=std::any_of(it->members.begin(),it->members.end(),[](const Saved& s){return s.player_owned;});
            if(it->members.empty())it=parked_.erase(it);
            else if(!handover_pending && allow_normal && CanRestore(*it,immediate || owned) && Restore(*it)) {
                if(owned)for(const auto& s:it->members)if(s.player_owned && s.id.Same())
                    Log("player horse restored at finish: entity="+std::to_string(s.id.entity)+
                        " model="+std::to_string(s.id.model));
                it=parked_.erase(it);
            } else ++it;
        }
        if(before && !Count())Log("all isolated entity returns/handoffs complete");
    }
};
}
