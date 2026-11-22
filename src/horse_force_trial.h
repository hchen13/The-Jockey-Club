#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

// Script-thread-only F11 experiment. This state never grants a formal race
// permission to change its force native. The race owns and validates the lease.
namespace horse_motion::force_trial {
enum class Mode {Point,CenterOfMass};
inline const char* Name(Mode mode){return mode==Mode::CenterOfMass?"B-center-of-mass":"A-point";}
enum class Actual {None,Point,CenterOfMass,Mixed,Uncertain};
inline const char* Name(Actual mode) {
    switch(mode) {
    case Actual::Point:return "A-point";
    case Actual::CenterOfMass:return "B-center-of-mass";
    case Actual::Mixed:return "MIXED-A+B";
    case Actual::Uncertain:return "UNCERTAIN";
    default:return "none";
    }
}
enum class Validity {Pending,Valid,Invalid};
inline const char* Name(Validity state){return state==Validity::Valid?"valid":state==Validity::Invalid?"INVALID":"pending";}
struct Evidence {
    Mode expected=Mode::Point;Actual actual=Actual::None;Validity valid=Validity::Pending;
    bool sealed=false,fallback_to_point=false;
    std::uint64_t point_calls=0,center_calls=0;
    std::string reason;
};
struct LineRevision {
    std::uint64_t build_serial=0,map_revision=0,line_map_revision=0,route_serial=0;
    bool operator==(const LineRevision&) const = default;
};
struct Actor {
    int ped=0;std::uint32_t model=0;std::uintptr_t address=0;
    bool operator==(const Actor&) const = default;
    bool Valid()const{return ped && model && address;}
};
struct Target {
    Actor horse,rider;
    std::uint64_t race_token=0;int dispatch=0;
    LineRevision line;
    bool operator==(const Target&) const = default;
    bool Valid()const{return horse.Valid() && rider.Valid() && horse.ped!=rider.ped && race_token && dispatch>0;}
};
struct Vec {float x=0,y=0,z=0;};
struct Sample {
    double now=0;float along=0;
    Vec position,velocity,forward,rotation;
    bool mounted=false,horse_air=false,horse_falling=false,horse_ragdoll=false;
    bool rider_falling=false,rider_ragdoll=false,horse_static=false,collision_disabled=false;
    bool horse_dead=false,rider_dead=false;
    int engine_scaled_ms=0,engine_unscaled_ms=0;
    LineRevision line;
};
using Guard=bool (*)(const Target&);
using Logger=void (*)(const std::string&);
class Probe {
public:
    static constexpr std::size_t kSamples=24;
    static constexpr double kSampleSeconds=.1,kHeartbeatSeconds=.25;
    void SetRequested(bool value){requested_=value;}
    bool Requested()const{return requested_;}
    Mode NextMode()const{return requested_?Mode::CenterOfMass:Mode::Point;}
    bool Active()const{return active_;}
    Mode CurrentMode()const{return mode_;}
    bool HasDispatch()const{return has_dispatch_;}
    const Evidence& Report()const{return evidence_;}
    const Target& Bound()const{return target_;}
    std::uint64_t Calls()const{return calls_;}
    std::size_t Samples()const{return count_;}
    void ResetSession(){End("session-reset");requested_=false;has_dispatch_=false;evidence_={};guard_=owner_=nullptr;logger_=nullptr;target_={};}
    bool Begin(Target target,double now,Guard guard,Logger logger,Guard owner=nullptr) {
        // A dispatch is one frozen experiment, including a refused/failed one.
        // Never turn B back on after an A fallback in the same dispatch.
        if(has_dispatch_ && target.race_token==target_.race_token && target.dispatch==target_.dispatch) {
            if(active_ && target.horse==target_.horse && target.rider==target_.rider)return true;
            if(active_)End("same-dispatch-target-changed");
            if(logger_)logger_(Prefix()+" RETRY_BLOCKED: same dispatch remains closed; next dispatch required");
            return false;
        }
        End("next-dispatch");
        target_=target;mode_=NextMode();guard_=guard;owner_=owner?owner:guard;logger_=logger;active_=false;has_dispatch_=true;
        evidence_={};evidence_.expected=mode_;
        heartbeat_=now;last_sample_.reset();last_log_=now;last_force_.reset();
        calls_=0;count_=0;next_=0;
        try {
            if(!target.Valid() || !std::isfinite(now) || !guard || !guard(target)) {End("begin-refused");return false;}
            active_=true;
            if(logger_)logger_(Prefix()+" begin native="+NativeName()+
                " force_type=1 world_direction=1 scale_by_physical_scalar=1"
                " point_flags=0,1,1,0,1 com_children=0 native_global_scaling=unavailable"
                " com_position=unavailable angular_velocity=unavailable"
                " sample_hz=10 log_hz=1; mode frozen until next rider");
        } catch(...) {Invalidate("begin-exception");throw;}
        return true;
    }
    void Heartbeat(double now) {
        if(!active_)return;
        if(!std::isfinite(now) || now<heartbeat_){End("clock-reset");return;}
        heartbeat_=now;
    }
    bool Due(double now)const {
        return active_ && std::isfinite(now) && (!last_sample_ || now<*last_sample_ || now-*last_sample_>=kSampleSeconds);
    }
    // The callbacks are the unchanged point ABI and the COM ABI, respectively.
    // The exact submitted vector is passed through; this is not a new curve.
    template<class Point,class Center>
    Mode Submit(Actor horse,int observed_rider,Vec force,double now,Point&& point,Center&& center) {
        bool subject=false,attempted=false,returned=false;
        try {
            if(active_ && horse.ped==target_.horse.ped && (horse!=target_.horse ||
                (observed_rider && observed_rider!=target_.rider.ped)))End("target-changed");
            bool scoped=active_ && horse==target_.horse && (!observed_rider || observed_rider==target_.rider.ped);
            if(scoped && (!std::isfinite(now) || now<heartbeat_ || now-heartbeat_>kHeartbeatSeconds || !guard_(target_))) {
                End("scope-invalid");scoped=false;
            }
            const Mode selected=scoped?mode_:Mode::Point;
            subject=has_dispatch_ && horse==target_.horse && (!observed_rider || observed_rider==target_.rider.ped) &&
                (scoped || (owner_ && owner_(target_)));
            attempted=true;
            if(selected==Mode::CenterOfMass)center(force);else point(force);
            returned=true;
            if(subject) {
                const Actual previous=evidence_.actual;
                if(selected==Mode::CenterOfMass)++evidence_.center_calls;else ++evidence_.point_calls;
                last_force_=Force{now,force,++calls_,selected};
                if(evidence_.actual!=Actual::Uncertain) evidence_.actual=evidence_.point_calls && evidence_.center_calls?
                    Actual::Mixed:evidence_.center_calls?Actual::CenterOfMass:Actual::Point;
                if(evidence_.actual==Actual::Mixed)Invalidate("mixed-force-interfaces");
                else if(evidence_.sealed && evidence_.valid!=Validity::Invalid)Invalidate("force-after-terminal");
                if(evidence_.valid==Validity::Invalid && previous!=evidence_.actual && logger_)
                    logger_(Prefix()+" FALLBACK_SUBMITTED DO_NOT_COUNT_AS_VALID_AB");
            }
            return selected;
        } catch(...) {
            if(subject || (has_dispatch_ && horse==target_.horse)) {
                if(attempted && !returned)evidence_.actual=Actual::Uncertain;
                Invalidate("force-exception");
            }
            throw;
        }
    }
    void Observe(Sample sample,bool immediate=false) {
        if(!active_ || !std::isfinite(sample.now) || (!immediate && !Due(sample.now)))return;
        Entry entry{sample,last_force_,calls_};
        ring_[next_]=entry;next_=(next_+1)%kSamples;count_=std::min(count_+1,kSamples);
        last_sample_=sample.now;
        try {
            if(logger_ && (sample.now<last_log_ || sample.now-last_log_>=1)) {
                logger_(Format(entry,"sample"));last_log_=sample.now;
            }
        } catch(...) {Invalidate("observation-exception");throw;}
    }
    // A native fall/finish is a terminal observation, not permission to label a
    // previously invalid or mixed run valid. Cancellation is incomplete.
    void Finish(const char* reason){Close(reason,true);}
    void End(const char* reason){Close(reason,false);}
private:
    void Close(const char* reason,bool terminal) {
        if(!has_dispatch_ || evidence_.sealed)return;
        // Revoke before formatting or calling a logger. No callback can retain
        // authority if reporting fails or a lifecycle operation re-enters.
        active_=false;guard_=nullptr;
        evidence_.sealed=true;evidence_.reason=reason;
        const bool uniform=(mode_==Mode::CenterOfMass && evidence_.actual==Actual::CenterOfMass) ||
            (mode_==Mode::Point && evidence_.actual==Actual::Point);
        evidence_.valid=terminal && uniform && calls_?Validity::Valid:Validity::Invalid;
        evidence_.fallback_to_point=mode_==Mode::CenterOfMass && evidence_.valid==Validity::Invalid;
        if(logger_) {
            for(std::size_t i=0;i<count_;++i)logger_(Format(ring_[(next_+kSamples-count_+i)%kSamples],"tail"));
            logger_(Prefix()+" end reason="+reason+" force_calls="+std::to_string(calls_));
        }
        count_=next_=0;
    }
    void Invalidate(const char* reason) {
        if(!has_dispatch_)return;
        if(!evidence_.sealed){End(reason);return;}
        const bool changed=evidence_.valid!=Validity::Invalid || evidence_.reason!=reason;
        active_=false;guard_=nullptr;evidence_.valid=Validity::Invalid;evidence_.reason=reason;
        evidence_.fallback_to_point=mode_==Mode::CenterOfMass;
        if(changed && logger_)logger_(Prefix()+" INVALIDATED DO_NOT_COUNT_AS_VALID_AB");
    }
    struct Force {double now;Vec vector;std::uint64_t call;Mode mode;};
    struct Entry {Sample sample;std::optional<Force> force;std::uint64_t calls;};
    bool requested_=false,active_=false,has_dispatch_=false;
    Mode mode_=Mode::Point;Target target_;
    Evidence evidence_;
    Guard guard_=nullptr,owner_=nullptr;Logger logger_=nullptr;
    double heartbeat_=0,last_log_=0;
    std::optional<double> last_sample_;
    std::optional<Force> last_force_;
    std::uint64_t calls_=0;
    std::array<Entry,kSamples> ring_{};std::size_t next_=0,count_=0;
    const char* NativeName()const{return mode_==Mode::CenterOfMass?"0x31DA7CEC5334DB37":"0xF15E8F5D333F09C4";}
    std::string Prefix()const {
        return std::string("trial force: expected=")+Name(evidence_.expected)+" actual="+Name(evidence_.actual)+
            " valid="+Name(evidence_.valid)+" sealed="+std::to_string(evidence_.sealed)+
            " fallback_to_A="+std::to_string(evidence_.fallback_to_point)+
            " point_calls="+std::to_string(evidence_.point_calls)+" com_calls="+std::to_string(evidence_.center_calls)+
            " evidence_tag="+(evidence_.valid==Validity::Valid?"UNIFORM_INTERFACE_ONLY":"DO_NOT_COUNT_AS_VALID_AB")+
            " invalid_reason="+(evidence_.reason.empty()?"none":evidence_.reason)+" token="+std::to_string(target_.race_token)+
            " dispatch="+std::to_string(target_.dispatch)+" horse="+std::to_string(target_.horse.ped)+
            " horse_model="+std::to_string(target_.horse.model)+" horse_address="+std::to_string(target_.horse.address)+
            " rider="+std::to_string(target_.rider.ped)+" rider_model="+std::to_string(target_.rider.model)+
            " rider_address="+std::to_string(target_.rider.address)+
            " bind_line_build_serial="+std::to_string(target_.line.build_serial)+
            " bind_map_revision="+std::to_string(target_.line.map_revision)+
            " bind_line_map_revision="+std::to_string(target_.line.line_map_revision)+
            " bind_route_serial="+std::to_string(target_.line.route_serial);
    }
    std::string Format(const Entry& entry,const char* event)const {
        const auto& s=entry.sample;
        std::ostringstream out;out<<std::fixed<<std::setprecision(4)<<Prefix()<<' '<<event<<" t="<<s.now<<" along="<<s.along;
        const auto vec=[&](const char* label,Vec v){out<<' '<<label<<"=("<<v.x<<','<<v.y<<','<<v.z<<')';};
        vec("position",s.position);vec("velocity",s.velocity);vec("forward",s.forward);vec("rotation_xyz_deg_order2",s.rotation);
        out<<" force_calls="<<entry.calls<<" force_age_ms=";
        if(entry.force && s.now>=entry.force->now)out<<(s.now-entry.force->now)*1000;else out<<"unavailable";
        if(entry.force){vec("last_force_world",entry.force->vector);out<<" last_force_interface="<<Name(entry.force->mode);}
        out<<" mounted="<<s.mounted<<" horse_air="<<s.horse_air<<" horse_falling="<<s.horse_falling<<
            " horse_ragdoll="<<s.horse_ragdoll<<" rider_falling="<<s.rider_falling<<" rider_ragdoll="<<s.rider_ragdoll<<
            " horse_static="<<s.horse_static<<" collision_disabled="<<s.collision_disabled<<
            " horse_dead="<<s.horse_dead<<" rider_dead="<<s.rider_dead<<
            " engine_scaled_ms="<<s.engine_scaled_ms<<" engine_unscaled_ms="<<s.engine_unscaled_ms<<
            " line_build_serial="<<s.line.build_serial<<" map_revision="<<s.line.map_revision<<
            " line_map_revision="<<s.line.line_map_revision<<" route_serial="<<s.line.route_serial<<
            " line_changed_since_bind="<<(s.line.build_serial!=target_.line.build_serial ||
                s.line.line_map_revision!=target_.line.line_map_revision || s.line.route_serial!=target_.line.route_serial)<<
            " map_changed_since_bind="<<(s.line.map_revision!=target_.line.map_revision);
        return out.str();
    }
};
inline Probe probe;
} // namespace horse_motion::force_trial
