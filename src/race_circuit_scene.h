#pragma once
#include "race_segmented_circuit.h"
#include "game_natives.h"
#include "race_circuit_exclusion.h"
#include <main.h>
#include <set>
#include <functional>

// Local, ownership-bound street closure. Never changes road nodes, navigation
// meshes, speed volumes, global density or permanent story/PersChar data.
namespace horse_races::circuit_scene {
enum class Preparation {Waiting,Ready,Failed};
struct Actor {
    Ped ped=0;Hash model=0;std::uintptr_t address=0;
    bool Same() const {return ped && ENTITY::DOES_ENTITY_EXIST(ped) && ENTITY::GET_ENTITY_MODEL(ped)==model &&
        reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(ped))==address;}
};
struct Spectator {
    Actor actor;
    AnimScene scene=0;
    std::string slot;
    std::string startup;
    std::vector<std::string> idles,reactions;
    bool clips_checked=false,reanchored=false;
    Vector3 position{},outward{};
    Vector3 scene_origin{};
    float scene_rotation=0;
    double next_reaction=0,release_after=0,metadata_at=-1,reanchor_at=0;
    double placed_at=0,next_start=0,next_status=0;
    int start_attempts=0,components=0;
    bool render_ready=false,running=false;
    std::string pending="actor not sampled";
};
// One oriented box per STRETCH of course, not per recorded node. R*'s own
// race closures are a handful of boxes 50-140 m long; ours were 480 boxes
// per aggregate on the mountain trail, twice over, and after a day of races
// the game asserted inside the box-add native (0x80000003 at
// RDR2.exe+0x25B520C, 2026-09-14 02:35 and 2026-09-11 in the same volume
// code). A stretch is consecutive nodes until its chord exceeds
// kStretchMetres or the heading turns kStretchDegrees from the stretch's
// first segment; its box is the chord plus the nodes' largest lateral
// deviation from it either side, so every node lies inside.
struct ClosureBox {Vector3 centre{};float yaw_degrees=0,length=0,width=0,height=0;};
inline constexpr float kStretchMetres=120,kStretchDegrees=25;
inline std::vector<ClosureBox> ClosureBoxes(const std::vector<Vector3>& nodes,float extra_length,float base_width,
    float stretch_metres=kStretchMetres,float stretch_degrees=kStretchDegrees) {
    std::vector<ClosureBox> boxes;
    const std::size_t n=nodes.size();
    std::size_t start=0;
    while(start+1<n) {
        std::size_t end=start+1;
        const float heading0=std::atan2(nodes[start+1].y-nodes[start].y,nodes[start+1].x-nodes[start].x);
        while(end+1<n) {
            const auto& a=nodes[start];const auto& c=nodes[end+1];
            if(destination::Distance(a,c)>stretch_metres)break;
            const float heading=std::atan2(nodes[end+1].y-nodes[end].y,nodes[end+1].x-nodes[end].x);
            float turn=std::abs(heading-heading0);if(turn>3.14159265F)turn=2*3.14159265F-turn;
            if(turn*57.2957795F>stretch_degrees)break;
            ++end;
        }
        const auto& a=nodes[start];const auto& b=nodes[end];
        const float dx=b.x-a.x,dy=b.y-a.y,chord=std::hypot(dx,dy);
        const float ux=chord>.01F?dx/chord:1,uy=chord>.01F?dy/chord:0;
        float lateral=0,zmin=a.z,zmax=a.z;
        for(std::size_t i=start;i<=end;++i) {
            const float px=nodes[i].x-a.x,py=nodes[i].y-a.y;
            lateral=std::max(lateral,std::abs(-uy*px+ux*py));zmin=std::min(zmin,nodes[i].z);zmax=std::max(zmax,nodes[i].z);
        }
        ClosureBox box;
        box.centre={(a.x+b.x)/2,(a.y+b.y)/2,(zmin+zmax)/2+2};
        box.yaw_degrees=std::atan2(dy,dx)*57.2957795F;
        box.length=chord+extra_length;box.width=base_width+2*lateral;box.height=10+(zmax-zmin);
        boxes.push_back(box);
        start=end;
    }
    return boxes;
}
// A long route is not allowed to cost hundreds of volumes: every box added
// to an aggregate is a script object with a GUID from the same pool the
// entity queries and item sets draw on, and the 11 km one-on-one's 179
// boxes per aggregate were a third of it (2026-09-19). Past
// kMaxClosureBoxes the stretches are lengthened and allowed more turn until
// the count fits; every node still lies inside its box.
inline constexpr std::size_t kMaxClosureBoxes=64;
inline std::vector<ClosureBox> BoundedClosureBoxes(const std::vector<Vector3>& nodes,float extra_length,float base_width) {
    float metres=kStretchMetres,degrees=kStretchDegrees;
    std::vector<ClosureBox> boxes;
    for(int relax=0;relax<8;++relax) {
        boxes=ClosureBoxes(nodes,extra_length,base_width,metres,degrees);
        if(boxes.size()<=kMaxClosureBoxes)break;
        metres*=1.5F;degrees=std::min(degrees*1.4F,80.0F);
    }
    return boxes;
}
class StreetScene {
    enum class Phase {Idle,Loading,FadingOut,Building,FadingIn,Ready,Active};
    Phase phase_=Phase::Idle;
    course::Path ring_,entry_,exit_;
    std::vector<Ped> racers_;
    circuit_exclusion::Exclusion traffic_;
    std::vector<course::Path> pending_traffic_paths_;
    Volume volume_=0,road_volume_=0;
    int scenario_block_=0;
    bool scenario_owned_=false;
    Hash model_=0;
    bool model_requested_=false,fade_owned_=false,cleared_=false;
    bool blackout_audited_=false;   // The full sweep under the blackout: done once a preparation.
    double residual_since_=-1;
    // Set when someone else blacked the screen out before handing the race
    // over - the camp does, at the moment the player chooses - so the fade
    // is taken over instead of being started again and refused.
    bool adopt_fade_=false;
    bool spectators_=true,allow_wolves_=false,clearance_=true;
    // Route::continuous_clearance. Without it the track is cleared for the
    // start only; once the flag drops nothing is policed.
    bool continuous_=false;
    double phase_at_=0,restore_at_=0,next_cheer_=0;
    std::vector<Spectator> crowd_,departed_;
    std::function<void(const std::string&)> log_;
    bool BeginTraffic(std::vector<course::Path> paths) {
        if(traffic_.Begin(paths,racers_,log_,allow_wolves_))return true;
        if(traffic_.error=="native/player handover still pending") {
            // Only on-course takeovers block Begin. Wait in Loading, before
            // fade/teleport, instead of cancelling a paid ticket immediately.
            pending_traffic_paths_=std::move(paths);
            Log("waiting for an on-course native/player handover before blackout");
            return true;
        }
        error=traffic_.error;return false;
    }
    void Log(const std::string& s){if(log_)log_("circuit scene: "+s);}
    const char* PhaseName() const {
        switch(phase_) {
        case Phase::Idle:return "idle";
        case Phase::Loading:return "asset loading";
        case Phase::FadingOut:return "fading out";
        case Phase::Building:return "audience placement/render";
        case Phase::FadingIn:return "fading in";
        case Phase::Ready:return "ready";
        case Phase::Active:return "racing";
        }
        return "unknown";
    }
    static Actor Identity(Ped p){return {p,ENTITY::GET_ENTITY_MODEL(p),reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(p))};}
    static bool Exists(Entity e){return e && ENTITY::DOES_ENTITY_EXIST(e);}
    course::Projection Nearest(Vector3 p) const {
        auto best=course::Project(ring_,p);
        for(const auto* path:{&entry_,&exit_}) {
            const auto candidate=course::Project(*path,p);
            if(candidate.valid && (!best.valid || candidate.distance<best.distance))best=candidate;
        }
        return best;
    }
    bool Audience(Entity e) const {
        for(const auto* list:{&crowd_,&departed_})for(const auto& s:*list)
            if(s.actor.ped==e && s.actor.Same())return true;
        return false;
    }
    bool GuardTraffic(double now,bool racing=false,bool force=false) {
        if(!clearance_)return true;
        // Audience identity is NOT permission to occupy the racing lane.
        for(auto* list:{&crowd_,&departed_})for(auto& s:*list)if(s.actor.Same() &&
            Nearest(ENTITY::GET_ENTITY_COORDS(s.actor.ped,true,false)).distance<=circuit_exclusion::kLaneHalfWidth+2)
            RemoveAnimation(s);
        if(!traffic_.Tick(now,[&](Entity e){return Audience(e);},racing,force)) {
            error=traffic_.error;
            if(racing || !error.starts_with("track residual after isolation:"))
                Log("track exclusion failed: "+error);
            return false;
        }
        return true;
    }
    bool ClearTraffic(double now) {
        if(!CAM::IS_SCREEN_FADED_OUT()){error="traffic isolation requested without owned blackout";return false;}
        if(!GuardTraffic(now,false,true))return false;
        Log(allow_wolves_?"blackout audit passed: only participants and native wolves permitted on track":
            "blackout audit passed: only explicit participants permitted on track");return true;
    }
    float NearestRacer(Vector3 position) const {
        float distance=INFINITY;
        for(auto p:racers_)if(Exists(p))distance=std::min(distance,
            destination::Distance(position,ENTITY::GET_ENTITY_COORDS(p,true,false)));
        return distance;
    }
    bool CreateClosure(const lap::Definition& d) {
        RemoveClosure(); // Never a second pair of aggregates over a first left standing.
        volume_=VOLUME::_CREATE_VOLUME_AGGREGATE_WITH_CUSTOM_NAME("TJC_Circuit_Closure");
        if(!volume_ || !VOLUME::DOES_VOLUME_EXIST(volume_))return false;
        road_volume_=VOLUME::_CREATE_VOLUME_AGGREGATE_WITH_CUSTOM_NAME("TJC_Circuit_Road");
        if(!road_volume_ || !VOLUME::DOES_VOLUME_EXIST(road_volume_))return false;
        const std::vector<const std::vector<Vector3>*> sections=d.single_loop?
            std::vector<const std::vector<Vector3>*>{&d.shared}:
            std::vector<const std::vector<Vector3>*>{&d.entry,&d.shared,&d.returning,&d.finish};
        int boxes=0;
        for(const auto* section:sections) {
            for(const auto& box:BoundedClosureBoxes(*section,60,60))
                VOLUME::_ADD_BOX_VOLUME_TO_VOLUME_AGGREGATE(volume_,box.centre.x,box.centre.y,box.centre.z,0,0,box.yaw_degrees,box.length,box.width,box.height);
            for(const auto& box:BoundedClosureBoxes(*section,4,32)) {
                VOLUME::_ADD_BOX_VOLUME_TO_VOLUME_AGGREGATE(road_volume_,box.centre.x,box.centre.y,box.centre.z,0,0,box.yaw_degrees,box.length,box.width,box.height);
                ++boxes;
            }
        }
        Log("closure aggregates: "+std::to_string(boxes)+" stretch boxes per aggregate (was one per node)");
        // Ambient movement filters cannot express an individual race roster.
        // Their coarse exclusions avoid impeding racers; the full-pool guard
        // still isolates EVERY nonparticipant bypassing this advisory filter.
        constexpr int exclude=16|262144|524288;
        constexpr Hash any=static_cast<Hash>(-1);
        // Native public-hanging pattern: block all ambient creation, independently
        // from the narrower movement filter that exempts script-directed racers.
        // PFF_DangerousAnimals permits natural wolves to stream and approach.
        // The per-frame model allowlist still isolates bears/cougars/etc.; this
        // coarse population bit is NOT permission for all predators on track.
        const int wildlife=allow_wolves_?4096:0;
        POPULATION::_ADD_AMBIENT_SPAWN_RESTRICTION(volume_,0,wildlife,0,any,any,0);
        POPULATION::_ADD_AMBIENT_AVOIDANCE_RESTRICTION(road_volume_,0,exclude|wildlife,0,any,any,2);
        // Include parked wagons / hitching / roadside scenarios. Do not cancel
        // already-running protected shop/story scenarios (no CANCEL_ACTIVE bit).
        scenario_block_=static_cast<int>(PED::_ADD_SCENARIO_BLOCKING_VOLUME(road_volume_,false,4|8));
        scenario_owned_=PED::_IS_SCENARIO_BLOCKING_AREA_VALID(scenario_block_);
        if(!scenario_owned_)return false;
        // SP uses these population restrictions (mudtown2 / public hanging).
        // Do NOT require _ADD_POP_MULTIPLIER_VOLUME: on the verified live SP
        // session its subsystem is disabled and ADD returns -1 before allocating.
        // Its EXISTS query is not a getter for the restrictions just registered.
        // Existing and late traffic is handled by reversible full-pool isolation.
        Log("SP spawn/avoidance restrictions registered; volume="+std::to_string(volume_));
        return true;
    }
    Preparation AuditBlackout(double now) {
        if(ClearTraffic(now)){residual_since_=-1;return Preparation::Ready;}
        // A linked ped/horse/wagon can be moved back onto the lane by the
        // game's own attachment update immediately after another member is
        // parked. Give the next script frames a chance to re-sweep while the
        // screen is still black. Other clearance failures remain hard errors.
        if(error.starts_with("track residual after isolation:")) {
            if(residual_since_<0){residual_since_=now;Log("blackout clearance retry: "+error);}
            if(now-residual_since_<2.0){error.clear();return Preparation::Waiting;}
            error="blackout clearance still occupied after 2 s: "+error;
        }
        return Preparation::Failed;
    }
    void RemoveClosure() {
        if(scenario_owned_){PED::REMOVE_SCENARIO_BLOCKING_AREA(scenario_block_,false);scenario_owned_=false;}
        scenario_block_=0;
        if(road_volume_ && VOLUME::DOES_VOLUME_EXIST(road_volume_)) {
            POPULATION::_REMOVE_AMBIENT_AVOIDANCE_RESTRICTION(road_volume_);VOLUME::DELETE_VOLUME(road_volume_);
        }
        road_volume_=0;
        if(volume_ && VOLUME::DOES_VOLUME_EXIST(volume_)) {
            POPULATION::_REMOVE_AMBIENT_SPAWN_RESTRICTION(volume_);
            VOLUME::DELETE_VOLUME(volume_);Log("street restrictions removed; pending isolated entity returns="+std::to_string(traffic_.Count()));
        }
        volume_=0;restore_at_=0;
        traffic_.RestorePending();
    }
