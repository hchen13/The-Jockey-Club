#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace horse_motion::native_speed_table {
// Two PRIVATE copies of one horse's gait table. No archetype/global data,
// coordinates, velocities, task pointers, vtables or code are ever modified.
struct Table {
    std::array<float,16> speed{};
    std::array<std::uint8_t,4> flags{};
    bool operator==(const Table&) const = default;
    float Top() const{return speed[7];}
    float Limit() const{return speed[15];}
    bool Valid() const {
        if(flags!=std::array<std::uint8_t,4>{1,1,0,0})return false;
        for(float v:speed)if(!std::isfinite(v) || v<0 || v>100)return false;
        for(int i=0;i<4;++i)if(speed[i]>speed[i+4] ||
            (i && (speed[i]<speed[i-1] || speed[i+4]<speed[i+3])))return false;
        for(int i=0;i<4;++i)if(speed[i+8]>speed[i+12] ||
            (i && speed[i+12]<speed[i+11]))return false;
        return Top()>=10 && Top()<=25 && Limit()>=10 && Limit()<=25;
    }
};
static_assert(sizeof(Table)==0x44);
inline bool SameExceptCaps(Table a,Table b){a.speed[7]=b.speed[7]=0;a.speed[15]=b.speed[15]=0;return a==b;}
inline bool SameFloat(float a,float b){return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);}
struct Key {
    std::uintptr_t entity=0,motion=0,task=0,archetype=0,weak=0;
    std::uint32_t entity_id=0,interpolation_bits=0;
    bool operator==(const Key&) const = default;
};
struct Snapshot {
    Key key{};
    Table task{},motion{};
    float derived_native_top=0;
    bool valid=false;
    int checkpoint=0; // WindowsApi gate reached; 12 = fully validated snapshot.
    float derived_native_limit=0;
};
enum class Slot {Task,Motion};
enum class State {Idle,Unavailable,Refused,Applied,Restored,Conflict,RestoreSkipped};
inline const char* Name(State state){
    switch(state){
    case State::Unavailable:return "layout-or-task-unavailable";
    case State::Refused:return "request-refused";
    case State::Applied:return "private-tables-written";
    case State::Restored:return "both-private-tables-restored";
    case State::RestoreSkipped:return "restore-skipped-changed-or-unavailable";
    case State::Conflict:return "table-changed-no-overwrite";
    default:return "idle";
    }
}
struct Observation {
    int state=0,checkpoint=0,restore_mask=0;
    bool active=false;
    float native_top=0,applied_top=0,observed_task=0,observed_motion=0,derived=0;
    float native_limit=0,applied_limit=0,observed_task_limit=0,observed_motion_limit=0,derived_limit=0;
};
// Api::Capture(entity) validates exact build, actor, producer and both copies.
// Api::Compare(key,slot,expected_table,index,new) revalidates ownership/context
// and EVERY table byte, then compare/exchanges only index 7 or 15 (four bytes).
// It must refuse inaccessible/recycled/foreign data rather than force a write.
template<class Api> class Lease {
    Api api_{};
    Snapshot baseline_{};
    Table owned_task_{},owned_motion_{};
    float applied_=0;
    double last_=-1;
    bool held_=false;
    State state_=State::Idle;
    int checkpoint_=0,restore_mask_=0;
    float observed_task_=0,observed_motion_=0,derived_=0;
    float observed_task_limit_=0,observed_motion_limit_=0,derived_limit_=0;
    bool OursOrOriginal(const Table& actual,const Table& original,const Table& owned) const{
        if(!SameExceptCaps(actual,original))return false;
        for(int index:{7,15})if(!SameFloat(actual.speed[index],owned.speed[index]) &&
            !SameFloat(actual.speed[index],original.speed[index]))return false;
        return true;
    }
    bool WriteCopy(Slot slot,Table& owned,const Table& desired){
        // Open the range before increasing gait speed; lower gait speed before
        // closing it. Track every successful field so a later CAS failure can
        // restore the partial update, including a cap-only write.
        const std::array order=desired.Top()>=owned.Top()?std::array{15,7}:std::array{7,15};
        for(int index:order){
            if(SameFloat(owned.speed[index],desired.speed[index]))continue;
            if(!api_.Compare(baseline_.key,slot,owned,index,desired.speed[index]))return false;
            owned.speed[index]=desired.speed[index];
        }
        // A no-op is still verified; stale owned values cannot claim restore.
        return api_.Compare(baseline_.key,slot,owned,7,owned.Top());
    }
public:
    explicit Lease(Api api={}):api_(api){}
    Lease(const Lease&)=delete;
    Lease& operator=(const Lease&)=delete;
    Lease(Lease&& other) noexcept:api_(std::move(other.api_)),baseline_(other.baseline_),
        owned_task_(other.owned_task_),owned_motion_(other.owned_motion_),
        applied_(other.applied_),last_(other.last_),held_(std::exchange(other.held_,false)),state_(other.state_),
        checkpoint_(other.checkpoint_),restore_mask_(other.restore_mask_),observed_task_(other.observed_task_),
        observed_motion_(other.observed_motion_),derived_(other.derived_),
        observed_task_limit_(other.observed_task_limit_),observed_motion_limit_(other.observed_motion_limit_),
        derived_limit_(other.derived_limit_){}
    Lease& operator=(Lease&& other) noexcept {
        if(this!=&other){Release();api_=std::move(other.api_);baseline_=other.baseline_;applied_=other.applied_;
            owned_task_=other.owned_task_;owned_motion_=other.owned_motion_;
            last_=other.last_;held_=std::exchange(other.held_,false);state_=other.state_;
            checkpoint_=other.checkpoint_;restore_mask_=other.restore_mask_;observed_task_=other.observed_task_;
            observed_motion_=other.observed_motion_;derived_=other.derived_;
            observed_task_limit_=other.observed_task_limit_;observed_motion_limit_=other.observed_motion_limit_;
            derived_limit_=other.derived_limit_;}return *this;
    }
    ~Lease(){Release();}
    bool Active() const{return held_;}
    float NativeTop() const{return held_?baseline_.task.Top():0;}
    float AppliedTop() const{return held_?applied_:0;}
    State Status() const{return state_;}
    int Checkpoint() const{return checkpoint_;}
    int RestoreMask() const{return restore_mask_;} // Task=1, motion=2; 3 confirms BOTH CAS restores.
    float ObservedTaskTop() const{return observed_task_;}
    float ObservedMotionTop() const{return observed_motion_;}
    float DerivedTop() const{return derived_;}
    float AppliedLimit() const{return held_?owned_motion_.Limit():0;}
    Observation Observe() const{return {static_cast<int>(state_),checkpoint_,restore_mask_,held_,
        NativeTop(),AppliedTop(),observed_task_,observed_motion_,derived_,
        held_?baseline_.task.Limit():0,AppliedLimit(),observed_task_limit_,observed_motion_limit_,derived_limit_};}
    void Release(){
        if(!held_)return;
        // Restore the producer first. A subsequent engine refresh then copies
        // its original value, not our extension, into the motion component.
        const auto current=api_.Capture(baseline_.key.entity);
        if(current.valid && current.key==baseline_.key){
            if(OursOrOriginal(current.task,baseline_.task,owned_task_))owned_task_=current.task;
            if(OursOrOriginal(current.motion,baseline_.motion,owned_motion_))owned_motion_=current.motion;
        }
        const bool task=WriteCopy(Slot::Task,owned_task_,baseline_.task);
        const bool motion=WriteCopy(Slot::Motion,owned_motion_,baseline_.motion);
        restore_mask_=(task?1:0)|(motion?2:0);
        held_=false;applied_=0;last_=-1;state_=restore_mask_==3?State::Restored:State::RestoreSkipped;
    }
    bool Update(std::uintptr_t entity,float target,double now){
        if(!entity || !std::isfinite(target) || target<10 || target>25 || !std::isfinite(now)){
            Release();state_=State::Refused;return false;
        }
        auto current=api_.Capture(entity);
        checkpoint_=current.checkpoint;
        observed_task_=current.task.Top();observed_motion_=current.motion.Top();derived_=current.derived_native_top;
        observed_task_limit_=current.task.Limit();observed_motion_limit_=current.motion.Limit();
        derived_limit_=current.derived_native_limit;
        if(!current.valid){Release();state_=State::Unavailable;return false;}
        if(held_ && (current.key!=baseline_.key ||
            !OursOrOriginal(current.task,baseline_.task,owned_task_) ||
            !OursOrOriginal(current.motion,baseline_.motion,owned_motion_))){
            Release();current=api_.Capture(entity);
            if(!current.valid){state_=State::Unavailable;return false;}
        }
        if(!held_){
            // Never adopt a value from an earlier/foreign patch as the original.
            // The default producer's independently read archetype/interpolation
            // must explain BOTH unmodified tables before acquiring a lease.
            if(!current.task.Valid() || current.task!=current.motion ||
                !std::isfinite(current.derived_native_top) ||
                std::abs(current.task.Top()-current.derived_native_top)>.0001F ||
                !std::isfinite(current.derived_native_limit) ||
                std::abs(current.task.Limit()-current.derived_native_limit)>.0001F ||
                target<=current.task.Top() || target>current.task.Top()*1.6F){
                state_=State::Refused;return false;
            }
            baseline_=current;applied_=current.task.Top();last_=now;held_=true;
        }
        owned_task_=current.task;owned_motion_=current.motion;
        if(target<=baseline_.task.Top() || target>baseline_.task.Top()*1.6F || now<last_){
            Release();state_=State::Refused;return false;
        }
        // Extend the native gallop band progressively, not a speed/position
        // teleport. The native task continues to own gait, steering and motion.
        const float step=3.5F*static_cast<float>(std::clamp(now-last_,0.0,.1));
        const float next=std::clamp(target,applied_-step,applied_+step);
        last_=now;
        auto desired=baseline_.task;
        desired.speed[7]=next;
        // Native range helper 0x1accbdc reads index 15 when flag[1] is set;
        // 0xddc641..65e clamps a mounted speed request against that result.
        // Extending index 7 alone left this second bound at 15.74 m/s.
        desired.speed[15]=std::max(baseline_.task.Limit(),next);
        if(!WriteCopy(Slot::Task,owned_task_,desired) || !WriteCopy(Slot::Motion,owned_motion_,desired)){
            Release();state_=State::Conflict;return false;
        }
        applied_=next;state_=State::Applied;return true;
    }
};
} // namespace horse_motion::native_speed_table