#include "race_circuit_scene_spectators.inl"
public:
    std::string error;
    void RememberPlayerMount(Ped mount) {traffic_.RememberPlayerMount(mount);}
    bool Busy() const {return phase_!=Phase::Idle || volume_!=0;}
    // Metres of road either side of the grid that a destination start clears.
    // A race with no recorded line has no course to police; what its blackout
    // guarantees is that nothing stands on or rolls into the start.
    static constexpr float kStartAreaBehind=15,kStartAreaAhead=45;
    course::Path start_area_;
    // A destination start: fade, grid and countdown like every hosted race, and
    // the start area cleared under the blackout. No closure volume - there is
    // no course to close - and nothing is policed once the flag drops.
    bool BeginDestination(double now,std::function<void(const std::string&)> log,std::vector<Ped> racers,
        Vector3 grid,Vector3 towards) {
        if(Busy())return false;
        log_=std::move(log);error.clear();clearance_=true;continuous_=false;spectators_=allow_wolves_=false;
        racers_=std::move(racers);phase_=Phase::Loading;phase_at_=now;cleared_=false;adopt_fade_=false;blackout_audited_=false;
        residual_since_=-1;
        Vector3 dir{towards.x-grid.x,towards.y-grid.y,0};
        const float length=std::hypot(dir.x,dir.y);
        if(length<1){error="destination grid has no direction";return false;}
        dir.x/=length;dir.y/=length;
        const auto at=[&](float metres){return Vector3{grid.x+dir.x*metres,grid.y+dir.y*metres,grid.z};};
        if(!start_area_.Build({at(-kStartAreaBehind),grid,at(kStartAreaAhead)})){error="start area line invalid";return false;}
        if(!BeginTraffic({start_area_}))return false;
        Log("destination starting scene: fade, countdown, and the start area cleared ("+
            std::to_string(static_cast<int>(kStartAreaBehind+kStartAreaAhead))+" m of road); no closure volume");
        return true;
    }
    bool AddParticipants(const std::vector<Ped>& actors) {
        if(!clearance_){error="the course is no longer policed";return false;}
        if(!traffic_.AddParticipants(actors)){error=traffic_.error;return false;}
        for(Ped p:actors)if(std::find(racers_.begin(),racers_.end(),p)==racers_.end())racers_.push_back(p);
        return true;
    }
    bool Begin(const lap::Definition& d,const lap::Plan& plan,std::vector<Ped> racers,double now,
        std::function<void(const std::string&)> log,bool continuous=false) {
        if(Busy() || departed_.size()>24)return false;
        log_=std::move(log);error.clear();clearance_=true;continuous_=continuous;racers_=racers;ring_=plan.profiles[1];entry_=plan.profiles[0];exit_=plan.profiles[2];
        spectators_=d.spectators;allow_wolves_=d.allow_wolves;
        phase_=Phase::Loading;phase_at_=now;cleared_=false;adopt_fade_=false;blackout_audited_=false;
        residual_since_=-1;
        if(!BeginTraffic(d.single_loop?std::vector<course::Path>{ring_}:
            std::vector<course::Path>{ring_,entry_,exit_}))return false;
        if(!spectators_) {
            if(!CreateClosure(d)){error="local closure/scenario restriction creation failed";return false;}
            Log("preparing full-course closure without spectators; native wolves="+std::to_string(allow_wolves_));return true;
        }
        model_=MISC::GET_HASH_KEY("A_M_M_VALTOWNFOLK_01");
        if(!STREAMING::IS_MODEL_VALID(model_)){error="audience model invalid";return false;}
        STREAMING::REQUEST_MODEL(model_,false);model_requested_=true;
        for(std::size_t i=0;i<8;++i) {
            // Current live SP logs twice confirm this standalone scene supplies
            // all Idle/React variants. 02/03/05 only exposed Enter, whose route
            // into S_BASE has not been validated outside the original mission.
            // Keep eight independent actors with random outfits/reaction timing.
            Spectator s;s.slot="male_civ_06";
            const auto asset="script@story@mud2@ig@ig_15@fight_crowd@"+s.slot;
            // Native mudtown2::func_917 loads every crowd scene through Enter.
            s.scene=ANIMSCENE::_CREATE_ANIM_SCENE(asset.c_str(),0,"pl_Enter",false,true);
            if(!s.scene || !ANIMSCENE::DOES_ANIM_SCENE_EXIST(s.scene)){error="audience scene unavailable";return false;}
            crowd_.push_back(s);ANIMSCENE::LOAD_ANIM_SCENE(s.scene);
        }
        if(!CreateClosure(d)){error="local closure/scenario restriction creation failed";return false;}
        Log("preloading; 8 native standing spectators; own local pedestrian/rider/wagon closure");return true;
    }
#include "race_circuit_scene_prepare.inl"
    void AdoptBlackout(){adopt_fade_=true;fade_owned_=true;}
    void Started(){if(phase_==Phase::Ready)phase_=Phase::Active;}
    template<class Cheer> bool TickActive(double now,bool speech_busy,Cheer cheer) {
        if(phase_!=Phase::Active)return true;
        // The flag has dropped. Only a route that asks for continuous clearance
        // keeps policing the course; a refused isolation is then declined, never
        // allowed to cancel the race the player is riding.
        if(continuous_ && !GuardTraffic(now,true))return false;
        for(auto& s:crowd_)if(s.actor.Same() && !traffic_.Held(s.actor.ped) && !ENTITY::IS_ENTITY_DEAD(s.actor.ped) && now>=s.next_reaction) {
            s.next_reaction=now+7+(s.actor.ped%5);
            const bool nearby=NearestRacer(s.position)<32;
            // Original crowd transitions only leave the native S_BASE section;
            // do not interrupt Enter or restart a reaction before it finishes.
            const auto& choices=nearby?s.reactions:s.idles;
            if(ANIMSCENE::IS_ANIM_SCENE_RUNNING(s.scene,false) &&
                ANIMSCENE::IS_ANIM_SCENE_IN_SECTION(s.scene,"S_BASE",true)) {
                for(const auto& name:choices)if(ANIMSCENE::_IS_ANIM_SCENE_PLAYBACK_LIST_PHASE_LOADED(s.scene,name.c_str()) &&
                    !ANIMSCENE::_IS_ANIM_SCENE_PLAYBACK_LIST_PHASE_ACTIVE(s.scene,name.c_str())) {
                    ANIMSCENE::SET_ANIM_SCENE_PLAY_LIST(s.scene,name.c_str(),true);break;
                }
            }
            if(nearby && !speech_busy && now>=next_cheer_){cheer(s.actor.ped);next_cheer_=now+6;}
        }
        return true;
    }
    void Finish(double now) {
        if(phase_==Phase::Idle && crowd_.empty())return;
        pending_traffic_paths_.clear();
        const bool black=fade_owned_ && CAM::IS_SCREEN_FADED_OUT();
        traffic_.Stop();
        if(black)traffic_.RestorePending(true);
        for(auto& s:crowd_) {
            RemoveAnimation(s);
            if(!s.actor.Same())continue;
            if(black) {Ped p=s.actor.ped;PED::DELETE_PED(&p);continue;}
            if(!traffic_.Held(s.actor.ped) && !ENTITY::IS_ENTITY_DEAD(s.actor.ped)) {
                PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(s.actor.ped,false);
                TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(s.actor.ped,{s.position.x+s.outward.x*12,s.position.y+s.outward.y*12,s.position.z},
                    1.0F,8000,2.0F,0,40000.0F);
            }
            s.release_after=now+8;departed_.push_back(s);
        }
        crowd_.clear();
        if(fade_owned_){CAM::DO_SCREEN_FADE_IN(350);fade_owned_=false;}
        if(model_requested_){STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model_);model_requested_=false;}
        phase_=Phase::Idle;
        if(black || !cleared_)RemoveClosure();else restore_at_=now+4;
        Log(spectators_?"finish/abort: audience departs on foot; road restoration scheduled; fade ownership released":
            "finish/abort: road restoration scheduled; fade ownership released (no audience)");
    }
    void Background(double now) {
        if(restore_at_ && now>=restore_at_)RemoveClosure();
        // Native stable/player handovers cannot wait for audience departure or
        // the normal off-camera return. Keep checking during the 4s reopening.
        if(phase_==Phase::Idle)traffic_.RestorePending(false,!volume_);
        for(auto it=departed_.begin();it!=departed_.end();) {
            if(!it->actor.Same()){it=departed_.erase(it);continue;}
            const auto p=it->actor.ped;
            if(traffic_.Held(p)){++it;continue;}
            if(now>=it->release_after) {
                // No visible deletion: after the short outward walk, hand the
                // living spectator to ordinary town wandering. Dead bodies are
                // likewise released to normal game cleanup, never made invisible.
                if(!ENTITY::IS_ENTITY_DEAD(p))TASK::TASK_WANDER_STANDARD(p,10,0);
                Ped owned=p;ENTITY::SET_PED_AS_NO_LONGER_NEEDED(&owned);it=departed_.erase(it);
            } else ++it;
        }
    }
};
}
